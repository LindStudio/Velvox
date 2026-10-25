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

// 0.6 engine: parameters, voice mapping
class DoublerParameterTests final : public juce::UnitTest
{
public:
    DoublerParameterTests() : juce::UnitTest ("Velvox doubler parameters", "Velvox") {}

    void runTest() override
    {
        beginTest ("Doubler parameters: new IDs exist with old-behaviour defaults");
        {
            const auto def = [this] (const char* id) -> float
            {
                const auto* s = findParameterSpec (id);
                expect (s != nullptr, juce::String ("missing ") + id);
                return s != nullptr ? s->defaultValue : -12345.0f;
            };
            expectEquals (def (pid::doublerVoices), 0.0f, "Classic");
            const float delays[] { 14.0f, 19.18f, 24.0f, 31.0f }, detunes[] { 0.0f, 0.0f, -5.0f, 5.0f };
            const float gains[] { 0.0f, 0.0f, -3.0f, -3.0f }, pans[] { -100.0f, 100.0f, -50.0f, 50.0f };
            for (int v = 0; v < 4; ++v)
            {
                expectEquals (def (pid::doublerVoiceOn[v]), 1.0f);
                expectWithinAbsoluteError (def (pid::doublerVoiceDelay[v]), delays[v], 1.0e-4f);
                expectEquals (def (pid::doublerVoiceDetune[v]), detunes[v]);
                expectEquals (def (pid::doublerVoiceGain[v]), gains[v]);
                expectEquals (def (pid::doublerVoicePan[v]), pans[v]);
            }
            expectEquals (def (pid::doublerModDepth), 8.0f);
            expectWithinAbsoluteError (def (pid::doublerModRate), 0.31f, 1.0e-6f);
            expectEquals (def (pid::doublerLowCut), 120.0f);
            expectEquals (def (pid::doublerDry), 0.0f);
            expect (findParameterSpec ("doublerV3Pan_2") != nullptr, "instance 2 bank");

            OfflineParameters p;
            const auto s = p.read (dsp::Routing::fromString ("v2,doubler")).doubler;
            expect (s.mode == dsp::DoublerMode::classic);
            expectWithinAbsoluteError (s.voices[1].delayMs, 19.18f, 1.0e-4f);
            expectEquals (s.voices[0].pan, -1.0f);
            expectEquals (s.voices[3].pan, 0.5f);
            expectEquals (s.lowCutHz, 120.0f);
            expectEquals (s.dryDb, 0.0f);

            p.set (pid::doublerVoices, 2.0f);
            p.set (pid::doublerV3Gain, -12.0f);
            p.set (pid::doublerV2On, 0.0f);
            const auto f = p.read (dsp::Routing::fromString ("v2,doubler")).doubler;
            expect (f.mode == dsp::DoublerMode::four);
            expectEquals (f.voices[2].gainDb, -12.0f);
            expect (! f.voices[1].on);
        }

        beginTest ("Doubler voices: Classic follows the 0.2 controls");
        {
            dsp::DoublerSettings s;
            s.delayMs = 25.0f;
            s.width = 0.6f;
            s.detuneCents = 12.0f;
            s.voices[0].delayMs = 3.0f;   // ignored in Classic
            const auto r = dsp::DoublerModule::resolveVoices (s);
            expectEquals (r.count, 2);
            expectWithinAbsoluteError (r.v[0].delayMs, 25.0f, 1.0e-4f);
            expectWithinAbsoluteError (r.v[1].delayMs, 34.25f, 1.0e-3f);
            expectWithinAbsoluteError (r.v[0].pan, -0.6f, 1.0e-6f);
            expectWithinAbsoluteError (r.v[1].pan, 0.6f, 1.0e-6f);
            expectEquals (r.v[0].detuneCents, 0.0f);
            expectEquals (r.v[0].gain, 1.0f);
            expect (r.v[0].on && r.v[1].on && ! r.v[2].on && ! r.v[3].on);
            expectEquals (r.modDepthCents, 12.0f);
            expectEquals (r.normalisation, 1.0f);
        }

        beginTest ("Doubler voices: 2 and 4 voice modes use the per-voice values");
        {
            dsp::DoublerSettings s;
            s.mode = dsp::DoublerMode::four;
            s.width = 0.5f;
            s.modDepthCents = 3.0f;
            s.voices[1].on = false;
            s.voices[2].gainDb = -6.0f;
            const auto r = dsp::DoublerModule::resolveVoices (s);
            expectEquals (r.count, 4);
            expectWithinAbsoluteError (r.v[2].delayMs, 24.0f, 1.0e-4f);
            expectWithinAbsoluteError (r.v[2].gain, 0.501187f, 1.0e-5f);
            expectEquals (r.v[2].detuneCents, -5.0f);
            expectWithinAbsoluteError (r.v[0].pan, -0.5f, 1.0e-6f);
            expectWithinAbsoluteError (r.v[3].pan, 0.25f, 1.0e-6f);
            expect (! r.v[1].on);
            expectEquals (r.modDepthCents, 3.0f);
            expectWithinAbsoluteError (r.normalisation, 0.70711f, 1.0e-4f);

            s.mode = dsp::DoublerMode::two;
            const auto t = dsp::DoublerModule::resolveVoices (s);
            expectEquals (t.count, 2);
            expect (! t.v[2].on && ! t.v[3].on);
            expectEquals (t.normalisation, 1.0f);
        }
    }
};

