// Saturation 0.6 (spec 5.6, QUALITY_BASELINE gap #1): antialiasing at equal THD, a flat linear
// chain at any Mix, constant latency, click-free switching, the four characters, the UI feed and
// the Low / Mid / High multiband mode.

#include "TestHelpers.h"
#include "DSP/SaturationModule.h"
#include <juce_dsp/juce_dsp.h>

using namespace velvox;
using namespace velvox::test;
using dsp::SaturationModule;
using dsp::SatSettings;

namespace
{
    constexpr double fs48 = 48000.0;

    // Mono test signal through one module instance (both channels carry the same signal)
    std::vector<float> render (SaturationModule& sat, const std::vector<float>& in, int block = 512)
    {
        std::vector<float> l (in), r (in);
        for (size_t start = 0; start < in.size(); start += (size_t) block)
        {
            const int n = (int) std::min ((size_t) block, in.size() - start);
            float* ch[] { l.data() + start, r.data() + start };
            sat.process (ch, 2, n);
        }
        return l;
    }

    std::vector<float> sineVec (double fs, double freq, double dbfs, double seconds)
    {
        const float a = (float) std::pow (10.0, dbfs / 20.0);
        std::vector<float> v ((size_t) (seconds * fs));
        for (size_t i = 0; i < v.size(); ++i)
            v[i] = a * (float) std::sin (2.0 * pi * freq * (double) i / fs);
        return v;
    }

    SatSettings single (int character, float driveDb, float mix = 1.0f)
    {
        SatSettings s;
        s.enabled = true;
        s.driveDb = driveDb;
        s.character = character;
        s.mix = mix;
        s.outputDb = 0.0f;
        return s;
    }

    std::vector<float> renderWith (const SatSettings& s, const std::vector<float>& in, double fs = fs48)
    {
        auto sat = std::make_unique<SaturationModule>();
        sat->setSettings (s);
        sat->prepare (fs, 512);
        return render (*sat, in);
    }

    // Power spectrum of a Blackman-Harris windowed segment (as tools/refhost/qmetrics._tone_spectrum)
    struct Spectrum
    {
        std::vector<double> power;
        double binHz = 1.0;

        double band (double fc, int halfBins = 8) const
        {
            const int k = (int) std::lround (fc / binHz);
            double p = 0.0;
            for (int i = std::max (0, k - halfBins); i <= std::min ((int) power.size() - 1, k + halfBins); ++i)
                p += power[(size_t) i];
            return p;
        }
    };

