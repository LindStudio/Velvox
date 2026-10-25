// 0.6 dynamics quality track: Compressor, De-Esser, Vocal Smash, Level Ride.
// Measurable quality targets (QUALITY_BASELINE gaps #6 and #8), the new options and the
// lock-free UI data feeds of the module panels.

#include "TestHelpers.h"
#include "DSP/DynamicsFeed.h"
#include "DSP/CompressorModule.h"
#include "DSP/SmashModule.h"
#include "DSP/DeEsserModule.h"
#include "DSP/RideModule.h"

using namespace velvox;
using namespace velvox::test;
using namespace velvox::dsp;

namespace
{
    constexpr double twoPi = 6.283185307179586;
}

//==============================================================================
class DynamicsFeedTests final : public juce::UnitTest
{
public:
    DynamicsFeedTests() : juce::UnitTest ("Dynamics feed", "Dynamics") {}

    void runTest() override
    {
        beginTest ("FrameFifo: order kept");
        {
            FrameFifo<int, 8> f;
            for (int i = 0; i < 5; ++i)
                expect (f.push (i));
            int out[8] {};
            expectEquals (f.pull (out, 3), 3);
            expectEquals (out[0], 0);
            expectEquals (out[2], 2);
            for (int i = 5; i < 9; ++i)
                expect (f.push (i));
            expectEquals (f.pull (out, 8), 6);
            expectEquals (out[0], 3);
            expectEquals (out[5], 8);
        }

        beginTest ("Dynamics feed: frames carry the peak of each frame");
        {
            const double fs = 48000.0;
            DynamicsHistory h;
            h.prepare (fs, 5.0);
            const int n = (int) fs;   // 1 s
            for (int i = 0; i < n; ++i)
            {
                const float x = 0.5f * (float) std::sin (twoPi * 1000.0 * i / fs);
                h.add (std::abs (x), std::abs (0.5f * x), 6.0f);
            }
            std::vector<DynamicsFrame> frames (DynamicsHistory::capacity);
            const int got = h.pull (frames.data(), (int) frames.size());
            expectEquals (got, n / juce::roundToInt (fs * 0.005));
            expectWithinAbsoluteError (h.frameSeconds(), 0.005, 1.0e-9);
            const auto& last = frames[(size_t) got - 1];
            expectWithinAbsoluteError (last.inputDb, -6.02f, 0.1f);
            expectWithinAbsoluteError (last.outputDb, -12.04f, 0.1f);
            expectWithinAbsoluteError (last.gainReductionDb, 6.0f, 1.0e-6f);
        }

        beginTest ("Dynamics feed: never blocks, drops when full");
        {
            DynamicsHistory h;
            h.prepare (48000.0, 1.0);
            const int samplesPerFrame = 48;
            for (int i = 0; i < 10 * DynamicsHistory::capacity * samplesPerFrame; ++i)
                h.add (0.1f, 0.1f, 0.0f);
            std::vector<DynamicsFrame> frames (DynamicsHistory::capacity * 2);
            expectEquals (h.pull (frames.data(), (int) frames.size()), DynamicsHistory::capacity);
            expectEquals (frames[0].inputDb, juce::Decibels::gainToDecibels (0.1f, -120.0f));
            // Silence reads as the floor, never -inf
            h.reset();
            for (int i = 0; i < samplesPerFrame; ++i)
                h.add (0.0f, 0.0f, 0.0f);
            expectEquals (h.pull (frames.data(), 4), 1);
            expectEquals (frames[0].inputDb, -120.0f);
        }
    }
};

static DynamicsFeedTests dynamicsFeedTests;

//==============================================================================
namespace
{
    using Buffer = juce::AudioBuffer<float>;

    Buffer sineBuffer (double fs, double freq, float amp, double seconds, int channels = 2)
    {
        const int n = (int) (seconds * fs);
        Buffer b (channels, n);
        for (int i = 0; i < n; ++i)
        {
            const float v = amp * (float) std::sin (twoPi * freq * i / fs);
            for (int c = 0; c < channels; ++c)
                b.setSample (c, i, v);
        }
        return b;
    }

