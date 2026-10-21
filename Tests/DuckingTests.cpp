// Reverb / delay ducking with internal and external (sidechain) detection.

#include "TestHelpers.h"

using namespace velvox;
using namespace velvox::test;

namespace
{
    constexpr double fs = 48000.0;

    // Vocal-like test signal: noise for `onSeconds`, then silence
    juce::AudioBuffer<float> burstThenSilence (double onSeconds, double totalSeconds, float amp, int seed)
    {
        juce::Random r (seed);
        juce::AudioBuffer<float> b (2, (int) (totalSeconds * fs));
        b.clear();
        for (int i = 0; i < (int) (onSeconds * fs); ++i)
        {
            const float v = amp * (r.nextFloat() * 2.0f - 1.0f);
            b.setSample (0, i, v);
            b.setSample (1, i, v);
        }
        return b;
    }

    void reverbSend (Harness& h)
    {
        h.neutral();
        h.set (pid::reverbOn, 1.0f);
        h.set (pid::reverbMix, 50.0f);
        h.set (pid::reverbDecay, 3.0f);
        h.set (pid::reverbPreDelay, 0.0f);
    }

    // Wet part of the output = output - latency-aligned dry input
    juce::AudioBuffer<float> wetOf (const juce::AudioBuffer<float>& out, const juce::AudioBuffer<float>& in, int latency)
    {
        juce::AudioBuffer<float> w (out);
        for (int ch = 0; ch < w.getNumChannels(); ++ch)
            for (int i = latency; i < w.getNumSamples(); ++i)
                w.setSample (ch, i, out.getSample (ch, i) - in.getSample (ch, i - latency));
        return w;
    }
}

class DuckingTests final : public juce::UnitTest
{
public:
    DuckingTests() : juce::UnitTest ("Velvox ducking", "Velvox") {}

    void runTest() override
    {
        beginTest ("Ducker: reduction = min(amount, level - threshold), with release");
        {
            dsp::Ducker d;
            d.prepare (fs);
            d.setSettings ({ 9.0f, -30.0f, 5.0f, 200.0f });

            std::vector<float> det ((size_t) fs, 0.316f), gains ((size_t) fs); // -10 dBFS
            const float* ptr[1] { det.data() };
            d.computeGains (ptr, 1, (int) fs, gains.data());
            expectWithinAbsoluteError (juce::Decibels::gainToDecibels (gains.back()), -9.0f, 0.05f, "capped at amount");

            d.setSettings ({ 24.0f, -30.0f, 5.0f, 200.0f });
            d.computeGains (ptr, 1, (int) fs, gains.data());
            expectWithinAbsoluteError (juce::Decibels::gainToDecibels (gains.back()), -20.0f, 0.1f, "level - threshold");

            std::fill (det.begin(), det.end(), 0.0f);
            d.computeGains (ptr, 1, (int) (0.2 * fs), gains.data());
            const float afterOneRelease = -juce::Decibels::gainToDecibels (gains[(size_t) (0.2 * fs) - 1]);
            logMessage ("Reduction after one release time: " + juce::String (afterOneRelease, 2) + " dB (from 20 dB)");
            expect (afterOneRelease > 4.0f && afterOneRelease < 10.0f, "about 1/e left after one time constant");
        }

        beginTest ("Internal ducking: reverb drops under the vocal and returns in the gap");
        {
            const auto in = burstThenSilence (2.0, 4.0, 0.3f, 3);
            Harness plain, ducked;
            reverbSend (plain);
            reverbSend (ducked);
            ducked.set (pid::reverbDuckAmount, 12.0f);
            ducked.set (pid::reverbDuckThreshold, -40.0f);
            ducked.set (pid::reverbDuckRelease, 150.0f);

            const int L = plain.proc.getLatencySamples();
            const auto wPlain = wetOf (plain.run (in), in, L);
            const auto wDucked = wetOf (ducked.run (in), in, L);

            const int during = (int) (1.0 * fs), after = (int) (2.8 * fs), len = (int) (0.5 * fs);
            const float dropDuring = rmsDb (wDucked, during, len) - rmsDb (wPlain, during, len);
            const float dropAfter = rmsDb (wDucked, after, len) - rmsDb (wPlain, after, len);
            logMessage ("Reverb return vs. unducked: " + juce::String (dropDuring, 1) + " dB during the vocal, "
                        + juce::String (dropAfter, 1) + " dB in the gap");
            expectWithinAbsoluteError (dropDuring, -12.0f, 1.5f);
            expectGreaterThan (dropAfter, -1.0f, "reverb must come back in the gap");
            expectGreaterThan (ducked.proc.getChain().getReverbDucker().gainReductionDb.pull(), -0.1f);
        }

        beginTest ("Delay ducking is independent of reverb ducking");
        {
            const auto in = burstThenSilence (2.0, 2.5, 0.3f, 5);
            Harness h;
            h.neutral();
            h.set (pid::delayOn, 1.0f);
            h.set (pid::delayMix, 50.0f);
            h.set (pid::delayTime, 100.0f);
            h.set (pid::reverbDuckAmount, 24.0f); // reverb off: must not affect the delay
            Harness d2;
            d2.neutral();
            d2.set (pid::delayOn, 1.0f);
            d2.set (pid::delayMix, 50.0f);
            d2.set (pid::delayTime, 100.0f);
            d2.set (pid::delayDuckAmount, 9.0f);
            d2.set (pid::delayDuckThreshold, -40.0f);

            const int L = h.proc.getLatencySamples();
            const auto w1 = wetOf (h.run (in), in, L);
            const auto w2 = wetOf (d2.run (in), in, L);
            const float drop = rmsDb (w2, (int) fs, (int) (0.5 * fs)) - rmsDb (w1, (int) fs, (int) (0.5 * fs));
            logMessage ("Delay return drop: " + juce::String (drop, 1) + " dB");
            expectWithinAbsoluteError (drop, -9.0f, 1.5f);
        }

        beginTest ("External sidechain drives the ducking; vocal alone does not");
        {
            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses.add (juce::AudioChannelSet::stereo());
            layout.inputBuses.add (juce::AudioChannelSet::stereo());
            layout.outputBuses.add (juce::AudioChannelSet::stereo());

            const auto runWithSidechain = [&] (bool kickOn)
            {
                Harness h;
                expect (h.proc.setBusesLayout (layout), "sidechain layout accepted");
                h.proc.prepareToPlay (fs, 512);
                reverbSend (h);
                h.set (pid::duckSource, 1.0f);
                h.set (pid::reverbDuckAmount, 12.0f);
                h.set (pid::reverbDuckThreshold, -40.0f);

                // 4 channels: main L/R = steady vocal, sidechain L/R = "kick" bursts
                const int n = (int) (3.0 * fs);
                juce::AudioBuffer<float> buf (4, n);
                juce::Random r (9);
                for (int i = 0; i < n; ++i)
                {
                    const float v = 0.2f * (r.nextFloat() * 2.0f - 1.0f);
                    const bool kick = kickOn && (i % 24000) < 4800; // 100 ms every 500 ms
                    buf.setSample (0, i, v);
                    buf.setSample (1, i, v);
                    buf.setSample (2, i, kick ? 0.8f : 0.0f);
                    buf.setSample (3, i, kick ? 0.8f : 0.0f);
                }
                juce::MidiBuffer midi;
                for (int start = 0; start < n; start += 512)
                {
                    juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 4, start, std::min (512, n - start));
                    h.proc.processBlock (view, midi);
                }
                expect (h.proc.getChain().meters.sidechainInUse.load(), "sidechain should be used");
                return h.proc.getChain().getReverbDucker().gainReductionDb.pull();
            };

            const float withKick = runWithSidechain (true);
            const float withoutKick = runWithSidechain (false);
            logMessage ("Reverb reduction with kick: " + juce::String (withKick, 1) + " dB, without kick: " + juce::String (withoutKick, 2) + " dB");
            expectGreaterThan (withKick, 10.0f);
            expectLessThan (withoutKick, 0.01f, "the vocal itself must not duck when the sidechain is selected");
        }

