// Loudness (BS.1770), level matcher, stereo width, correlation, mono monitor.

#include "TestHelpers.h"

using namespace velvox;
using namespace velvox::test;

namespace
{
    // Integrated loudness of [start, start + len)
    float lufsOf (const juce::AudioBuffer<float>& b, int start, int len, double fs)
    {
        return dsp::LoudnessMeter::integratedLufs (b.getArrayOfReadPointers(), b.getNumChannels(),
                                                   start + len, fs, start);
    }

    juce::AudioBuffer<float> stereoNoise (double fs, double seconds, float amp, int seedL, int seedR)
    {
        juce::Random rl (seedL), rr (seedR);
        const int n = (int) (seconds * fs);
        juce::AudioBuffer<float> b (2, n);
        for (int i = 0; i < n; ++i)
        {
            b.setSample (0, i, amp * (rl.nextFloat() * 2.0f - 1.0f));
            b.setSample (1, i, amp * (rr.nextFloat() * 2.0f - 1.0f));
        }
        return b;
    }
}

class OutputStageTests final : public juce::UnitTest
{
public:
    OutputStageTests() : juce::UnitTest ("Velvox output stage", "Velvox") {}

    void runTest() override
    {
        beginTest ("K-weighting curve (ITU-R BS.1770)");
        {
            for (double fs : { 44100.0, 48000.0, 96000.0 })
            {
                const double at1k = dsp::LoudnessMeter::kWeightingGainDb (997.0, fs);
                const double at10k = dsp::LoudnessMeter::kWeightingGainDb (10000.0, fs);
                const double at20 = dsp::LoudnessMeter::kWeightingGainDb (20.0, fs);
                logMessage ("fs " + juce::String (fs) + ": 1 kHz " + juce::String (at1k, 2) + " dB, 10 kHz "
                            + juce::String (at10k, 2) + " dB, 20 Hz " + juce::String (at20, 1) + " dB");
                expectWithinAbsoluteError (at1k, 0.69, 0.1);
                expect (at10k > 3.5 && at10k < 4.5);
                expectLessThan (at20, -8.0);
            }
        }

        beginTest ("Loudness calibration: -20 dBFS 1 kHz sine on both channels reads -20 LUFS");
        {
            const double fs = 48000.0;
            dsp::LoudnessMeter m;
            m.prepare (fs);
            m.setWindowSeconds (0.4f);
            juce::AudioBuffer<float> b (2, (int) (4 * fs));
            for (int i = 0; i < b.getNumSamples(); ++i)
                for (int ch = 0; ch < 2; ++ch)
                    b.setSample (ch, i, 0.1f * (float) std::sin (2.0 * pi * 997.0 * i / fs));
            m.process (b.getArrayOfReadPointers(), 2, b.getNumSamples());
            logMessage ("Measured: " + juce::String (m.getLufs(), 2) + " LUFS");
            expectWithinAbsoluteError (m.getLufs(), -20.0f, 0.15f);
        }

        beginTest ("Auto gain brings a louder preset back to input loudness");
        {
            Harness h;
            for (auto& p : getFactoryPresets())
                if (p.name == "Aggressive Vocal")
                    p.applyTo (h.proc.getState());
            h.set (pid::outputGain, 0.0f);
            h.set (pid::inputGain, 6.0f);        // make the processed signal clearly louder
            h.set (pid::levelMatchMode, 2.0f);
            h.set (pid::levelMatchWindow, 0.0f); // fast

            const auto in = h.noise (0.05f, 8.0, 51);
            const auto out = h.run (in);
            const int start = (int) (5.0 * h.fs), len = (int) (3.0 * h.fs);
            const float inL = lufsOf (in, start, len, h.fs);
            const float outL = lufsOf (out, start, len, h.fs);
            const float corr = h.proc.getChain().meters.levelMatchDb.load();
            logMessage ("Input " + juce::String (inL, 2) + " LUFS, output " + juce::String (outL, 2)
                        + " LUFS, correction " + juce::String (corr, 2) + " dB");
            expectWithinAbsoluteError (outL, inL, 1.0f);

            // The same chain without matching: the correction must equal its excess loudness
            h.set (pid::levelMatchMode, 0.0f);
            const auto unmatched = h.run (in);
            const float excess = lufsOf (unmatched, start, len, h.fs) - inL;
            logMessage ("Unmatched excess loudness: " + juce::String (excess, 2) + " dB");
            expectGreaterThan (excess, 1.0f, "test setup must be louder than the input");
            expectWithinAbsoluteError (corr, -excess, 0.7f);
        }

        beginTest ("Auto gain never exceeds its range");
        {
            Harness h;
            h.neutral();
            h.set (pid::inputGain, 18.0f);       // processed is 18 dB louder than the input
            h.set (pid::levelMatchMode, 2.0f);
            h.set (pid::levelMatchWindow, 0.0f);
            h.set (pid::levelMatchRange, 12.0f);
            h.run (h.noise (0.01f, 8.0, 53));
            const float corr = h.proc.getChain().meters.levelMatchDb.load();
            logMessage ("Correction with 18 dB difference and 12 dB range: " + juce::String (corr, 2) + " dB");
            expectWithinAbsoluteError (corr, -12.0f, 0.05f);
        }

        beginTest ("Bypass match: bypass is as loud as the processed sound");
        {
            Harness h;
            for (auto& p : getFactoryPresets())
                if (p.name == "Modern Pop")
                    p.applyTo (h.proc.getState());
            h.set (pid::outputGain, -3.0f);
            h.set (pid::levelMatchMode, 1.0f);
            h.set (pid::levelMatchWindow, 0.0f);
            h.run (h.noise (0.05f, 5.0, 55));           // let the matcher learn
            const float processedHeard = h.proc.getChain().meters.processedLufs.load() - 3.0f;

            h.set (pid::bypass, 1.0f);
            const auto out = h.run (h.noise (0.05f, 4.0, 56));
            const float bypassL = lufsOf (out, (int) (1.0 * h.fs), (int) (3.0 * h.fs), h.fs);
            logMessage ("Processed (heard) " + juce::String (processedHeard, 2) + " LUFS, bypass " + juce::String (bypassL, 2) + " LUFS");
            expectWithinAbsoluteError (bypassL, processedHeard, 1.0f);
        }

        beginTest ("Level matcher holds its correction during silence");
        {
            Harness h;
            h.neutral();
            h.set (pid::compOn, 1.0f);
            h.set (pid::compMakeup, 6.0f);
            h.set (pid::compThreshold, 0.0f);
            h.set (pid::levelMatchMode, 2.0f);
            h.set (pid::levelMatchWindow, 0.0f);
            h.run (h.noise (0.05f, 4.0, 57));
            const float before = h.proc.getChain().meters.levelMatchDb.load();
            const auto out = h.run (h.silence (4.0));
            const float after = h.proc.getChain().meters.levelMatchDb.load();
            logMessage ("Correction before / after 4 s silence: " + juce::String (before, 3) + " / " + juce::String (after, 3) + " dB");
            expectWithinAbsoluteError (before, -6.0f, 0.5f);
            expectWithinAbsoluteError (after, before, 0.01f);
            expect (allFinite (out));
        }

        beginTest ("Width: 0 % is mono, 200 % doubles the side, mono sum never changes");
        {
            const auto runWidth = [] (float width)
            {
                Harness h;
                h.neutral();
                h.set (pid::stereoWidth, width);
                const auto in = stereoNoise (h.fs, 1.0, 0.3f, 61, 62);
                return std::make_pair (in, h.run (in));
            };

            const int L = 3, from = 4800;
            {
                const auto [in, out] = runWidth (0.0f);
                double diff = 0.0;
                for (int i = from; i < out.getNumSamples(); ++i)
                    diff = std::max (diff, (double) std::abs (out.getSample (0, i) - out.getSample (1, i)));
                expectLessThan (diff, 1.0e-6, "0 % must be mono");
            }
            for (float w : { 0.0f, 50.0f, 150.0f, 200.0f })
            {
                const auto [in, out] = runWidth (w);
                double sumErr = 0.0, sideErr = 0.0;
                for (int i = from; i < out.getNumSamples(); ++i)
                {
                    const float inSum = in.getSample (0, i - L) + in.getSample (1, i - L);
                    const float outSum = out.getSample (0, i) + out.getSample (1, i);
                    sumErr = std::max (sumErr, (double) std::abs (inSum - outSum));
                    const float inSide = in.getSample (0, i - L) - in.getSample (1, i - L);
                    const float outSide = out.getSample (0, i) - out.getSample (1, i);
                    sideErr = std::max (sideErr, (double) std::abs (outSide - inSide * w * 0.01f));
                }
                expectLessThan (sumErr, 1.0e-5, "mono sum unchanged at width " + juce::String (w));
                expectLessThan (sideErr, 1.0e-5, "side scaled by width " + juce::String (w));
            }
        }

        beginTest ("Mono below: low side content removed, highs keep their width");
        {
            const auto sideLevel = [] (double freq)
            {
                Harness h;
                h.neutral();
                h.set (pid::monoBelow, 150.0f);
                juce::AudioBuffer<float> in (2, (int) h.fs);
                for (int i = 0; i < in.getNumSamples(); ++i)
                {
                    const float s = 0.3f * (float) std::sin (2.0 * pi * freq * i / h.fs);
                    in.setSample (0, i, s);
                    in.setSample (1, i, -s); // pure side signal
                }
                const auto out = h.run (in);
                juce::AudioBuffer<float> side (1, out.getNumSamples());
                for (int i = 0; i < out.getNumSamples(); ++i)
                    side.setSample (0, i, 0.5f * (out.getSample (0, i) - out.getSample (1, i)));
                return rmsDb (side, 24000, 24000) - juce::Decibels::gainToDecibels (0.3f / std::sqrt (2.0f));
            };
            const float low = sideLevel (40.0), high = sideLevel (2000.0);
            logMessage ("Side at 40 Hz: " + juce::String (low, 1) + " dB, at 2 kHz: " + juce::String (high, 2) + " dB");
            expectLessThan (low, -20.0f);
            expectWithinAbsoluteError (high, 0.0f, 0.3f);
        }

        beginTest ("Correlation meter");
        {
            const double fs = 48000.0;
            const auto measure = [fs] (const juce::AudioBuffer<float>& b)
            {
                dsp::CorrelationMeter c;
                c.prepare (fs);
                c.process (b.getReadPointer (0), b.getReadPointer (1), b.getNumSamples());
                return c.getCorrelation();
            };
            auto same = stereoNoise (fs, 1.0, 0.3f, 71, 71);
            auto inverted = same;
            inverted.applyGain (1, 0, inverted.getNumSamples(), -1.0f);
            const auto independent = stereoNoise (fs, 1.0, 0.3f, 72, 73);
            expectWithinAbsoluteError (measure (same), 1.0f, 1.0e-4f);
            expectWithinAbsoluteError (measure (inverted), -1.0f, 1.0e-4f);
            expectLessThan (std::abs (measure (independent)), 0.15f);
        }

        beginTest ("Mono monitor sums to mono and is never saved");
        {
            Harness h;
            h.neutral();
            h.proc.setMonoMonitor (true);
            const auto out = h.run (stereoNoise (h.fs, 0.5, 0.3f, 81, 82));
            double diff = 0.0;
            for (int i = 0; i < out.getNumSamples(); ++i)
                diff = std::max (diff, (double) std::abs (out.getSample (0, i) - out.getSample (1, i)));
            expectLessThan (diff, 1.0e-7);

            juce::MemoryBlock block;
            h.proc.getStateInformation (block);
            Harness h2;
            h2.proc.setStateInformation (block.getData(), (int) block.getSize());
            expect (! h2.proc.isMonoMonitor());
        }

        beginTest ("Level matching on a mono track and with extreme input");
        {
            Harness h (48000.0, 512, 1);
            h.set (pid::levelMatchMode, 2.0f);
            h.set (pid::inputGain, 24.0f);
            auto in = h.noise (1.0f, 3.0, 91);
            const auto out = h.run (in);
            expect (allFinite (out));
            expectLessOrEqual (peakOf (out, 0, out.getNumSamples()), 1.0f);
        }
    }
};

static OutputStageTests outputStageTests;