    // Runs any module with process (float* const*, int, int) in blocks
    template <typename Module>
    void runBlocks (Module& m, Buffer& b, int block, std::function<void (int)> before = {})
    {
        for (int start = 0; start < b.getNumSamples(); start += block)
        {
            const int n = std::min (block, b.getNumSamples() - start);
            if (before)
                before (start);
            float* ch[2] { b.getWritePointer (0, start), b.getWritePointer (std::min (1, b.getNumChannels() - 1), start) };
            m.process (ch, b.getNumChannels(), n);
        }
    }

    // Amplitude of the component at `freq` (Goertzel-like DFT bin over [start, start + len))
    double toneAmp (const Buffer& b, int start, int len, double freq, double fs)
    {
        double re = 0.0, im = 0.0;
        for (int i = 0; i < len; ++i)
        {
            const double ph = twoPi * freq * (start + i) / fs;
            re += b.getSample (0, start + i) * std::cos (ph);
            im += b.getSample (0, start + i) * std::sin (ph);
        }
        return 2.0 * std::sqrt (re * re + im * im) / len;
    }

    // THD in dB (harmonics 2..10) over a whole number of periods
    double thdDb (const Buffer& b, int start, int len, double f0, double fs)
    {
        const double h1 = toneAmp (b, start, len, f0, fs);
        double sum = 0.0;
        for (int k = 2; k <= 10; ++k)
            sum += std::pow (toneAmp (b, start, len, k * f0, fs), 2.0);
        return 20.0 * std::log10 (std::sqrt (sum) / h1 + 1.0e-20);
    }

    float peakDb (const Buffer& b, int start, int len)
    {
        return juce::Decibels::gainToDecibels (b.getMagnitude (0, start, len), -200.0f);
    }

    CompSettings vocalComp()
    {
        CompSettings s;
        s.thresholdDb = -24.0f; s.ratio = 4.0f; s.kneeDb = 6.0f; s.attackMs = 10.0f; s.releaseMs = 120.0f; s.makeupDb = 0.0f;
        return s;
    }
}

class CompressorQualityTests final : public juce::UnitTest
{
public:
    CompressorQualityTests() : juce::UnitTest ("Compressor quality", "Dynamics") {}

    void runTest() override
    {
        for (double fs : { 48000.0, 96000.0 })
        {
            beginTest ("Compressor: low ballistics distortion at 100 Hz (" + juce::String (fs / 1000.0, 0) + " kHz)");
            CompressorModule m;
            m.prepare (fs, 512);
            m.setSettings (vocalComp());
            auto b = sineBuffer (fs, 100.0, juce::Decibels::decibelsToGain (-10.0f), 3.0);
            runBlocks (m, b, 512);
            const double thd = thdDb (b, (int) (2.0 * fs), (int) fs, 100.0, fs);
            logMessage ("THD at 100 Hz, -10 dBFS, 10/120 ms: " + juce::String (thd, 1) + " dB (baseline -48, Pro-C 2 Vocal -57)");
            expectLessThan (thd, -60.0);
        }

        beginTest ("Compressor: static curve within 0.3 dB of the ideal knee");
        {
            const double fs = 48000.0;
            CompressorModule m;
            m.prepare (fs, 512);
            m.setSettings (vocalComp());
            float worst = 0.0f, worstAt = 0.0f;
            for (float level = -40.0f; level <= 0.01f; level += 2.0f)
            {
                auto b = sineBuffer (fs, 1000.0, juce::Decibels::decibelsToGain (level), 0.6);
                runBlocks (m, b, 512);
                const float outDb = peakDb (b, (int) (0.45 * fs), (int) (0.15 * fs));
                const float ideal = CompressorModule::computeGainDb (level, -24.0f, 4.0f, 6.0f);
                if (std::abs (outDb - ideal) > std::abs (worst))
                {
                    worst = outDb - ideal;
                    worstAt = level;
                }
            }
            logMessage ("Largest static curve deviation " + juce::String (worst, 2) + " dB at " + juce::String (worstAt, 0) + " dBFS");
            expectLessThan (std::abs (worst), 0.3f);
        }

        beginTest ("Compressor: block size does not change the output");
        {
            const double fs = 48000.0;
            juce::Random r (5);
            Buffer in (2, (int) fs);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < in.getNumSamples(); ++i)
                    in.setSample (c, i, 0.4f * (r.nextFloat() * 2.0f - 1.0f) * (float) (0.5 + 0.5 * std::sin (twoPi * 3.0 * i / fs)));
            Buffer outs[2] { in, in };
            const int blocks[2] { 1, 512 };
            for (int k = 0; k < 2; ++k)
            {
                CompressorModule m;
                m.prepare (fs, 512);
                m.setSettings (vocalComp());
                runBlocks (m, outs[k], blocks[k]);
            }
            float maxDiff = 0.0f;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < in.getNumSamples(); ++i)
                    maxDiff = std::max (maxDiff, std::abs (outs[0].getSample (c, i) - outs[1].getSample (c, i)));
            expectLessThan (maxDiff, 1.0e-6f);
        }
    }
};

