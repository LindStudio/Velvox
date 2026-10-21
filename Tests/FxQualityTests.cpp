// Reverb and delay engine 0.4: decay accuracy, stability, click-free changes, timing.

#include <juce_core/juce_core.h>
#include "DSP/ReverbModule.h"
#include "DSP/DelayModule.h"

using namespace velvox::dsp;

namespace
{
    constexpr double fs = 48000.0;

    template <typename Fx>
    juce::AudioBuffer<float> runFx (Fx& fx, const juce::AudioBuffer<float>& in, int block = 256,
                                    std::function<void (int sampleIndex)> beforeBlock = {})
    {
        juce::AudioBuffer<float> out (2, in.getNumSamples());
        out.clear();
        for (int start = 0; start < in.getNumSamples(); start += block)
        {
            const int n = std::min (block, in.getNumSamples() - start);
            if (beforeBlock)
                beforeBlock (start);
            const float* ip[2] { in.getReadPointer (0, start), in.getReadPointer (std::min (1, in.getNumChannels() - 1), start) };
            float* op[2] { out.getWritePointer (0, start), out.getWritePointer (1, start) };
            fx.process (ip, op, 2, n);
        }
        return out;
    }

    juce::AudioBuffer<float> impulse (double seconds)
    {
        juce::AudioBuffer<float> b (2, (int) (seconds * fs));
        b.clear();
        b.setSample (0, 0, 0.5f);
        b.setSample (1, 0, 0.5f);
        return b;
    }

    juce::AudioBuffer<float> noise (double seconds, float amp, double silenceAfter = 0.0, int seed = 3)
    {
        juce::Random r (seed);
        const int n = (int) (seconds * fs), total = n + (int) (silenceAfter * fs);
        juce::AudioBuffer<float> b (2, total);
        b.clear();
        for (int i = 0; i < n; ++i)
        {
            const float x = amp * (r.nextFloat() * 2.0f - 1.0f);
            b.setSample (0, i, x);
            b.setSample (1, i, x);
        }
        return b;
    }

    // Low-passed noise (4th order at 250 Hz): the reverb return stays smooth, so any
    // discontinuity stands out from the signal's own curvature
    juce::AudioBuffer<float> smoothNoise (double seconds, float amp, int seed = 5)
    {
        auto b = noise (seconds, amp * 6.0f, 0.0, seed);
        for (int k = 0; k < 2; ++k)
        {
            Biquad f;
            f.setCoeffs (design::lowPass (fs, 250.0, 0.7071));
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < b.getNumSamples(); ++i)
                    b.setSample (ch, i, f.process (b.getSample (ch, i), ch));
        }
        return b;
    }

    double rmsDb (const juce::AudioBuffer<float>& b, int start, int len)
    {
        double sum = 0.0;
        len = std::min (len, b.getNumSamples() - start);
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = start; i < start + len; ++i)
                sum += (double) b.getSample (ch, i) * b.getSample (ch, i);
        return 10.0 * std::log10 (sum / std::max (1, len * b.getNumChannels()) + 1.0e-30);
    }

    bool allFinite (const juce::AudioBuffer<float>& b)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (b.getSample (ch, i)))
                    return false;
        return true;
    }

    // RT60 from the Schroeder integral (fit -5 .. -25 dB) of channel average
    double rt60 (const juce::AudioBuffer<float>& b)
    {
        const int n = b.getNumSamples();
        std::vector<double> e ((size_t) n);
        double acc = 0.0;
        for (int i = n - 1; i >= 0; --i)
        {
            const double m = 0.5 * (b.getSample (0, i) + b.getSample (1, i));
            acc += m * m;
            e[(size_t) i] = acc;
        }
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        int cnt = 0;
        for (int i = 0; i < n; ++i)
        {
            const double db = 10.0 * std::log10 (e[(size_t) i] / e[0] + 1.0e-30);
            if (db <= -5.0 && db >= -25.0)
            {
                const double t = i / fs;
                sx += t; sy += db; sxx += t * t; sxy += t * db; ++cnt;
            }
        }
        if (cnt < 10)
            return 0.0;
        const double slope = (cnt * sxy - sx * sy) / (cnt * sxx - sx * sx);
        return slope < 0.0 ? -60.0 / slope : 0.0;
    }

    // Largest second difference relative to the 99th percentile: a click is a single
    // sample far outside the signal's own curvature distribution
    double clickRatio (const juce::AudioBuffer<float>& b, int from)
    {
        std::vector<float> d2;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = std::max (2, from); i < b.getNumSamples(); ++i)
                d2.push_back (std::abs (b.getSample (ch, i) - 2.0f * b.getSample (ch, i - 1) + b.getSample (ch, i - 2)));
        if (d2.empty())
            return 0.0;
        const float mx = *std::max_element (d2.begin(), d2.end());
        std::nth_element (d2.begin(), d2.begin() + (long) (d2.size() * 99 / 100), d2.end());
        const float p99 = d2[d2.size() * 99 / 100];
        return p99 > 0.0f ? mx / p99 : 0.0;
    }
}

