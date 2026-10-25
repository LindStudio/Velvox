// VOCAL CHECK diagnoses (0.4): every finding explains itself (evidence, why it matters,
// suggestion, confidence), clean material raises no problem, noise is recognised, and
// the processor tells whether the chain already takes care of a finding.

#include "TestHelpers.h"
#include "SyntheticVocal.h"
#include "DSP/VocalAnalyzer.h"

using namespace velvox;
using namespace velvox::test;
using namespace velvox::dsp;

namespace
{
    AnalysisResult analyse (const automix::CapturedAudio& v, double fs)
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

    const Finding* find (const AnalysisResult& r, const juce::String& text)
    {
        for (auto& f : r.findings)
            if (f.id.startsWith (text))   // 0.6: stable ids ("rumble", "resonance.3150", "noise")
                return &f;
        return nullptr;
    }

    // Feeds a vocal through the real processor and refreshes the analysis
    void runThroughPlugin (Harness& h, const automix::CapturedAudio& v)
    {
        juce::AudioBuffer<float> block (2, 512);
        juce::MidiBuffer midi;
        for (int start = 0; start + 512 <= v.numSamples(); start += 512)
        {
            for (int ch = 0; ch < 2; ++ch)
                block.copyFrom (ch, 0, v.vocal[(size_t) std::min (ch, (int) v.vocal.size() - 1)].data() + start, 512);
            h.proc.processBlock (block, midi);
            if ((start / 512) % 8 == 0)
                h.proc.pumpAnalyzer();
        }
        h.proc.pumpAnalyzer();
        h.proc.refreshAnalysis();
    }
}

class VocalCheckTests final : public juce::UnitTest
{
public:
    VocalCheckTests() : juce::UnitTest ("Headliner Vocal Check diagnoses", "Headliner") {}