static CompressorQualityTests compressorQualityTests;

//==============================================================================
namespace
{
    // Seconds from `from` until the compressor's gain reduction falls below `belowDb`
    double recoverySeconds (CompressorModule& m, double fs, double belowDb)
    {
        Buffer quiet (2, 16);
        for (int k = 0; k < (int) (3.0 * fs / 16); ++k)
        {
            quiet.clear();
            float* ch[2] { quiet.getWritePointer (0), quiet.getWritePointer (1) };
            m.process (ch, 2, 16);
            if (m.getGainReductionDb() < belowDb)
                return (k + 1) * 16 / fs;
        }
        return 3.0;
    }

    double maxSecondDifference (const Buffer& b, int start, int len)
    {
        double d = 0.0;
        for (int i = start + 1; i < start + len - 1; ++i)
            d = std::max (d, (double) std::abs (b.getSample (0, i + 1) - 2.0f * b.getSample (0, i) + b.getSample (0, i - 1)));
        return d;
    }
}

class CompressorOptionTests final : public juce::UnitTest
{
public:
    CompressorOptionTests() : juce::UnitTest ("Compressor options", "Dynamics") {}

    void runTest() override
    {
        const double fs = 48000.0;

        beginTest ("Compressor auto release: short bursts recover faster than sustained compression");
        {
            double rec[2][2] {};   // [auto][short/long]
            for (int a = 0; a < 2; ++a)
                for (int l = 0; l < 2; ++l)
                {
                    CompressorModule m;
                    m.prepare (fs, 512);
                    auto s = vocalComp();
                    s.thresholdDb = -30.0f;
                    s.autoRelease = a == 1;
                    m.setSettings (s);
                    auto b = sineBuffer (fs, 1000.0, 0.5f, l == 0 ? 0.03 : 2.0);
                    runBlocks (m, b, 64);
                    rec[a][l] = recoverySeconds (m, fs, 3.0);
                }
            logMessage ("Recovery to 3 dB GR after 30 ms / 2 s: fixed " + juce::String (rec[0][0] * 1000.0, 0) + " / "
                        + juce::String (rec[0][1] * 1000.0, 0) + " ms, auto " + juce::String (rec[1][0] * 1000.0, 0) + " / "
                        + juce::String (rec[1][1] * 1000.0, 0) + " ms");
            expectLessThan (rec[1][0], 0.6 * rec[1][1], "auto: program dependent");
            expectWithinAbsoluteError (rec[0][0] / rec[0][1], 1.0, 0.25, "fixed: same release for both");
        }

        beginTest ("Compressor auto makeup keeps a -18 dBFS tone at its level");
        {
            CompressorModule m;
            m.prepare (fs, 512);
            auto s = vocalComp();
            s.autoMakeup = true;
            m.setSettings (s);
            auto b = sineBuffer (fs, 1000.0, juce::Decibels::decibelsToGain (-18.0f), 1.0);
            runBlocks (m, b, 512);
            const float outDb = peakDb (b, (int) (0.8 * fs), (int) (0.2 * fs));
            logMessage ("Auto makeup " + juce::String (CompressorModule::autoMakeupDb (s), 2) + " dB, -18 dBFS tone out at "
                        + juce::String (outDb, 2) + " dBFS");
            expectWithinAbsoluteError (outDb, -18.0f, 0.3f);
            expectGreaterThan (CompressorModule::autoMakeupDb (s), 1.0f);
        }

        beginTest ("Compressor transfer curve matches the rendered steady state");
        {
            CompressorModule m;
            m.prepare (fs, 512);
            auto s = vocalComp();
            s.makeupDb = 3.0f;
            s.autoMakeup = true;
            m.setSettings (s);
            float worst = 0.0f;
            for (float level = -40.0f; level <= -0.99f; level += 3.0f)
            {
                auto b = sineBuffer (fs, 1000.0, juce::Decibels::decibelsToGain (level), 0.5);
                runBlocks (m, b, 512);
                worst = std::max (worst, std::abs (peakDb (b, (int) (0.35 * fs), (int) (0.15 * fs)) - CompressorModule::transferDb (s, level)));
            }
            logMessage ("Largest transfer curve error " + juce::String (worst, 3) + " dB");
            expectLessThan (worst, 0.3f);
        }

        beginTest ("Compressor: toggles do not click");
        {
            for (int which = 0; which < 2; ++which)
            {
                CompressorModule m;
                m.prepare (fs, 512);
                auto s = vocalComp();
                s.thresholdDb = -30.0f;
                m.setSettings (s);
                auto b = sineBuffer (fs, 1000.0, 0.5f, 1.0);
                runBlocks (m, b, 512, [&] (int start)
                {
                    if (start == 512 * 47)
                    {
                        (which == 0 ? s.autoMakeup : s.autoRelease) = true;
                        m.setSettings (s);
                    }
                });
                const double omega2 = std::pow (twoPi * 1000.0 / fs, 2.0);
                const double limit = 1.5 * omega2 * b.getMagnitude (0, 0, b.getNumSamples());
                const double d2 = maxSecondDifference (b, (int) (0.3 * fs), (int) (0.7 * fs));
                logMessage (juce::String (which == 0 ? "Auto makeup" : "Auto release") + " toggle: max 2nd difference "
                            + juce::String (d2, 4) + " (smooth limit " + juce::String (limit, 4) + ")");
                expectLessThan (d2, limit);
            }
        }

        beginTest ("Compressor: silence stays silent");
        {
            CompressorModule m;
            m.prepare (fs, 512);
            auto s = vocalComp();
            s.autoMakeup = s.autoRelease = true;
            m.setSettings (s);
            Buffer b (2, 4800);
            b.clear();
            runBlocks (m, b, 512);
            expectEquals (b.getMagnitude (0, b.getNumSamples()), 0.0f);
            expectEquals (m.getGainReductionDb(), 0.0f);
        }

        beginTest ("Compressor options: parameters exist and default to the old behaviour");
        {
            Harness h;
            expectEquals (h.get (pid::compAutoRelease), 0.0f);
            expectEquals (h.get (pid::compAutoMakeup), 0.0f);
            h.set (pid::compAutoRelease, 1.0f);
            h.set (pid::compAutoMakeup, 1.0f);
            expectEquals (h.get (pid::compAutoRelease), 1.0f);
        }
    }
};