    Spectrum spectrumOf (const std::vector<float>& y, double fs, int order = 15)
    {
        const int n = 1 << order;
        const size_t start = y.size() / 4;
        jassert (start + (size_t) n <= y.size());
        std::vector<float> buf ((size_t) (2 * n), 0.0f);
        for (int i = 0; i < n; ++i)
        {
            const double t = 2.0 * pi * i / (n - 1);
            const double w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t);
            buf[(size_t) i] = (float) (y[start + (size_t) i] * w);
        }
        juce::dsp::FFT fft (order);
        fft.performFrequencyOnlyForwardTransform (buf.data());
        Spectrum s;
        s.binHz = fs / n;
        s.power.resize ((size_t) (n / 2 + 1));
        for (size_t i = 0; i < s.power.size(); ++i)
            s.power[i] = (double) buf[i] * buf[i];
        return s;
    }

    double dbOf (double p) { return 10.0 * std::log10 (std::max (p, 1.0e-30)); }

    // qmetrics.aliasing_db: energy outside DC, the fundamental and its harmonics, relative to the fundamental
    double aliasingDb (const std::vector<float>& y, double fs, double f0)
    {
        const auto s = spectrumOf (y, fs);
        std::vector<bool> keep (s.power.size(), true);
        for (size_t i = 0; i < keep.size(); ++i)
            if ((double) i * s.binHz < 20.0)
                keep[i] = false;
        for (int k = 1; k * f0 < fs / 2; ++k)
        {
            const int c = (int) std::lround (k * f0 / s.binHz);
            for (int i = std::max (0, c - 8); i <= std::min ((int) keep.size() - 1, c + 8); ++i)
                keep[(size_t) i] = false;
        }
        double rest = 0.0;
        for (size_t i = 0; i < keep.size(); ++i)
            if (keep[i])
                rest += s.power[i];
        return dbOf (rest / s.band (f0));
    }

    double thdDb (const std::vector<float>& y, double fs, double f0)
    {
        const auto s = spectrumOf (y, fs);
        double h = 0.0;
        for (int k = 2; k <= 40 && k * f0 < fs / 2 - 100; ++k)
            h += s.band (k * f0);
        return dbOf (h / s.band (f0));
    }

    // THD of the static curve itself (one period of a 1 kHz sine at 48 kHz, exact DFT)
    double staticThdDb (int character, float driveDb, double dbfs)
    {
        constexpr int n = 48;
        const float d = juce::Decibels::decibelsToGain (driveDb);
        const float a = (float) std::pow (10.0, dbfs / 20.0);
        double p1 = 0.0, ph = 0.0;
        for (int k = 1; k < n / 2; ++k)
        {
            double re = 0.0, im = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const double y = SaturationModule::shape (a * (float) std::sin (2.0 * pi * i / n), d, character);
                re += y * std::cos (2.0 * pi * k * i / n);
                im += y * std::sin (2.0 * pi * k * i / n);
            }
            (k == 1 ? p1 : ph) += re * re + im * im;
        }
        return dbOf (ph / p1);
    }

    // Gain of a tone through the module, dB (steady state)
    double toneGainDb (const SatSettings& s, double freq, double fs = fs48)
    {
        const auto in = sineVec (fs, freq, -50.0, 1.0);
        const auto out = renderWith (s, in, fs);
        const auto si = spectrumOf (in, fs), so = spectrumOf (out, fs);
        return dbOf (so.band (freq) / si.band (freq));
    }

    // Energy above 5 kHz (8th-order Butterworth high-pass), dB rel. the signal's RMS
    double splatterDb (const std::vector<float>& y, double fs, size_t from)
    {
        auto coeffs = juce::dsp::FilterDesign<float>::designIIRHighpassHighOrderButterworthMethod (5000.0f, fs, 8);
        std::vector<juce::dsp::IIR::Filter<float>> f (coeffs.size());
        for (size_t i = 0; i < coeffs.size(); ++i)
            f[i].coefficients = coeffs[i];
        double hp = 0.0, all = 0.0;
        for (size_t i = 0; i < y.size(); ++i)
        {
            float v = y[i];
            for (auto& s : f)
                v = s.processSample (v);
            if (i >= from)
            {
                hp = std::max (hp, (double) v * v);
                all += (double) y[i] * y[i];
            }
        }
        return dbOf (hp / (all / (double) (y.size() - from)));
    }
}

class SaturationTests final : public juce::UnitTest
{
public:
    SaturationTests() : juce::UnitTest ("Velvox saturation 0.6", "Velvox") {}

