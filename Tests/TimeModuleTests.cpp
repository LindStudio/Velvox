// Time modules (0.6 quality track): Delay time changes / characters / echo taps,
// Pulse shapes and Amount, Stutter display feed and repeats.

#include <juce_dsp/juce_dsp.h>
#include "TestHelpers.h"
#include "DSP/DelayModule.h"

using namespace velvox;
using namespace velvox::dsp;

namespace
{
    constexpr double fs = 48000.0;
    constexpr double twoPi = 6.283185307179586;

    // Sine with a 50 ms raised-cosine fade-in (a hard onset would splatter into every spectrum)
    juce::AudioBuffer<float> sine (double seconds, double freq, float amp)
    {
        juce::AudioBuffer<float> b (2, (int) (seconds * fs));
        const int fadeIn = (int) (0.05 * fs);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double g = i < fadeIn ? 0.5 - 0.5 * std::cos (twoPi * 0.5 * i / fadeIn) : 1.0;
            const float x = amp * (float) (g * std::sin (twoPi * freq * i / fs));
            b.setSample (0, i, x);
            b.setSample (1, i, x);
        }
        return b;
    }

    // Runs a delay in `block`-sample blocks; `automate` may change the settings before each block
    juce::AudioBuffer<float> runDelay (DelayModule& d, const juce::AudioBuffer<float>& in, int block = 64,
                                       std::function<void (int)> automate = {})
    {
        juce::AudioBuffer<float> out (2, in.getNumSamples());
        out.clear();
        for (int start = 0; start < in.getNumSamples(); start += block)
        {
            const int n = std::min (block, in.getNumSamples() - start);
            if (automate)
                automate (start);
            const float* ip[2] { in.getReadPointer (0, start), in.getReadPointer (1, start) };
            float* op[2] { out.getWritePointer (0, start), out.getWritePointer (1, start) };
            d.process (ip, op, 2, n);
        }
        return out;
    }

    DelaySettings openDelay (float timeMs, float feedback = 0.0f)
    {
        DelaySettings s;
        s.timeMs = timeMs;
        s.feedback = feedback;
        s.lowCutHz = 20.0f;
        s.highCutHz = 20000.0f;
        return s;
    }

    // 5 ms RMS envelope (dB) of one channel between two sample positions
    std::vector<double> envelopeDb (const juce::AudioBuffer<float>& b, int ch, int from, int to)
    {
        const int w = (int) (0.005 * fs);
        std::vector<double> e;
        for (int s = from; s + w <= to; s += w)
        {
            double sum = 0.0;
            for (int i = s; i < s + w; ++i)
                sum += (double) b.getSample (ch, i) * b.getSample (ch, i);
            e.push_back (10.0 * std::log10 (sum / w + 1.0e-30));
        }
        return e;
    }

    // Frequency of each period (positive zero crossings, linearly interpolated)
    std::vector<double> periodFrequencies (const juce::AudioBuffer<float>& b, int ch, int from, int to)
    {
        std::vector<double> crossings, f;
        for (int i = std::max (1, from); i < to; ++i)
        {
            const float a = b.getSample (ch, i - 1), c = b.getSample (ch, i);
            if (a < 0.0f && c >= 0.0f)
                crossings.push_back (i - 1 + a / (a - c));
        }
        for (size_t k = 1; k < crossings.size(); ++k)
            f.push_back (fs / (crossings[k] - crossings[k - 1]));
        return f;
    }

    // Power fraction (dB) of one channel in the bins `select` picks (Blackman-Harris windowed 8192-point FFT (-92 dB side lobes), averaged)
    double energyFractionDb (const juce::AudioBuffer<float>& b, int ch, int from, int to, const std::function<bool (double)>& select)
    {
        constexpr int order = 13, n = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> buf ((size_t) (2 * n));
        double above = 0.0, total = 0.0;
        for (int s = from; s + n <= to; s += n / 2)
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            for (int i = 0; i < n; ++i)
                buf[(size_t) i] = b.getSample (ch, s + i) * (float) (0.35875 - 0.48829 * std::cos (twoPi * i / n) + 0.14128 * std::cos (2.0 * twoPi * i / n) - 0.01168 * std::cos (3.0 * twoPi * i / n));
            fft.performFrequencyOnlyForwardTransform (buf.data());
            for (int k = 1; k < n / 2; ++k)
            {
                const double p = (double) buf[(size_t) k] * buf[(size_t) k];
                total += p;
                if (select (k * fs / n))
                    above += p;
            }
        }
        return 10.0 * std::log10 (above / std::max (1.0e-30, total) + 1.0e-30);
    }

    double energyAboveDb (const juce::AudioBuffer<float>& b, int ch, int from, int to, double cutHz)
    {
        return energyFractionDb (b, ch, from, to, [cutHz] (double f) { return f > cutHz; });
    }

    // Energy away from every harmonic of `f0` below Nyquist (aliasing), relative to the total
    double inharmonicDb (const juce::AudioBuffer<float>& b, int ch, int from, int to, double f0)
    {
        return energyFractionDb (b, ch, from, to, [f0] (double f)
        {
            for (double h = f0; h < 0.5 * fs; h += f0)
                if (std::abs (f - h) < 40.0)
                    return false;
            return f > 30.0;
        });
    }

    int peakIndex (const juce::AudioBuffer<float>& b, int ch, int from, int to)
    {
        int p = from;
        for (int i = from; i < to; ++i)
            if (std::abs (b.getSample (ch, i)) > std::abs (b.getSample (ch, p)))
                p = i;
        return p;
    }

    float absMax (const juce::AudioBuffer<float>& b, int ch, int from, int to)
    {
        return b.getMagnitude (ch, from, to - from);
    }
}

