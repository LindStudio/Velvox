// VOCAL CHECK 0.6: finding schema (id, category, severity, confidence, German texts,
// fix action), measurements, rules, score, fix planning and the apply model.

#include "TestHelpers.h"
#include "SyntheticVocal.h"
#include "DSP/VocalAnalyzer.h"
#include "Check/CheckTypes.h"

using namespace velvox;
using namespace velvox::test;
using namespace velvox::dsp;

namespace
{
    AnalysisResult analyseVocal (const automix::CapturedAudio& v, double fs)
    {
        CheckModule check;
        check.setEnabled (true);
        VocalAnalyzer analyzer (check);
        analyzer.setSampleRate (fs);
        const int n = v.numSamples();
        for (int start = 0; start < n; start += 512)
        {
            const int len = std::min (512, n - start);
            const float* ch[2] { v.vocal[0].data() + start, v.vocal[v.vocal.size() > 1 ? 1 : 0].data() + start };
            check.process (ch, 2, len);
            analyzer.pump();
        }
        return analyzer.analyse();
    }

    juce::String describe (const Finding& f)
    {
        return f.id + " [" + juce::String (juce::roundToInt (f.confidence * 100.0f)) + " %] " + f.problem + " | " + f.fix;
    }
}

class VocalCheckFixTests final : public juce::UnitTest
{
public:
    VocalCheckFixTests() : juce::UnitTest ("Headliner Vocal Check 0.6", "Headliner") {}

