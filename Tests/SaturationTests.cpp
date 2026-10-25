// Saturation 0.6 (spec 5.6, QUALITY_BASELINE gap #1): antialiasing at equal THD, a flat linear
// chain at any Mix, constant latency, click-free switching, the four characters, the UI feed and
// the Low / Mid / High multiband mode.

#include "TestHelpers.h"
#include "SyntheticVocal.h"
#include "DSP/SaturationModule.h"
#include "Parameters/SettingsReader.h"
#include <juce_dsp/juce_dsp.h>
#include <complex>

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

        beginTest ("Four clearly different characters (harmonic signature)");
        {
            std::array<std::array<float, 8>, 4> sig {};
            for (int c = 0; c < 4; ++c)
            {
                sig[(size_t) c] = SaturationModule::harmonicProfile (c, 12.0f, -12.0f);
                juce::String t ("character " + juce::String (c) + ":");
                for (float h : sig[(size_t) c])
                    t << " " << juce::String (h, 1);
                logMessage (t);
            }
            for (int a = 0; a < 4; ++a)
                for (int b = a + 1; b < 4; ++b)
                {
                    float d = 0.0f;
                    for (int k = 1; k < 5; ++k)   // H2..H5
                        d = std::max (d, std::abs (std::max (sig[(size_t) a][(size_t) k], -100.0f) - std::max (sig[(size_t) b][(size_t) k], -100.0f)));
                    expectGreaterThan (d, 6.0f, "characters " + juce::String (a) + " / " + juce::String (b));
                }
        }

        beginTest ("harmonicProfile and transferCurve match the processed audio");
        {
            for (int c = 0; c < 4; ++c)
            {
                const auto prof = SaturationModule::harmonicProfile (c, 12.0f, -12.0f);
                const auto out = renderWith (single (c, 12.0f), sineVec (fs48, 1000.0, -12.0, 2.0));
                const auto s = spectrumOf (out, fs48);
                for (int k = 2; k <= 3; ++k)
                {
                    const double measured = dbOf (s.band (k * 1000.0) / s.band (1000.0));
                    if (prof[(size_t) k - 1] > -80.0f)
                        expectWithinAbsoluteError ((double) prof[(size_t) k - 1], measured, 1.0, "H" + juce::String (k));
                }

                float x[5] { -1.0f, -0.25f, 0.0f, 0.1f, 0.5f }, y[5] {};
                SaturationModule::transferCurve (c, 12.0f, x, y, 5);
                const float d = juce::Decibels::decibelsToGain (12.0f);
                for (int i = 0; i < 5; ++i)
                    expectWithinAbsoluteError (y[i], SaturationModule::shape (x[i], d, c) * SaturationModule::compensationFor (d, c), 1.0e-6f);
            }
        }

        beginTest ("Loudness stays matched across the Drive range (vocal-like signal)");
        {
            VocalSpec vs;
            vs.seconds = 4.0;
            vs.levelDb = -18.0f;
            const auto voice = makeVocal (vs).vocal[0];
            const auto rms = [] (const std::vector<float>& v, size_t from)
            {
                double e = 0.0;
                for (size_t i = from; i < v.size(); ++i)
                    e += (double) v[i] * v[i];
                return 10.0 * std::log10 (e / (double) (v.size() - from) + 1.0e-30);
            };
            const double inDb = rms (voice, 4800);
            for (int c = 0; c < 4; ++c)
                for (float drive : { 0.0f, 6.0f, 12.0f, 18.0f, 24.0f })
                {
                    const double change = rms (renderWith (single (c, drive), voice), 4800) - inDb;
                    logMessage ("character " + juce::String (c) + ", drive " + juce::String (drive, 0) + " dB: level " + juce::String (change, 2) + " dB");
                    // Ruling (ledger): the 0.2 compensation is kept (old projects keep their level);
                    // it holds the vocal within -1.1 .. +1.7 dB over the whole Drive range
                    expectWithinAbsoluteError (change, 0.0, 2.0);
                }
        }

        beginTest ("Meters report band input and output levels");
        {
            SaturationModule sat;
            sat.setSettings (single (1, 12.0f));
            sat.prepare (fs48, 512);
            render (sat, sineVec (fs48, 1000.0, -12.0, 0.5));
            const float in = sat.getMeters().inDb[0].load(), out = sat.getMeters().outDb[0].load();
            logMessage ("meters: in " + juce::String (in, 1) + " dB, out " + juce::String (out, 1) + " dB");
            expectWithinAbsoluteError (in, -15.0f, 0.5f);   // RMS of a -12 dBFS sine
            expectWithinAbsoluteError (out, -15.0f, 1.0f);  // compensated
        }

        runMultibandTests();
    }

    // Complex gain of a tone (steady state), output / input
    static std::complex<double> complexGain (const SatSettings& s, double f)
    {
        const auto in = sineVec (fs48, f, -40.0, 1.0);
        const auto out = renderWith (s, in);
        std::complex<double> x {}, y {};
        for (size_t i = 24000; i < 48000; ++i)
        {
            const auto e = std::polar (1.0, -2.0 * pi * f * (double) i / fs48);
            x += (double) in[i] * e;
            y += (double) out[i] * e;
        }
        return y / x;
    }

    // The digital (TPT, bilinear with prewarp) 2nd-order Butterworth all-pass at the 4x rate
    static std::complex<double> allpassAt (double fc, double f)
    {
        const double fsH = 4.0 * fs48, k = std::sqrt (2.0);
        const double g = std::tan (pi * fc / fsH);
        const std::complex<double> s (0.0, std::tan (pi * f / fsH) / g);
        return (s * s - k * s + 1.0) / (s * s + k * s + 1.0);
    }

    static SatSettings multi (bool allOn, float drive = 12.0f, int character = 1)
    {
        SatSettings s = single (character, drive);
        s.multiband = true;
        for (auto& b : s.bands)
            b = { allOn, drive, character };
        return s;
    }

    void runMultibandTests()
    {
        beginTest ("Multiband parameters exist with defaults that keep the old behaviour");
        {
            Harness h;
            expectEquals ((int) h.get (pid::satMultiband), 0);
            expectWithinAbsoluteError (h.get (pid::satXoverLow), 250.0f, 0.01f);
            expectWithinAbsoluteError (h.get (pid::satXoverHigh), 4000.0f, 0.01f);
            for (auto* id : { pid::satLowOn, pid::satMidOn, pid::satHighOn, pid::satLink })
                expectEquals ((int) h.get (id), 1);
            for (auto* id : { pid::satLowDrive, pid::satMidDrive, pid::satHighDrive })
                expectWithinAbsoluteError (h.get (id), 6.0f, 0.01f);
            expectEquals ((int) h.get (pid::satLowChar), 0);
            expectEquals ((int) h.get (pid::satMidChar), 1);
            expectEquals ((int) h.get (pid::satHighChar), 1);
            expect (h.proc.getState().getParameter ("satLowDrive_2") != nullptr, "instance bank");
            expect (h.proc.getState().getParameter ("satMultiband_3") != nullptr, "instance bank");
        }

        beginTest ("Multiband off: band parameters change nothing (bit-exact single band)");
        {
            const auto runIt = [] (bool touchBands)
            {
                Harness h;
                h.neutral();
                h.set (pid::satOn, 1.0f);
                h.set (pid::satDrive, 15.0f);
                h.set (pid::satCharacter, 2.0f);
                if (touchBands)
                {
                    h.set (pid::satLowDrive, 24.0f);
                    h.set (pid::satHighChar, 3.0f);
                    h.set (pid::satMidOn, 0.0f);
                    h.set (pid::satXoverLow, 900.0f);
                }
                return h.run (h.sine (300.0, 0.3f, 1.0));
            };
            const auto a = runIt (false), b = runIt (true);
            float maxDiff = 0.0f;
            for (int i = 0; i < a.getNumSamples(); ++i)
                maxDiff = std::max (maxDiff, std::abs (a.getSample (0, i) - b.getSample (0, i)));
            expectEquals (maxDiff, 0.0f);
        }

        beginTest ("Bands off: the sum is a flat, phase-compensated LR4 all-pass (null against the analytic all-pass)");
        {
            auto s1 = multi (false);
            auto s2 = multi (false);
            s2.xoverLowHz = 1000.0f;
            s2.xoverHighHz = 1500.0f;
            for (double f : { 60.0, 150.0, 250.0, 700.0, 2000.0, 4000.0, 7000.0, 12000.0, 17000.0 })
            {
                const auto g1 = complexGain (s1, f), g2 = complexGain (s2, f);
                const auto expected = allpassAt (250.0, f) * allpassAt (4000.0, f) / (allpassAt (1000.0, f) * allpassAt (1500.0, f));
                const double err = 20.0 * std::log10 (std::abs (g1 / g2 - expected) + 1.0e-12);
                const double magDb = 20.0 * std::log10 (std::abs (g1));
                logMessage (juce::String (f, 0) + " Hz: |sum| " + juce::String (magDb, 3) + " dB, null vs all-pass " + juce::String (err, 1) + " dB");
                expectLessThan (err, -60.0);
                expectWithinAbsoluteError (magDb, 0.0, 0.05);
            }
        }

        beginTest ("Bands act separately");
        {
            // Only the high band drives hard: a low tone stays clean, a high tone distorts
            auto s = multi (false, 24.0f, 2);
            s.bands[2].on = true;
            const double lowThd = thdDb (renderWith (s, sineVec (fs48, 200.0, -12.0, 2.0)), fs48, 200.0);
            const double highThd = thdDb (renderWith (s, sineVec (fs48, 7000.0, -12.0, 2.0)), fs48, 7000.0);
            logMessage ("high band only: 200 Hz THD " + juce::String (lowThd, 1) + " dB, 7 kHz THD " + juce::String (highThd, 1) + " dB");
            expectLessThan (lowThd, -60.0);
            expectGreaterThan (highThd, -30.0);

            s = multi (false, 24.0f, 2);
            s.bands[0].on = true;
            const double lowThd2 = thdDb (renderWith (s, sineVec (fs48, 100.0, -12.0, 2.0)), fs48, 100.0);
            const double highThd2 = thdDb (renderWith (s, sineVec (fs48, 7000.0, -12.0, 2.0)), fs48, 7000.0);
            logMessage ("low band only: 100 Hz THD " + juce::String (lowThd2, 1) + " dB, 7 kHz THD " + juce::String (highThd2, 1) + " dB");
            expectGreaterThan (lowThd2, -30.0);
            expectLessThan (highThd2, -60.0);
        }

        beginTest ("Multiband aliasing at equal settings");
        {
            const auto tone = sineVec (fs48, 4987.0, -6.0, 2.0);
            for (int c = 0; c < 4; ++c)
            {
                const double a = aliasingDb (renderWith (multi (true, 12.0f, c), tone), fs48, 4987.0);
                logMessage ("multiband, character " + juce::String (c) + ": aliasing " + juce::String (a, 1) + " dB");
                expectLessThan (a, -67.0);
            }
        }

        beginTest ("Crossovers in the wrong order stay stable and flat");
        {
            auto s = multi (false);
            s.xoverLowHz = 1000.0f;
            s.xoverHighHz = 1000.0f;   // <= low: kept at 1.5 x low
            for (double f : { 100.0, 1000.0, 1500.0, 8000.0 })
                expectWithinAbsoluteError (20.0 * std::log10 (std::abs (complexGain (s, f))), 0.0, 0.05);
        }

        beginTest ("No click when switching between single band and multiband");
        {
            const auto in = sineVec (fs48, 200.0, -30.0, 2.0);
            SaturationModule sat;
            auto s = single (1, 0.0f);
            sat.setSettings (s);
            sat.prepare (fs48, 512);
            std::vector<float> l (in), r (in);
            int step = 0;
            for (size_t start = 0; start < in.size(); start += 512, ++step)
            {
                if (step % 15 == 7)
                {
                    s.multiband = ! s.multiband;
                    for (auto& b : s.bands)
                        b = { true, 0.0f, 1 };
                    sat.setSettings (s);
                }
                const int n = (int) std::min ((size_t) 512, in.size() - start);
                float* ch[] { l.data() + start, r.data() + start };
                sat.process (ch, 2, n);
            }
            const double sp = splatterDb (l, fs48, 4800);
            logMessage ("HF splatter while switching modes: " + juce::String (sp, 1) + " dB");
            expectLessThan (sp, -60.0);
        }

        beginTest ("Macros move all band drives together; Link helper keeps the differences");
        {
            OfflineParameters p;
            p.set (pid::satOn, 1.0f);
            p.set (pid::satMultiband, 1.0f);
            p.set (pid::satLowDrive, 3.0f);
            p.set (pid::satMidDrive, 6.0f);
            p.set (pid::satHighDrive, 9.0f);
            p.set (pid::macroWarmth, 100.0f);
            const auto cs = p.read (dsp::Routing::defaults());
            const auto& b = cs.sat.bands;
            expect (cs.sat.multiband);
            expectWithinAbsoluteError (b[1].driveDb - b[0].driveDb, 3.0f, 0.01f);
            expectWithinAbsoluteError (b[2].driveDb - b[1].driveDb, 3.0f, 0.01f);
            expectGreaterThan (b[0].driveDb, 3.5f, "the macro adds drive to every band");

            using dsp::linkedBandDrives;
            auto r = linkedBandDrives ({ 3.0f, 6.0f, 9.0f }, 1, 10.0f);
            expectEquals (r[0], 7.0f); expectEquals (r[1], 10.0f); expectEquals (r[2], 13.0f);
            r = linkedBandDrives ({ 3.0f, 6.0f, 20.0f }, 0, 10.0f);   // +7 would push High past 24: all move +4
            expectEquals (r[0], 7.0f); expectEquals (r[1], 10.0f); expectEquals (r[2], 24.0f);
            r = linkedBandDrives ({ 3.0f, 6.0f, 9.0f }, 2, 0.0f);     // -9 would push Low below 0: all move -3
            expectEquals (r[0], 0.0f); expectEquals (r[1], 3.0f); expectEquals (r[2], 6.0f);
        }
    }
};

static SaturationTests saturationTests;