static CompressorOptionTests compressorOptionTests;

//==============================================================================
class DynamicsFeedModuleTests final : public juce::UnitTest
{
public:
    DynamicsFeedModuleTests() : juce::UnitTest ("Dynamics module feeds", "Dynamics") {}

    void runTest() override
    {
        const double fs = 48000.0;
        std::vector<DynamicsFrame> frames (DynamicsHistory::capacity);

        beginTest ("Compressor feed: history shows input, output and gain reduction");
        {
            CompressorModule m;
            m.prepare (fs, 512);
            auto s = vocalComp();
            s.thresholdDb = -30.0f; s.kneeDb = 0.0f;
            m.setSettings (s);
            auto b = sineBuffer (fs, 1000.0, 0.5f, 1.0);
            runBlocks (m, b, 512);
            const int got = m.history.pull (frames.data(), (int) frames.size());
            expectEquals (got, (int) (fs / juce::roundToInt (fs * 0.005)));
            const auto& f = frames[(size_t) got - 1];
            logMessage ("Last frame: in " + juce::String (f.inputDb, 2) + " out " + juce::String (f.outputDb, 2) + " GR "
                        + juce::String (f.gainReductionDb, 2) + "; current GR " + juce::String (m.currentGainReductionDb.load(), 2));
            expectWithinAbsoluteError (f.inputDb, -6.02f, 0.3f);
            expectWithinAbsoluteError (f.outputDb, -24.0f, 0.5f);
            expectWithinAbsoluteError (f.gainReductionDb, 18.0f, 0.5f);
            expectWithinAbsoluteError (m.currentGainReductionDb.load(), 18.0f, 0.5f);
        }

        beginTest ("Smash feed: gain reduction column");
        {
            SmashModule m;
            m.prepare (fs, 512);
            SmashSettings s;
            s.enabled = true; s.amount = 0.8f;
            m.setSettings (s);
            auto b = sineBuffer (fs, 1000.0, 0.5f, 1.0);
            runBlocks (m, b, 512);
            const int got = m.history.pull (frames.data(), (int) frames.size());
            expectGreaterThan (got, 150);
            const auto& f = frames[(size_t) got - 1];
            logMessage ("Smash last frame: in " + juce::String (f.inputDb, 2) + " out " + juce::String (f.outputDb, 2) + " GR "
                        + juce::String (f.gainReductionDb, 2));
            expectWithinAbsoluteError (f.inputDb, -6.02f, 0.3f);
            expectGreaterThan (f.gainReductionDb, 10.0f);
            expectWithinAbsoluteError (m.currentGainReductionDb.load(), f.gainReductionDb, 1.0f);
            expectWithinAbsoluteError (f.outputDb, juce::Decibels::gainToDecibels (b.getMagnitude (0, b.getNumSamples() - 240, 240)), 0.1f);
        }
    }
};

