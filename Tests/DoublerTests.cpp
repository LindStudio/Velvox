// Vocal doubler: mono compatibility, real pitch variation, decorrelation.

#include "TestHelpers.h"

using namespace velvox;
using namespace velvox::test;

namespace
{
    constexpr double fs = 48000.0;

    juce::AudioBuffer<float> monoNoiseStereo (double seconds, float amp, int seed)
    {
        juce::Random r (seed);
        juce::AudioBuffer<float> b (2, (int) (seconds * fs));
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float v = amp * (r.nextFloat() * 2.0f - 1.0f);
            b.setSample (0, i, v);
            b.setSample (1, i, v);
        }
        return b;
    }

    juce::AudioBuffer<float> runModule (const dsp::DoublerSettings& s, const juce::AudioBuffer<float>& in)
    {
        dsp::DoublerModule d;
        d.setSettings (s);
        d.prepare (fs, 256);
        juce::AudioBuffer<float> out (in);
        for (int start = 0; start < out.getNumSamples(); start += 256)
        {
            const int n = std::min (256, out.getNumSamples() - start);
            float* ptrs[2] { out.getWritePointer (0, start), out.getWritePointer (1, start) };
            d.process (ptrs, 2, n);
        }
        return out;
    }

    // Frequency from interpolated rising zero crossings
    double frequencyOf (const float* x, int n)
    {
        double first = -1.0, last = -1.0;
        int count = 0;
        for (int i = 1; i < n; ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            {
                const double t = (i - 1) + x[i - 1] / (double) (x[i - 1] - x[i]);
                if (first < 0.0) first = t;
                last = t;
                ++count;
            }
        return count > 1 ? (count - 1) * fs / (last - first) : 0.0;
    }

    double correlationOf (const juce::AudioBuffer<float>& b, int start)
    {
        double lr = 0, ll = 0, rr = 0;
        for (int i = start; i < b.getNumSamples(); ++i)
        {
            const double l = b.getSample (0, i), r = b.getSample (1, i);
            lr += l * r; ll += l * l; rr += r * r;
        }
        return lr / std::sqrt (ll * rr);
    }
}

class DoublerTests final : public juce::UnitTest
{
public:
    DoublerTests() : juce::UnitTest ("Velvox doubler", "Velvox") {}

