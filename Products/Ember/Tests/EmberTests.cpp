// EMBER De-Esser test runner.
//   EmberTests                          all checks (exit code 1 on a failure)
//   EmberTests --render <wav> <dir>     AUTO on the take, then render: <name>_ember.wav, <name>_delta.wav + report
//   EmberTests --snapshots <dir>        PNGs of SIMPLE and ADVANCED (both languages)
//   EmberTests --marketing <take.wav> <dir>   website pictures (2x) of both views, captured at the strongest S
//   EmberTests --rig <dir>                     the live rig (FIGUR_ANIMIEREN.md): rest pose + test poses as PNG, 600 px
//   EmberTests --mascot-loops <dir>            website loops of Ember (listening, dancing with/without S sparks, idle, lispy, sleeping), 2x
//   env EMBER_VOCALS=<dir or ;-list>    also runs AUTO on these real takes (report + sanity checks)
#include <juce_audio_formats/juce_audio_formats.h>
#include "EmberEngine.h"
#include "EmberAuto.h"
#include "EmberParams.h"
#include "EmberProcessor.h"
#include "EmberEditor.h"
#include "EmberMascot.h"
#include "EmberRig.h"
#include "EmberLiveMascot.h"
#include "EmberPresets.h"
#include "Text/I18n.h"

using namespace ember;

namespace
{
    int passed = 0, failed = 0;
    juce::String currentTest;

    void check (bool ok, const juce::String& what)
    {
        if (ok)
            ++passed;
        else
        {
            ++failed;
            std::printf ("  FAIL [%s] %s\n", currentTest.toRawUTF8(), what.toRawUTF8());
        }
    }

    void near (double a, double b, double tol, const juce::String& what)
    {
        check (std::abs (a - b) <= tol, what + " (got " + juce::String (a, 4) + ", want " + juce::String (b, 4) + " +- " + juce::String (tol, 4) + ")");
    }

    struct Test
    {
        Test (const char* name) { currentTest = name; std::printf ("%s\n", name); }
    };

    // ------------------------------------------------------------ signals
    struct Biq
    {
        vocalypse::dsp::Biquad b;
        float run (float x) { return b.process (x, 0); }
    };

    // A synthetic take: vowels (harmonics of 180 Hz, falling 9 dB/oct above 1 kHz), S bursts (noise 5-11 kHz),
    // pauses. Returns the sample ranges of the S bursts and vowels.
    struct Take
    {
        std::vector<float> x;
        std::vector<std::pair<int, int>> sRanges, vRanges;
    };

    Take makeTake (double fs, double seconds, float sLevelDb = -16.0f, float vowelPeakDb = -12.0f, bool withS = true, unsigned seed = 7)
    {
        Take t;
        const int n = (int) (seconds * fs);
        t.x.assign ((size_t) n, 0.0f);
        juce::Random rnd ((juce::int64) seed);
        Biq hp1, hp2, lp1, lp2;
        hp1.b.setCoeffs (vocalypse::dsp::design::highPass (fs, 5000.0, 0.54));
        hp2.b.setCoeffs (vocalypse::dsp::design::highPass (fs, 5000.0, 1.31));
        lp1.b.setCoeffs (vocalypse::dsp::design::lowPass (fs, 11000.0, 0.54));
        lp2.b.setCoeffs (vocalypse::dsp::design::lowPass (fs, 11000.0, 1.31));

        int i = 0, phrase = 0;
        double phase = 0.0;
        while (i < n)
        {
            const int vowelLen = (int) (fs * (0.25 + 0.1 * (phrase % 3)));
            const int sLen = (int) (fs * (0.08 + 0.04 * (phrase % 2)));
            const int pause = (int) (fs * 0.07);
            const float f0 = 170.0f + 25.0f * (float) (phrase % 4);
            const float vGain = vocalypse::dsp::dbToGain (vowelPeakDb - 3.0f * (float) (phrase % 3)) * 0.35f;

            const int v0 = i;
            for (int k = 0; k < vowelLen && i < n; ++k, ++i)
            {
                phase += f0 / fs;
                float s = 0.0f;
                for (int h = 1; h * f0 < 5000.0f; ++h)
                {
                    const float hz = h * f0;
                    const float amp = hz < 1000.0f ? 1.0f / h : (1.0f / h) * std::pow (1000.0f / hz, 1.5f);
                    s += amp * (float) std::sin (juce::MathConstants<double>::twoPi * phase * h);
                }
                const float env = std::min (1.0f, std::min (k, vowelLen - k) / (float) (fs * 0.02));
                t.x[(size_t) i] = s * vGain * env;
            }
            t.vRanges.push_back ({ v0 + (int) (fs * 0.03), i - (int) (fs * 0.03) });

            if (withS)
            {
                const int s0 = i;
                const float sGain = vocalypse::dsp::dbToGain (sLevelDb + 2.0f * (float) (phrase % 3)) * 2.2f;
                for (int k = 0; k < sLen && i < n; ++k, ++i)
                {
                    const float w = rnd.nextFloat() * 2.0f - 1.0f;
                    const float b = lp2.run (lp1.run (hp2.run (hp1.run (w))));
                    const float env = std::min (1.0f, std::min (k, sLen - k) / (float) (fs * 0.01));
                    t.x[(size_t) i] = b * sGain * env;
                }
                t.sRanges.push_back ({ s0 + (int) (fs * 0.012), i - (int) (fs * 0.012) });
            }
            i += pause;
            ++phrase;
        }
        return t;
    }

    // Runs the engine over a mono signal (as stereo, identical channels) in blocks
    std::vector<float> run (Engine& e, const std::vector<float>& in, int block = 512)
    {
        std::vector<float> l = in, r = in;
        for (int start = 0; start < (int) in.size(); start += block)
        {
            const int n = std::min (block, (int) in.size() - start);
            float* ch[2] { l.data() + start, r.data() + start };
            e.process (ch, 2, n);
        }
        return l;
    }

    double energyDb (const std::vector<float>& x, int from, int to, int offset = 0)
    {
        double s = 0.0;
        int c = 0;
        for (int i = from + offset; i < to + offset && i < (int) x.size(); ++i, ++c)
            s += (double) x[(size_t) i] * x[(size_t) i];
        return 10.0 * std::log10 (std::max (1.0e-20, s / std::max (1, c)));
    }

    double rangesDb (const std::vector<float>& x, const std::vector<std::pair<int, int>>& ranges, int offset)
    {
        double s = 0.0;
        long c = 0;
        for (auto [a, b] : ranges)
            for (int i = a + offset; i < b + offset && i < (int) x.size(); ++i, ++c)
                s += (double) x[(size_t) i] * x[(size_t) i];
        return 10.0 * std::log10 (std::max (1.0e-20, s / std::max (1L, c)));
    }

    float maxDiffDelayed (const std::vector<float>& out, const std::vector<float>& in, int delay, int from = 0)
    {
        float m = 0.0f;
        for (int i = std::max (from, delay); i < (int) out.size(); ++i)
            m = std::max (m, std::abs (out[(size_t) i] - in[(size_t) (i - delay)]));
        return m;
    }

    struct E : Engine
    {
        E (double fs, Settings s = {}) { setSettings (s); prepare (fs, 512); }
    };

    // ------------------------------------------------------------ tests
    void testLayout()
    {
        Test t ("layout + defaults");
        EmberAudioProcessor p;
        check (p.getParameters().size() == 13, "13 parameters");
        const auto s = readSettings (p.state);
        near (s.freq, voiceMidHz, 0.5, "default freq");
        near (s.thresholdDb, voiceMidThrDb, 0.01, "default threshold");
        near (s.rangeDb, 5.0, 0.01, "default range (gentle)");
        near (s.amount, 0.5, 1e-4, "default Amount 50 %");
        check (s.smart && s.relative && s.mode == 0 && s.listen == 0 && ! s.bypass, "default modes (Smart detection)");
        check (p.getBypassParameter() != nullptr, "host bypass parameter");
    }

    void testLatency()
    {
        Test t ("latency is 5 ms at every rate");
        for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        {
            E e (fs);
            check (e.getLatencySamples() == (int) std::lround (0.005 * fs), "latency at " + juce::String (fs));
        }
        EmberAudioProcessor p;
        p.prepareToPlay (48000.0, 256);
        check (p.getLatencySamples() == 240, "processor reports 240 samples at 48 kHz");
    }