static DynamicsFeedModuleTests dynamicsFeedModuleTests;

//==============================================================================
namespace
{
    // Phrase material: 250 ms vowel (300 Hz) then 80 ms "S" (7/8/9 kHz partials), repeated
    struct SibilantPhrase
    {
        Buffer buffer;
        std::vector<std::pair<int, int>> sRegions;
    };

    SibilantPhrase sibilantPhrase (double fs, float amp, int repeats = 8)
    {
        const int vowel = (int) (0.25 * fs), s = (int) (0.08 * fs);
        SibilantPhrase p { Buffer (2, repeats * (vowel + s)), {} };
        juce::Random r (11);
        double ph[3] { r.nextDouble(), r.nextDouble(), r.nextDouble() };
        for (int k = 0; k < repeats; ++k)
        {
            const int v0 = k * (vowel + s), s0 = v0 + vowel;
            for (int i = 0; i < vowel; ++i)
                for (int c = 0; c < 2; ++c)
                    p.buffer.setSample (c, v0 + i, amp * (float) std::sin (twoPi * 300.0 * (v0 + i) / fs));
            for (int i = 0; i < s; ++i)
            {
                float v = 0.0f;
                for (int h = 0; h < 3; ++h)
                    v += 0.25f * amp * (float) std::sin (twoPi * ((7000.0 + 1000.0 * h) * (s0 + i) / fs + ph[h]));
                for (int c = 0; c < 2; ++c)
                    p.buffer.setSample (c, s0 + i, v);
            }
            p.sRegions.push_back ({ s0, s });
        }
        return p;
    }

    double energy (const Buffer& b, int start, int len)
    {
        double e = 0.0;
        for (int i = start; i < start + len; ++i)
            e += (double) b.getSample (0, i) * b.getSample (0, i);
        return e;
    }

    // Mean reduction (dB) of the S regions, skipping the first two repeats
    double sReduction (const SibilantPhrase& in, const Buffer& out)
    {
        double ei = 0.0, eo = 0.0;
        for (size_t k = 2; k < in.sRegions.size(); ++k)
        {
            ei += energy (in.buffer, in.sRegions[k].first, in.sRegions[k].second);
            eo += energy (out, in.sRegions[k].first, in.sRegions[k].second);
        }
        return 10.0 * std::log10 (ei / eo);
    }

    DeEssSettings deessSettings (bool relative)
    {
        DeEssSettings s;
        s.freq = 6500.0f; s.thresholdDb = -30.0f; s.rangeDb = 12.0f; s.mode = 0; s.relative = relative;
        return s;
    }
}

class DeEsserQualityTests final : public juce::UnitTest
{
public:
    DeEsserQualityTests() : juce::UnitTest ("De-esser quality", "Dynamics") {}