    void runTest() override
    {
        beginTest ("Mono safe 100 %: mono sum is exactly the original");
        {
            dsp::DoublerSettings s;
            s.enabled = true;
            s.mix = 1.0f;
            s.width = 1.0f;
            s.monoSafe = 1.0f;
            s.detuneCents = 15.0f;
            const auto in = monoNoiseStereo (1.0, 0.3f, 3);
            const auto out = runModule (s, in);
            double err = 0.0, sideEnergy = 0.0;
            for (int i = 0; i < in.getNumSamples(); ++i)
            {
                err = std::max (err, (double) std::abs ((out.getSample (0, i) + out.getSample (1, i))
                                                        - (in.getSample (0, i) + in.getSample (1, i))));
                sideEnergy += std::pow (out.getSample (0, i) - out.getSample (1, i), 2.0);
            }
            expectLessThan (err, 1.0e-5, "mono sum");
            expectGreaterThan (sideEnergy, 1.0, "voices must still be audible in stereo");
        }

        beginTest ("Doubled output is decorrelated, not just a level difference");
        {
            dsp::DoublerSettings s;
            s.enabled = true;
            s.mix = 0.8f;
            s.width = 1.0f;
            s.monoSafe = 0.0f;
            const auto out = runModule (s, monoNoiseStereo (2.0, 0.3f, 5));
            const int from = 4800;
            const float l = rmsDb (out, from, out.getNumSamples() - from, 0);
            const float r = rmsDb (out, from, out.getNumSamples() - from, 1);
            const double corr = correlationOf (out, from);
            logMessage ("L/R level difference " + juce::String (l - r, 2) + " dB, correlation " + juce::String (corr, 3));
            expectWithinAbsoluteError (l, r, 1.0f, "balanced left / right");
            expectLessThan (corr, 0.9, "left and right must differ in content");
            expectGreaterThan (corr, 0.0, "but stay positively correlated (no phase problems)");
        }

        beginTest ("Detune produces a real pitch variation");
        {
            const auto maxDeviationCents = [] (float detune)
            {
                dsp::DoublerSettings s;
                s.enabled = true;
                s.mix = 1.0f;
                s.width = 1.0f;     // left channel = left voice only (plus dry)
                s.monoSafe = 0.0f;
                s.detuneCents = detune;
                s.toneHz = 20000.0f;

                juce::AudioBuffer<float> in (2, (int) (8.0 * fs));
                for (int i = 0; i < in.getNumSamples(); ++i)
                    for (int ch = 0; ch < 2; ++ch)
                        in.setSample (ch, i, 0.3f * (float) std::sin (2.0 * pi * 1000.0 * i / fs));
                auto out = runModule (s, in);

                // Isolate the voices by removing the untouched dry signal
                out.addFrom (0, 0, in, 0, 0, in.getNumSamples(), -1.0f);

                double maxDev = 0.0;
                const int win = (int) (0.05 * fs);
                for (int start = (int) (0.5 * fs); start + win < out.getNumSamples(); start += win)
                {
                    const double f = frequencyOf (out.getReadPointer (0, start), win);
                    maxDev = std::max (maxDev, std::abs (1200.0 * std::log2 (f / 1000.0)));
                }
                return maxDev;
            };

            const double still = maxDeviationCents (0.0f);
            const double moving = maxDeviationCents (20.0f);
            logMessage ("Peak pitch deviation: " + juce::String (still, 2) + " ct at 0 ct, " + juce::String (moving, 2) + " ct at 20 ct");
            expectLessThan (still, 0.5);
            expectGreaterThan (moving, 10.0);
            expectLessThan (moving, 35.0);
        }

        beginTest ("Mono safe reduces comb filtering in the mono sum");
        {
            const auto monoError = [] (float monoSafe)
            {
                dsp::DoublerSettings s;
                s.enabled = true;
                s.mix = 0.7f;
                s.width = 0.8f;
                s.monoSafe = monoSafe;
                const auto in = monoNoiseStereo (2.0, 0.3f, 7);
                const auto out = runModule (s, in);
                double e = 0.0;
                for (int i = 4800; i < in.getNumSamples(); ++i)
                    e += std::pow ((out.getSample (0, i) + out.getSample (1, i)) - (in.getSample (0, i) + in.getSample (1, i)), 2.0);
                return e;
            };
            const double e0 = monoError (0.0f), e50 = monoError (0.5f), e100 = monoError (1.0f);
            logMessage ("Mono-sum deviation energy: " + juce::String (e0, 1) + " / " + juce::String (e50, 1) + " / " + juce::String (e100, 6));
            expectLessThan (e50, e0 * 0.5);
            expectLessThan (e100, 1.0e-6);
        }

        beginTest ("Doubler in the plugin: off is transparent, on changes the signal, extremes stay stable");
        {
            Harness h;
            h.neutral();
            const auto in = h.noise (0.3f, 1.0, 11);
            const auto off = h.run (in);
            const int L = h.proc.getLatencySamples();
            double err = 0.0;
            for (int i = 4800 + L; i < in.getNumSamples(); ++i)
                err = std::max (err, (double) std::abs (off.getSample (0, i) - in.getSample (0, i - L)));
            expectLessThan (err, 1.0e-5, "doubler off");

            h.set (pid::doublerOn, 1.0f);
            h.set (pid::doublerMix, 100.0f);
            h.set (pid::doublerDetune, 25.0f);
            h.set (pid::doublerDelay, 40.0f);
            h.set (pid::doublerMonoSafe, 0.0f);
            const auto on = h.run (h.noise (1.0f, 3.0, 12));
            expect (allFinite (on));
            expectGreaterThan (rmsDb (on, 48000, 48000, 0), -20.0f);

            Harness mono (48000.0, 256, 1);
            mono.set (pid::doublerOn, 1.0f);
            const auto m = mono.run (mono.noise (0.3f, 1.0, 13));
            expect (allFinite (m));
        }
    }
};

static DoublerTests doublerTests;