    void runTest() override
    {
        constexpr double fs = 48000.0;

        beginTest ("Finding schema: id, category, German problem / fix text, confidence above the threshold");
        {
            VocalSpec s;
            s.seconds = 16.0;
            s.sampleRate = fs;
            s.rumbleDb = -26.0f;
            s.resonanceHz = 3150.0f;
            s.resonanceDb = 16.0f;
            const auto r = analyseVocal (makeVocal (s), fs);
            expect (! r.findings.empty());
            for (auto& f : r.findings)
            {
                logMessage ("  " + describe (f));
                expect (f.id.isNotEmpty(), "id");
                expect (f.problem.isNotEmpty() && f.fix.isNotEmpty(), "texts: " + f.id);
                expect (f.fix.startsWith (juce::String::fromUTF8 ("\xe2\x86\x92")), "fix text starts with an arrow: " + f.id);
                expect (f.confidence >= check::showThreshold && f.confidence <= 1.0f, "confidence: " + f.id);
            }
        }

        beginTest ("Measurements: pitch of the voice");
        {
            VocalSpec s;
            s.seconds = 14.0;
            s.sampleRate = fs;
            s.f0 = 220.0f;
            const auto r = analyseVocal (makeVocal (s), fs);
            logMessage ("  f0 median " + juce::String (r.f0MedianHz, 1) + " Hz, pitched " + juce::String (r.pitchedShare, 2)
                        + ", notes " + juce::String ((int) r.sungNotesHz.size()));
            expect (r.pitchValid);
            // The syllables use semitones around 220 Hz: the median and every sung note on that grid
            expectWithinAbsoluteError (r.f0MedianHz, 220.0f, 220.0f * 0.06f);
            for (float note : r.sungNotesHz)
            {
                const float semis = 12.0f * std::log2 (note / 220.0f);
                expectWithinAbsoluteError (semis, std::round (semis), 0.4f, "note on the semitone grid");
            }
            expect (! r.highVoice, "220 Hz median, lowest notes below 250 Hz");
            expect (r.sungNotesHz.size() >= 3);

            s.f0 = 440.0f;
            const auto high = analyseVocal (makeVocal (s), fs);
            expectWithinAbsoluteError (high.f0MedianHz, 440.0f, 440.0f * 0.06f);
            expect (high.highVoice);
        }

        beginTest ("Measurements: loudness (K-weighted, gated) and loudness range");
        {
            const int n = (int) (10.0 * fs);
            automix::CapturedAudio a;
            a.sampleRate = fs;
            a.channels = 2;
            a.vocal.assign (2, std::vector<float> ((size_t) n));
            for (int i = 0; i < n; ++i)
                a.vocal[0][(size_t) i] = a.vocal[1][(size_t) i] = 0.1414f * (float) std::sin (2.0 * pi * 1000.0 * i / fs);
            const auto r = analyseVocal (a, fs);
            logMessage ("  1 kHz at -20 dB RMS: " + juce::String (r.loudnessLufs, 2) + " LUFS");
            expectWithinAbsoluteError (r.loudnessLufs, -20.0f, 0.7f);

            // 4 s at -14 dB, 4 s at -30 dB, three times: about 16 LU
            const int m = (int) (24.0 * fs);
            automix::CapturedAudio b;
            b.sampleRate = fs;
            b.channels = 1;
            b.vocal.assign (1, std::vector<float> ((size_t) m));
            for (int i = 0; i < m; ++i)
            {
                const float level = ((i / (int) (4.0 * fs)) % 2 == 0) ? -14.0f : -30.0f;
                b.vocal[0][(size_t) i] = 1.414f * juce::Decibels::decibelsToGain (level) * (float) std::sin (2.0 * pi * 500.0 * i / fs);
            }
            const auto rb = analyseVocal (b, fs);
            logMessage ("  LRA " + juce::String (rb.lraLu, 1) + " LU");
            expect (rb.lraValid);
            expectWithinAbsoluteError (rb.lraLu, 16.0f, 2.0f);
        }

        beginTest ("Measurements: plosives (low-frequency bursts) are counted");
        {
            VocalSpec s;
            s.seconds = 16.0;
            s.sampleRate = fs;
            auto v = makeVocal (s);
            const auto clean = analyseVocal (v, fs);
            expectEquals (clean.plosiveCount, 0, "no plosives in the clean take");
            addPlosives (v, 6, 0.5f);
            const auto r = analyseVocal (v, fs);
            logMessage ("  plosives " + juce::String (r.plosiveCount) + " (" + juce::String (r.plosivesPerMinute, 1) + " / min)");
            expectWithinAbsoluteError (r.plosiveCount, 6, 1);
            expectGreaterThan (r.plosivesPerMinute, 10.0f);
        }

        beginTest ("Measurements: S centre, mono loss, generation");
        {
            VocalSpec s;
            s.seconds = 14.0;
            s.sampleRate = fs;
            s.sibilance = 1.5f;
            s.channels = 2;
            const auto v = makeVocal (s);
            const auto r = analyseVocal (v, fs);
            logMessage ("  S centre " + juce::String (r.sibilanceHz, 0) + " Hz, mono loss " + juce::String (r.monoLossDb, 2) + " dB");
            expect (r.sibilanceHz > 4500.0f && r.sibilanceHz < 11000.0f);
            expectWithinAbsoluteError (r.monoLossDb, 0.0f, 0.1f);

            auto inverted = v;
            for (auto& x : inverted.vocal[1])
                x = -x;
            const auto ri = analyseVocal (inverted, fs);
            expectGreaterThan (ri.monoLossDb, 20.0f, "anti-phase channels cancel in mono");

            CheckModule check;
            VocalAnalyzer analyzer (check);
            const auto g = analyzer.getGeneration();
            analyzer.clear();
            expectEquals ((int) (analyzer.getGeneration() - g), 1);
        }
    }

    // Low-frequency thumps (80 Hz, 40 ms) at syllable-like positions, `amp` peak
    static void addPlosives (automix::CapturedAudio& v, int count, float amp)
    {
        const double fs = v.sampleRate;
        const int len = (int) (0.04 * fs);
        for (int k = 0; k < count; ++k)
        {
            const int start = (int) ((1.0 + 2.2 * k) * fs);
            for (auto& ch : v.vocal)
                for (int i = 0; i < len && start + i < (int) ch.size(); ++i)
                {
                    const float env = std::sin (juce::MathConstants<float>::pi * (float) i / (float) len);
                    ch[(size_t) (start + i)] += amp * env * (float) std::sin (2.0 * pi * 80.0 * i / fs);
                }
        }
    }
};

static VocalCheckFixTests vocalCheckFixTests;