    void runTest() override
    {
        const double fs = 48000.0;

        beginTest ("De-esser relative detection: loud and quiet phrases are treated alike");
        {
            double red[2][2] {};   // [relative][loud/quiet]
            for (int rel = 0; rel < 2; ++rel)
                for (int q = 0; q < 2; ++q)
                {
                    DeEsserModule m;
                    m.prepare (fs, 512);
                    m.setSettings (deessSettings (rel == 1));
                    const auto in = sibilantPhrase (fs, q == 0 ? 0.5f : 0.05f);
                    auto out = in.buffer;
                    runBlocks (m, out, 512);
                    red[rel][q] = sReduction (in, out);
                }
            logMessage ("S reduction loud / quiet: absolute " + juce::String (red[0][0], 2) + " / " + juce::String (red[0][1], 2)
                        + " dB, relative " + juce::String (red[1][0], 2) + " / " + juce::String (red[1][1], 2) + " dB");
            expectGreaterThan (red[0][0] - red[0][1], 3.0, "absolute: level dependent (old behaviour)");
            expectLessThan (std::abs (red[1][0] - red[1][1]), 1.0, "relative: level independent");
            expectGreaterThan (red[1][1], 3.0, "relative: quiet phrase de-essed as well");
        }

        beginTest ("De-esser split band nulls when inactive");
        {
            for (int rel = 0; rel < 2; ++rel)
            {
                DeEsserModule m;
                m.prepare (fs, 512);
                auto s = deessSettings (rel == 1);
                s.thresholdDb = 0.0f;
                m.setSettings (s);
                const auto in = sineBuffer (fs, 1000.0, 0.1f, 1.0);
                auto out = in;
                runBlocks (m, out, 512);
                float d = 0.0f;
                for (int i = 0; i < in.getNumSamples(); ++i)
                    d = std::max (d, std::abs (in.getSample (0, i) - out.getSample (0, i)));
                expectEquals (d, 0.0f);
            }
        }

        beginTest ("De-esser: silence stays silent");
        {
            DeEsserModule m;
            m.prepare (fs, 512);
            m.setSettings (deessSettings (true));
            Buffer b (2, 9600);
            b.clear();
            runBlocks (m, b, 512);
            expectEquals (b.getMagnitude (0, b.getNumSamples()), 0.0f);
            expectEquals (m.currentReductionDb.load(), 0.0f);
        }

        beginTest ("De-esser feed: spectrum, reduction and band range");
        {
            DeEsserModule m;
            m.prepare (fs, 512);
            m.setSettings (deessSettings (false));
            const auto in = sibilantPhrase (fs, 0.5f, 3);
            auto out = in.buffer;
            float maxRed = 0.0f;
            runBlocks (m, out, 256, [&] (int) { maxRed = std::max (maxRed, m.currentReductionDb.load()); });
            expectGreaterThan (maxRed, 6.0f);
            expectEquals (m.bandLowHz.load(), 6500.0f);
            expectEquals (m.bandHighHz.load(), 20000.0f);
            std::vector<float> tap (SampleTap::capacity);
            const int got = m.spectrumTap.pull (tap.data(), (int) tap.size());
            expectGreaterThan (got, 8000);
            expectEquals (tap[0], in.buffer.getSample (0, 0), "the tap carries the input");
        }

        beginTest ("De-esser detection: parameter exists and defaults to Absolute");
        {
            Harness h;
            expectEquals (h.get (pid::deessDetection), 0.0f);
        }
    }
};

static DeEsserQualityTests deEsserQualityTests;

//==============================================================================
namespace
{
    // "Words": noise bursts of `wordMs` at cycling RMS levels, separated by quiet gaps
    struct Words
    {
        Buffer buffer;
        std::vector<std::pair<int, int>> words;   // start, length
    };

    Words wordSequence (double fs, const std::vector<float>& levelsDb, int cycles, double wordMs = 300.0, double gapMs = 80.0,
                        float gapDb = -80.0f)
    {
        const int w = (int) (wordMs * 0.001 * fs), g = (int) (gapMs * 0.001 * fs);
        const int n = (int) levelsDb.size() * cycles;
        Words out { Buffer (2, n * (w + g) + g), {} };
        juce::Random r (31);
        const float noiseRms = 1.0f / std::sqrt (3.0f);
        int pos = 0;
        const auto fill = [&] (int start, int len, float db)
        {
            const float a = juce::Decibels::decibelsToGain (db) / noiseRms;
            for (int i = start; i < start + len; ++i)
            {
                const float v = a * (r.nextFloat() * 2.0f - 1.0f);
                out.buffer.setSample (0, i, v);
                out.buffer.setSample (1, i, v);
            }
        };
        for (int k = 0; k < n; ++k)
        {
            fill (pos, g, gapDb);
            pos += g;
            fill (pos, w, levelsDb[(size_t) k % levelsDb.size()]);
            out.words.push_back ({ pos, w });
            pos += w;
        }
        fill (pos, g, gapDb);
        return out;
    }