    void testNull()
    {
        Test t ("no S: bit-exact apart from the delay");
        const double fs = 48000.0;
        std::vector<float> x ((size_t) fs);
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = 0.25f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 300.0 * i / fs)
                 + 0.1f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / fs);
        // 20 ms fade-in: a tone switched on hard from silence is a click, and clicks have S-band energy
        for (int i = 0; i < (int) (fs * 0.02); ++i)
            x[(size_t) i] *= (float) i / (float) (fs * 0.02);
        E e (fs);
        const auto y = run (e, x);
        check (maxDiffDelayed (y, x, e.getLatencySamples()) < 1.0e-6f, "split mode null");
        Settings w;
        w.mode = 1;
        E e2 (fs, w);
        check (maxDiffDelayed (run (e2, x), x, e2.getLatencySamples()) < 1.0e-6f, "wide mode null");
        Settings z;
        z.amount = 0.0f;
        z.thresholdDb = -60.0f;
        E e3 (fs, z);
        const auto take = makeTake (fs, 3.0);
        check (maxDiffDelayed (run (e3, take.x), take.x, e3.getLatencySamples()) < 1.0e-6f, "Amount 0 % = untouched, even with S");
        Settings m;
        m.mix = 0.0f;
        m.thresholdDb = -60.0f;
        E e4 (fs, m);
        check (maxDiffDelayed (run (e4, take.x), take.x, e4.getLatencySamples()) < 1.0e-6f, "mix 0 % = dry");
    }

    void testReduction()
    {
        Test t ("S sounds reduced, vowels untouched");
        const double fs = 44100.0;
        const auto take = makeTake (fs, 6.0);
        Settings s;
        s.thresholdDb = -36.0f;
        E e (fs, s);
        const auto y = run (e, take.x);
        const int L = e.getLatencySamples();
        const double sIn = rangesDb (take.x, take.sRanges, 0), sOut = rangesDb (y, take.sRanges, L);
        const double vIn = rangesDb (take.x, take.vRanges, 0), vOut = rangesDb (y, take.vRanges, L);
        std::printf ("    S %.2f -> %.2f dB, vowels %.2f -> %.2f dB\n", sIn, sOut, vIn, vOut);
        check (sIn - sOut > 3.0, "S at least 3 dB softer");
        check (std::abs (vIn - vOut) < 0.3, "vowels within 0.3 dB");

        // Range caps the reduction; Amount 100 % doubles it
        for (float amount : { 0.5f, 1.0f })
        {
            Settings r;
            r.thresholdDb = -60.0f;
            r.rangeDb = 4.0f;
            r.amount = amount;
            E er (fs, r);
            float maxGr = 0.0f;
            std::vector<float> l = take.x, rr = take.x;
            for (int start = 0; start < (int) l.size(); start += 256)
            {
                float* ch[2] { l.data() + start, rr.data() + start };
                er.process (ch, 2, std::min (256, (int) l.size() - start));
                maxGr = std::max (maxGr, er.gainReductionDb.pull());
            }
            near (maxGr, 4.0 * amountScale (amount), 0.05, "max reduction = range x amountScale (" + juce::String (amount) + ")");
        }
    }

    void testWide()
    {
        Test t ("wideband dips the whole voice on S only");
        const double fs = 48000.0;
        const auto take = makeTake (fs, 4.0);
        Settings s;
        s.mode = 1;
        s.thresholdDb = -36.0f;
        E e (fs, s);
        const auto y = run (e, take.x);
        const int L = e.getLatencySamples();
        check (rangesDb (take.x, take.sRanges, 0) - rangesDb (y, take.sRanges, L) > 3.0, "S softer");
        check (std::abs (rangesDb (take.x, take.vRanges, 0) - rangesDb (y, take.vRanges, L)) < 0.3, "vowels unchanged");
    }

    void testListen()
    {
        Test t ("listen: delta + processed = dry, S band hides the vowel");
        const double fs = 48000.0;
        const auto take = makeTake (fs, 3.0);
        Settings s;
        s.thresholdDb = -40.0f;
        E a (fs, s);
        s.listen = 2;
        E d (fs, s);
        const auto ya = run (a, take.x), yd = run (d, take.x);
        const int L = a.getLatencySamples();
        float m = 0.0f;
        for (int i = L; i < (int) ya.size(); ++i)
            m = std::max (m, std::abs (ya[(size_t) i] + yd[(size_t) i] - take.x[(size_t) (i - L)]));
        check (m < 1.0e-5f, "delta + out == dry (max err " + juce::String (m, 7) + ")");

        std::vector<float> sine ((size_t) fs);
        for (size_t i = 0; i < sine.size(); ++i)
            sine[i] = 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 300.0 * i / fs);
        Settings b;
        b.listen = 1;
        E eb (fs, b);
        const auto yb = run (eb, sine);
        check (energyDb (yb, (int) (fs * 0.5), (int) fs) < energyDb (sine, (int) (fs * 0.5), (int) fs) - 60.0, "300 Hz is > 60 dB down in the S band");
    }

    void testBypass()
    {
        Test t ("bypass = delayed dry, no click");
        const double fs = 48000.0;
        const auto take = makeTake (fs, 2.0);
        Settings s;
        s.thresholdDb = -60.0f;
        s.bypass = true;
        E e (fs, s);
        const auto y = run (e, take.x);
        check (maxDiffDelayed (y, take.x, e.getLatencySamples()) < 1.0e-6f, "bypassed output == input");
    }

    void testLookahead()
    {
        Test t ("lookahead catches the S onset");
        const double fs = 48000.0;
        // pause, then a sudden loud S
        auto take = makeTake (fs, 1.0, -6.0f, -40.0f, true);
        std::vector<float> x ((size_t) fs, 0.0f);
        juce::Random rnd (3);
        Biq hp;
        hp.b.setCoeffs (vocalypse::dsp::design::highPass (fs, 7000.0, 0.7));
        const int onset = (int) (fs * 0.5);
        for (int i = onset; i < (int) x.size(); ++i)
            x[(size_t) i] = 0.5f * hp.run (rnd.nextFloat() * 2.0f - 1.0f);
        const auto onsetPeak = [&] (float la)
        {
            Settings s;
            s.lookaheadMs = la;
            s.rangeDb = 12.0f;
            s.thresholdDb = -40.0f;
            s.relative = false;
            s.mode = 1;
            E e (fs, s);
            const auto y = run (e, x);
            const int L = e.getLatencySamples();
            float pk = 0.0f;
            for (int i = onset + L; i < onset + L + (int) (fs * 0.001); ++i)
                pk = std::max (pk, std::abs (y[(size_t) i]));
            return pk;
        };
        const float noLa = onsetPeak (0.0f), la = onsetPeak (2.0f);
        std::printf ("    first ms peak: lookahead 0 = %.3f, 2 ms = %.3f\n", noLa, la);
        check (la < noLa * 0.7f, "2 ms lookahead lowers the onset peak");
        juce::ignoreUnused (take);
    }

    void testBlockSizes()
    {
        Test t ("same result for every block size");
        const double fs = 44100.0;
        const auto take = makeTake (fs, 2.0);
        Settings s;
        s.thresholdDb = -38.0f;
        E ref (fs, s);
        const auto yr = run (ref, take.x, 512);
        for (int block : { 1, 7, 16, 37, 128, 4096 })
        {
            E e (fs, s);
            const auto y = run (e, take.x, block);
            float m = 0.0f;
            for (size_t i = 0; i < y.size(); ++i)
                m = std::max (m, std::abs (y[i] - yr[i]));
            check (m < 0.003f, "block " + juce::String (block) + " max diff " + juce::String (m, 6));
        }
    }

    void testCpu()
    {
        Test t ("CPU: 60 s of stereo audio at 48 kHz");
        const double fs = 48000.0;
        const auto take = makeTake (fs, 10.0);
        Settings s;
        s.thresholdDb = -40.0f;
        E e (fs, s);
        std::vector<float> l (512), r (512);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int rep = 0; rep < 6; ++rep)
            for (int start = 0; start + 512 <= (int) take.x.size(); start += 512)
            {
                std::copy_n (take.x.data() + start, 512, l.data());
                std::copy_n (take.x.data() + start, 512, r.data());
                float* ch[2] { l.data(), r.data() };
                e.process (ch, 2, 512);
            }
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::printf ("    %.0f ms for 60 s = %.2f %% of one core\n", ms, ms / 600.0);
        check (ms < 1200.0, "below 2 % of one core");
    }

    void testRobust()
    {
        Test t ("NaN / Inf input recovers, silence stays silent");
        const double fs = 48000.0;
        E e (fs);
        std::vector<float> x (4800, 0.1f);
        x[100] = std::numeric_limits<float>::quiet_NaN();
        x[200] = std::numeric_limits<float>::infinity();
        auto y = run (e, x, 256);
        auto take = makeTake (fs, 1.0);
        y = run (e, take.x, 256);
        bool finite = true;
        for (float v : y)
            finite = finite && std::isfinite (v);
        check (finite, "output finite after bad input");
        E s (fs);
        std::vector<float> silence (48000, 0.0f);
        const auto ys = run (s, silence);
        check (*std::max_element (ys.begin(), ys.end()) == 0.0f, "silence in = silence out");
    }

    void testAuto()
    {
        Test t ("AUTO finds the S range and a fitting threshold");
        for (double fs : { 44100.0, 48000.0, 96000.0 })
        {
            const auto take = makeTake (fs, 12.0);
            const auto r = analyse (take.x.data(), (int) take.x.size(), fs);
            std::printf ("    %.0f Hz: status %d, freq %.0f (peak %.0f), thr %.1f, range %.1f, S med %.1f / 90 %.1f, vowel95 %.1f, exp %.1f, frames S %d V %d\n",
                         fs, (int) r.status, r.freq, r.peakHz, r.thresholdDb, r.rangeDb, r.sibilantMedianDb, r.sibilant90Db, r.vowel95Db,
                         r.expectedReductionDb, r.sibilantFrames, r.vowelFrames);
            std::printf ("    vowels lose %.2f dB\n", r.vowelReductionDb);
            check (r.status == AutoResult::Status::ok, "ok");
            check (r.freq >= 4000.0f && r.freq <= 8000.0f, "corner in 4-8 kHz");
            check (r.thresholdDb > r.vowel95Db, "threshold above the vowels");
            std::printf ("    S vs vowels %.1f dB -> target %.1f dB, mixed frames lose %.2f dB\n", r.sibilanceVsVowelDb, r.targetDb, r.mixedReductionDb);
            check (r.targetDb >= autoMinTargetDb && r.targetDb <= autoMaxTargetDb, "target within the AUTO window");
            check (r.expectedReductionDb > 0.3f && r.expectedReductionDb <= r.targetDb + 0.6f, "S lose at most the target (rendered)");
            check (r.mixedReductionDb <= autoMixedMaxDb + 0.05f, "vowel side of each S kept");
            check (r.vowelReductionDb <= autoVowelMaxDb + 0.02f, "vowels <= 0.1 dB");
            check (r.rangeDb <= autoMaxRangeDb, "range <= autoMaxRangeDb");

            Settings s;
            s.freq = r.freq;
            s.thresholdDb = r.thresholdDb;
            s.rangeDb = r.rangeDb;
            s.releaseMs = autoReleaseMs;
            E e (fs, s);
            const auto y = run (e, take.x);
            const int L = e.getLatencySamples();
            const double sd = rangesDb (take.x, take.sRanges, 0) - rangesDb (y, take.sRanges, L);
            const double vd = rangesDb (take.x, take.vRanges, 0) - rangesDb (y, take.vRanges, L);
            std::printf ("    applied: S -%.2f dB, vowels -%.2f dB\n", sd, vd);
            check (sd > 0.3 && sd < 4.5, "S reduced, subtly (0.3-4.5 dB)");
            check (std::abs (vd) < 0.5, "vowels within 0.5 dB");
        }

        // Quiet take: the same result (relative detection)
        {
            auto loud = makeTake (48000.0, 10.0);
            auto quiet = loud;
            for (auto& v : quiet.x)
                v *= 0.1f;
            const auto a = analyse (loud.x.data(), (int) loud.x.size(), 48000.0);
            const auto b = analyse (quiet.x.data(), (int) quiet.x.size(), 48000.0);
            near (a.freq, b.freq, 1.0, "quiet take: same corner");
            near (a.thresholdDb, b.thresholdDb, 1.0, "quiet take: same threshold (relative)");
        }

        const auto vowelsOnly = makeTake (48000.0, 8.0, -16.0f, -12.0f, false);
        check (analyse (vowelsOnly.x.data(), (int) vowelsOnly.x.size(), 48000.0).status == AutoResult::Status::noSibilance, "no S -> noSibilance");
        const auto shortTake = makeTake (48000.0, 2.0);
        check (analyse (shortTake.x.data(), (int) shortTake.x.size(), 48000.0).status == AutoResult::Status::tooShort, "2 s -> tooShort");
    }

    void testCapture()
    {
        Test t ("AUTO capture skips pauses, stops when full");
        AutoCapture c;
        c.prepare (8000.0);
        c.start();
        std::vector<float> loud (800, 0.3f), quiet (800, 0.0001f);
        const float* chL[1] { loud.data() };
        const float* chQ[1] { quiet.data() };
        c.push (chL, 1, 800);
        c.push (chQ, 1, 800);
        check (c.getNumSamples() == 800, "pause not captured");
        for (int i = 0; i < 400; ++i)
            c.push (chL, 1, 800);
        check (c.isFull() && ! c.isCapturing(), "stops at 30 s");
    }

    void testPolish()
    {
        Test t ("polish 2026-11-15: NaN settings, AUTO buffer on demand, mode switch");
        const double fs = 48000.0;
        const auto take = makeTake (fs, 1.0, -6.0f);
        // non-finite settings fall back to defaults: the audio keeps flowing
        {
            Engine e;
            Settings s;
            s.freq = std::numeric_limits<float>::quiet_NaN();
            s.thresholdDb = std::numeric_limits<float>::infinity();
            s.mix = std::numeric_limits<float>::quiet_NaN();
            e.setSettings (s);
            e.prepare (fs, 256);
            std::vector<float> y (take.x);
            for (int start = 0; start + 256 <= (int) y.size(); start += 256)
            {
                float* ch[1] { y.data() + start };
                e.process (ch, 1, 256);
            }
            float peak = 0.0f;
            bool finite = true;
            for (float v : y) { finite = finite && std::isfinite (v); peak = std::max (peak, std::abs (v)); }
            check (finite && peak > 0.1f, "NaN settings: finite, not muted");
        }
        // the 30 s buffer exists only once AUTO is used
        {
            AutoCapture c;
            c.prepare (fs);
            check (! c.isFull() && c.getNumSamples() == 0, "prepared, no buffer yet");
            c.start();
            check (c.isCapturing(), "start makes the buffer");
            std::vector<float> nanBlock (256, std::numeric_limits<float>::quiet_NaN());
            const float* chN[1] { nanBlock.data() };
            c.push (chN, 1, 256);
            check (c.getNumSamples() == 0, "a NaN block is not captured");
            const float* ch[1] { take.x.data() };
            c.push (ch, 1, 4096);
            check (c.getNumSamples() == 4096, "audio captured");
        }
        // split <-> wideband while S are reduced: no jump bigger than the signal itself makes
        {
            Engine e;
            Settings s;
            s.thresholdDb = -60.0f;
            e.setSettings (s);
            e.prepare (fs, 64);
            std::vector<float> y (take.x);
            for (int start = 0, blk = 0; start + 64 <= (int) y.size(); start += 64, ++blk)
            {
                if (blk % 40 == 0)
                {
                    s.mode = 1 - s.mode;
                    e.setSettings (s);
                }
                float* ch[1] { y.data() + start };
                e.process (ch, 1, 64);
            }
            bool finite = true;
            for (float v : y) finite = finite && std::isfinite (v);
            check (finite, "mode switching stays finite");
        }
    }

    void testLicence()
    {
        Test t ("licence: off by default, locked = unprocessed, key activates, own file");
        namespace lic = vocalypse::licensing;
        {
            EmberAudioProcessor p;
            check (p.getLicence().isOn() == (EMBER_LICENSING != 0), "build switch decides");
            check (p.getLicence().productName() == "EMBER" && p.getLicence().buyUrl().isNotEmpty(), "EMBER's name and shop");
            // Polar 2026-11-16: German / English checkout by UI language
            const auto before = vocalypse::i18n::current();
            vocalypse::i18n::setCurrent (vocalypse::i18n::Language::german);
            const auto de = p.getLicence().buyUrl();
            vocalypse::i18n::setCurrent (vocalypse::i18n::Language::english);
            const auto en = p.getLicence().buyUrl();
            vocalypse::i18n::setCurrent (before);
            check (de == buyUrlGerman && en == buyUrlEnglish, "checkout follows the UI language (" + de + " / " + en + ")");
        }

        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ember-licence-test");
        dir.deleteRecursively();
        const auto file = dir.getChildFile ("EMBER.license");
        const juce::int64 now = 1'800'000'000'000LL, day = 24LL * 3600 * 1000;

        // a VOCALYPSE licence file is no EMBER licence
        {
            lic::Record r;
            r.key = "K"; r.instanceId = "I"; r.firstRunMs = now;
            lic::writeRecord (file, r);
            check (lic::readRecord (file, licenceConfig().identity).key.isEmpty(), "VOCALYPSE file refused");
        }

        // trial started 20 days ago -> locked
        lic::Record old;
        old.firstRunMs = now - 20 * day;
        lic::writeRecord (file, old, licenceConfig().identity);
        juce::String lastUrl;
        auto cfg = licenceConfig();
        cfg.on = true;
        cfg.file = file;
        cfg.asynchronous = false;
        cfg.now = [now] { return now; };
        // a Polar activation of a key of Jakob's organisation (benefit as configured, or any if none is set)
        const auto polarActivated = [] (const char* activation, const char* org = polarOrganizationId)
        {
            return juce::String (R"({"id":")") + activation + R"(","label":"PC","license_key":{"status":"granted",)"
                   + R"("organization_id":")" + org + R"(","benefit_id":")" + juce::String (polarBenefitId)
                   + R"(","customer":{"name":"Jakob"}}})";
        };
        cfg.transport = [&lastUrl, polarActivated] (const juce::String& url, const juce::var&) -> std::pair<int, juce::String>
        {
            lastUrl = url;
            return { 200, polarActivated ("inst-1") };
        };
        lic::Manager m (cfg);
        check (m.status().access == lic::Access::locked && m.isLocked(), "trial over -> locked");

        // Jakob 2026-11-15: no trial - a first start without a key is locked at once, the shop link gets a free key
        {
            const auto fresh = dir.getChildFile ("fresh.license");
            auto c = cfg;
            c.file = fresh;
            lic::Manager first (c);
            check (licenceConfig().trialDays == 0 && licenceConfig().freeKey, "EMBER: no trial, free key");
            check (first.status().access == lic::Access::locked && first.isLocked(), "first start without key -> locked");
        }

        const double fs = 48000.0;
        const auto take = makeTake (fs, 1.0, -6.0f);   // loud S: processed audio clearly differs
        const auto runProcessor = [&] (EmberAudioProcessor& p)
        {
            juce::AudioBuffer<float> buf (2, 256);
            juce::MidiBuffer midi;
            std::vector<float> out;
            for (int start = 0; start + 256 <= (int) take.x.size(); start += 256)
            {
                for (int ch = 0; ch < 2; ++ch)
                    buf.copyFrom (ch, 0, take.x.data() + start, 256);
                p.processBlock (buf, midi);
                out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + 256);
            }
            return out;
        };
        {
            EmberAudioProcessor p;
            p.setLicenceForTest (m);
            p.prepareToPlay (fs, 256);
            if (auto* thr = p.state.getParameter (pid::threshold))
                thr->setValueNotifyingHost (thr->convertTo0to1 (-60.0f));
            const auto y = runProcessor (p);
            check (maxDiffDelayed (y, take.x, p.getLatencySamples(), (int) (fs * 0.1)) < 1.0e-6f, "locked: output == delayed input");
        }

        bool ok = false;
        m.activate ("38B1460A-5104-4067-A91D-77B872934D51", [&ok] (bool r, const juce::String&) { ok = r; });
        check (ok && lastUrl.endsWith ("/activate"), "key activates");
        check (m.status().access == lic::Access::licensed && ! m.isLocked(), "licensed -> unlocked");
        check (lic::readRecord (file, licenceConfig().identity).instanceId == "inst-1", "activation stored in EMBER's file");

        // Polish 2026-11-15: pasted keys, the weekly check, releasing, a key of another product
        check (lic::cleanKey (juce::String::fromUTF8 (" 38B1460A-5104\xc2\xa0\r\n-4067 ")) == "38B1460A-5104-4067", "pasted key cleaned");
        {
            const auto checkWith = [&] (int code, const char* body)
            {
                auto c = cfg;
                c.now = [now, day] { return now + 8 * day; };   // the weekly check is due
                c.transport = [code, body] (const juce::String&, const juce::var&) -> std::pair<int, juce::String> { return { code, body }; };
                lic::Manager later (c);   // checks in its constructor (synchronous here)
                return later.status().access;
            };
            check (checkWith (429, R"({"error":"Too many requests"})") == lic::Access::licensed, "429 on the weekly check: stays activated");
            check (checkWith (503, "") == lic::Access::licensed, "server down: stays activated");
            check (checkWith (404, R"({"error":"ResourceNotFound","detail":"Not found"})") == lic::Access::locked, "revoked key (404): locked");
            check (checkWith (200, (juce::String (R"({"status":"revoked","organization_id":")") + polarOrganizationId + R"("})").toRawUTF8())
                       == lic::Access::locked, "clearly invalid: locked");
        }
        {
            // a key of another organisation: refused, and the store gets its activation back
            juce::StringArray calls;
            auto c = cfg;
            c.file = dir.getChildFile ("foreign.license");
            c.transport = [&calls, polarActivated] (const juce::String& url, const juce::var& json) -> std::pair<int, juce::String>
            {
                calls.add (url.fromLastOccurrenceOf ("/", false, false) + (json.hasProperty ("activation_id") ? ":" + json["activation_id"].toString() : ""));
                return url.endsWith ("deactivate") ? std::pair<int, juce::String> { 204, {} }
                                                   : std::pair<int, juce::String> { 200, polarActivated ("x", "00000000-0000-4000-8000-000000000000") };
            };
            lic::Manager other (c);
            juce::String msg;
            other.activate ("K-1", [&msg] (bool, const juce::String& m) { msg = m; });
            check (other.status().access == lic::Access::locked, "foreign key refused");
            check (calls.joinIntoString (",") == "activate,deactivate:x", "its activation is given back (got " + calls.joinIntoString (",") + ")");
            check (msg.contains ("EMBER"), "own message for a foreign key");
        }
        {
            EmberAudioProcessor p;
            p.setLicenceForTest (m);
            p.prepareToPlay (fs, 256);
            if (auto* thr = p.state.getParameter (pid::threshold))
                thr->setValueNotifyingHost (thr->convertTo0to1 (-60.0f));
            const auto y = runProcessor (p);
            check (maxDiffDelayed (y, take.x, p.getLatencySamples(), (int) (fs * 0.1)) > 1.0e-3f, "licensed: processed again");
        }

        // Polar answers (2026-11-16): benefit check, 403 limit, 204 release, 422 malformed
        {
            const auto act = R"({"id":"a1","license_key":{"status":"granted","organization_id":"o","benefit_id":"b1","customer":{"name":"J"}}})";
            check (lic::parseReply (act, 200, lic::Call::activate, "o", "b1").ok, "own benefit accepted");
            check (lic::parseReply (act, 200, lic::Call::activate, "o", {}).instanceId == "a1", "activation id read");
            check (! lic::parseReply (act, 200, lic::Call::activate, "o", "b2").ok, "key of another benefit refused");
            const auto limit = lic::parseReply (R"({"error":"NotPermitted","detail":"License key activation limit reached"})", 403, lic::Call::activate, "o", {});
            check (limit.reached && ! limit.ok && limit.detail.contains ("limit"), "403 limit read");
            check (lic::parseReply ({}, 204, lic::Call::deactivate, {}, {}).ok, "204 released");
            const auto val = lic::parseReply (R"({"status":"granted","organization_id":"o","benefit_id":"b1","activation":{"id":"a1"}})", 200, lic::Call::validate, "o", "b1");
            check (val.ok && val.instanceId == "a1", "validate granted");
            check (! lic::parseReply (R"({"detail":[{"msg":"bad"}]})", 422, lic::Call::validate, "o", {}).ok, "422 not ok");
            check (lic::cleanKey ("EMBER_38B1460A-5104 ") == "EMBER_38B1460A-5104", "prefixed key kept");
        }
        dir.deleteRecursively();

        // Optional: the real Polar API with a key of Jakob's organisation (never stored in the repo):
        //   set EMBER_LICENCE_TEST_KEY=<key>  ->  activate, weekly re-check, release
        const auto realKey = juce::SystemStats::getEnvironmentVariable ("EMBER_LICENCE_TEST_KEY", {});
        if (realKey.isNotEmpty())
        {
            auto live = licenceConfig();
            live.on = true;
            live.file = dir.getChildFile ("EMBER.license");
            live.asynchronous = false;
            juce::int64 clock = juce::Time::currentTimeMillis();
            live.now = [&clock] { return clock; };
            lic::Manager lm (live);
            bool done = false; juce::String message;
            lm.activate (realKey, [&] (bool r, const juce::String& m) { done = r; message = m; });
            std::printf ("    Polar activate: %s (%s)\n", done ? "ok" : "FAILED", message.toRawUTF8());
            check (done && lm.status().access == lic::Access::licensed, "real key activates (organisation + benefit match)");
            clock += 8 * day;                               // a week later: re-check online
            lm.checkIfDue();
            check (lm.status().access == lic::Access::licensed && ! lm.record().revoked, "weekly re-check: still valid");
            bool released = false;
            lm.deactivate ([&] (bool r, const juce::String& m) { released = r; message = m; });
            std::printf ("    Polar deactivate: %s (%s)\n", released ? "ok" : "FAILED", message.toRawUTF8());
            check (released, "release frees the activation again");
            dir.deleteRecursively();
        }
    }

    void testProcessor()
    {
        Test t ("processor: state, AUTO applies, layouts");
        EmberAudioProcessor p;
        p.prepareToPlay (48000.0, 512);
        if (auto* thr = p.state.getParameter (pid::threshold))
            thr->setValueNotifyingHost (thr->convertTo0to1 (-22.5f));
        if (auto* m = p.state.getParameter (pid::mode))
            m->setValueNotifyingHost (1.0f);
        juce::MemoryBlock mb;
        p.getStateInformation (mb);
        EmberAudioProcessor q;
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        near (readSettings (q.state).thresholdDb, -22.5, 0.05, "threshold restored");
        check (readSettings (q.state).mode == 1, "mode restored");

        // AUTO through the processor: feed a take block by block
        const auto take = makeTake (48000.0, 13.0);
        p.startAuto();
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        for (int start = 0; start + 512 <= (int) take.x.size(); start += 512)
        {
            for (int ch = 0; ch < 2; ++ch)
                buf.copyFrom (ch, 0, take.x.data() + start, 512);
            p.processBlock (buf, midi);
        }
        check (p.getAutoPhase() == EmberAudioProcessor::AutoPhase::listening, "still listening (no timer in tests)");
        p.finishAuto();
        check (p.getAutoPhase() == EmberAudioProcessor::AutoPhase::analysing, "analysing in the background");
        p.waitForAuto();
        check (p.getAutoPhase() == EmberAudioProcessor::AutoPhase::done, "done");
        const auto s = readSettings (p.state);
        near (s.freq, p.getLastAuto().freq, 15.0, "freq applied");
        near (s.thresholdDb, p.getLastAuto().thresholdDb, 0.1, "threshold applied");
        check (s.mode == 1 && s.smart && std::abs (s.amount - 0.5f) < 1e-3f, "mode kept (wideband), smart, Amount 50 %");
        check (p.getAutoMessage().isNotEmpty(), "message");

        juce::AudioProcessor::BusesLayout mono, stereo, mixed;
        mono.inputBuses.add (juce::AudioChannelSet::mono()); mono.outputBuses.add (juce::AudioChannelSet::mono());
        stereo.inputBuses.add (juce::AudioChannelSet::stereo()); stereo.outputBuses.add (juce::AudioChannelSet::stereo());
        mixed.inputBuses.add (juce::AudioChannelSet::mono()); mixed.outputBuses.add (juce::AudioChannelSet::stereo());
        check (p.checkBusesLayoutSupported (mono) && p.checkBusesLayoutSupported (stereo), "mono + stereo");
        check (! p.checkBusesLayoutSupported (mixed), "mono -> stereo refused");
    }

    void testMascot()
    {
        Test t ("mascot: four moods, an accent per S (Jakob 2026-11-15)");
        MascotBrain b;
        const double dt = 1.0 / 30.0;
        const auto runFor = [&] (MascotInput in, double seconds) { for (double s = 0; s < seconds; s += dt) b.update (in, dt); };
        MascotInput in;
        runFor (in, 1.0);
        check (b.getState() == MascotState::idle, "idle without signal");
        in.hasSignal = true;
        runFor (in, 2.5);
        check (b.getState() == MascotState::dancing, "dancing");

        // S: he keeps dancing, every S is one accent
        {
            const int before = b.getAccentCount();
            int changes = 0;
            auto last = b.getState();
            for (int i = 0; i < 600; ++i)   // 20 s, a 0.17 s S every 1.5 s
            {
                in.reductionDb = (i % 45) < 5 ? 3.0f : 0.0f;
                b.update (in, dt);
                if (b.getState() != last) { ++changes; last = b.getState(); }
            }
            check (changes == 0 && b.getState() == MascotState::dancing, "S do not change the mood");
            check (b.getAccentCount() - before == 14, "one accent per S (got " + juce::String (b.getAccentCount() - before) + ")");
        }
        // a long S is one accent, it decays; a louder S is a stronger accent
        {
            MascotBrain c;
            MascotInput s;
            s.hasSignal = true;
            for (double x = 0; x < 2.5; x += dt) c.update (s, dt);
            s.reductionDb = 2.0f;
            c.update (s, dt);
            check (c.getAccent() > 0.9f && c.getAccentCount() == 1, "accent at the S");
            const float soft = c.getAccentStrength();
            for (double x = 0; x < 1.0; x += dt) c.update (s, dt);
            check (c.getAccentCount() == 1 && c.getAccent() == 0.0f, "a held S is one accent, then it fades");
            s.reductionDb = 0.0f;
            c.update (s, dt);
            s.reductionDb = 8.0f;
            for (int i = 0; i < 6; ++i) c.update (s, dt);
            check (c.getAccentCount() == 2 && c.getAccentStrength() > soft + 0.3f, "a louder S: a stronger accent");
            // flicker within minAccentGapS is not a new S
            s.reductionDb = 0.0f; c.update (s, dt);
            s.reductionDb = 3.0f; c.update (s, dt);
            const int n = c.getAccentCount();
            s.reductionDb = 0.0f; c.update (s, dt);
            s.reductionDb = 3.0f; c.update (s, dt);
            check (c.getAccentCount() == n, "no accent within 0.15 s of the last");
        }

        in.reductionDb = 11.0f;
        runFor (in, 1.0);
        check (b.isHot() && b.getState() == MascotState::dancing, "too much: hot (bubble), still dancing");
        in.reductionDb = 0.0f;
        runFor (in, 0.3);
        check (b.isHot(), "stays hot a moment");
        runFor (in, 1.0);
        check (! b.isHot(), "then cools down");

        // Calm: short gaps in the vocal keep him dancing
        in.hasSignal = false;
        runFor (in, 0.2);
        in.hasSignal = true;
        runFor (in, 0.2);
        check (b.getState() == MascotState::dancing, "a breath does not stop the dance");
        in.hasSignal = false;
        runFor (in, 3.0);
        check (b.getState() == MascotState::idle, "silence: idle");
        in.hasSignal = true;
        runFor (in, 2.5);
        in.hostStopped = true;   // pause in the host (a reverb tail may still sound)
        runFor (in, 0.1);
        check (b.getState() == MascotState::idle, "host stopped: idle at once (Jakob: danced on in pause)");
        in.hostStopped = false;
        in.hasSignal = false;

        in.autoListening = true;
        runFor (in, 0.1);
        check (b.getState() == MascotState::listening, "listening");
        in.autoListening = false;
        in.bypass = true;
        runFor (in, 0.1);
        check (b.getState() == MascotState::sleeping, "sleeping on bypass");
        for (int s = 0; s < numMascotStates; ++s)
            check (juce::String (mascotStateName ((MascotState) s)).isNotEmpty(), "state name");
    }

    void testMascotClips()
    {
        Test t ("mascot clips: out of the old mood, into the new one, then its loop; sparks at an S");
        MascotView view;
        view.setSize (500, 400);
        auto& f = view.getFramesForTest();
        f.clear();
        juce::Image img (juce::Image::ARGB, 8, 8, true);
        const auto addN = [&] (MascotState s, MascotClip c, int n) { for (int i = 0; i < n; ++i) f.add (s, c, img); };
        addN (MascotState::idle, MascotClip::loop, 24);
        addN (MascotState::idle, MascotClip::out, 12);       // 0.5 s at 24 fps
        addN (MascotState::dancing, MascotClip::in, 12);
        addN (MascotState::dancing, MascotClip::loop, 24);
        const double dt = 1.0 / 30.0;
        MascotInput in;
        view.setInput (in);
        for (int i = 0; i < 60; ++i) view.stepForTest (dt);
        check (view.getShownState() == MascotState::idle && view.getShownClip() == MascotClip::loop, "idle loop");
        in.hasSignal = true;
        view.setInput (in);
        bool sawOut = false, sawIn = false;
        for (int i = 0; i < 180; ++i)
        {
            view.stepForTest (dt);
            sawOut = sawOut || (view.getShownState() == MascotState::idle && view.getShownClip() == MascotClip::out);
            sawIn = sawIn || (view.getShownState() == MascotState::dancing && view.getShownClip() == MascotClip::in);
        }
        check (sawOut, "played idle_out");
        check (sawIn, "played dancing_in");
        check (view.getShownState() == MascotState::dancing && view.getShownClip() == MascotClip::loop, "ends in the dancing loop");
        view.sparkForTest();
        check (view.getSparkCountForTest() >= 5, "an S throws sparks and the S");
    }

    void testMascotFrames (const juce::File& snapshotDir)
    {
        Test t ("mascot pictures built in (Jakob's Ember, 2026-11-10)");
        const int decodedBefore = MascotFrames::decodedPicturesForTest();
        MascotFrames frames;
        check (MascotFrames::decodedPicturesForTest() == decodedBefore, "opening decodes no picture");
        for (int s = 0; s < numMascotStates; ++s)
        {
            const auto st = (MascotState) s;
            check (frames.has (st), juce::String ("picture for ") + mascotStateName (st));
            if (frames.has (st))
                check (frames.frame (st, MascotClip::loop, 0).getWidth() >= 512 && frames.frame (st, MascotClip::loop, 0).hasAlphaChannel(),
                       juce::String ("size + alpha ") + mascotStateName (st));
        }

        // Every picture decodes; only a few stay decoded, shared by all windows (was ~281 MB per window)
        {
            int total = 0, valid = 0;
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            for (int s = 0; s < numMascotStates; ++s)
                for (int c = 0; c < 3; ++c)
                    for (int i = 0; i < frames.count ((MascotState) s, (MascotClip) c); ++i)
                    {
                        ++total;
                        valid += frames.frame ((MascotState) s, (MascotClip) c, i).isValid() ? 1 : 0;
                    }
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            check (total > 0 && valid == total, "every picture decodes");
            check (MascotFrames::decodedPicturesForTest() <= MascotFrames::cacheCapacity, "at most cacheCapacity pictures decoded");
            std::printf ("    %d pictures, %.2f ms per decode, %d kept decoded\n", total, ms / juce::jmax (1, total),
                         MascotFrames::decodedPicturesForTest());

            MascotFrames second;
            const auto a = frames.frame (MascotState::idle, MascotClip::loop, 0);
            const auto b = second.frame (MascotState::idle, MascotClip::loop, 0);
            check (a.getPixelData() == b.getPixelData(), "two windows share one decoded picture");
            check (MascotFrames::decodedPicturesForTest() <= MascotFrames::cacheCapacity, "second window adds nothing");
        }
        if (snapshotDir == juce::File())
            return;

        // One picture per mood, as the plugin shows it (500 x 400)
        const std::pair<const char*, MascotInput> moods[] {
            { "idle", {} },
            { "dancing", { false, true, false, 0.0f } },
            { "dancing_s", { false, true, false, 6.0f } },
            { "lispy", { false, true, false, 12.0f } },
            { "sleeping", { true, false, false, 0.0f } },
            { "listening", { false, true, true, 0.0f } },
        };
        snapshotDir.createDirectory();
        for (const auto& [name, in] : moods)
        {
            MascotView view;
            view.setSize (500, 400);
            view.setInput (in);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (3000);
            juce::Image img (juce::Image::ARGB, 500, 400, true);
            {
                juce::Graphics g (img);
                g.fillAll (juce::Colour (0xff05060a));
                view.paintEntireComponent (g, true);
            }
            juce::PNGImageFormat png;
            juce::FileOutputStream out (snapshotDir.getChildFile (juce::String ("mascot_") + name + ".png"));
            if (out.openedOk()) { out.setPosition (0); out.truncate(); png.writeImageToStream (img, out); }

            // the live S sparks (drawn by the view, not in the pictures): 4 s at 24 pictures/s, an S every 0.75 s
            if (juce::String (name) == "dancing_s")
                for (int k = 1; k <= 96; ++k)
                {
                    auto pulse = in;
                    pulse.reductionDb = (k % 18) < 4 ? 2.0f + (float) (k % 36) / 6.0f : 0.0f;
                    view.setInput (pulse);
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (42);
                    juce::Image seq (juce::Image::ARGB, 500, 400, true);
                    {
                        juce::Graphics g (seq);
                        g.fillAll (juce::Colour (0xff05060a));
                        view.paintEntireComponent (g, true);
                    }
                    juce::FileOutputStream o2 (snapshotDir.getChildFile ("mascot_dance_" + juce::String (k).paddedLeft ('0', 3) + ".png"));
                    if (o2.openedOk()) { o2.setPosition (0); o2.truncate(); png.writeImageToStream (seq, o2); }
                }
        }
    }

    juce::Image snapshot (EmberEditor& ed)
    {
        for (int i = 0; i < 40; ++i)
        {
            ed.tickForTest();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
        }
        return ed.createComponentSnapshot (ed.getLocalBounds());
    }

    void testSmart()
    {
        Test t ("Smart detection: S reduced, bright vowels keep their air");
        const double fs = 48000.0;
        auto take = makeTake (fs, 6.0);
        // add "air" to the vowels: soft noise above 7 kHz (a bright, breathy voice)
        juce::Random rnd (11);
        Biq a1, a2;
        a1.b.setCoeffs (vocalypse::dsp::design::highPass (fs, 7000.0, 0.54));
        a2.b.setCoeffs (vocalypse::dsp::design::highPass (fs, 7000.0, 1.31));
        for (auto [s0, s1] : take.vRanges)
            for (int i = s0; i < s1; ++i)
                take.x[(size_t) i] += 0.012f * a2.run (a1.run (rnd.nextFloat() * 2.0f - 1.0f));
        const auto airDb = [&] (const std::vector<float>& x, int offset)
        {
            Biq h1, h2;
            h1.b.setCoeffs (vocalypse::dsp::design::highPass (fs, 7000.0, 0.54));
            h2.b.setCoeffs (vocalypse::dsp::design::highPass (fs, 7000.0, 1.31));
            std::vector<float> hp (x.size());
            for (size_t i = 0; i < x.size(); ++i)
                hp[i] = h2.run (h1.run (x[i]));
            return rangesDb (hp, take.vRanges, offset);
        };
        double sLoss[2], airLoss[2];
        for (int smart = 0; smart < 2; ++smart)
        {
            Settings s;
            s.thresholdDb = -50.0f;   // a heavy hand-set setting
            s.rangeDb = 10.0f;
            s.freq = 5000.0f;
            s.smart = smart == 1;
            E e (fs, s);
            const auto y = run (e, take.x);
            const int L = e.getLatencySamples();
            sLoss[smart] = rangesDb (take.x, take.sRanges, 0) - rangesDb (y, take.sRanges, L);
            airLoss[smart] = airDb (take.x, 0) - airDb (y, L);
        }
        std::printf ("    plain: S -%.2f dB, vowel air -%.2f dB | smart: S -%.2f dB, vowel air -%.2f dB\n", sLoss[0], airLoss[0], sLoss[1], airLoss[1]);
        check (sLoss[1] > sLoss[0] - 1.5, "smart still takes the S down");
        check (airLoss[1] < airLoss[0] * 0.5 && airLoss[1] < 1.5, "smart leaves the air of the vowels (less than half the loss, < 1.5 dB)");
    }

    // 2026-11-13 (Jakob vs. Pro-DS: "bei Delta hoert man die Stimme mit"): an S that runs straight into a vowel.
    // The reduction must be gone when the vowel starts, however long the release - only the S is in the delta.
    void testSIntoVowel()
    {
        Test t ("smart: S straight into a vowel - the vowel is not reduced");
        const double fs = 48000.0;
        std::vector<float> x;
        std::vector<std::pair<int, int>> sR, vR;
        juce::Random rnd (5);
        Biq hp1, hp2;
        hp1.b.setCoeffs (vocalypse::dsp::design::highPass (fs, 5000.0, 0.54));
        hp2.b.setCoeffs (vocalypse::dsp::design::highPass (fs, 5000.0, 1.31));
        double phase = 0.0;
        for (int rep = 0; rep < 8; ++rep)
        {
            const int s0 = (int) x.size(), sLen = (int) (fs * 0.1), vLen = (int) (fs * 0.25);
            for (int k = 0; k < sLen; ++k)
                x.push_back (0.25f * hp2.run (hp1.run (rnd.nextFloat() * 2.0f - 1.0f)) * std::min (1.0f, k / (float) (fs * 0.01)));
            const int v0 = (int) x.size();
            for (int k = 0; k < vLen; ++k)
            {
                phase += 200.0 / fs;
                float v = 0.0f;
                for (int h = 1; h * 200 < 9000; ++h)   // bright vowel: harmonics up to 9 kHz
                    v += (h * 200 < 1000 ? 1.0f / h : 0.3f / h) * (float) std::sin (juce::MathConstants<double>::twoPi * phase * h);
                x.push_back (0.12f * v);
            }
            for (int k = 0; k < (int) (fs * 0.1); ++k)
                x.push_back (0.0f);
            sR.push_back ({ s0 + (int) (fs * 0.015), v0 - (int) (fs * 0.005) });
            vR.push_back ({ v0 + (int) (fs * 0.003), v0 + (int) (fs * 0.08) });   // the first 80 ms of the vowel
        }
        for (float releaseMs : { 35.0f, 150.0f })
        {
            Settings s;
            s.thresholdDb = -50.0f;
            s.rangeDb = 10.0f;
            s.freq = 5000.0f;
            s.releaseMs = releaseMs;
            E e (fs, s);
            const auto y = run (e, x);
            const int L = e.getLatencySamples();
            const double sLoss = rangesDb (x, sR, 0) - rangesDb (y, sR, L);
            const double vLoss = rangesDb (x, vR, 0) - rangesDb (y, vR, L);
            std::printf ("    release %.0f ms: S -%.2f dB, vowel onset -%.3f dB\n", releaseMs, sLoss, vLoss);
            // (white noise up to 24 kHz: the split bell takes the S range, the air above it stays)
            check (sLoss > 3.0, "the S is taken down");
            check (vLoss < 0.1, "the vowel after the S keeps its level (release " + juce::String (releaseMs, 0) + " ms)");
        }
    }

    void testPresets()
    {
        Test t ("presets: factory, user, modified marker, A/B");
        EmberAudioProcessor p;
        p.prepareToPlay (48000.0, 512);
        const auto& list = factoryPresets();
        check (list.size() >= 8, "factory presets");
        for (int i = 0; i < (int) list.size(); ++i)
        {
            p.loadFactoryPreset (i);
            check (! p.isPresetModified(), juce::String ("fresh preset not modified: ") + list[(size_t) i].nameEn);
            for (const auto& [id, v] : list[(size_t) i].values)
                if (auto* raw = p.state.getRawParameterValue (id))
                    near (raw->load(), v, std::max (0.02, std::abs (v) * 0.002), juce::String (list[(size_t) i].nameEn) + " " + id);
        }
        p.loadFactoryPreset (1);
        if (auto* thr = p.state.getParameter (pid::threshold))
            thr->setValueNotifyingHost (thr->convertTo0to1 (-12.0f));
        check (p.isPresetModified(), "change -> modified");

        // user preset round trip
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ember_preset_test");
        dir.deleteRecursively();
        const auto file = dir.getChildFile ("Mein Test");
        check (p.saveUserPreset (file), "save");
        check (dir.getChildFile (juce::String ("Mein Test") + presetExtension).existsAsFile(), "file with extension");
        p.loadFactoryPreset (0);
        check (p.loadUserPreset (dir.getChildFile (juce::String ("Mein Test") + presetExtension)), "load");
        near (readSettings (p.state).thresholdDb, -12.0, 0.05, "user preset value back");
        check (p.getPresetName() == "Mein Test" && ! p.isPresetModified(), "user preset name, not modified");

        // Polish 2026-11-15: a dot in the name stays, a "nan" in a shared preset does not mute the plugin
        check (p.saveUserPreset (dir.getChildFile ("Lead 1.5")) && dir.getChildFile (juce::String ("Lead 1.5") + presetExtension).existsAsFile(),
               "a dot in the preset name is kept");
        {
            const auto bad = dir.getChildFile (juce::String ("Kaputt") + presetExtension);
            bad.replaceWithText (R"(<EmberPreset><Param id="emFreq" value="nan"/><Param id="emThreshold" value="-20"/></EmberPreset>)");
            check (p.loadUserPreset (bad), "corrupt preset still loads");
            const auto s = readSettings (p.state);
            check (std::isfinite (s.freq) && std::abs (s.thresholdDb + 20.0f) < 0.1f, "nan skipped, other values kept");
        }
        dir.deleteRecursively();

        // bypass is never part of a preset
        if (auto* b = p.state.getParameter (pid::bypass))
            b->setValueNotifyingHost (1.0f);
        p.loadFactoryPreset (2);
        check (readSettings (p.state).bypass, "loading a preset keeps bypass");
        if (auto* b = p.state.getParameter (pid::bypass))
            b->setValueNotifyingHost (0.0f);

        // A/B
        p.loadFactoryPreset (0);
        p.switchSlot (1);   // B starts as a copy of A
        near (readSettings (p.state).thresholdDb, voiceMidThrDb, 0.05, "B starts as A");
        if (auto* thr = p.state.getParameter (pid::threshold))
            thr->setValueNotifyingHost (thr->convertTo0to1 (-40.0f));
        p.switchSlot (0);
        near (readSettings (p.state).thresholdDb, voiceMidThrDb, 0.05, "A back");
        p.switchSlot (1);
        near (readSettings (p.state).thresholdDb, -40.0, 0.05, "B kept its change");
        p.copyActiveToOther();
        p.switchSlot (0);
        near (readSettings (p.state).thresholdDb, -40.0, 0.05, "B copied to A");

        // stepping wraps around the factory list
        p.loadFactoryPreset ((int) list.size() - 1);
        const auto users = userPresets();
        p.stepPreset (1);
        check (users.isEmpty() ? p.getFactoryIndex() == 0 : p.getFactoryIndex() == -1, "next after the last factory preset");
    }

    // Usability review 2026-11-19: AUTO keeps the user's settings and leaves "before" on A, listening is no setting,
    // AUTO waits for audio and finishes on its own, undo / redo
    void testUsability()
    {
        Test t ("usability: AUTO keeps settings + before on A, listen is no setting, AUTO without audio, undo");
        const auto setReal = [] (EmberAudioProcessor& p, const char* id, float v)
        {
            if (auto* prm = p.state.getParameter (id))
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (prm->convertTo0to1 (v));
                prm->endChangeGesture();
            }
        };
        const auto real = [] (EmberAudioProcessor& p, const char* id) { return p.state.getRawParameterValue (id)->load(); };
        const auto take = makeTake (48000.0, 13.0);
        const auto feed = [&take] (EmberAudioProcessor& p, double fromS, double toS)
        {
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            for (int start = (int) (fromS * 48000.0); start + 512 <= std::min ((int) take.x.size(), (int) (toS * 48000.0)); start += 512)
            {
                for (int ch = 0; ch < 2; ++ch)
                    buf.copyFrom (ch, 0, take.x.data() + start, 512);
                p.processBlock (buf, midi);
            }
        };

        // AUTO: only frequency, threshold, range and release; mode, output, mix, Amount, times stay; before on A
        {
            EmberAudioProcessor p;
            p.prepareToPlay (48000.0, 512);
            setReal (p, pid::mode, 1.0f);
            setReal (p, pid::output, 3.0f);
            setReal (p, pid::mix, 80.0f);
            setReal (p, pid::amount, 70.0f);
            setReal (p, pid::attack, 1.0f);
            setReal (p, pid::lookahead, 1.0f);
            setReal (p, pid::threshold, -20.0f);
            setReal (p, pid::listen, 2.0f);
            p.startAuto();
            feed (p, 0.0, 13.0);
            p.finishAuto();
            p.waitForAuto();
            check (p.getAutoPhase() == EmberAudioProcessor::AutoPhase::done, "AUTO done");
            const auto& r = p.getLastAuto();
            near (real (p, pid::mode), 1.0, 0.01, "mode kept (wideband)");
            near (real (p, pid::output), 3.0, 0.05, "output kept");
            near (real (p, pid::mix), 80.0, 0.1, "mix kept");
            near (real (p, pid::amount), 70.0, 0.1, "Amount kept");
            near (real (p, pid::attack), 1.0, 0.02, "attack kept");
            near (real (p, pid::lookahead), 1.0, 0.02, "lookahead kept");
            near (real (p, pid::threshold), r.thresholdDb, 0.1, "threshold from AUTO");
            near (real (p, pid::release), autoReleaseMs, 0.5, "release from AUTO");
            check (real (p, pid::listen) < 0.5f, "AUTO switches listening off");
            // wideband dips the whole voice: the vowel next to each S keeps AUTO gentler there (it says what it does)
            check (r.expectedReductionDb > 0.3f && r.vowelReductionDb <= autoVowelMaxDb + 0.05f && r.mixedReductionDb <= autoMixedMaxDb + 0.05f,
                   "measured in wideband at 70 %: S down " + juce::String (r.expectedReductionDb, 2) + " dB, vowels "
                   + juce::String (r.vowelReductionDb, 2) + " dB");
            check (p.getActiveSlot() == 1 && p.isShowingAutoResult(), "AUTO result on B (active)");
            check (p.getAutoMessage().contains ("A"), "the message says where before is");
            p.switchSlot (0);
            near (real (p, pid::threshold), -20.0, 0.05, "A: the settings from before");
            near (real (p, pid::freq), voiceMidHz, 1.0, "A: frequency from before");
            check (p.getPresetName() == presetName (factoryPresets()[0]) && ! p.isShowingAutoResult(), "A: preset name from before");
            p.switchSlot (1);
            near (real (p, pid::threshold), r.thresholdDb, 0.1, "B: AUTO again");
            check (p.isShowingAutoResult(), "B: still the AUTO setting");
            setReal (p, pid::range, 10.0f);
            check (! p.isShowingAutoResult(), "an edit ends the AUTO result");
        }
        // Amount 70 %: AUTO measures with the Amount as it is, so the S lose about as much as at 50 % (a higher threshold)
        {
            Settings at50, at70;
            at70.amount = 0.7f;
            const auto r50 = analyse (take.x.data(), (int) take.x.size(), 48000.0, at50);
            const auto r70 = analyse (take.x.data(), (int) take.x.size(), 48000.0, at70);
            check (std::abs (r70.expectedReductionDb - r50.expectedReductionDb) < 0.5f && r70.thresholdDb >= r50.thresholdDb
                       && r70.vowelReductionDb <= autoVowelMaxDb + 0.05f,
                   "70 %: S down " + juce::String (r70.expectedReductionDb, 2) + " dB (50 %: " + juce::String (r50.expectedReductionDb, 2)
                       + " dB), threshold " + juce::String (r70.thresholdDb, 1) + " (50 %: " + juce::String (r50.thresholdDb, 1) + ")");
        }
        // Amount too low to work with: AUTO sets 50 % and says so
        {
            EmberAudioProcessor p;
            p.prepareToPlay (48000.0, 512);
            setReal (p, pid::amount, 10.0f);
            p.startAuto();
            feed (p, 0.0, 13.0);
            p.finishAuto();
            p.waitForAuto();
            check (p.didAutoRaiseAmount() && std::abs (real (p, pid::amount) - 50.0f) < 0.1f, "Amount 10 % -> 50 %");
            check (p.getAutoMessage().contains ("Amount"), "and the message says so");
        }

        // Listening (Delta / S range) is not part of presets, A/B, "modified" or a reopened project
        {
            EmberAudioProcessor p;
            p.prepareToPlay (48000.0, 512);
            p.loadFactoryPreset (0);
            setReal (p, pid::listen, 2.0f);
            check (! p.isPresetModified(), "Delta does not mark the preset modified");
            check (capture (p.state).count (pid::listen) == 0, "not captured");
            p.switchSlot (1);
            p.switchSlot (0);
            near (real (p, pid::listen), 2.0, 0.01, "A/B keeps listening as it is");
            p.loadFactoryPreset (3);
            near (real (p, pid::listen), 2.0, 0.01, "a preset does not switch listening");
            juce::MemoryBlock mb;
            p.getStateInformation (mb);
            EmberAudioProcessor q;
            q.setStateInformation (mb.getData(), (int) mb.getSize());
            check (real (q, pid::listen) < 0.5f, "a project reopens with listening off");
        }

        // Locked: AUTO does not run (the audio passes through unprocessed)
        {
            const auto lf = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ember-ux-lock.license");
            lf.deleteFile();
            auto c = licenceConfig();
            c.on = true;
            c.asynchronous = false;
            c.file = lf;
            c.transport = [] (const juce::String&, const juce::var&) -> std::pair<int, juce::String> { return { 0, {} }; };
            vocalypse::licensing::Manager locked (c);
            EmberAudioProcessor p;
            p.setLicenceForTest (locked);
            p.prepareToPlay (48000.0, 512);
            p.startAuto();
            check (p.getAutoPhase() == EmberAudioProcessor::AutoPhase::idle, "locked: AUTO does not start");
            lf.deleteFile();
        }

        // AUTO without audio: "press play" after a few seconds; after enough voice and a stop it finishes by itself
        {
            EmberAudioProcessor p;
            p.prepareToPlay (48000.0, 512);
            p.startAuto();
            p.updateAuto (0.0);
            p.updateAuto (2.0);
            check (! p.isAutoWaitingForAudio(), "2 s: still just listening");
            p.updateAuto (autoQuietSeconds + 0.5);
            check (p.isAutoWaitingForAudio() && p.getAutoMessage().contains ("Play"), "no audio: press play");
            check (p.getAutoPhase() == EmberAudioProcessor::AutoPhase::listening, "and it keeps listening");
            feed (p, 0.0, 5.5);
            p.updateAuto (6.0);
            check (! p.isAutoWaitingForAudio() && p.getAutoPhase() == EmberAudioProcessor::AutoPhase::listening, "voice: listening");
            p.updateAuto (6.0 + autoQuietSeconds + 0.1);
            check (p.getAutoPhase() == EmberAudioProcessor::AutoPhase::analysing, "enough voice, then the transport stopped: done by itself");
            p.waitForAuto();
            check (p.getAutoPhase() == EmberAudioProcessor::AutoPhase::done, "and applied");
        }

        // Undo / redo: an edit = one step, the same control again right away = the same step, a preset = one step
        {
            EmberAudioProcessor p;
            p.prepareToPlay (48000.0, 512);
            check (! p.canUndo() && ! p.undo(), "nothing to undo yet");
            setReal (p, pid::threshold, -40.0f);
            setReal (p, pid::threshold, -45.0f);
            setReal (p, pid::freq, 8000.0f);
            check (p.canUndo(), "can undo");
            check (p.undo(), "undo");
            near (real (p, pid::freq), voiceMidHz, 1.0, "frequency back");
            near (real (p, pid::threshold), -45.0, 0.05, "threshold still changed");
            check (p.undo(), "undo again");
            near (real (p, pid::threshold), voiceMidThrDb, 0.05, "both threshold moves were one step");
            check (p.canRedo() && p.redo(), "redo");
            near (real (p, pid::threshold), -45.0, 0.05, "threshold redone");
            p.loadFactoryPreset (8);
            p.undo();
            near (real (p, pid::threshold), -45.0, 0.05, "preset undone in one step");
            near (real (p, pid::freq), voiceMidHz, 1.0, "frequency too");
            setReal (p, pid::listen, 2.0f);
            check (p.undo() && real (p, pid::listen) > 1.5f, "listening is no undo step");
        }
    }

    void testEditor (const juce::File& snapshotDir)
    {
        Test t ("editor: both views, scaling, languages");
        EmberAudioProcessor p;
        p.prepareToPlay (48000.0, 512);
        UserSettings::useFileForTests (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ember_test.settings"));
        for (auto lang : { vocalypse::i18n::Language::german, vocalypse::i18n::Language::english })
        {
            vocalypse::i18n::overrideSystemLanguage (lang);
            std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
            auto* ed = dynamic_cast<EmberEditor*> (base.get());
            check (ed != nullptr, "editor type");
            if (ed == nullptr)
                return;
            ed->setSize (look::baseW, look::baseH);   // the editor remembers its last size
            const juce::String suffix = lang == vocalypse::i18n::Language::german ? "de" : "en";
            for (bool adv : { false, true })
            {
                ed->setAdvanced (adv);
                const auto img = snapshot (*ed);
                check (img.isValid() && img.getWidth() == look::baseW, "snapshot " + juce::String (adv ? "advanced" : "simple"));
                if (! adv)
                {
                    // only Ember himself starts AUTO, not the empty stage around him (review 2026-11-19)
                    auto& m = ed->getPanel().getMascot();
                    check (! m.hitsFigure ({ 6.0f, 6.0f }) && ! m.hitsFigure ({ (float) m.getWidth() - 6.0f, (float) m.getHeight() * 0.5f }),
                           "a click beside Ember is no AUTO");
                    check (m.hitsFigure (m.getLocalBounds().getCentre().toFloat()), "a click on Ember is");
                }
                if (snapshotDir != juce::File())
                {
                    snapshotDir.createDirectory();
                    juce::PNGImageFormat png;
                    juce::FileOutputStream out (snapshotDir.getChildFile (juce::String (adv ? "advanced_" : "simple_") + suffix + ".png"));
                    if (out.openedOk()) { out.setPosition (0); out.truncate(); png.writeImageToStream (img, out); }
                    // twice the resolution for the website (sharp on high-density screens)
                    const auto hi = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
                    juce::FileOutputStream out2 (snapshotDir.getChildFile (juce::String (adv ? "advanced_" : "simple_") + suffix + "@2x.png"));
                    if (out2.openedOk()) { out2.setPosition (0); out2.truncate(); png.writeImageToStream (hi, out2); }
                }
            }
            ed->setSize (look::baseW * 3 / 2, look::baseH * 3 / 2);
            check (ed->getWidth() == look::baseW * 3 / 2, "resizes");
            check (ed->isAdvanced(), "view kept");
        }
        // The lock card (no key yet), both languages
        if (snapshotDir != juce::File())
        {
            const auto lf = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ember-lock-snap.license");
            lf.deleteFile();
            auto c = licenceConfig();
            c.on = true;
            c.asynchronous = false;
            c.file = lf;
            c.transport = [] (const juce::String&, const juce::var&) -> std::pair<int, juce::String> { return { 0, {} }; };
            vocalypse::licensing::Manager locked (c);
            EmberAudioProcessor q;
            q.setLicenceForTest (locked);
            q.prepareToPlay (48000.0, 512);
            for (auto lang : { vocalypse::i18n::Language::german, vocalypse::i18n::Language::english })
            {
                vocalypse::i18n::overrideSystemLanguage (lang);
                std::unique_ptr<juce::AudioProcessorEditor> base (q.createEditor());
                base->setSize (look::baseW, look::baseH);
                auto* ed = dynamic_cast<EmberEditor*> (base.get());
                const auto img = snapshot (*ed);
                juce::PNGImageFormat png;
                juce::FileOutputStream out (snapshotDir.getChildFile (juce::String ("locked_") + (lang == vocalypse::i18n::Language::german ? "de" : "en") + ".png"));
                if (out.openedOk()) { out.setPosition (0); out.truncate(); png.writeImageToStream (img, out); }
            }
            lf.deleteFile();
        }
        vocalypse::i18n::overrideSystemLanguage (std::nullopt);
        check ((bool) p.state.state.getProperty ("advanced", false), "view stored in the state");

        // Language switch in the menu: rebuilds the panel, keeps the view, is remembered
        {
            std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
            auto* ed = dynamic_cast<EmberEditor*> (base.get());
            ed->setLanguage ("en");
            check (! vocalypse::i18n::isGerman() && ed->isAdvanced(), "English, view kept");
            ed->setLanguage ("de");
            check (vocalypse::i18n::isGerman(), "German");
            juce::SharedResourcePointer<UserSettings> us;
            check (us->getLanguage() == "de", "language remembered");
            ed->setLanguage ("system");
        }
    }

    // ------------------------------------------------------------ real takes
    std::unique_ptr<juce::AudioBuffer<float>> loadWav (const juce::File& f, double& fs)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
        if (r == nullptr)
            return nullptr;
        fs = r->sampleRate;
        auto b = std::make_unique<juce::AudioBuffer<float>> ((int) r->numChannels, (int) r->lengthInSamples);
        r->read (b.get(), 0, (int) r->lengthInSamples, 0, true, true);
        return b;
    }

    std::vector<float> monoOf (const juce::AudioBuffer<float>& b)
    {
        std::vector<float> m ((size_t) b.getNumSamples(), 0.0f);
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                m[(size_t) i] += b.getSample (ch, i) / (float) b.getNumChannels();
        return m;
    }

    // AUTO as the plugin does it: only the non-silent blocks of the first 30 s
    AutoResult autoOn (const std::vector<float>& mono, double fs, bool smart = true)
    {
        AutoCapture c;
        c.prepare (fs);
        c.start();
        for (int start = 0; start < (int) mono.size() && c.isCapturing(); start += 512)
        {
            const float* ch[1] { mono.data() + start };
            c.push (ch, 1, std::min (512, (int) mono.size() - start));
        }
        return analyse (c.data(), c.getNumSamples(), fs, smart);
    }

    int render (const juce::File& in, const juce::File& dir)
    {
        double fs = 0.0;
        auto buf = loadWav (in, fs);
        if (buf == nullptr) { std::printf ("cannot read %s\n", in.getFullPathName().toRawUTF8()); return 1; }
        const auto mono = monoOf (*buf);
        const auto r = autoOn (mono, fs);
        std::printf ("%s: status %d freq %.0f thr %.1f range %.1f (S med %.1f, vowel95 %.1f, expected -%.1f dB, S frames %d)\n",
                     in.getFileName().toRawUTF8(), (int) r.status, r.freq, r.thresholdDb, r.rangeDb, r.sibilantMedianDb, r.vowel95Db,
                     r.expectedReductionDb, r.sibilantFrames);
        Settings s;
        if (r.status == AutoResult::Status::ok) { s.freq = r.freq; s.thresholdDb = r.thresholdDb; s.rangeDb = r.rangeDb; s.releaseMs = autoReleaseMs; }
        // EMBER_MANUAL="threshold,range,smart" renders a hand-set (heavy) setting instead of AUTO's
        const auto manual = juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("EMBER_MANUAL", {}), ",", "");
        if (manual.size() == 3)
        {
            s.thresholdDb = manual[0].getFloatValue();
            s.rangeDb = manual[1].getFloatValue();
            s.smart = manual[2].getIntValue() != 0;
            s.freq = 5000.0f;
            std::printf ("  manual: thr %.1f range %.1f smart %d freq 5000\n", s.thresholdDb, s.rangeDb, (int) s.smart);
        }
        dir.createDirectory();
        for (int listen : { 0, 2 })
        {
            s.listen = listen;
            Engine e;
            e.setSettings (s);
            e.prepare (fs, 512);
            juce::AudioBuffer<float> out (*buf);
            const int nch = std::min (2, out.getNumChannels());
            const int L = e.getLatencySamples();
            juce::AudioBuffer<float> padded (nch, out.getNumSamples() + L);
            padded.clear();
            for (int ch = 0; ch < nch; ++ch)
                padded.copyFrom (ch, 0, out, ch, 0, out.getNumSamples());
            float maxGr = 0.0f;
            for (int start = 0; start < padded.getNumSamples(); start += 512)
            {
                const int n = std::min (512, padded.getNumSamples() - start);
                float* ch[2] { padded.getWritePointer (0, start), padded.getWritePointer (nch > 1 ? 1 : 0, start) };
                e.process (ch, nch, n);
                maxGr = std::max (maxGr, e.gainReductionDb.pull());
            }
            for (int ch = 0; ch < nch; ++ch)
                out.copyFrom (ch, 0, padded, ch, L, out.getNumSamples());
            const auto file = dir.getChildFile (in.getFileNameWithoutExtension() + (listen == 0 ? "_ember.wav" : "_delta.wav"));
            file.deleteFile();
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::FileOutputStream> os (file.createOutputStream());
            if (os == nullptr) { std::printf ("cannot write %s\n", file.getFullPathName().toRawUTF8()); return 1; }
            std::unique_ptr<juce::OutputStream> stream (os.release());
            auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (fs).withNumChannels (nch).withBitsPerSample (32).withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
            if (w == nullptr) return 1;
            w->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
            if (listen == 0)
                std::printf ("  max reduction %.1f dB -> %s\n", maxGr, file.getFullPathName().toRawUTF8());
        }
        return 0;
    }

    // Offline render for listening and measuring (sound audit 2026-11-19): EmberTests --process <in.wav> <out.wav> [key=value ...]
    //   freq thr range amount attack release la mix mode det(0 smart,1 rel,2 abs) listen gain(dB in) block
    //   auto=1 (AUTO first, as the plugin applies it) autosec=12 autostart=0  trace=<wav> (applied reduction, dB, dry-aligned)
    int processCli (const juce::StringArray& args)
    {
        double fs = 0.0;
        auto buf = loadWav (juce::File (args[1]), fs);
        if (buf == nullptr) { std::printf ("cannot read\n"); return 1; }
        std::map<juce::String, float> kv;
        juce::String tracePath;
        for (int i = 3; i < args.size(); ++i)
        {
            const auto k = args[i].upToFirstOccurrenceOf ("=", false, false), v = args[i].fromFirstOccurrenceOf ("=", false, false);
            if (k == "trace") tracePath = v; else kv[k] = v.getFloatValue();
        }
        const auto has = [&] (const char* k) { return kv.count (k) > 0; };
        if (has ("gain"))
            buf->applyGain (dbToGain (kv["gain"]));
        Settings s;
        if (has ("auto") && kv["auto"] > 0.5f)
        {
            const auto mono = monoOf (*buf);
            const int start = (int) ((has ("autostart") ? kv["autostart"] : 0.0f) * fs);
            const double secs = has ("autosec") ? kv["autosec"] : autoEnoughSeconds;
            AutoCapture c;
            c.prepare (fs);
            c.start();
            for (int p = start; p < (int) mono.size() && c.isCapturing() && c.getSeconds() < secs; p += 512)
            {
                const float* ch[1] { mono.data() + p };
                c.push (ch, 1, std::min (512, (int) mono.size() - p));
            }
            const auto r = analyse (c.data(), c.getNumSamples(), fs, true);
            std::printf ("AUTO status %d freq %.0f thr %.1f range %.1f peakHz %.0f target %.2f expected %.2f vowel %.3f mixed %.3f sVsV %.1f Sframes %d secs %.1f\n",
                         (int) r.status, r.freq, r.thresholdDb, r.rangeDb, r.peakHz, r.targetDb, r.expectedReductionDb, r.vowelReductionDb,
                         r.mixedReductionDb, r.sibilanceVsVowelDb, r.sibilantFrames, r.seconds);
            if (r.status == AutoResult::Status::ok) { s.freq = r.freq; s.thresholdDb = r.thresholdDb; s.rangeDb = r.rangeDb; s.releaseMs = autoReleaseMs; }
        }
        if (has ("freq")) s.freq = kv["freq"];
        if (has ("thr")) s.thresholdDb = kv["thr"];
        if (has ("range")) s.rangeDb = kv["range"];
        if (has ("amount")) s.amount = kv["amount"];
        if (has ("attack")) s.attackMs = kv["attack"];
        if (has ("release")) s.releaseMs = kv["release"];
        if (has ("la")) s.lookaheadMs = kv["la"];
        if (has ("mix")) s.mix = kv["mix"];
        if (has ("mode")) s.mode = (int) kv["mode"];
        if (has ("listen")) s.listen = (int) kv["listen"];
        if (has ("det")) { const int d = (int) kv["det"]; s.smart = d == 0; s.relative = d <= 1; }
        const int block = has ("block") ? std::max (1, (int) kv["block"]) : 512;
        std::printf ("SETTINGS freq %.0f thr %.1f range %.1f amount %.2f att %.2f rel %.0f la %.2f mode %d smart %d rel %d\n",
                     s.freq, s.thresholdDb, s.rangeDb, s.amount, s.attackMs, s.releaseMs, s.lookaheadMs, s.mode, (int) s.smart, (int) s.relative);

        Engine e;
        e.setSettings (s);
        e.prepare (fs, block);
        const int nch = std::min (2, buf->getNumChannels()), N = buf->getNumSamples(), L = e.getLatencySamples();
        juce::AudioBuffer<float> padded (nch, N + L);
        padded.clear();
        for (int ch = 0; ch < nch; ++ch)
            padded.copyFrom (ch, 0, *buf, ch, 0, N);
        const bool wantTrace = tracePath.isNotEmpty();
        std::vector<float> trace (wantTrace ? (size_t) (N + L) : 0, 0.0f);
        const int stepBlock = wantTrace ? 1 : block;
        for (int start = 0; start < padded.getNumSamples(); start += stepBlock)
        {
            const int n = std::min (stepBlock, padded.getNumSamples() - start);
            float* ch[2] { padded.getWritePointer (0, start), padded.getWritePointer (nch > 1 ? 1 : 0, start) };
            e.process (ch, nch, n);
            if (wantTrace)
                trace[(size_t) start] = e.currentReductionDb.load();
        }
        const auto write = [&] (const juce::File& file, const juce::AudioBuffer<float>& b)
        {
            file.deleteFile();
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
            auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (fs).withNumChannels (b.getNumChannels()).withBitsPerSample (32).withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
            if (w != nullptr) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        };
        juce::AudioBuffer<float> out (nch, N);
        for (int ch = 0; ch < nch; ++ch)
            out.copyFrom (ch, 0, padded, ch, L, N);
        write (juce::File (args[2]), out);
        if (wantTrace)
        {
            const int la = juce::jlimit (0, L, (int) std::lround (s.lookaheadMs * 0.001 * fs));
            juce::AudioBuffer<float> t (1, N);
            for (int i = 0; i < N; ++i)
                t.setSample (0, i, i + la < N + L ? trace[(size_t) (i + la)] : 0.0f);
            write (juce::File (tracePath), t);
        }
        std::printf ("LATENCY %d\n", L);
        return 0;
    }

    // Website / marketing pictures: a real take runs through EMBER (AUTO set up on it), the editor is ticked like the
    // real 30 fps timer, and both views are captured at 2x right at the strongest S, so spectrum, meters and Ember
    // show the plugin at work instead of an empty window.
    int marketing (const juce::File& in, const juce::File& dir)
    {
        double fs = 0.0;
        auto take = loadWav (in, fs);
        if (take == nullptr) { std::printf ("cannot read %s\n", in.getFullPathName().toRawUTF8()); return 1; }
        // a dry take sits low; bring its peak to -1 dBFS so the spectrum fills the display as in a mix
        if (const float peak = take->getMagnitude (0, take->getNumSamples()); peak > 0.0f)
            take->applyGain (0.89f / peak);
        const int n = take->getNumSamples(), block = 512;
        const auto stereoBlock = [&] (juce::AudioBuffer<float>& buf, int start)
        {
            const int len = std::min (block, n - start);
            buf.clear();
            for (int ch = 0; ch < 2; ++ch)
                buf.copyFrom (ch, 0, *take, std::min (ch, take->getNumChannels() - 1), start, len);
        };
        const auto setUp = [&] (EmberAudioProcessor& p)
        {
            p.prepareToPlay (fs, block);
            p.startAuto();
            juce::AudioBuffer<float> buf (2, block);
            juce::MidiBuffer midi;
            for (int s = 0; s + block <= std::min (n, (int) (fs * 13.0)); s += block) { stereoBlock (buf, s); p.processBlock (buf, midi); }
            p.finishAuto();
            p.waitForAuto();
        };

        // pass 1: where is the strongest reduction (after a few seconds, so the spectrum has filled)?
        // SIMPLE is captured at the strongest S (Ember at work); ADVANCED at the loudest vowel right after a clear S,
        // so the spectrum shows the whole voice while the reduction trace still shows the S
        int bestStart = 0, vowelStart = 0;
        {
            EmberAudioProcessor p;
            setUp (p);
            juce::AudioBuffer<float> buf (2, block);
            juce::MidiBuffer midi;
            float best = 0.0f, bestVowel = 0.0f;
            int lastS = -1000000;
            for (int s = 0; s + block <= n; s += block)
            {
                stereoBlock (buf, s);
                const float rms = buf.getRMSLevel (0, 0, block);
                p.processBlock (buf, midi);
                const float gr = p.engine.gainReductionDb.pull();
                if (s <= (int) (fs * 4.0))
                    continue;
                if (gr > best) { best = gr; bestStart = s; }
                if (gr >= 3.0f) lastS = s;
                if (s - lastS < (int) (fs * 0.3) && gr < 0.5f && rms > bestVowel) { bestVowel = rms; vowelStart = s; }
            }
            if (vowelStart == 0) vowelStart = bestStart;
            std::printf ("strongest reduction %.1f dB at %.2f s, vowel after an S at %.2f s (AUTO: %s)\n",
                         best, bestStart / fs, vowelStart / fs, p.getAutoMessage().toRawUTF8());
        }
        // pass 2: play up to that moment with the editor running, then capture
        dir.createDirectory();
        UserSettings::useFileForTests (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ember_marketing.settings"));
        juce::PNGImageFormat png;
        for (auto lang : { vocalypse::i18n::Language::german, vocalypse::i18n::Language::english })
            for (bool adv : { false, true })
            {
                vocalypse::i18n::overrideSystemLanguage (lang);
                EmberAudioProcessor p;
                setUp (p);
                std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
                auto* ed = dynamic_cast<EmberEditor*> (base.get());
                if (ed == nullptr) return 1;
                ed->setSize (look::baseW, look::baseH);
                ed->setAdvanced (adv);
                juce::AudioBuffer<float> buf (2, block);
                juce::MidiBuffer midi;
                const int target = adv ? vowelStart : bestStart;
                const int from = std::max (0, target - (int) (fs * 3.0));
                int sinceTick = 0;
                for (int s = from; s <= target; s += block)
                {
                    stereoBlock (buf, s);
                    p.processBlock (buf, midi);
                    sinceTick += block;
                    if (sinceTick >= (int) (fs / 30.0)) { sinceTick = 0; ed->tickForTest(); juce::MessageManager::getInstance()->runDispatchLoopUntil (1); }
                }
                for (int i = 0; i < 2; ++i) { ed->tickForTest(); juce::MessageManager::getInstance()->runDispatchLoopUntil (2); }
                const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
                const auto name = juce::String (adv ? "advanced_" : "simple_") + (lang == vocalypse::i18n::Language::german ? "de" : "en") + "@2x.png";
                juce::FileOutputStream out (dir.getChildFile (name));
                if (out.openedOk()) { out.setPosition (0); out.truncate(); png.writeImageToStream (img, out); }
                std::printf ("  %s %dx%d\n", name.toRawUTF8(), img.getWidth(), img.getHeight());
            }
        vocalypse::i18n::overrideSystemLanguage (std::nullopt);
        return 0;
    }

    // Website loops of Ember as the plugin draws him (2x, 24 pictures/s, 4 s each, after a warm-up so no cross-fade
    // is caught): listening (AUTO), dancing with the S sparks (an S every 0.75 s), dancing without S.
    // The live rig: the rest pose and the test poses of FIGUR_ANIMIEREN.md section 5, rendered as the plugin draws
    // them, on white (edges show), to compare with the browser prototype (Products/Ember/Mascot/live_v17)
    struct RigShot { const char* name; RigPose pose; };
    std::vector<RigShot> rigShots()
    {
        auto p = [] (std::function<void (RigPose&)> f) { RigPose r; f (r); return r; };
        return {
            { "rest", RigPose {} },
            { "head_right_arms_out", p ([] (RigPose& r) { r.head = 12; r.headX = 9; r.armL = -40; r.armR = 40; r.earL = -28; r.earR = 28; r.tail = 28; }) },
            { "head_left_arm_in", p ([] (RigPose& r) { r.head = -13; r.headX = -10; r.headY = 8; r.armR = 45; r.armL = 4; r.earL = 30; r.earR = -30; r.tail = -30; r.lean = -3; r.squash = 0.06f; }) },
            { "eyes_half", p ([] (RigPose& r) { r.lid = 0.5f; r.browY = 3; r.browTilt = -3; }) },
            { "eyes_shut", p ([] (RigPose& r) { r.lid = 1.0f; }) },
            { "squint_cross_mouth", p ([] (RigPose& r) { r.lowLid = 0.55f; r.browY = 4; r.browTilt = 9; r.head = 10; r.headX = 8; r.mouth = 0.8f; }) },
            { "look_corner", p ([] (RigPose& r) { r.lookX = 1; r.lookY = 1; r.pupil = 1.12f; r.browY = -6; r.browTilt = -4; }) },
            { "mouth_wide", p ([] (RigPose& r) { r.mouth = 1.15f; r.mouthW = 0.8f; r.bob = -30; r.squash = -0.05f; r.armL = -42; r.armR = 42; }) },
        };
    }

    // The live brain (step 3): what Ember does, without drawing
    void testLiveBrain()
    {
        currentTest = "live brain";
        auto run = [] (LiveBrain& b, MascotInput in, float seconds, std::function<void (MascotInput&, float)> each = {})
        {
            for (float t = 0; t < seconds; t += 1.0f / 60.0f)
            {
                if (each) each (in, t);
                b.update (in, 1.0f / 60.0f, { 517, 466 });
            }
        };
        {
            LiveBrain b; MascotInput in; in.bypass = true;
            run (b, in, 2.0f);
            check (b.getMode() == LiveBrain::Mode::sleep && b.pose().lid > 0.9f, "bypass: he sleeps, eyes shut");
        }
        {
            LiveBrain b; MascotInput in; in.hasSignal = true; in.bpm = 128;
            float minBob = 0;
            run (b, in, 3.0f, [&] (MascotInput&, float) { minBob = std::min (minBob, b.pose().bob); });
            check (b.getMode() == LiveBrain::Mode::groove && minBob < -6.0f, "a vocal plays: he grooves (bounces " + juce::String (minBob, 1) + " px)");
            in.hostStopped = true;
            run (b, in, 0.2f);
            check (b.getMode() != LiveBrain::Mode::groove, "the host stops: he stops at once");
        }
        {
            LiveBrain b; MascotInput in; in.hasSignal = true;
            // an S every 0.6 s: 6 dB for 80 ms
            run (b, in, 4.0f, [] (MascotInput& m, float t) { m.reductionDb = std::fmod (t, 0.6f) < 0.08f ? 6.0f : 0.0f; });
            check (b.eatenCount() >= 4, "every S flies in and he eats it (" + juce::String (b.eatenCount()) + " in 4 s)");
        }
        {
            LiveBrain b; MascotInput in; in.hasSignal = true; in.reductionDb = 12.0f;
            run (b, in, 2.0f);
            check (b.getMode() == LiveBrain::Mode::hot, "over 9 dB for a while: too much");
        }
        {
            LiveBrain b; MascotInput in; in.autoListening = true;
            run (b, in, 1.0f);
            check (b.getMode() == LiveBrain::Mode::listen && b.pose().head > 6.0f, "AUTO: he leans in and listens");
            in.autoListening = false;
            run (b, in, 0.5f);
            check (b.getMode() == LiveBrain::Mode::proud, "AUTO done: proud");
        }
        {
            LiveBrain b; MascotInput in;
            b.say ("Hallo du, ich bin Ember.");
            float maxMouth = 0, minBob = 0;
            run (b, in, 1.5f, [&] (MascotInput&, float) { maxMouth = std::max (maxMouth, b.pose().mouth); minBob = std::min (minBob, b.pose().bob); });
            check (maxMouth > 0.5f, "talking opens the mouth (" + juce::String (maxMouth, 2) + ")");
            check (minBob > -3.0f, "talking does not bounce him (" + juce::String (minBob, 1) + " px)");
        }
        {
            LiveBrain b; MascotInput in;
            run (b, in, 40.0f);
            check (b.getMode() == LiveBrain::Mode::sleep, "nothing happens for 40 s: he falls asleep");
        }
        {
            LiveMascotView v;
            v.setSize (520, 402);
            v.setVisible (true);
            for (int k = 0; k < 30; ++k) v.stepForTest (1.0f / 60.0f);
            juce::Image img (juce::Image::ARGB, 520, 402, true, juce::SoftwareImageType());
            {
                juce::Graphics g (img);
                v.paint (g);
            }
            int opaque = 0;
            for (int y = 0; y < 402; y += 4) for (int x = 0; x < 520; x += 4) opaque += img.getPixelAt (x, y).getAlpha() > 128;
            check (opaque > 800, "the view draws him (" + juce::String (opaque) + ")");
        }
    }

    int rigPictures (const juce::File& dir, bool transparent = false)
    {
        dir.createDirectory();
        const int size = transparent ? 1200 : 600;   // transparent: for mockups / marketing, twice the size
        juce::PNGImageFormat png;
        for (const auto& shot : rigShots())
        {
            juce::Image img (juce::Image::ARGB, size, size, true);
            {
                juce::Graphics g (img);
                if (! transparent)
                    g.fillAll (juce::Colours::white);
                RigRenderer r;
                r.render (g, img.getBounds().toFloat(), shot.pose);
            }
            auto f = dir.getChildFile (juce::String (shot.name) + ".png");
            f.deleteFile();
            juce::FileOutputStream out (f);
            png.writeImageToStream (img, out);
        }
        std::printf ("rig pictures: %s\n", dir.getFullPathName().toRawUTF8());
        return 0;
    }

    void testRig()
    {
        currentTest = "rig";
        const auto& rig = MascotRig::get();
        check (rig.isValid(), "rig data loads (parts, rig.json, eyes)");
        check (rig.parts.size() == 7 && rig.meshes.size() == 5, "7 parts, 5 meshes");
        // the figure is drawn and sits inside the area; the plugin's size (~300 px) renders fast enough for 60 fps
        juce::Image img (juce::Image::ARGB, 300, 300, true);
        RigRenderer r;
        RigPose moved; moved.head = 10; moved.armL = -30; moved.tail = 20; moved.lid = 0.5f; moved.mouth = 0.7f;
        double worst = 0, sumRest = 0, sumMoved = 0;
        for (int k = 0; k < 20; ++k)
        {
            img.clear (img.getBounds());
            juce::Graphics g (img);
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            r.render (g, img.getBounds().toFloat(), (k % 2) ? moved : RigPose {});
            const auto dt = juce::Time::getMillisecondCounterHiRes() - t0;
            (k % 2 ? sumMoved : sumRest) += dt;
            if (k >= 2) worst = std::max (worst, dt);       // the first frames build the caches
        }
        std::printf ("  rig: rest %.2f ms, moved %.2f ms on average\n", sumRest / 10.0, sumMoved / 10.0);
        // the editor's real stage (402 px) on a 2x screen: drawn at full pixel density
        {
            juce::Image big (juce::Image::ARGB, 804, 804, true, juce::SoftwareImageType());   // the screen's pixels
            RigRenderer r2;
            double sumR = 0, sumM = 0;
            for (int k = 0; k < 12; ++k)
            {
                juce::Graphics g (big);
                g.addTransform (juce::AffineTransform::scale (2.0f));
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                r2.render (g, { 0.0f, 0.0f, 402.0f, 402.0f }, (k % 2) ? moved : RigPose {}, 2.0f);
                if (k >= 2) ((k % 2) ? sumM : sumR) += juce::Time::getMillisecondCounterHiRes() - t0;
            }
            std::printf ("  rig: 402 px stage at 2x: rest %.2f ms, moved %.2f ms (last: figure %.2f of it face %.2f, outline %.2f, onto screen %.2f)\n",
                         sumR / 5.0, sumM / 5.0, r2.lastDrawMs, r2.lastFaceMs, r2.lastOutlineMs, r2.lastCompositeMs);
            check (sumR / 5.0 < 16.0 && sumM / 5.0 < 16.0, "2x screen: under 16 ms a frame (60 fps)");
        }
        int opaque = 0;
        for (int y = 0; y < 300; y += 3)
            for (int x = 0; x < 300; x += 3)
                opaque += img.getPixelAt (x, y).getAlpha() > 128 ? 1 : 0;
        check (opaque > 1000 && opaque < 6000, "the figure covers a sensible part of the area (" + juce::String (opaque) + ")");
        check (img.getPixelAt (2, 2).getAlpha() == 0, "nothing in the corner");
        std::printf ("  rig: worst frame %.2f ms at 300 px\n", worst);
        check (worst < 12.0, "a frame at 300 px renders in under 12 ms (" + juce::String (worst, 2) + " ms)");
    }

    int mascotLoops (const juce::File& dir)
    {
        dir.createDirectory();
        struct Loop { const char* name; MascotInput in; bool pulses; };
        const Loop loops[] {
            { "listening", { false, true, true, 0.0f }, false },
            { "dance_s", { false, true, false, 0.0f }, true },
            { "dance", { false, true, false, 0.0f }, false },
            { "idle", {}, false },
            { "lispy", { false, true, false, 12.0f }, false },
            { "sleeping", { true, false, false, 0.0f }, false },
        };
        juce::PNGImageFormat png;
        for (const auto& l : loops)
        {
            MascotView view;
            view.setSize (480, 360);
            view.setInput (l.in);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (3500);
            for (int k = 1; k <= 96; ++k)
            {
                if (l.pulses)
                {
                    auto pulse = l.in;
                    pulse.reductionDb = (k % 18) < 4 ? 4.0f + (float) (k % 36) / 6.0f : 0.0f;
                    view.setInput (pulse);
                }
                juce::MessageManager::getInstance()->runDispatchLoopUntil (42);
                juce::Image img (juce::Image::ARGB, 960, 720, true);
                {
                    juce::Graphics g (img);
                    g.fillAll (juce::Colour (0xff05060a));
                    g.addTransform (juce::AffineTransform::scale (2.0f));
                    view.paintEntireComponent (g, true);
                }
                juce::FileOutputStream out (dir.getChildFile (juce::String (l.name) + "_" + juce::String (k).paddedLeft ('0', 3) + ".png"));
                if (out.openedOk()) { out.setPosition (0); out.truncate(); png.writeImageToStream (img, out); }
            }
            std::printf ("  %s: 96 pictures\n", l.name);
        }
        return 0;
    }

    void testRealTakes()
    {
        const auto env = juce::SystemStats::getEnvironmentVariable ("EMBER_VOCALS", {});
        if (env.isEmpty())
            return;
        Test t ("real takes (EMBER_VOCALS)");
        juce::Array<juce::File> files;
        for (const auto& part : juce::StringArray::fromTokens (env, ";", ""))
        {
            const juce::File f (part.trim());
            if (f.isDirectory())
                files.addArray (f.findChildFiles (juce::File::findFiles, true, "*.wav"));
            else if (f.existsAsFile())
                files.add (f);
        }
        int ok = 0;
        for (const auto& f : files)
        {
            double fs = 0.0;
            auto buf = loadWav (f, fs);
            if (buf == nullptr || buf->getNumSamples() < (int) (fs * 4.0))
                continue;
            const auto mono = monoOf (*buf);
            const auto r = autoOn (mono, fs);
            std::printf ("    %-48s %s freq %5.0f thr %6.1f range %4.1f S -%.1f V -%.2f M -%.2f dB (S/V %.1f) peak %.0f\n",
                         f.getFileName().substring (0, 48).toRawUTF8(), r.status == AutoResult::Status::ok ? "ok  " : "none",
                         r.freq, r.thresholdDb, r.rangeDb, r.expectedReductionDb, r.vowelReductionDb, r.mixedReductionDb, r.sibilanceVsVowelDb, r.peakHz);
            if (r.status != AutoResult::Status::ok)
                continue;
            ++ok;
            if (juce::SystemStats::getEnvironmentVariable ("EMBER_COMPARE", {}).isNotEmpty())
            {
                const auto p = autoOn (mono, fs, false);
                std::printf ("      plain relative: thr %6.1f range %4.1f S -%.1f V -%.2f M -%.2f | smart: S -%.1f V -%.2f M -%.2f\n",
                             p.thresholdDb, p.rangeDb, p.expectedReductionDb, p.vowelReductionDb, p.mixedReductionDb,
                             r.expectedReductionDb, r.vowelReductionDb, r.mixedReductionDb);
            }
            check (r.freq >= autoMinCornerHz && r.freq <= autoMaxCornerHz, f.getFileName() + ": corner in range");
            check (r.vowelReductionDb <= 0.15f, f.getFileName() + ": vowels untouched");
            check (r.mixedReductionDb <= autoMixedMaxDb + 0.1f, f.getFileName() + ": S next to vowels keeps the vowel");
            check (r.rangeDb <= autoMaxRangeDb && r.expectedReductionDb <= autoMaxTargetDb + 0.5f, f.getFileName() + ": subtle");
        }
        std::printf ("    %d of %d takes set up\n", ok, files.size());
    }
}