static DoublerParameterTests doublerParameterTests;

// 0.6 engine: per-voice accuracy, artefacts, modulation, closeness to 0.2
#include "LegacyDoublerV05.h"

class DoublerEngineTests final : public juce::UnitTest
{
public:
    DoublerEngineTests() : juce::UnitTest ("Velvox doubler engine", "Velvox") {}

    using Buffer = juce::AudioBuffer<float>;

    template <typename Module, typename Settings>
    static Buffer runAny (const Settings& s, const Buffer& in, double rate = 48000.0, int block = 256,
                          const std::function<void (int, Settings&)>& perBlock = {})
    {
        auto d = std::make_unique<Module>();
        d->prepare (rate, block);
        d->setSettings (s);
        Buffer out (in);
        auto cur = s;
        for (int start = 0; start < out.getNumSamples(); start += block)
        {
            if (perBlock)
            {
                perBlock (start, cur);
                d->setSettings (cur);
            }
            const int n = std::min (block, out.getNumSamples() - start);
            float* ptrs[2] { out.getWritePointer (0, start), out.getNumChannels() > 1 ? out.getWritePointer (1, start) : nullptr };
            d->process (ptrs, out.getNumChannels(), n);
        }
        return out;
    }

    static Buffer run (const dsp::DoublerSettings& s, const Buffer& in, double rate = 48000.0,
                       const std::function<void (int, dsp::DoublerSettings&)>& perBlock = {})
    {
        return runAny<dsp::DoublerModule> (s, in, rate, 256, perBlock);
    }

    // One voice alone, centred, no dry, no modulation, open filters
    static dsp::DoublerSettings solo (int voice, float delayMs, float detune, dsp::DoublerMode mode = dsp::DoublerMode::four)
    {
        dsp::DoublerSettings s;
        s.enabled = true;
        s.mix = 1.0f;
        s.width = 1.0f;
        s.monoSafe = 0.0f;
        s.mode = mode;
        s.modDepthCents = 0.0f;
        s.lowCutHz = 20.0f;
        s.toneHz = 20000.0f;
        s.dryDb = -60.0f;
        for (int v = 0; v < 4; ++v)
            s.voices[(size_t) v] = { v == voice, delayMs, detune, 0.0f, 0.0f };
        return s;
    }

    static Buffer sine (double f, double seconds, double rate = 48000.0, float amp = 0.3f, int channels = 2)
    {
        Buffer b (channels, (int) (seconds * rate));
        for (int i = 0; i < b.getNumSamples(); ++i)
            for (int c = 0; c < channels; ++c)
                b.setSample (c, i, amp * (float) std::sin (2.0 * pi * f * i / rate));
        return b;
    }

