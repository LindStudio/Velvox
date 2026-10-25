// 0.6 dynamics quality track: Compressor, De-Esser, Vocal Smash, Level Ride.
// Measurable quality targets (QUALITY_BASELINE gaps #6 and #8), the new options and the
// lock-free UI data feeds of the module panels.

#include "TestHelpers.h"
#include "DSP/DynamicsFeed.h"
#include "DSP/CompressorModule.h"

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