class FxQualityTests final : public juce::UnitTest
{
public:
    FxQualityTests() : juce::UnitTest ("Headliner reverb and delay engine", "Headliner") {}

    void runTest() override
    {
        beginTest ("Click detector self-check: a hard reset of a running reverb is detected");
        {
            ReverbSettings s;
            s.type = 2;
            s.highCutHz = 1500.0f;
            ReverbModule rv;
            rv.setSettings (s);
            rv.prepare (fs, 256);
            const auto in = smoothNoise (4.0, 0.25);
            const auto clean = runFx (rv, in);
            ReverbModule rv2;
            rv2.setSettings (s);
            rv2.prepare (fs, 256);
            const auto cut = runFx (rv2, in, 256, [&] (int pos) { if (pos == (int) (2.0 * fs) / 256 * 256) rv2.reset(); });
            logMessage ("  clean " + juce::String (clickRatio (clean, (int) (0.5 * fs)), 2) + ", with reset " + juce::String (clickRatio (cut, (int) (0.5 * fs)), 2));
            expect (clickRatio (clean, (int) (0.5 * fs)) < 4.0);
            expect (clickRatio (cut, (int) (0.5 * fs)) > 4.0, "the detector sees an abrupt cut");
        }

        beginTest ("Reverb: every character is stable at the longest decay and the strongest bass");
        for (int type = 0; type < ReverbModule::numTypes; ++type)
        {
            ReverbSettings s;
            s.type = type;
            s.decaySeconds = ReverbModule::maxDecaySeconds;
            s.bassMultiplier = 2.0f;
            s.size = 1.0f;
            s.dampingHz = 20000.0f;
            s.modulation = 1.0f;
            ReverbModule rv;
            rv.setSettings (s);
            rv.prepare (fs, 256);
            for (int l = 0; l < ReverbModule::numLines; ++l)
                expect (rv.loopGainAtDc (l) < 1.0f, "loop gain < 1 at DC");
            const auto out = runFx (rv, noise (0.5, 0.5, 30.0));
            expect (allFinite (out));
            const double early = rmsDb (out, (int) (1.0 * fs), (int) fs), late = rmsDb (out, (int) (29.0 * fs), (int) fs);
            expect (late < early - 3.0, juce::String (ReverbModule::voicing (type).name) + ": decays (" + juce::String (early, 1)
                                            + " -> " + juce::String (late, 1) + " dB)");
        }

        beginTest ("Reverb: the tail fades to silence after the input stops");
        for (int type = 0; type < ReverbModule::numTypes; ++type)
        {
            ReverbSettings s;
            s.type = type;
            s.decaySeconds = 2.0f;
            ReverbModule rv;
            rv.setSettings (s);
            rv.prepare (fs, 256);
            const auto out = runFx (rv, noise (0.5, 0.5, 12.0));
            const double end = rmsDb (out, out.getNumSamples() - (int) fs, (int) fs);
            expect (end < -100.0, juce::String (ReverbModule::voicing (type).name) + ": " + juce::String (end, 1) + " dB after 11 s");
        }

        beginTest ("Reverb: RT60 follows the decay control");
        for (int type : { 1, 2, 5 })
            for (float decay : { 1.0f, 4.0f, 10.0f })
            {
                ReverbSettings s;
                s.type = type;
                s.decaySeconds = decay;
                s.dampingHz = 20000.0f;
                s.highCutHz = 20000.0f;
                s.lowCutHz = 20.0f;
                ReverbModule rv;
                rv.setSettings (s);
                rv.prepare (fs, 256);
                const double t = rt60 (runFx (rv, impulse (decay * 2.0 + 1.0)));
                logMessage ("  " + juce::String (ReverbModule::voicing (type).name) + " decay " + juce::String (decay) + " s -> RT60 " + juce::String (t, 2) + " s");
                expect (std::abs (t - decay) < 0.25 * decay, juce::String (ReverbModule::voicing (type).name) + " decay "
                                                               + juce::String (decay) + " s measured " + juce::String (t, 2) + " s");
            }

        beginTest ("Reverb: bass decay and damping shape the tail per frequency");
        {
            const auto bandRt = [] (float bassMult, float dampHz, bool lows)
            {
                ReverbSettings s;
                s.type = 2;
                s.decaySeconds = 3.0f;
                s.bassMultiplier = bassMult;
                s.dampingHz = dampHz;
                s.highCutHz = 20000.0f;
                s.lowCutHz = 20.0f;
                ReverbModule rv;
                rv.setSettings (s);
                rv.prepare (fs, 256);
                auto out = runFx (rv, impulse (8.0));
                Biquad f;
                f.setCoeffs (lows ? design::lowPass (fs, 180.0, 0.7071) : design::highPass (fs, 6000.0, 0.7071));
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < out.getNumSamples(); ++i)
                        out.setSample (ch, i, f.process (out.getSample (ch, i), ch));
                return rt60 (out);
            };
            const double lowShort = bandRt (0.5f, 20000.0f, true), lowNormal = bandRt (1.0f, 20000.0f, true);
            expect (lowShort < 0.75 * lowNormal, "bass x0.5 shortens the low decay: " + juce::String (lowShort, 2) + " vs " + juce::String (lowNormal, 2));
            const double hiDark = bandRt (1.0f, 2000.0f, false), hiBright = bandRt (1.0f, 20000.0f, false);
            expect (hiDark < 0.7 * hiBright, "damping shortens the high decay: " + juce::String (hiDark, 2) + " vs " + juce::String (hiBright, 2));
        }