        beginTest ("External selected without a sidechain falls back to the vocal");
        {
            Harness h;
            reverbSend (h);
            h.set (pid::duckSource, 1.0f);
            h.set (pid::reverbDuckAmount, 12.0f);
            h.set (pid::reverbDuckThreshold, -40.0f);
            h.run (burstThenSilence (1.0, 1.0, 0.3f, 11));
            expect (! h.proc.getChain().meters.sidechainInUse.load());
            expectGreaterThan (h.proc.getChain().getReverbDucker().gainReductionDb.pull(), 10.0f);
        }

        beginTest ("Ducking also works with reverb as an insert");
        {
            Harness h;
            reverbSend (h);
            h.set (pid::reverbDuckAmount, 12.0f);
            h.set (pid::reverbDuckThreshold, -40.0f);
            auto r = dsp::Routing::defaults();
            r.setInsert (dsp::ModuleId::reverb, 0, true);
            h.proc.setRouting (r);
            const auto out = h.run (burstThenSilence (1.0, 1.5, 0.3f, 13));
            expect (allFinite (out));
            expectGreaterThan (h.proc.getChain().getReverbDucker().gainReductionDb.pull(), 10.0f);
        }

        beginTest ("Ducking at 0 dB amount is bit-transparent");
        {
            const auto in = burstThenSilence (1.0, 1.5, 0.3f, 15);
            Harness a, b;
            reverbSend (a);
            reverbSend (b);
            b.set (pid::reverbDuckThreshold, -60.0f); // amount stays 0
            const auto oa = a.run (in), ob = b.run (in);
            double d = 0.0;
            for (int i = 0; i < oa.getNumSamples(); ++i)
                d = std::max (d, (double) std::abs (oa.getSample (0, i) - ob.getSample (0, i)));
            expectLessThan (d, 1.0e-9);
        }
    }
};

static DuckingTests duckingTests;