    double rmsDbOf (const Buffer& b, int start, int len) { return 10.0 * std::log10 (energy (b, start, len) / len + 1.0e-20); }

    double stdOf (const std::vector<double>& v)
    {
        double m = 0.0, s = 0.0;
        for (auto x : v) m += x;
        m /= (double) v.size();
        for (auto x : v) s += (x - m) * (x - m);
        return std::sqrt (s / (double) v.size());
    }
}

class RideQualityTests final : public juce::UnitTest
{
public:
    RideQualityTests() : juce::UnitTest ("Level Ride quality", "Dynamics") {}

    void runTest() override
    {
        const double fs = 48000.0;

        beginTest ("LEVEL RIDE: word-level evening");
        {
            const auto in = wordSequence (fs, { -19.0f, -10.0f, -15.0f, -6.0f }, 6);   // corrections within +-8 dB
            RideModule m;
            m.prepare (fs, 256);
            RideSettings s;
            s.enabled = true;   // defaults: target -18, amount 70 %, +-8 dB, response 50 %, phrase 40 %
            m.setSettings (s);
            auto out = in.buffer;
            runBlocks (m, out, 256);
            std::vector<double> li, lo;
            for (size_t k = 4; k < in.words.size(); ++k)
            {
                li.push_back (rmsDbOf (in.buffer, in.words[k].first, in.words[k].second));
                lo.push_back (rmsDbOf (out, in.words[k].first, in.words[k].second));
            }
            logMessage ("Word level spread: input " + juce::String (stdOf (li), 2) + " dB, output " + juce::String (stdOf (lo), 2) + " dB");
            // defaults blend 40 % of the phrase level in, so words are evened by about a third
            expectLessThan (stdOf (lo), 0.7 * stdOf (li));
        }

        beginTest ("LEVEL RIDE: breaths in the gaps between words are not lifted");
        {
            // quiet words (boosted) with 200 ms breath noise at -40 dBFS (above Ignore below) in between
            const auto in = wordSequence (fs, { -26.0f }, 6, 400.0, 200.0, -40.0f);
            RideModule m;
            m.prepare (fs, 256);
            RideSettings s;
            s.enabled = true;
            m.setSettings (s);
            auto out = in.buffer;
            float worstRise = 0.0f, endOfWord = 0.0f;
            size_t k = 0;
            runBlocks (m, out, 32, [&] (int start)
            {
                while (k < in.words.size() && start >= in.words[k].first + in.words[k].second)
                {
                    endOfWord = m.currentGainDb.load();
                    ++k;
                }
                const bool inGap = k > 1 && k < in.words.size() && start < in.words[k].first - 32;
                if (inGap)
                    worstRise = std::max (worstRise, m.currentGainDb.load() - endOfWord);
            });
            logMessage ("Largest gain rise in a breath gap: " + juce::String (worstRise, 2) + " dB");
            expectLessThan (worstRise, 0.5f);
        }

        beginTest ("LEVEL RIDE: constant lookahead latency while in the chain, on or off");
        {
            Harness h;
            h.neutral();
            const int base = h.proc.getLatencySamples();
            h.set (pid::rideOn, 1.0f);
            h.run (h.sine (300.0, 0.1f, 0.3));
            const int on = h.proc.getLatencySamples();
            h.set (pid::rideOn, 0.0f);
            h.run (h.sine (300.0, 0.1f, 0.3));
            const int off = h.proc.getLatencySamples();
            logMessage ("Latency: without ride " + juce::String (base) + ", ride on " + juce::String (on) + ", ride off " + juce::String (off));
            expectEquals (on - base, (int) std::lround (RideModule::lookaheadMs * 0.001 * h.fs));
            expectEquals (off, on);
        }
    }
};

static RideQualityTests rideQualityTests;

//==============================================================================
namespace
{
    std::vector<RideSegment> rideSegments (const Buffer& input, int block, RideSettings s = {})
    {
        RideModule m;
        m.prepare (48000.0, 512);
        s.enabled = true;
        m.setSettings (s);
        auto out = input;
        std::vector<RideSegment> segs;
        RideSegment tmp[16];
        runBlocks (m, out, block, [&] (int) { for (int k = m.segments.pull (tmp, 16); k > 0; k = m.segments.pull (tmp, 16)) segs.insert (segs.end(), tmp, tmp + k); });
        Buffer tail (2, 48000);
        tail.clear();
        runBlocks (m, tail, block);
        for (int k = m.segments.pull (tmp, 16); k > 0; k = m.segments.pull (tmp, 16))
            segs.insert (segs.end(), tmp, tmp + k);
        return segs;
    }
}