        beginTest ("Reverb: changing the character, size, decay and movement never clicks");
        {
            ReverbSettings s;
            s.type = 1;
            s.highCutHz = 1500.0f;
            ReverbModule rv;
            rv.setSettings (s);
            rv.prepare (fs, 256);
            const auto in = smoothNoise (12.0, 0.25);
            const auto out = runFx (rv, in, 256, [&] (int pos)
            {
                const int step = pos / (int) (0.5 * fs);
                ReverbSettings t = s;
                t.type = step % ReverbModule::numTypes;
                t.size = (step % 3) * 0.45f;
                t.decaySeconds = 0.5f + (float) (step % 5) * 2.0f;
                t.modulation = (step % 2) ? 1.0f : 0.0f;
                t.dampingHz = (step % 4) ? 3000.0f : 15000.0f;
                rv.setSettings (t);
            });
            expect (allFinite (out));
            const double ratio = clickRatio (out, (int) (0.5 * fs));
            logMessage ("  automation click ratio " + juce::String (ratio, 2));
            expect (ratio < 4.0, "largest curvature / p99 = " + juce::String (ratio, 2));
        }

        beginTest ("Reverb: width 0 gives a mono return, width 1 a wide one");
        {
            for (float w : { 0.0f, 1.0f })
            {
                ReverbSettings s;
                s.type = 2;
                s.width = w;
                ReverbModule rv;
                rv.setSettings (s);
                rv.prepare (fs, 256);
                const auto out = runFx (rv, noise (1.0, 0.3f, 1.0));
                double lr = 0, ll = 0, rr = 0;
                for (int i = (int) (0.2 * fs); i < out.getNumSamples(); ++i)
                {
                    lr += out.getSample (0, i) * out.getSample (1, i);
                    ll += out.getSample (0, i) * out.getSample (0, i);
                    rr += out.getSample (1, i) * out.getSample (1, i);
                }
                const double corr = lr / std::sqrt (ll * rr + 1.0e-30);
                if (w == 0.0f)
                    expect (corr > 0.999, "mono: corr " + juce::String (corr, 3));
                else
                    expect (std::abs (corr) < 0.3, "wide: corr " + juce::String (corr, 3));
            }
        }

        beginTest ("Delay: echoes land exactly on the host tempo grid");
        for (double bpm : { 128.0, 150.0, 174.0 })
            for (int div : { 3, 5, 6, 7, 9 })   // 1/16, 1/8T, 1/8, 1/8D, 1/4
            {
                DelaySettings s;
                s.timeMs = DelayModule::timeForTempo (bpm, div);
                s.feedback = 0.0f;
                s.lowCutHz = 20.0f;
                s.highCutHz = 20000.0f;
                DelayModule d;
                d.setSettings (s);
                d.prepare (fs, 256);
                const auto out = runFx (d, impulse (2.0));
                int peak = 0;
                for (int i = 1; i < out.getNumSamples(); ++i)
                    if (std::abs (out.getSample (0, i)) > std::abs (out.getSample (0, peak)))
                        peak = i;
                const double expected = s.timeMs * 0.001 * fs;
                expect (std::abs (peak - expected) <= 1.0, juce::String (bpm) + " BPM division " + juce::String (div) + ": echo at "
                                                            + juce::String (peak) + ", expected " + juce::String (expected, 1));
            }