    void runTest() override
    {
        constexpr double fs = 48000.0;

        beginTest ("Every finding explains itself: evidence, why it matters, suggestion, confidence");
        {
            VocalSpec s;
            s.seconds = 16.0;
            s.sampleRate = fs;
            s.sibilance = 1.5f;
            s.rumbleDb = -28.0f;
            s.resonanceHz = 3150.0f;
            s.resonanceDb = 20.0f;
            s.phraseRangeDb = 20.0f;
            const auto r = analyse (makeVocal (s), fs);
            expect (r.enoughMaterial);
            expectGreaterThan ((int) r.findings.size(), 2, "several problems in a problematic take");
            for (auto& f : r.findings)
            {
                logMessage ("  [" + juce::String (f.certainty == Finding::Certainty::sure ? "likely" : "observation") + ", "
                            + juce::String (juce::roundToInt (f.confidence * 100.0f)) + " %] " + f.title + " | " + f.detail);
                expect (f.title.isNotEmpty() && f.detail.isNotEmpty() && f.why.isNotEmpty() && f.advice.isNotEmpty(), "complete: " + f.title);
                expect (f.confidence > 0.0f && f.confidence < 1.0f, "confidence in range: " + f.title);
            }
            expect (find (r, "rumble") != nullptr || find (r, "low end") != nullptr, "rumble found");
            expect (find (r, "resonance") != nullptr, "resonance found");
        }

        beginTest ("Confidence grows with the distance from the threshold");
        {
            VocalSpec mild, strong;
            for (auto* s : { &mild, &strong })
            {
                s->seconds = 16.0;
                s->sampleRate = fs;
            }
            mild.rumbleDb = -40.0f;
            strong.rumbleDb = -22.0f;
            const auto rm = analyse (makeVocal (mild), fs), rs = analyse (makeVocal (strong), fs);
            const auto* fm = find (rm, "low end");
            const auto* fs2 = find (rs, "low end");
            if (fm == nullptr)
                fm = find (rm, "rumble");
            if (fs2 == nullptr)
                fs2 = find (rs, "rumble");
            expect (fs2 != nullptr, "strong rumble found");
            if (fm != nullptr && fs2 != nullptr)
                expectGreaterThan (fs2->confidence, fm->confidence, "stronger problem, higher confidence");
        }

        beginTest ("Clean material raises no likely problem; noisy material is recognised");
        {
            VocalSpec clean;
            clean.seconds = 16.0;
            clean.sampleRate = fs;
            const auto rc = analyse (makeVocal (clean), fs);
            int problems = 0;
            for (auto& f : rc.findings)
            {
                logMessage ("  clean take: " + f.title + " (" + juce::String (juce::roundToInt (f.confidence * 100.0f)) + " %)");
                problems += f.certainty == Finding::Certainty::sure && f.category != check::Category::dynamics ? 1 : 0;   // phrase dynamics are a mix target, not a recording problem
            }
            expectEquals (problems, 0, "no likely problem in a clean synthetic take");

            VocalSpec noisy = clean;
            noisy.noiseDb = -42.0f;
            const auto rn = analyse (makeVocal (noisy), fs);
            logMessage ("  noise floor " + juce::String (rn.noiseFloorDb, 1) + " dBFS, " + juce::String (rn.snrDb, 1) + " dB below the voice");
            expect (rn.noiseFloorDb > -60.0f, "noise floor measured");
            expect (find (rn, "noise") != nullptr || find (rn, "Noisy") != nullptr, "noise finding");
        }

        beginTest ("Handling: the chain around VOCAL CHECK is taken into account");
        {
            VocalSpec s;
            s.seconds = 14.0;
            s.sampleRate = fs;
            s.channels = 2;
            s.rumbleDb = -24.0f;
            const auto v = makeVocal (s);
            const auto eqFinding = [] (Harness& h) -> const Finding*
            {
                for (auto& f : h.proc.getLatestAnalysis().findings)
                    if (f.module == ModuleId::eq)
                        return &f;
                return nullptr;
            };

            // Check first, the EQ (with low cut) after it: the rumble is handled later
            Harness after;
            after.neutral();
            after.set (pid::checkOn, 1.0f);
            after.set (pid::eqOn, 1.0f);
            after.set (pid::hpfOn, 1.0f);
            after.proc.setRouting (Routing::fromString ("v2,check,eq,delay:send,reverb:send"));
            runThroughPlugin (after, v);
            const auto* f1 = eqFinding (after);
            expect (f1 != nullptr, "rumble measured at VOCAL CHECK (before the EQ)");
            if (f1 != nullptr)
            {
                logMessage ("  " + f1->title + " -> " + f1->handlingText);
                expect (f1->handling == Finding::Handling::handledLater, "the EQ after VOCAL CHECK takes care of it");
            }

            // EQ before the check: measured after it, so it may need more
            Harness before;
            before.neutral();
            before.set (pid::checkOn, 1.0f);
            before.set (pid::eqOn, 1.0f);
            before.set (pid::lowGain, 6.0f);   // boosts the lows instead of cutting
            before.proc.setRouting (Routing::fromString ("v2,eq,check,delay:send,reverb:send"));
            runThroughPlugin (before, v);
            const auto* f2 = eqFinding (before);
            if (f2 != nullptr)
            {
                logMessage ("  " + f2->title + " -> " + f2->handlingText);
                expect (f2->handling == Finding::Handling::appliedBefore);
            }

            // EQ off: not handled, no status text
            Harness off;
            off.neutral();
            off.set (pid::checkOn, 1.0f);
            off.proc.setRouting (Routing::fromString ("v2,check,eq,delay:send,reverb:send"));
            off.set (pid::eqOn, 0.0f);
            runThroughPlugin (off, v);
            if (const auto* f3 = eqFinding (off))
                expect (f3->handling == Finding::Handling::notHandled && f3->handlingText.isEmpty());
        }
    }
};

static VocalCheckTests vocalCheckTests;