    static std::vector<double> monoOf (const Buffer& b)
    {
        std::vector<double> m ((size_t) b.getNumSamples());
        for (int i = 0; i < b.getNumSamples(); ++i)
            m[(size_t) i] = b.getNumChannels() > 1 ? b.getSample (0, i) + b.getSample (1, i) : b.getSample (0, i);
        return m;
    }

    // Frequency per window from interpolated rising zero crossings
    static std::vector<double> centsTrack (const std::vector<double>& x, double f0, double rate, double fromS, double winS)
    {
        std::vector<double> out;
        const int win = (int) (winS * rate);
        for (int start = (int) (fromS * rate); start + win < (int) x.size(); start += win)
        {
            double first = -1.0, last = -1.0;
            int count = 0;
            for (int i = start + 1; i < start + win; ++i)
                if (x[(size_t) i - 1] < 0.0 && x[(size_t) i] >= 0.0)
                {
                    const double t = (i - 1) + x[(size_t) i - 1] / (x[(size_t) i - 1] - x[(size_t) i]);
                    if (first < 0.0) first = t;
                    last = t;
                    ++count;
                }
            if (count > 2)
                out.push_back (1200.0 * std::log2 ((count - 1) * rate / (last - first) / f0));
        }
        return out;
    }

    static double median (std::vector<double> v)
    {
        std::sort (v.begin(), v.end());
        return v.empty() ? 0.0 : v[v.size() / 2];
    }

    static double stddev (const std::vector<double>& v)
    {
        if (v.empty())
            return 0.0;
        double m = 0.0, q = 0.0;
        for (auto x : v) m += x;
        m /= (double) v.size();
        for (auto x : v) q += (x - m) * (x - m);
        return std::sqrt (q / (double) v.size());
    }

    // Std (dB) of the 20 ms energy (AM roughness of a steady tone)
    static double roughnessDb (const std::vector<double>& x, double rate, double fromS = 0.5)
    {
        std::vector<double> e;
        const int w = (int) (0.02 * rate);
        for (int s = (int) (fromS * rate); s + w < (int) x.size(); s += w / 2)
        {
            double sum = 0.0;
            for (int i = s; i < s + w; ++i) sum += x[(size_t) i] * x[(size_t) i];
            e.push_back (10.0 * std::log10 (sum / w + 1e-20));
        }
        return stddev (e);
    }

    static double lrCorrelation (const Buffer& b, int from)
    {
        double lr = 0, ll = 0, rr = 0;
        for (int i = from; i < b.getNumSamples(); ++i)
        {
            const double l = b.getSample (0, i), r = b.getSample (1, i);
            lr += l * r; ll += l * l; rr += r * r;
        }
        return lr / std::sqrt (ll * rr + 1e-30);
    }

    static double peakDelayMs (const std::vector<double>& x, int impulseAt, double rate)
    {
        int k = impulseAt;
        for (int i = impulseAt; i < (int) x.size(); ++i)
            if (std::abs (x[(size_t) i]) > std::abs (x[(size_t) k])) k = i;
        double frac = 0.0;
        if (k > 0 && k + 1 < (int) x.size())
        {
            const double a = x[(size_t) k - 1], b = x[(size_t) k], c = x[(size_t) k + 1];
            const double den = a - 2.0 * b + c;
            if (den != 0.0) frac = 0.5 * (a - c) / den;
        }
        return (k + frac - impulseAt) * 1000.0 / rate;
    }