class RideFeedTests final : public juce::UnitTest
{
public:
    RideFeedTests() : juce::UnitTest ("Level Ride feed", "Dynamics") {}

    void runTest() override
    {
        const double fs = 48000.0;
        const int L = (int) std::lround (RideModule::lookaheadMs * 0.001 * fs);

        beginTest ("LEVEL RIDE feed: one segment per word");
        {
            const auto in = wordSequence (fs, { -20.0f, -12.0f, -16.0f, -24.0f, -14.0f }, 1, 300.0, 150.0);
            const auto segs = rideSegments (in.buffer, 256);
            expectEquals ((int) segs.size(), 5);
            if (segs.size() == 5)
            {
                RideModule m;   // the rendered gain, for "after"
                m.prepare (fs, 512);
                RideSettings s;
                s.enabled = true;
                m.setSettings (s);
                auto out = in.buffer;
                runBlocks (m, out, 256);
                const float set[5] { -20.0f, -12.0f, -16.0f, -24.0f, -14.0f };
                for (int k = 0; k < 5; ++k)
                {
                    const auto& g = segs[(size_t) k];
                    const int ws = in.words[(size_t) k].first, wl = in.words[(size_t) k].second;
                    logMessage ("Segment " + juce::String (k) + ": start " + juce::String ((g.startSample - L - ws) / fs * 1000.0, 1)
                                + " ms off, length " + juce::String (g.lengthSamples / fs * 1000.0, 0) + " ms, before "
                                + juce::String (g.levelBeforeDb, 2) + " after " + juce::String (g.levelAfterDb, 2));
                    expectWithinAbsoluteError ((double) (g.startSample - L - ws), 0.0, 0.03 * fs, "start");
                    expectWithinAbsoluteError ((double) g.lengthSamples, (double) wl, 0.06 * fs, "length");
                    expectWithinAbsoluteError (g.levelBeforeDb - segs[0].levelBeforeDb, set[k] - set[0], 0.7f, "before levels follow the input");
                    const double applied = rmsDbOf (out, ws + L, wl) - rmsDbOf (in.buffer, ws, wl);
                    expectWithinAbsoluteError ((double) (g.levelAfterDb - g.levelBeforeDb), applied, 1.0, "after - before = applied gain");
                }
            }
        }

        beginTest ("LEVEL RIDE feed: a dip between words splits them");
        {
            auto in = wordSequence (fs, { -14.0f, -30.0f, -14.0f }, 1, 400.0, 0.0, -14.0f);   // 400 / 100 dip / 400
            // shorten the middle "word" to a 100 ms dip 16 dB down
            const auto& w = in.words;
            in.buffer.applyGain (w[1].first + (int) (0.1 * fs), w[1].second - (int) (0.1 * fs), 0.0f);
            Buffer b (2, w[1].first + (int) (0.1 * fs) + w[2].second);
            b.clear();
            for (int c = 0; c < 2; ++c)
            {
                b.copyFrom (c, 0, in.buffer, c, 0, w[1].first + (int) (0.1 * fs));
                b.copyFrom (c, w[1].first + (int) (0.1 * fs), in.buffer, c, w[2].first, w[2].second);
            }
            const auto segs = rideSegments (b, 256);
            logMessage ("Segments with a 100 ms dip: " + juce::String ((int) segs.size()));
            expectEquals ((int) segs.size(), 2);
        }

        beginTest ("LEVEL RIDE feed: sustained notes are split every 2 s, block size does not matter");
        {
            const auto in = wordSequence (fs, { -16.0f }, 1, 5000.0, 100.0);
            const auto a = rideSegments (in.buffer, 256), b = rideSegments (in.buffer, 1);
            expectEquals ((int) a.size(), 3);
            expect (a.size() == b.size());
            for (size_t k = 0; k < std::min (a.size(), b.size()); ++k)
            {
                expectEquals ((double) a[k].startSample, (double) b[k].startSample);
                expectEquals (a[k].lengthSamples, b[k].lengthSamples);
                expectWithinAbsoluteError (a[k].levelAfterDb, b[k].levelAfterDb, 1.0e-3f);
            }
        }
    }
};

static RideFeedTests rideFeedTests;