double emberThreadCpuMs();
double emberProcessCpuMs();

// Step 5 of FIGUR_ANIMIEREN.md: CPU with several instances. N plugins process a looped take in real time on one audio
// thread while their editors are open on the desktop (live Ember at 60 fps); CPU time is taken per thread, so audio
// and drawing (message thread, shared by all editors in a host) are reported separately.
static int bench (int seconds)
{
    struct AudioLoop final : juce::Thread
    {
        std::vector<EmberAudioProcessor*> procs;
        std::vector<float> src;
        std::atomic<double> cpuMs { 0 }, worstBlockMs { 0 };
        std::atomic<int> blocks { 0 };
        AudioLoop() : juce::Thread ("bench audio") {}
        void run() override
        {
            const int n = 256;
            juce::AudioBuffer<float> buf (2, n);
            juce::MidiBuffer midi;
            size_t pos = 0;
            const double blockMs = 1000.0 * n / 48000.0, c0 = emberThreadCpuMs();
            double next = juce::Time::getMillisecondCounterHiRes();
            while (! threadShouldExit())
            {
                const auto w0 = juce::Time::getMillisecondCounterHiRes();
                for (auto* p : procs)
                {
                    for (int i = 0; i < n; ++i)
                    {
                        const float x = src[(pos + (size_t) i) % src.size()];
                        buf.setSample (0, i, x);
                        buf.setSample (1, i, x);
                    }
                    p->processBlock (buf, midi);
                }
                worstBlockMs = std::max (worstBlockMs.load(), juce::Time::getMillisecondCounterHiRes() - w0);
                pos = (pos + n) % src.size();
                ++blocks;
                cpuMs = emberThreadCpuMs() - c0;
                next += blockMs;
                const double wait = next - juce::Time::getMillisecondCounterHiRes();
                if (wait > 1.0)
                    juce::Thread::sleep ((int) wait);
            }
        }
    };

    UserSettings::useFileForTests (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ember_bench.settings"));
    const auto take = makeTake (48000.0, 10.0);
    std::printf ("Ember bench: %d s per run, 48 kHz / 256, editors open (live Ember)\n", seconds);
    std::printf ("  scale  inst | audio %% core  worst block | host UI thread %% core          | whole process %% core\n");
    for (bool inFront : { true, false })
    for (float scale : { 1.0f, 2.0f })
    {
        LiveMascotView::alwaysInFront = inFront;
        if (scale == 1.0f)
            std::printf (inFront ? "  -- cursor on the plugin (60 fps for a single window)\n" : "  -- cursor elsewhere (30 fps)\n");
        juce::Desktop::getInstance().setGlobalScaleFactor (scale);
        for (int count : { 1, 2, 4, 8 })
        {
            std::vector<std::unique_ptr<EmberAudioProcessor>> procs;
            std::vector<std::unique_ptr<juce::AudioProcessorEditor>> eds;
            AudioLoop loop;
            loop.src = take.x;
            for (int i = 0; i < count; ++i)
            {
                procs.push_back (std::make_unique<EmberAudioProcessor>());
                procs.back()->setPlayConfigDetails (2, 2, 48000.0, 256);
                procs.back()->prepareToPlay (48000.0, 256);
                eds.emplace_back (procs.back()->createEditor());
                auto& ed = *eds.back();
                ed.setTopLeftPosition (40 + 30 * i, 40 + 30 * i);
                ed.addToDesktop (juce::ComponentPeer::windowHasTitleBar);
                ed.setVisible (true);
                loop.procs.push_back (procs.back().get());
            }
            juce::MessageManager::getInstance()->runDispatchLoopUntil (1500);   // settle (first decode, window creation)
            LiveMascotView::benchFigureMs = 0; LiveMascotView::benchFloorMs = 0; LiveMascotView::benchRunesMs = 0;
            LiveMascotView::benchComposeMs = 0; LiveMascotView::benchFrames = 0;
            loop.startThread (juce::Thread::Priority::highest);
            const double m0 = emberThreadCpuMs(), p0 = emberProcessCpuMs(), w0 = juce::Time::getMillisecondCounterHiRes();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (seconds * 1000);
            const double wall = juce::Time::getMillisecondCounterHiRes() - w0, ui = emberThreadCpuMs() - m0, all = emberProcessCpuMs() - p0;
            loop.stopThread (2000);
            const double audioWall = loop.blocks.load() * 256 * 1000.0 / 48000.0;
            std::printf ("  %4.0fx  %4d | %7.2f %%    %6.2f ms   | %6.1f %% (%4.1f %% per editor) | %6.1f %%\n", scale, count,
                         100.0 * loop.cpuMs.load() / std::max (1.0, audioWall), loop.worstBlockMs.load(),
                         100.0 * ui / wall, 100.0 * ui / wall / count, 100.0 * all / wall);
            if (const int fr = LiveMascotView::benchFrames.load(); fr > 0)
                std::printf ("            per frame: figure %.2f, seal floor %.2f, runes+fire %.2f, rest %.2f ms (%d frames)\n",
                             LiveMascotView::benchFigureMs.load() / fr, LiveMascotView::benchFloorMs.load() / fr,
                             LiveMascotView::benchRunesMs.load() / fr, LiveMascotView::benchComposeMs.load() / fr, fr);
            eds.clear();
            for (auto& p : procs) p->releaseResources();
        }
    }
    juce::Desktop::getInstance().setGlobalScaleFactor (1.0f);
    LiveMascotView::alwaysInFront = false;
    return 0;
}

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);   // progress stays visible even if a test crashes
    juce::ScopedJuceInitialiser_GUI gui;
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (juce::String::fromUTF8 (argv[i]));

    if (args.size() >= 3 && args[0] == "--process")
        return processCli (args);
    if (args.size() >= 3 && args[0] == "--render")
        return render (juce::File (args[1]), juce::File (args[2]));
    if (args.size() >= 3 && args[0] == "--marketing")
        return marketing (juce::File (args[1]), juce::File (args[2]));
    if (args.size() >= 2 && args[0] == "--rig")
        return rigPictures (juce::File (args[1]), args.size() >= 3 && args[2] == "transparent");
    if (args.size() >= 1 && args[0] == "--bench")
        return bench (args.size() >= 2 ? juce::jmax (2, args[1].getIntValue()) : 8);
    if (args.size() >= 2 && args[0] == "--mascot-loops")
        return mascotLoops (juce::File (args[1]));

    juce::File snapshotDir;
    if (args.size() >= 2 && args[0] == "--snapshots")
        snapshotDir = juce::File (args[1]);

    testLayout();
    testLatency();
    testNull();
    testReduction();
    testWide();
    testListen();
    testBypass();
    testLookahead();
    testBlockSizes();
    testCpu();
    testRobust();
    testPolish();
    testAuto();
    testCapture();
    testProcessor();
    testLicence();
    testMascot();
    testSmart();
    testSIntoVowel();
    testPresets();
    testUsability();
    testMascotClips();
    testRig();
    testLiveBrain();
    testMascotFrames (snapshotDir);
    testEditor (snapshotDir);
    testRealTakes();

    std::printf ("\nEMBER: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