    void runTest() override
    {
        beginTest ("Aliasing at equal THD (4987 Hz, -6 dBFS) - Saturn 2 HQ class");
        {
            const auto tone = sineVec (fs48, 4987.0, -6.0, 2.0);
            const char* names[] { "Warm", "Tape", "Tube", "Hard" };
            for (int c = 0; c < SaturationModule::numCharacters; ++c)
                for (float drive : { 12.0f, 24.0f })
                {
                    const double a = aliasingDb (renderWith (single (c, drive), tone), fs48, 4987.0);
                    logMessage (juce::String (names[c]) + " drive " + juce::String (drive, 0) + " dB: aliasing " + juce::String (a, 1) + " dB");
                    expectLessThan (a, drive < 20.0f ? -67.0 : -62.0, names[c]);
                }
        }

        beginTest ("THD calibration unchanged (1 kHz, -12 / -24 dBFS = the static curve)");
        {
            for (int c = 0; c < SaturationModule::numCharacters; ++c)
                for (double level : { -12.0, -24.0 })
                {
                    const double measured = thdDb (renderWith (single (c, 12.0f), sineVec (fs48, 1000.0, level, 2.0)), fs48, 1000.0);
                    const double expected = staticThdDb (c, 12.0f, level);
                    logMessage ("character " + juce::String (c) + " at " + juce::String (level, 0) + " dBFS: THD "
                                + juce::String (measured, 2) + " dB, curve " + juce::String (expected, 2) + " dB");
                    expectWithinAbsoluteError (measured, expected, 0.3);
                }
        }

        beginTest ("Linear chain flat at Mix 100 % and 50 % (no comb between dry and wet)");
        {
            for (float mix : { 1.0f, 0.5f })
            {
                const auto s = single (1, 0.0f, mix);
                const double ref = toneGainDb (s, 1000.0);
                for (double f : { 100.0, 200.0, 5000.0, 10000.0, 15000.0, 18000.0 })
                {
                    const double g = toneGainDb (s, f) - ref;
                    if (std::abs (g) > 0.1)
                        logMessage ("mix " + juce::String (mix) + ": " + juce::String (f, 0) + " Hz " + juce::String (g, 2) + " dB");
                    expectWithinAbsoluteError (g, 0.0, 0.1, "at " + juce::String (f, 0) + " Hz, mix " + juce::String (mix));
                }
            }
        }

        beginTest ("Constant latency, matching the real delay, at 44.1 / 48 / 96 kHz");
        {
            for (double fs : { 44100.0, 48000.0, 96000.0 })
            {
                SaturationModule sat;
                sat.prepare (fs, 512);
                const int off = sat.getLatencySamples();
                sat.setSettings (single (1, 0.0f));
                const int on = sat.getLatencySamples();
                expectEquals (on, off, "latency must not depend on the settings");
                expect (on > 0);

                // Alignment of the wet path (three inharmonic mid tones) ~ the reported latency
                std::vector<float> in ((size_t) fs);
                for (size_t i = 0; i < in.size(); ++i)
                    for (double f : { 210.0, 730.0, 1530.0 })
                        in[i] += 0.003f * (float) std::sin (2.0 * pi * f * (double) i / fs);
                const auto out = render (sat, in);
                double best = 1.0e30; int bestLag = 0;
                const size_t from = (size_t) (0.5 * fs);
                for (int lag = 0; lag < 64; ++lag)
                {
                    double e = 0.0;
                    for (size_t i = from; i < from + 4800; ++i)
                        e += std::pow ((double) out[i] - in[i - (size_t) lag], 2.0);
                    if (e < best) { best = e; bestLag = lag; }
                }
                logMessage (juce::String (fs, 0) + " Hz: latency " + juce::String (on) + ", best aligned lag " + juce::String (bestLag));
                expectEquals (bestLag, on);
            }
        }

        beginTest ("Mix 0 % and off: exactly the delayed dry");
        {
            const auto in = sineVec (fs48, 440.0, -6.0, 0.5);
            for (int mode = 0; mode < 2; ++mode)
            {
                auto s = single (2, 18.0f, mode == 0 ? 0.0f : 1.0f);
                s.enabled = mode == 0;
                SaturationModule sat;
                sat.setSettings (s);
                sat.prepare (fs48, 512);
                const auto out = render (sat, in);
                const int lat = sat.getLatencySamples();
                double e = 0.0, p = 0.0;
                for (size_t i = (size_t) lat; i < in.size(); ++i)
                {
                    e += std::pow ((double) out[i] - in[i - (size_t) lat], 2.0);
                    p += (double) in[i] * in[i];
                }
                expectLessThan (dbOf (e / p), -100.0);
            }
        }

        beginTest ("No clicks when switching on/off or moving Mix to 0 %");
        {
            const auto in = sineVec (fs48, 200.0, -30.0, 3.0);
            SaturationModule sat;
            auto s = single (1, 0.0f, 1.0f);
            s.enabled = false;
            sat.setSettings (s);
            sat.prepare (fs48, 512);
            std::vector<float> l (in), r (in);
            int step = 0;
            for (size_t start = 0; start < in.size(); start += 512, ++step)
            {
                if (step % 20 == 10)
                {
                    // alternate: on / off / on at mix 40 % / mix 0 %
                    const int phase = (step / 20) % 4;
                    s.enabled = phase != 1;
                    s.mix = phase == 2 ? 0.4f : (phase == 3 ? 0.0f : 1.0f);
                    sat.setSettings (s);
                }
                const int n = (int) std::min ((size_t) 512, in.size() - start);
                float* ch[] { l.data() + start, r.data() + start };
                sat.process (ch, 2, n);
            }
            const double sp = splatterDb (l, fs48, 4800);
            logMessage ("HF splatter while switching: " + juce::String (sp, 1) + " dB (input alone "
                        + juce::String (splatterDb (in, fs48, 4800), 1) + " dB, always on "
                        + juce::String (splatterDb (renderWith (single (1, 0.0f), in), fs48, 4800), 1) + " dB)");
            expectLessThan (sp, -60.0);
        }
    }
};

static SaturationTests saturationTests;