    void runTest() override
    {
        constexpr double fs = 48000.0;

        for (double rate : { 48000.0, 96000.0 })
        {
            beginTest ("Doubler: per-voice delay is exact (" + juce::String (rate / 1000.0) + " kHz)");
            for (auto [voice, ms] : { std::pair { 0, 7.0f }, std::pair { 1, 33.0f }, std::pair { 3, 58.0f } })
            {
                Buffer in (2, (int) (0.5 * rate));
                in.clear();
                const int at = (int) (0.2 * rate);
                in.setSample (0, at, 0.5f);
                in.setSample (1, at, 0.5f);
                const auto out = monoOf (run (solo (voice, ms, 0.0f), in, rate));
                const double d = peakDelayMs (out, at, rate);
                logMessage ("voice " + juce::String (voice + 1) + ": set " + juce::String (ms) + " ms, measured " + juce::String (d, 4));
                expectWithinAbsoluteError (d, (double) ms, 0.05);
            }
        }

        beginTest ("Doubler: per-voice detune is exact and steady");
        {
            const float detunes[] { -20.0f, 12.0f, -7.0f, 30.0f };
            for (int v = 0; v < 4; ++v)
                for (double rate : { 48000.0, 96000.0 })
                {
                    if (rate > 50000.0 && v != 3) continue;
                    const auto out = monoOf (run (solo (v, 20.0f, detunes[v]), sine (440.0, 6.0, rate), rate));
                    const auto tr = centsTrack (out, 440.0, rate, 0.5, 0.1);
                    const double med = median (tr), sd = stddev (tr);
                    logMessage ("voice " + juce::String (v + 1) + " @" + juce::String (rate) + ": set " + juce::String (detunes[v]) + " ct, median "
                                + juce::String (med, 3) + " ct, std " + juce::String (sd, 3));
                    expectWithinAbsoluteError (med, (double) detunes[v], 0.5);
                    expectLessThan (sd, 1.0);
                }
        }

        beginTest ("Doubler: a detuned voice keeps its set delay on average");
        {
            for (float c : { 8.0f, -8.0f, 30.0f, -3.0f })
            {
                Buffer in (2, (int) (16.0 * fs));
                in.clear();
                const int period = (int) (0.1 * fs);
                for (int i = period; i < in.getNumSamples(); i += period)
                    for (int ch = 0; ch < 2; ++ch)
                        in.setSample (ch, i, 0.5f);
                const auto out = monoOf (run (solo (0, 20.0f, c), in, fs));
                double sum = 0.0;
                int n = 0;
                for (int i = period; i + period < (int) out.size(); i += period)
                {
                    int k = i;
                    for (int j = i; j < i + period; ++j)
                        if (std::abs (out[(size_t) j]) > std::abs (out[(size_t) k])) k = j;
                    sum += (k - i) * 1000.0 / fs;
                    ++n;
                }
                const double mean = sum / n;
                logMessage (juce::String (c) + " ct: mean delay " + juce::String (mean, 2) + " ms (set 20)");
                expectWithinAbsoluteError (mean, 20.0, 1.0);
            }
        }

        beginTest ("Doubler: a detuned voice has no splice artefacts");
        {
            for (float c : { 30.0f, -30.0f, 6.0f })
            {
                const auto out = monoOf (run (solo (0, 20.0f, c), sine (440.0, 8.0), fs));
                const double r = roughnessDb (out, fs);
                // spurious: energy away from the shifted tone (Hann window, +-15 Hz main lobe)
                const int order = 18, n = 1 << order, from = (int) (1.0 * fs);
                std::vector<float> fft ((size_t) n * 2, 0.0f);
                for (int i = 0; i < n && from + i < (int) out.size(); ++i)
                    fft[(size_t) i] = (float) (out[(size_t) (from + i)] * (0.5 - 0.5 * std::cos (2.0 * pi * i / n)));
                juce::dsp::FFT f (order);
                f.performFrequencyOnlyForwardTransform (fft.data());
                const double f1 = 440.0 * std::pow (2.0, c / 1200.0);
                double main = 0.0, rest = 0.0;
                for (int k = 1; k < n / 2; ++k)
                {
                    const double hz = k * fs / n, p = (double) fft[(size_t) k] * fft[(size_t) k];
                    (std::abs (hz - f1) < 15.0 ? main : rest) += p;
                }
                const double spurious = 10.0 * std::log10 (rest / (main + 1e-30) + 1e-30);
                logMessage (juce::String (c) + " ct: roughness " + juce::String (r, 3) + " dB, spurious " + juce::String (spurious, 1) + " dB");
                expectLessThan (r, 0.3);
                expectLessThan (spurious, -50.0);
            }
        }

        beginTest ("Doubler: modulation is a smooth random walk");
        {
            auto s = solo (0, 20.0f, 0.0f, dsp::DoublerMode::two);
            s.modDepthCents = 20.0f;
            s.modRateHz = 0.5f;
            const auto out = monoOf (run (s, sine (1000.0, 30.0), fs));
            auto tr = centsTrack (out, 1000.0, fs, 1.0, 0.05);
            double peak = 0.0, mean = 0.0;
            for (auto c : tr) { peak = std::max (peak, std::abs (c)); mean += c; }
            mean /= (double) juce::jmax ((size_t) 1, tr.size());
            logMessage ("peak pitch deviation " + juce::String (peak, 2) + " ct at 20 ct depth");
            expectGreaterThan (peak, 10.0);
            expectLessThan (peak, 35.0);
            // not periodic: the track does not repeat itself after one nominal period or later
            for (auto& c : tr) c -= mean;
            double r0 = 1e-30;
            for (auto c : tr) r0 += c * c;
            double worst = 0.0;
            for (int lag = 40; lag < 200 && (size_t) lag < tr.size(); ++lag)   // 2 .. 10 s
            {
                double r = 0.0;
                for (size_t i = 0; i + (size_t) lag < tr.size(); ++i) r += tr[i] * tr[i + (size_t) lag];
                worst = std::max (worst, r / r0 * (double) tr.size() / (double) (tr.size() - (size_t) lag));
            }
            logMessage ("largest autocorrelation beyond one period " + juce::String (worst, 3));
            expectLessThan (worst, 0.6);
        }

        beginTest ("Doubler: Classic is close to the 0.2 engine");
        {
            struct Case { float mix, width, delay, detune, monoSafe, tone; };
            for (const auto& c : { Case { 0.5f, 0.8f, 14.0f, 8.0f, 0.5f, 9000.0f }, Case { 1.0f, 1.0f, 30.0f, 20.0f, 0.0f, 20000.0f },
                                   Case { 0.3f, 0.5f, 5.0f, 0.0f, 0.8f, 4000.0f }, Case { 0.7f, 0.9f, 40.0f, 25.0f, 0.2f, 12000.0f } })
            {
                dsp::DoublerSettings s;
                s.enabled = true; s.mix = c.mix; s.width = c.width; s.delayMs = c.delay; s.detuneCents = c.detune;
                s.monoSafe = c.monoSafe; s.toneHz = c.tone;
                legacy::LegacyDoublerSettings o;
                o.enabled = true; o.mix = c.mix; o.width = c.width; o.delayMs = c.delay; o.detuneCents = c.detune;
                o.monoSafe = c.monoSafe; o.toneHz = c.tone;

                juce::Random rnd (21);
                Buffer noise (2, (int) (6.0 * fs));
                for (int i = 0; i < noise.getNumSamples(); ++i)
                {
                    const float v = 0.2f * (rnd.nextFloat() * 2.0f - 1.0f);
                    noise.setSample (0, i, v); noise.setSample (1, i, v);
                }
                const auto tone = sine (440.0, 20.0);
                const auto nNew = run (s, noise), nOld = runAny<legacy::LegacyDoubler> (o, noise);
                const auto tNew = run (s, tone), tOld = runAny<legacy::LegacyDoubler> (o, tone);
                const int from = (int) fs, len = nNew.getNumSamples() - from;
                const double lvl = 0.5 * ((rmsDb (nNew, from, len, 0) + rmsDb (nNew, from, len, 1)) - (rmsDb (nOld, from, len, 0) + rmsDb (nOld, from, len, 1)));
                const double corrNew = lrCorrelation (nNew, from), corrOld = lrCorrelation (nOld, from);
                const double rNew = roughnessDb (monoOf (tNew), fs), rOld = roughnessDb (monoOf (tOld), fs);
                logMessage ("mix " + juce::String (c.mix) + " delay " + juce::String (c.delay) + " detune " + juce::String (c.detune)
                            + ": level diff " + juce::String (lvl, 2) + " dB, corr " + juce::String (corrNew, 3) + " vs " + juce::String (corrOld, 3)
                            + ", roughness " + juce::String (rNew, 2) + " vs " + juce::String (rOld, 2) + " dB");
                expectWithinAbsoluteError (lvl, 0.0, 0.5);
                // Remaining differences: the Lagrange taps keep the voices' highs (linear interpolation of 0.2
                // dulled them: up to +0.4 dB level, a little less correlation with the Tone open) and the
                // random-walk path changes which beat phases a tone passes through (roughness noise)
                expectWithinAbsoluteError (corrNew, corrOld, 0.06);
                expectLessThan (rNew, rOld + 0.4);
                expectGreaterThan (rNew, rOld - 0.6);
            }
        }

        beginTest ("Doubler: dry level and the true law at Mix 100 %");
        {
            const auto in = sine (440.0, 1.0);
            auto s = solo (0, 20.0f, 0.0f);
            s.dryDb = -6.0f;
            s.mix = 0.0f;
            const auto half = run (s, in);
            expectWithinAbsoluteError (rmsDb (half, 4800, 24000, 0) - rmsDb (in, 4800, 24000, 0), -6.0f, 0.05f, "dry -6 dB at Mix 0");

            Buffer burst (2, (int) fs);
            burst.clear();
            for (int i = 0; i < 480; ++i)
                for (int c = 0; c < 2; ++c)
                    burst.setSample (c, i, 0.5f * (float) std::sin (2.0 * pi * 1000.0 * i / fs));
            for (auto mode : { 0, 1 })
            {
                auto t = solo (0, 20.0f, 0.0f, dsp::DoublerMode::two);
                if (mode == 0) t.dryDb = -60.0f;                                          // dry off
                else { t.dryDb = 0.0f; t.mixLaw = dsp::MixLaw::trueDryWet; t.mix = 1.0f; } // true law, Mix 100 %
                const auto out = run (t, burst);
                expectLessThan (rmsDb (out, 0, 480, 0), rmsDb (burst, 0, 480, 0) - 60.0f, mode == 0 ? "dry off" : "true law 100 %");
            }
        }

        beginTest ("Doubler: detune sweep while playing stays click-free");
        {
            auto s = solo (0, 20.0f, 0.0f, dsp::DoublerMode::two);
            const float steps[] { 0.0f, 30.0f, -30.0f, 5.0f, 0.0f, -12.0f, 30.0f, 0.0f };
            const auto out = monoOf (run (s, sine (440.0, 4.0), fs, [&] (int start, dsp::DoublerSettings& cur)
            {
                cur.voices[0].detuneCents = steps[juce::jlimit (0, 7, (int) (start / (0.5 * fs)))];
            }));
            const double expected = 0.3 * std::pow (2.0 * pi * 440.0 * 1.02 / fs, 2.0);
            double worst = 0.0;
            for (size_t i = (size_t) (0.05 * fs); i + 1 < out.size(); ++i)
                worst = std::max (worst, std::abs (out[i + 1] - 2.0 * out[i] + out[i - 1]));
            logMessage ("max 2nd difference " + juce::String (worst / expected, 2) + "x a clean tone");
            expectLessThan (worst, 3.0 * expected);
        }

        beginTest ("Doubler: 1 ms delay with 25 ct modulation and 30 ct detune stays causal and finite");
        {
            auto s = solo (0, 1.0f, 30.0f, dsp::DoublerMode::two);
            s.modDepthCents = 25.0f;
            s.voices[1] = { true, 1.0f, -30.0f, 0.0f, 0.0f };
            juce::Random rnd (5);
            Buffer in (2, (int) (4.0 * fs));
            in.clear();
            for (int i = 0; i < (int) (3.0 * fs); ++i)
            {
                const float v = 0.3f * (rnd.nextFloat() * 2.0f - 1.0f);
                in.setSample (0, i, v); in.setSample (1, i, v);
            }
            const auto out = run (s, in);
            expect (allFinite (out));
            // well after the input stopped nothing may come back (a tap ahead of the write head would read old data)
            expectLessThan (rmsDb (out, (int) (3.3 * fs), (int) (0.6 * fs), 0), -100.0f);
            expectGreaterThan (rmsDb (out, (int) (1.0 * fs), (int) fs, 0), -30.0f);
        }

        beginTest ("Doubler: 4 voices on a mono bus keep mono-safe and the gain normalisation");
        {
            auto s = solo (0, 20.0f, 0.0f);
            for (auto& v : s.voices) v.on = true;
            s.dryDb = 0.0f;
            s.monoSafe = 1.0f;
            const auto in = sine (440.0, 1.0, fs, 0.3f, 1);
            const auto out = run (s, in);
            double err = 0.0;
            for (int i = 0; i < in.getNumSamples(); ++i)
                err = std::max (err, (double) std::abs (out.getSample (0, i) - in.getSample (0, i)));
            expectLessThan (err, 1.0e-5, "mono safe 100 %: mono = dry");

            // four voices at unity about as loud as two
            const auto loud = [&] (dsp::DoublerMode m)
            {
                auto t = solo (0, 20.0f, 0.0f, m);
                for (int v = 0; v < 4; ++v) t.voices[(size_t) v] = { true, 10.0f + 9.0f * (float) v, 0.0f, 0.0f, 0.0f };
                t.modDepthCents = 10.0f;
                juce::Random rnd (9);
                Buffer n (1, (int) (3.0 * fs));
                for (int i = 0; i < n.getNumSamples(); ++i) n.setSample (0, i, 0.2f * (rnd.nextFloat() * 2.0f - 1.0f));
                const auto o = run (t, n);
                return rmsDb (o, (int) fs, 2 * (int) fs, 0);
            };
            expectWithinAbsoluteError (loud (dsp::DoublerMode::four), loud (dsp::DoublerMode::two), 1.5f);
        }

        beginTest ("Doubler: an old state (no 0.6 IDs) loads as Classic with its 0.2 values");
        {
            Harness h;
            h.set (pid::doublerOn, 1.0f);
            h.set (pid::doublerDelay, 30.0f);
            h.set (pid::doublerVoices, 2.0f);
            h.set (pid::doublerV1Delay, 3.0f);
            juce::MemoryBlock blob;
            h.proc.getStateInformation (blob);
            auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize());
            auto tree = juce::ValueTree::fromXml (*xml);
            for (int i = tree.getNumChildren(); --i >= 0;)
            {
                const auto id = tree.getChild (i).getProperty ("id").toString();
                if (id.startsWith ("doublerV") || id.startsWith ("doublerMod") || id == "doublerLowCut" || id == "doublerDry")
                    tree.removeChild (i, nullptr);
            }
            juce::MemoryBlock old;
            juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), old);

            Harness fresh;
            fresh.proc.setStateInformation (old.getData(), (int) old.getSize());
            expectEquals (fresh.get (pid::doublerVoices), 0.0f, "Classic");
            expectEquals (fresh.get (pid::doublerDelay), 30.0f);
            expectWithinAbsoluteError (fresh.get (pid::doublerV1Delay), 14.0f, 1.0e-4f);
            const auto r = dsp::DoublerModule::resolveVoices (fresh.proc.readSettings().doubler);
            expectWithinAbsoluteError (r.v[1].delayMs, 41.1f, 1.0e-3f);
        }
    }
};

static DoublerEngineTests doublerEngineTests;