class TimeModuleTests final : public juce::UnitTest
{
public:
    TimeModuleTests() : juce::UnitTest ("Velvox time modules", "Velvox") {}

    void runTest() override
    {
        delayTimeChanges();
        delayCharacters();
    }

private:
    void delayCharacters()
    {
        beginTest ("Delay: ping-pong bounces L, R, L (also with spread)");
        for (float spreadMs : { 0.0f, 20.0f })
        {
            auto s = openDelay (200.0f, 0.5f);
            s.pingPong = true;
            s.spreadMs = spreadMs;
            DelayModule d;
            d.setSettings (s);
            d.prepare (fs, 256);
            // Hard-left 4 ms Hann-windowed 2 kHz burst (band-limited and DC-free: the loop
            // filters leave it intact); the ping-pong takes the mono sum
            juce::AudioBuffer<float> in (2, (int) (1.0 * fs));
            in.clear();
            const int pulse = 192;
            for (int i = 0; i < pulse; ++i)
                in.setSample (0, i, (float) ((0.5 - 0.5 * std::cos (twoPi * i / pulse)) * std::sin (twoPi * 2000.0 * i / fs)));
            const auto out = runDelay (d, in, 256);
            const int t = (int) (0.2 * fs), sp = (int) std::lround (spreadMs * 0.001 * fs), w = 400;
            const int e1 = t, e2 = 2 * t + sp, e3 = 3 * t + sp;
            const auto energy = [&] (int ch, int at)
            {
                double e = 0.0;
                for (int i = at - w; i < at + w; ++i)
                    e += (double) out.getSample (ch, i) * out.getSample (ch, i);
                return e;
            };
            const auto centre = [&] (int ch, int at)
            {
                double e = 0.0, m = 0.0;
                for (int i = at - w; i < at + w; ++i)
                {
                    const double v = (double) out.getSample (ch, i) * out.getSample (ch, i);
                    e += v;
                    m += v * (i - at);
                }
                return m / std::max (1.0e-30, e);
            };
            double inE = 0.0;
            for (int i = 0; i < pulse; ++i)
                inE += (double) in.getSample (0, i) * in.getSample (0, i);
            const auto db = [] (double ratio) { return 10.0 * std::log10 (ratio + 1.0e-30); };
            const double pulseCentre = 0.5 * (pulse - 1);
            const juce::String tag = " (spread " + juce::String (spreadMs) + " ms)";
            expectWithinAbsoluteError (centre (0, e1), pulseCentre, 2.0, "echo 1 on time, L" + tag);
            expectWithinAbsoluteError (centre (1, e2), pulseCentre, 2.0, "echo 2 on time, R" + tag);
            expectWithinAbsoluteError (centre (0, e3), pulseCentre, 2.0, "echo 3 on time, L" + tag);
            expectWithinAbsoluteError (db (energy (0, e1) / inE), -6.02, 0.1, "echo 1 = mono sum" + tag);
            expectWithinAbsoluteError (db (energy (1, e2) / energy (0, e1)), -6.02, 0.1, "echo 2 one feedback step down" + tag);
            expectWithinAbsoluteError (db (energy (0, e3) / energy (1, e2)), -6.02, 0.1, "echo 3 one feedback step down" + tag);
            expectLessThan (db (energy (1, e1) / energy (0, e1)), -80.0, "R silent at echo 1" + tag);
            expectLessThan (db (energy (0, e2) / energy (1, e2)), -80.0, "L silent at echo 2" + tag);
            expectLessThan (db (energy (1, e3) / energy (0, e3)), -80.0, "R silent at echo 3" + tag);
        }

        beginTest ("Delay: drive does not alias");
        {
            // Sanity: a quiet tone with the least drive passes the 2x stage cleanly
            auto lin = openDelay (300.0f);
            lin.drive = 0.001f;
            DelayModule dl;
            dl.setSettings (lin);
            dl.prepare (fs, 256);
            const auto clean = runDelay (dl, sine (2.6, 4100.0, 0.01f), 256);
            const double linAlias = inharmonicDb (clean, 0, (int) (0.5 * fs), (int) (2.6 * fs), 4100.0);
            logMessage ("  drive 0.1 %, 4.1 kHz at -40 dBFS: inharmonic energy " + juce::String (linAlias, 1) + " dB");
            expectLessThan (linAlias, -90.0);

            auto s = openDelay (300.0f);
            s.drive = 1.0f;
            DelayModule d;
            d.setSettings (s);
            d.prepare (fs, 256);
            const auto out = runDelay (d, sine (2.6, 4100.0, 0.5f), 256);
            const double alias = inharmonicDb (out, 0, (int) (0.5 * fs), (int) (2.6 * fs), 4100.0);
            logMessage ("  drive 100 %, 4.1 kHz at -6 dBFS: inharmonic energy " + juce::String (alias, 1) + " dB");
            expectLessThan (alias, -60.0);
        }
    }