        beginTest ("Delay: spread delays the right side, diffusion keeps the timing");
        {
            DelaySettings s;
            s.timeMs = 300.0f;
            s.feedback = 0.0f;
            s.lowCutHz = 20.0f;
            s.highCutHz = 20000.0f;
            s.spreadMs = 25.0f;
            DelayModule d;
            d.setSettings (s);
            d.prepare (fs, 256);
            const auto out = runFx (d, impulse (1.0));
            const auto peakOf = [&] (int ch)
            {
                int p = 0;
                for (int i = 1; i < out.getNumSamples(); ++i)
                    if (std::abs (out.getSample (ch, i)) > std::abs (out.getSample (ch, p)))
                        p = i;
                return p;
            };
            expect (std::abs (peakOf (0) - 0.300 * fs) <= 1.0 && std::abs (peakOf (1) - 0.325 * fs) <= 1.0,
                    "L " + juce::String (peakOf (0)) + ", R " + juce::String (peakOf (1)));

            s.spreadMs = 0.0f;
            s.diffusion = 0.8f;
            DelayModule dd;
            dd.setSettings (s);
            dd.prepare (fs, 256);
            const auto diffused = runFx (dd, impulse (1.0));
            double centre = 0.0, energy = 0.0;
            for (int i = 0; i < diffused.getNumSamples(); ++i)
            {
                const double e = (double) diffused.getSample (0, i) * diffused.getSample (0, i);
                centre += e * i;
                energy += e;
            }
            centre /= std::max (1.0e-30, energy);
            expect (centre > 0.29 * fs && centre < 0.33 * fs, "diffused echo energy centred at " + juce::String (1000.0 * centre / fs, 1) + " ms");
        }

        beginTest ("Delay: a new delay time crossfades - no click, no pitch glide");
        {
            DelaySettings s;
            s.timeMs = 300.0f;
            s.feedback = 0.4f;
            s.lowCutHz = 20.0f;
            s.highCutHz = 20000.0f;
            DelayModule d;
            d.setSettings (s);
            d.prepare (fs, 256);
            juce::AudioBuffer<float> in (2, (int) (6.0 * fs));
            for (int i = 0; i < in.getNumSamples(); ++i)
            {
                const float x = 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * i / fs);
                in.setSample (0, i, x);
                in.setSample (1, i, x);
            }
            const auto out = runFx (d, in, 256, [&] (int pos)
            {
                auto t = s;
                t.timeMs = pos < (int) (2.0 * fs) ? 300.0f : pos < (int) (4.0 * fs) ? 437.0f : 180.0f;
                d.setSettings (t);
            });
            // A 220 Hz sine has max |d2| = A (2 pi f / fs)^2; the echoes sum several such sines
            const double ratio = clickRatio (out, (int) (1.0 * fs));
            logMessage ("  delay time change click ratio " + juce::String (ratio, 2));
            expect (ratio < 2.5, "largest curvature / p99 = " + juce::String (ratio, 2));
        }

        beginTest ("Delay: maximum feedback with drive and diffusion stays bounded and decays");
        {
            DelaySettings s;
            s.timeMs = 120.0f;
            s.feedback = 0.9f;
            s.drive = 1.0f;
            s.diffusion = 1.0f;
            s.modulation = 1.0f;
            s.lowCutHz = 20.0f;
            s.highCutHz = 20000.0f;
            s.pingPong = true;
            DelayModule d;
            d.setSettings (s);
            d.prepare (fs, 256);
            const auto out = runFx (d, noise (5.0, 1.0, 40.0));
            expect (allFinite (out));
            expect (out.getMagnitude (0, out.getNumSamples()) < 2.0f, "peak " + juce::String (out.getMagnitude (0, out.getNumSamples()), 2));
            const double end = rmsDb (out, out.getNumSamples() - (int) fs, (int) fs);
            expect (end < -60.0, "decayed to " + juce::String (end, 1) + " dB");
        }
    }
};

static FxQualityTests fxQualityTests;