    void delayTimeChanges()
    {
        beginTest ("Delay: continuous time sweep glides (tape-style, no flutter, no dips)");
        {
            DelayModule d;
            d.setSettings (openDelay (250.0f));
            d.prepare (fs, 64);
            const auto in = sine (3.4, 1000.0, 0.3f);
            const int sweepEnd = (int) (3.0 * fs);
            const auto out = runDelay (d, in, 64, [&] (int pos)
            {
                const float t = 250.0f + 200.0f * (float) std::min (1.0, (double) pos / sweepEnd);
                d.setSettings (openDelay (t));
            });
            const auto env = envelopeDb (out, 0, (int) (0.8 * fs), (int) (3.2 * fs));
            const auto [mn, mx] = std::minmax_element (env.begin(), env.end());
            logMessage ("  sweep envelope range " + juce::String (*mx - *mn, 2) + " dB");
            expectLessThan (*mx - *mn, 1.0, "envelope range during the sweep");

            const auto f = periodFrequencies (out, 0, (int) (0.8 * fs), (int) (3.2 * fs));
            double maxStep = 0.0;
            for (size_t k = 1; k < f.size(); ++k)
                maxStep = std::max (maxStep, std::abs (f[k] - f[k - 1]));
            logMessage ("  sweep frequency step max " + juce::String (maxStep, 2) + " Hz, mid " + juce::String (f[f.size() / 2], 1) + " Hz");
            expectLessThan (maxStep, 3.0, "period-to-period frequency step");
        }

        beginTest ("Delay: a large jump crossfades instead of gliding (no pitch dive)");
        {
            DelayModule d;
            d.setSettings (openDelay (300.0f));
            d.prepare (fs, 64);
            const auto in = sine (3.5, 1000.0, 0.3f);
            const auto out = runDelay (d, in, 64, [&] (int pos)
            {
                d.setSettings (openDelay (pos < (int) (2.0 * fs) ? 300.0f : 437.25f));
            });
            const auto f = periodFrequencies (out, 0, (int) (1.0 * fs), (int) (3.4 * fs));
            double worst = 0.0;
            for (auto v : f)
                worst = std::max (worst, std::abs (v - 1000.0));
            logMessage ("  jump: worst frequency deviation " + juce::String (worst, 2) + " Hz");
            expectLessThan (worst, 15.0);
            // and it arrives: the echo sits at the new time afterwards
            DelayModule probe;
            probe.setSettings (openDelay (300.0f));
            probe.prepare (fs, 64);
            juce::AudioBuffer<float> imp (2, (int) (3.0 * fs));
            imp.clear();
            const int at = (int) (2.0 * fs);
            imp.setSample (0, at, 1.0f);
            imp.setSample (1, at, 1.0f);
            const auto r = runDelay (probe, imp, 64, [&] (int pos) { probe.setSettings (openDelay (pos < (int) (1.0 * fs) ? 300.0f : 437.25f)); });
            int peak = at;
            for (int i = at; i < r.getNumSamples(); ++i)
                if (std::abs (r.getSample (0, i)) > std::abs (r.getSample (0, peak)))
                    peak = i;
            expectWithinAbsoluteError ((double) (peak - at), 0.43725 * fs, 1.5, "echo time after the jump settled");
        }

        beginTest ("Delay: the loop is linear below full scale (no limiter distortion)");
        {
            DelayModule d;
            d.setSettings (openDelay (300.0f, 0.5f));
            d.prepare (fs, 256);
            // -10 dBFS in, the repeats build up to about -4 dBFS in the loop
            const auto out = runDelay (d, sine (3.0, 1000.0, 0.3f), 256);
            const double hf = energyAboveDb (out, 0, (int) (0.7 * fs), (int) (3.0 * fs), 2500.0);
            logMessage ("  -10 dBFS tone, feedback 50 %: energy above 2.5 kHz " + juce::String (hf, 1) + " dB");
            expectLessThan (hf, -100.0);
        }

        beginTest ("Delay: modulation has no zipper");
        {
            auto s = openDelay (300.0f);
            s.modulation = 1.0f;
            DelayModule d;
            d.setSettings (s);
            d.prepare (fs, 256);
            const auto out = runDelay (d, sine (3.0, 1000.0, 0.3f), 256);
            const double hf = energyAboveDb (out, 0, (int) (0.5 * fs), (int) (3.0 * fs), 2500.0);
            logMessage ("  modulation energy above 2.5 kHz " + juce::String (hf, 1) + " dB");
            expectLessThan (hf, -80.0);
        }
    }
};

static TimeModuleTests timeModuleTests;
