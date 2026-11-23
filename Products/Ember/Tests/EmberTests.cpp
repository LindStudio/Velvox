// EMBER De-Esser test runner.
//   EmberTests                          all checks (exit code 1 on a failure)
//   EmberTests --render <wav> <dir>     AUTO on the take, then render: <name>_ember.wav, <name>_delta.wav + report
//   EmberTests --snapshots <dir>        PNGs of SIMPLE and ADVANCED (both languages)
//   EmberTests --marketing <take.wav> <dir>   website pictures (2x) of both views, captured at the strongest S
//   EmberTests --rig <dir>                     the live rig (FIGUR_ANIMIEREN.md): rest pose + test poses as PNG, 600 px
//   EmberTests --frames <dir>                  the same SIMPLE frames every run (1x, 1.25x): compare before / after a GUI change
//   EmberTests --bench [s] [scales=1,1.25] [counts=1,4] [front=yes|no|both] [min=1] [hosted=1] [hz=60] [anim=full|reduced|off]   GUI CPU
//   EmberTests --mascot-loops <dir>            website loops of Ember (listening, dancing with/without S sparks, idle, lispy, sleeping), 2x
//   EmberTests --qa / --qa-full                the QA suite (EmberQaTests.cpp); --qa-full adds CPU, memory, the whole matrix, real takes
//   env EMBER_VOCALS=<dir or ;-list>    also runs AUTO on these real takes (report + sanity checks)
#include <juce_audio_formats/juce_audio_formats.h>
#include <thread>
#include "EmberEngine.h"
#include "EmberAuto.h"
#include "EmberParams.h"
#include "EmberProcessor.h"
#include "EmberEditor.h"
#include "EmberMascot.h"
#include "EmberRig.h"
#include "EmberLiveMascot.h"
#include "EmberPresets.h"
#include "EmberTestSignals.h"
#include "Text/I18n.h"
#include <thread>

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

    using testsig::Biq;
    using testsig::Take;
    using testsig::makeTake;

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
        check (p.getParameters().size() == 16, "16 parameters");
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
        Test t ("latency is 10 ms at every rate (the longest lookahead)");
        for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        {
            E e (fs);
            check (e.getLatencySamples() == (int) std::lround (0.010 * fs), "latency at " + juce::String (fs));
        }
        EmberAudioProcessor p;
        p.prepareToPlay (48000.0, 256);
        check (p.getLatencySamples() == 480, "processor reports 480 samples at 48 kHz");
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

    // Two different channels (a wide double): the right one is the left one later and quieter
    std::vector<float> rightOf (const std::vector<float>& x, int delay = 7, float gain = 0.8f)
    {
        std::vector<float> r (x.size(), 0.0f);
        for (size_t i = (size_t) delay; i < x.size(); ++i)
            r[i] = gain * x[i - (size_t) delay];
        return r;
    }

    // A voice limit (or the threshold floor) is what stopped AUTO short of its target
    bool heldBack (const AutoResult& r)
    {
        return r.thresholdDb <= autoMinThresholdDb + 0.5f || r.vowelReductionDb >= 0.9f * autoVowelMaxDb
            || r.vowelPresenceDb >= 0.9f * autoVowelPresenceMaxDb || r.vowelAirDb >= 0.9f * autoVowelAirMaxDb
            || r.mixedReductionDb >= 0.9f * autoMixedMaxDb || r.mixedPresenceDb >= 0.9f * autoMixedPresenceMaxDb;
    }

    // AUTO as the plugin runs it: the capture is fed in host blocks, then the take it holds is analysed
    AutoResult autoViaCapture (const float* const* ch, int nch, int len, double fs, int block = 512, Settings base = {},
                               double maxVoicedSeconds = autoEnoughSeconds, AutoCapture::Take* takeOut = nullptr)
    {
        AutoCapture c;
        c.prepare (fs);
        c.start();
        for (int start = 0; start < len && c.isCapturing() && c.getVoicedSeconds() < maxVoicedSeconds; start += block)
        {
            const float* p[2] { ch[0] + start, ch[nch > 1 ? 1 : 0] + start };
            c.push (p, nch, std::min (block, len - start));
        }
        c.stop();
        auto take = c.copyTake();
        const float* tc[2] { take.channel[0].data(), take.numChannels > 1 ? take.channel[1].data() : nullptr };
        auto r = analyse (tc, take.numChannels, take.numSamples(), fs, base);
        if (takeOut != nullptr)
            *takeOut = std::move (take);
        return r;
    }

    void testAuto()
    {
        Test t ("AUTO finds the S range and a fitting threshold");
        for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            // makeTake's S are white noise filtered to 5-11 kHz: at a higher rate the same noise spreads over a wider
            // band and the S come out 3 dB (96 kHz) / 6 dB (192 kHz) softer. Leveled here, so every rate hears the
            // same voice (unleveled, AUTO rightly took less off the softer S: 3.2-3.3 dB, the mixed frames' limit)
            const auto take = makeTake (fs, 12.0, -16.0f + 10.0f * (float) std::log10 (fs / 48000.0));
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            const auto r = analyse (take.x.data(), (int) take.x.size(), fs);
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            std::printf ("    %.0f Hz: status %d, freq %.0f (peak %.0f), thr %.1f, range %.1f, S med %.1f / 90 %.1f, vowel95 %.1f, frames S %d V %d rej %d\n",
                         fs, (int) r.status, r.freq, r.peakHz, r.thresholdDb, r.rangeDb, r.sibilantMedianDb, r.sibilant90Db, r.vowel95Db,
                         r.sibilantFrames, r.vowelFrames, r.rejectedFrames);
            std::printf ("    S vs vowels %.1f dB -> target %.2f, S band -%.2f | vowels -%.2f pres -%.2f air -%.2f | mixed -%.2f pres -%.2f | %d renders, %.0f ms\n",
                         r.sibilanceVsVowelDb, r.targetDb, r.expectedReductionDb, r.vowelReductionDb, r.vowelPresenceDb, r.vowelAirDb,
                         r.mixedReductionDb, r.mixedPresenceDb, r.renders, ms);
            const juce::String at = " (" + juce::String (fs) + ")";
            check (r.status == AutoResult::Status::ok, "ok" + at);
            check (r.freq >= 4000.0f && r.freq <= 8000.0f, "corner in 4-8 kHz" + at);
            check (r.thresholdDb > r.vowel95Db && r.thresholdDb >= autoMinThresholdDb, "threshold above the vowels and the floor" + at);
            check (r.targetDb >= autoMinTargetDb && r.targetDb <= autoMaxTargetDb, "target within the AUTO window" + at);
            // AUTO reaches its target unless a voice limit holds it back. Since the split band has the same (matched)
            // shape at every rate (2026-11-23), the presence of the S next to a vowel binds on this synthetic take at
            // 44.1-192 kHz alike (before, the bell cramped near Nyquist cut less presence at 44.1 / 48 kHz). The 15 real
            // takes all reach their target (testRealTakes). Held back, AUTO still has to take a clear part of the S.
            check (r.expectedReductionDb <= r.targetDb + 0.6f
                       && (r.expectedReductionDb >= r.targetDb - 0.5f || (heldBack (r) && r.expectedReductionDb >= 1.0f)),
                   "S reach the target (rendered): " + juce::String (r.expectedReductionDb, 2) + " of " + juce::String (r.targetDb, 2) + at);
            check (r.mixedReductionDb <= autoMixedMaxDb + 0.01f && r.mixedPresenceDb <= autoMixedPresenceMaxDb + 0.01f, "vowel side of each S kept" + at);
            check (r.vowelReductionDb <= autoVowelMaxDb + 0.01f && r.vowelPresenceDb <= autoVowelPresenceMaxDb + 0.01f
                       && r.vowelAirDb <= autoVowelAirMaxDb + 0.01f, "vowels: full band, presence and air kept" + at);
            check (r.rangeDb >= autoMinRangeDb && r.rangeDb <= autoMaxRangeDb, "range in the AUTO window" + at);
            check (r.renders <= 24, "at most 24 renders (" + juce::String (r.renders) + ")" + at);

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
            check (sd > 0.3 && sd < autoMaxTargetDb + 1.0, "S reduced, subtly" + at);
            check (std::abs (vd) < 0.5, "vowels within 0.5 dB" + at);
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

        // Soft S far under the vowels: the threshold never goes below the floor (EMBER would stop being dynamic)
        {
            const auto soft = makeTake (48000.0, 12.0, -34.0f);
            const auto r = analyse (soft.x.data(), (int) soft.x.size(), 48000.0);
            std::printf ("    soft S: status %d thr %.1f range %.1f S -%.2f of %.2f\n", (int) r.status, r.thresholdDb, r.rangeDb, r.expectedReductionDb, r.targetDb);
            check (r.status != AutoResult::Status::ok || r.thresholdDb >= autoMinThresholdDb, "threshold floor -45 dB");
        }

        // Determinism: the same take twice gives the same result
        {
            const auto take = makeTake (48000.0, 12.0, -16.0f, -12.0f, true, 11);
            const auto a = analyse (take.x.data(), (int) take.x.size(), 48000.0);
            const auto b = analyse (take.x.data(), (int) take.x.size(), 48000.0);
            check (a.freq == b.freq && a.thresholdDb == b.thresholdDb && a.rangeDb == b.rangeDb && a.expectedReductionDb == b.expectedReductionDb,
                   "the same take twice: the same result");
        }

        // The centre of the S does not wander between excerpts of the same voice
        {
            const auto take = makeTake (48000.0, 40.0, -16.0f, -12.0f, true, 5);
            float lo = 1.0e9f, hi = 0.0f;
            for (double from : { 0.0, 9.0, 18.0, 27.0 })
            {
                const int a = (int) (from * 48000.0);
                const auto r = analyse (take.x.data() + a, (int) (12.0 * 48000.0), 48000.0);
                lo = std::min (lo, r.peakHz);
                hi = std::max (hi, r.peakHz);
            }
            check (hi / lo < 1.04f, "S centre over 4 excerpts within 4 % (" + juce::String (lo, 0) + "-" + juce::String (hi, 0) + " Hz)");
        }

        // Breaths and hi-hat bleed in the pauses are no S: the centre (and so the frequency) stays where the S are
        {
            const double fs = 48000.0;
            const auto clean = makeTake (fs, 12.0);
            auto dirty = clean;
            juce::Random rnd (99);
            Biq hp1, hp2, bp1, bp2;
            hp1.b.setCoeffs (vocalypse::dsp::design::highPass (fs, 9000.0, 0.54));
            hp2.b.setCoeffs (vocalypse::dsp::design::highPass (fs, 9000.0, 1.31));
            bp1.b.setCoeffs (vocalypse::dsp::design::highPass (fs, 1000.0, 0.71));
            bp2.b.setCoeffs (vocalypse::dsp::design::lowPass (fs, 9000.0, 0.71));
            const float hat = vocalypse::dsp::dbToGain (-16.0f), breath = vocalypse::dsp::dbToGain (-44.0f);
            for (size_t k = 0; k + 1 < clean.sRanges.size(); ++k)
            {
                const int pauseStart = clean.sRanges[k].second + (int) (fs * 0.015);
                // a hi-hat hit (bright, 30 ms decay) in every pause, a breath (dull noise, 60 ms) in every other one
                for (int i = 0; i < (int) (fs * 0.05) && pauseStart + i < (int) dirty.x.size(); ++i)
                {
                    const float w = rnd.nextFloat() * 2.0f - 1.0f;
                    dirty.x[(size_t) (pauseStart + i)] += hat * std::exp (-i / (float) (fs * 0.01)) * hp2.run (hp1.run (w));
                    if (k % 2 == 0 && i < (int) (fs * 0.045))
                        dirty.x[(size_t) (pauseStart + i)] += breath * bp2.run (bp1.run (rnd.nextFloat() * 2.0f - 1.0f));
                }
            }
            const auto a = analyse (clean.x.data(), (int) clean.x.size(), fs);
            const auto b = analyse (dirty.x.data(), (int) dirty.x.size(), fs);
            std::printf ("    bleed: centre %.0f -> %.0f Hz, freq %.0f -> %.0f, S frames %d -> %d, rejected %d\n",
                         a.peakHz, b.peakHz, a.freq, b.freq, a.sibilantFrames, b.sibilantFrames, b.rejectedFrames);
            check (b.status == AutoResult::Status::ok && std::abs (b.peakHz / a.peakHz - 1.0f) < 0.04f, "hi-hat + breath: S centre within 4 %");
            check (b.rejectedFrames > a.rejectedFrames, "the bleed frames are rejected");
        }

        // Stereo: the S are panned left (20 dB lower on the right). The plugin detects the louder channel and dips
        // both, so AUTO measures in stereo: what it predicts is what the plugin then does. A mono sum (until
        // 2026-11-23) heard the S 5 dB softer than the detector does and set AUTO up for a different voice.
        {
            const double fs = 48000.0;
            const auto take = makeTake (fs, 12.0);
            std::vector<float> l = take.x, rr = take.x, sum (take.x.size());
            for (auto [a, b] : take.sRanges)   // the S (with their fades) 20 dB lower on the right
                for (int i = std::max (0, a - (int) (fs * 0.012)); i < std::min ((int) rr.size(), b + (int) (fs * 0.012)); ++i)
                    rr[(size_t) i] *= 0.1f;
            for (size_t i = 0; i < sum.size(); ++i)
                sum[i] = 0.5f * (l[i] + rr[i]);
            const auto applied = [&] (const AutoResult& r)
            {
                Settings s;
                s.freq = r.freq;
                s.thresholdDb = r.thresholdDb;
                s.rangeDb = r.rangeDb;
                s.releaseMs = autoReleaseMs;
                E e (fs, s);
                std::vector<float> yl = l, yr = rr;
                for (int start = 0; start < (int) yl.size(); start += 512)
                {
                    float* p[2] { yl.data() + start, yr.data() + start };
                    e.process (p, 2, std::min (512, (int) yl.size() - start));
                }
                // the S band of the left channel's S, as AUTO judges it
                Biq h1, h2, g1, g2;
                h1.b.setCoeffs (vocalypse::dsp::design::highPass (fs, autoSBandHz, 0.54));
                h2.b.setCoeffs (vocalypse::dsp::design::highPass (fs, autoSBandHz, 1.31));
                g1.b.setCoeffs (vocalypse::dsp::design::highPass (fs, autoSBandHz, 0.54));
                g2.b.setCoeffs (vocalypse::dsp::design::highPass (fs, autoSBandHz, 1.31));
                std::vector<float> hx (l.size()), hy (yl.size());
                for (size_t i = 0; i < l.size(); ++i) hx[i] = h2.run (h1.run (l[i]));
                for (size_t i = 0; i < yl.size(); ++i) hy[i] = g2.run (g1.run (yl[i]));
                return rangesDb (hx, take.sRanges, 0) - rangesDb (hy, take.sRanges, e.getLatencySamples());
            };
            const float* ch[2] { l.data(), rr.data() };
            const auto r = analyse (ch, 2, (int) l.size(), fs, Settings {});
            const auto m = analyse (sum.data(), (int) sum.size(), fs);
            const double sd = applied (r), md = applied (m);
            std::printf ("    stereo: thr %.1f, predicted -%.2f, plugin -%.2f dB | mono sum: thr %.1f, predicted -%.2f, plugin -%.2f dB\n",
                         r.thresholdDb, r.expectedReductionDb, sd, m.thresholdDb, m.expectedReductionDb, md);
            check (r.status == AutoResult::Status::ok && r.numChannels == 2, "stereo take analysed in stereo");
            check (std::abs (sd - r.expectedReductionDb) < 0.5, "stereo: what AUTO predicts is what the plugin does");
            const float* same[2] { l.data(), l.data() };
            check (analyse (same, 2, (int) l.size(), fs, Settings {}).numChannels == 1, "two equal channels are analysed once");
        }

        const auto vowelsOnly = makeTake (48000.0, 8.0, -16.0f, -12.0f, false);
        check (analyse (vowelsOnly.x.data(), (int) vowelsOnly.x.size(), 48000.0).status == AutoResult::Status::noSibilance, "no S -> noSibilance");
        const auto shortTake = makeTake (48000.0, 2.0);
        check (analyse (shortTake.x.data(), (int) shortTake.x.size(), 48000.0).status == AutoResult::Status::tooShort, "2 s -> tooShort");
    }

    // How long AUTO thinks, and how fast it stops when the plugin closes
    void testAutoTiming()
    {
        Test t ("AUTO analysis time, cancel");
        for (double fs : { 48000.0, 192000.0 })
            for (double secs : { 12.0, 30.0 })
            {
                const auto take = makeTake (fs, secs);
                const auto right = rightOf (take.x);
                const float* ch[2] { take.x.data(), right.data() };
                const double t0 = juce::Time::getMillisecondCounterHiRes();
                const auto r = analyse (ch, 2, (int) take.x.size(), fs, Settings {});
                const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
                std::printf ("    %3.0f kHz %2.0f s stereo: %6.0f ms, %d renders (status %d)\n", fs / 1000.0, secs, ms, r.renders, (int) r.status);
                check (r.status == AutoResult::Status::ok, "ok at " + juce::String (fs) + " / " + juce::String (secs) + " s");
            }
        // cancel: a 30 s take at 192 kHz stops within a few ms of the flag
        {
            const double fs = 192000.0;
            const auto take = makeTake (fs, 30.0);
            std::atomic<bool> cancel { false };
            std::atomic<double> doneAt { 0.0 };
            AutoResult r;
            std::thread th ([&] { r = analyse (take.x.data(), (int) take.x.size(), fs, Settings {}, &cancel); doneAt = juce::Time::getMillisecondCounterHiRes(); });
            juce::Thread::sleep (300);
            const double c0 = juce::Time::getMillisecondCounterHiRes();
            cancel = true;
            th.join();
            const double latency = doneAt.load() - c0;
            std::printf ("    cancel after 300 ms: stopped %.1f ms later\n", latency);
            check (r.status == AutoResult::Status::cancelled && latency < 100.0, "cancelled within 100 ms");
        }
    }

    void testCapture()
    {
        Test t ("AUTO capture: pauses cut to a gap, stereo, block size, rate change, threads");
        {
            AutoCapture c;
            c.prepare (8000.0);
            c.start();
            std::vector<float> loud (4000, 0.3f), quiet (16000, 0.0001f);
            const float* chL[1] { loud.data() };
            const float* chQ[1] { quiet.data() };
            c.push (chL, 1, 4000);
            c.push (chQ, 1, 16000);   // 2 s pause: cut to the gap
            c.push (chL, 1, 4000);
            near (c.getSeconds(), 1.0 + autoGapSeconds, 0.011, "a long pause shrinks to the gap");
            near (c.getVoicedSeconds(), 1.0, 0.011, "voice counted without it");
            for (int i = 0; i < 40 && c.isCapturing(); ++i)
                c.push (chL, 1, 4000);
            check (c.isFull() && ! c.isCapturing(), "stops when full");
            c.stop();
            check (c.copyTake().numChannels == 1, "equal channels: one channel");
        }
        // the same audio in blocks of 64, 512, 2048 (and an odd 333): the same take, so the same AUTO result
        {
            const double fs = 48000.0;
            auto take = makeTake (fs, 15.0);
            for (double at : { 3.0, 8.0 })   // two long pauses
                std::fill_n (take.x.begin() + (int) (at * fs), (int) (1.2 * fs), 0.0f);
            const auto right = rightOf (take.x);
            const float* ch[2] { take.x.data(), right.data() };
            AutoCapture::Take ref;
            const auto a = autoViaCapture (ch, 2, (int) take.x.size(), fs, 512, {}, 1.0e9, &ref);
            check (ref.numChannels == 2, "different channels: stereo take");
            for (int block : { 64, 333, 2048 })
            {
                AutoCapture::Take other;
                const auto b = autoViaCapture (ch, 2, (int) take.x.size(), fs, block, {}, 1.0e9, &other);
                check (other.channel[0] == ref.channel[0] && other.channel[1] == ref.channel[1], "block " + juce::String (block) + ": the same take");
                check (b.freq == a.freq && b.thresholdDb == a.thresholdDb && b.rangeDb == a.rangeDb
                           && std::abs (b.expectedReductionDb - a.expectedReductionDb) < 1.0e-4f,
                       "block " + juce::String (block) + ": the same AUTO result");
            }
            std::printf ("    blocks 64/333/512/2048: freq %.0f thr %.1f range %.1f, take %.2f s (voice %.2f s)\n", a.freq, a.thresholdDb, a.rangeDb,
                         ref.numSamples() / fs, a.seconds);
        }
        // a new sample rate while listening: listening starts again at that rate (not "too short")
        {
            AutoCapture c;
            c.prepare (48000.0);
            c.start();
            std::vector<float> loud (4800, 0.3f);
            const float* ch[1] { loud.data() };
            for (int i = 0; i < 10; ++i)
                c.push (ch, 1, 4800);
            c.prepare (48000.0);
            check (c.getNumSamples() == 48000 && c.isCapturing(), "the same rate: left alone");
            c.prepare (96000.0);
            check (c.isCapturing() && c.getNumSamples() == 0 && c.getRestarts() == 1 && c.getSampleRate() == 96000.0, "a new rate: listening again");
            c.push (ch, 1, 4800);
            near (c.getSeconds(), 0.05, 0.003, "at the new rate");
        }
        // the audio thread pushes while the message thread starts, stops and copies: no torn take, no crash
        {
            AutoCapture c;
            c.prepare (48000.0);
            std::atomic<bool> run { true };
            std::vector<float> l (480, 0.2f), r (480, -0.2f);
            std::thread audio ([&]
            {
                const float* ch[2] { l.data(), r.data() };
                while (run.load())
                    c.push (ch, 2, 480);
            });
            bool consistent = true;
            for (int i = 0; i < 300; ++i)
            {
                c.start();
                juce::Thread::yield();
                c.stop();
                const auto take = c.copyTake();
                if (take.numSamples() > 0)
                    consistent = consistent && take.numChannels == 2 && take.channel[0].back() == 0.2f && take.channel[1].back() == -0.2f;
            }
            run = false;
            audio.join();
            check (consistent, "start / stop / copy against a pushing audio thread");
        }
    }

    // The processor around AUTO: what happens when the user (or the host) does something while AUTO works
    void testAutoProcessor()
    {
        Test t ("AUTO in the processor: cancel, loads and changes while analysing, rate change");
        const auto take = makeTake (48000.0, 13.0);
        const auto right = rightOf (take.x);
        const auto feed = [&] (EmberAudioProcessor& p, double fs, double seconds)
        {
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            const int len = std::min ((int) take.x.size(), (int) (seconds * fs));
            for (int start = 0; start + 512 <= len; start += 512)
            {
                buf.copyFrom (0, 0, take.x.data() + start, 512);
                buf.copyFrom (1, 0, right.data() + start, 512);
                p.processBlock (buf, midi);
            }
        };
        const auto setReal = [] (EmberAudioProcessor& p, const char* id, float v)
        {
            if (auto* prm = p.state.getParameter (id))
                prm->setValueNotifyingHost (prm->convertTo0to1 (v));
        };
        const auto real = [] (EmberAudioProcessor& p, const char* id) { return p.state.getRawParameterValue (id)->load(); };
        using Phase = EmberAudioProcessor::AutoPhase;

        // the AUTO button cancels while analysing; closing the plugin never waits for the analysis
        {
            auto p = std::make_unique<EmberAudioProcessor>();
            p->prepareToPlay (48000.0, 512);
            setReal (*p, pid::threshold, -20.0f);
            p->startAuto();
            feed (*p, 48000.0, 13.0);
            p->finishAuto();
            p->cancelAuto();
            check (p->getAutoPhase() == Phase::idle, "cancel while analysing: idle");
            p->waitForAuto();
            near (real (*p, pid::threshold), -20.0, 0.05, "nothing applied");
            p->startAuto();
            feed (*p, 48000.0, 13.0);
            p->finishAuto();
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            p.reset();
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            std::printf ("    closing while analysing took %.1f ms\n", ms);
            check (ms < 150.0, "closing while analysing: quick (" + juce::String (ms, 1) + " ms)");
        }
        // a project loaded while analysing: its values stay, the analysis is dropped, no old result is shown
        {
            EmberAudioProcessor src;
            setReal (src, pid::threshold, -25.0f);
            juce::MemoryBlock mb;
            src.getStateInformation (mb);
            EmberAudioProcessor p;
            p.prepareToPlay (48000.0, 512);
            p.startAuto();
            feed (p, 48000.0, 13.0);
            p.finishAuto();
            p.setStateInformation (mb.getData(), (int) mb.getSize());
            check (p.getAutoPhase() == Phase::idle, "project loaded while analysing: dropped");
            p.waitForAuto();
            near (real (p, pid::threshold), -25.0, 0.05, "the project's threshold stays");
            // after a finished AUTO a loaded project clears its message
            p.startAuto();
            feed (p, 48000.0, 13.0);
            p.finishAuto();
            p.waitForAuto();
            check (p.getAutoPhase() == Phase::done, "AUTO done");
            p.setStateInformation (mb.getData(), (int) mb.getSize());
            check (p.getAutoPhase() == Phase::idle && p.getAutoMessage().isEmpty() && p.getLastAuto().status != AutoResult::Status::ok,
                   "project loaded after AUTO: no old result");
        }
        // a preset or the other A/B slot while analysing: dropped as well
        {
            EmberAudioProcessor p;
            p.prepareToPlay (48000.0, 512);
            p.startAuto();
            feed (p, 48000.0, 13.0);
            p.finishAuto();
            p.loadFactoryPreset (3);
            const float presetThr = real (p, pid::threshold);
            p.waitForAuto();
            check (p.getAutoPhase() == Phase::idle, "preset while analysing: dropped");
            near (real (p, pid::threshold), presetThr, 0.05, "the preset stays");

            p.switchSlot (1);
            setReal (p, pid::threshold, -22.0f);
            p.startAuto();
            feed (p, 48000.0, 13.0);
            p.finishAuto();
            p.switchSlot (0);
            const float aThr = real (p, pid::threshold);
            p.waitForAuto();
            check (p.getAutoPhase() == Phase::idle, "A/B while analysing: dropped");
            near (real (p, pid::threshold), aThr, 0.05, "slot A stays");
        }
        // mode changed while analysing: measured again with it, on the same take
        {
            EmberAudioProcessor p;
            p.prepareToPlay (48000.0, 512);
            p.startAuto();
            feed (p, 48000.0, 13.0);
            p.finishAuto();
            setReal (p, pid::mode, 1.0f);
            p.pollAuto();
            check (p.getAutoRuns() == 2 && p.getAutoPhase() == Phase::analysing, "mode changed: analysed again");
            p.waitForAuto();
            check (p.getAutoPhase() == Phase::done && real (p, pid::mode) > 0.5f, "done, wideband kept");
            check (p.getLastAuto().numChannels == 2, "measured in stereo");
        }
        // a new sample rate while listening: it listens again, and finishes normally
        {
            EmberAudioProcessor p;
            p.prepareToPlay (48000.0, 512);
            p.startAuto();
            feed (p, 48000.0, 4.0);
            p.prepareToPlay (96000.0, 512);
            p.updateAuto (100.0);
            check (p.getAutoPhase() == Phase::listening && p.getCapture().getNumSamples() == 0, "rate change: listening again");
            const auto t96 = makeTake (96000.0, 13.0);
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            for (int start = 0; start + 512 <= (int) t96.x.size(); start += 512)
            {
                for (int c = 0; c < 2; ++c)
                    buf.copyFrom (c, 0, t96.x.data() + start, 512);
                p.processBlock (buf, midi);
            }
            p.updateAuto (101.0);
            p.updateAuto (101.5 + autoQuietSeconds);   // playback stopped
            check (p.getAutoPhase() == Phase::analysing, "voice at the new rate, then a stop: analysing");
            p.waitForAuto();
            check (p.getAutoPhase() == Phase::done, "and done");
        }
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
            check (c.getNumSamples() > 3000 && c.getNumSamples() <= 4096, "audio captured (whole 5 ms chunks)");
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

    // ================================================================ fix/ember-dsp (2026-11-23)
    // Hann-windowed DFT power of x[from, to) at hz
    double tonePower (const std::vector<float>& x, int from, int to, double hz, double fs)
    {
        const int n = to - from;
        const double w = juce::MathConstants<double>::twoPi * hz / fs;
        double re = 0.0, im = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double win = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / (n - 1));
            re += win * x[(size_t) (from + i)] * std::cos (w * i);
            im -= win * x[(size_t) (from + i)] * std::sin (w * i);
        }
        return re * re + im * im;
    }

    // Amplitude of the component at hz around every sample (3 ms Hann window ending there)
    std::vector<float> amplitudeAt (const std::vector<float>& y, double hz, double fs)
    {
        const int n = (int) (fs * 0.003);
        std::vector<float> a (y.size(), 0.0f);
        double norm = 0.0;
        for (int k = 0; k < n; ++k)
            norm += 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * k / (n - 1));
        for (int i = n; i < (int) y.size(); i += 8)
        {
            double re = 0.0, im = 0.0;
            for (int k = 0; k < n; ++k)
            {
                const double win = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * k / (n - 1));
                const double ph = juce::MathConstants<double>::twoPi * hz * (i - n + k) / fs;
                re += win * y[(size_t) (i - n + k)] * std::cos (ph);
                im -= win * y[(size_t) (i - n + k)] * std::sin (ph);
            }
            for (int j = 0; j < 8 && i + j < (int) y.size(); ++j)
                a[(size_t) (i + j)] = (float) (2.0 * std::sqrt (re * re + im * im) / norm);
        }
        return a;
    }

    // Stereo run (separate channels), block-wise; optional change of settings at sample `at`
    std::pair<std::vector<float>, std::vector<float>> runStereo (Engine& e, std::vector<float> l, std::vector<float> r, int block = 256,
                                                                 int at = -1, const Settings* change = nullptr, std::vector<float>* trace = nullptr)
    {
        for (int start = 0; start < (int) l.size(); start += block)
        {
            if (change != nullptr && start <= at && at < start + block)
                e.setSettings (*change);
            const int n = std::min (block, (int) l.size() - start);
            float* ch[2] { l.data() + start, r.data() + start };
            e.process (ch, 2, n);
            if (trace != nullptr)
                trace->push_back (e.currentReductionDb.load());
        }
        return { l, r };
    }

    void testSmoothBand()
    {
        Test t ("split: the band's gain moves every sample (no 16-sample steps, no zipper)");
        const double fs = 44100.0;
        // a steady tone in the middle of the band whose level swells 3 times a second: the reduction follows it the
        // whole time (fast attack and release), so coefficient steps every 16 samples would show as sidebands at fs/16
        const double f0 = 6200.0 * splitCenterRatio;
        std::vector<float> x ((size_t) (fs * 2.0));
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double tt = (double) i / fs;
            const double env = 0.02 + 0.3 * (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * tt * 3.0));
            x[i] = (float) (env * std::sin (juce::MathConstants<double>::twoPi * f0 * tt));
        }
        Settings s;
        s.thresholdDb = -45.0f;
        s.rangeDb = 12.0f;
        s.attackMs = 0.3f;
        s.releaseMs = 20.0f;
        s.smart = false;
        s.relative = false;
        E e (fs, s);
        const auto y = run (e, x);
        const int from = (int) (fs * 0.5), to = (int) (fs * 1.5);
        const double tone = tonePower (y, from, to, f0, fs);
        double worst = 0.0;
        for (double side : { f0 - fs / 16.0, f0 + fs / 16.0, f0 - fs / 8.0, f0 + fs / 8.0 })
            worst = std::max (worst, tonePower (y, from, to, side, fs));
        const double db = 10.0 * std::log10 (worst / tone);
        const double lossDb = 10.0 * std::log10 (tonePower (x, from - e.getLatencySamples(), to - e.getLatencySamples(), f0, fs) / tone);
        std::printf ("    tone -%.1f dB, sidebands at fs/16 and fs/8: %.1f dB under the tone\n", lossDb, db);
        check (lossDb > 4.0, "the tone is reduced");
        check (db < -90.0, "no zipper sidebands (< -90 dB)");
    }

    void testBandShape()
    {
        Test t ("split band: the same shape at 44.1 - 192 kHz (matched design, no cramping)");
        double worstLow = 0.0, worstHigh = 0.0, oldWorst = 0.0;
        for (float freq : { 5000.0f, 6200.0f, 8000.0f })
            for (float width : { 1.5f, 3.0f, 4.0f })
                for (double hz : { 2000.0, 3000.0, 5000.0, 7000.0, 9000.0, 12000.0, 15000.0, 18000.0 })
                {
                    const double ref = splitResponseDb (192000.0, freq, width, 6.0, hz);
                    for (double fs : { 44100.0, 48000.0, 96000.0 })
                    {
                        const double d = std::abs (splitResponseDb (fs, freq, width, 6.0, hz) - ref);
                        (hz <= 12000.0 ? worstLow : worstHigh) = std::max (hz <= 12000.0 ? worstLow : worstHigh, d);
                    }
                    // the old bilinear bell (RBJ, Q 0.4 at 1.25 x freq) for comparison
                    const auto o44 = design::peak (44100.0, freq * 1.25, 0.4, -6.0), o192 = design::peak (192000.0, freq * 1.25, 0.4, -6.0);
                    oldWorst = std::max (oldWorst, (double) std::abs (gainToDb ((float) design::magnitude (o44, hz, 44100.0))
                                                             - gainToDb ((float) design::magnitude (o192, hz, 192000.0))));
                }
        std::printf ("    largest difference to 192 kHz: %.2f dB up to 12 kHz, %.2f dB up to 18 kHz (old bilinear bell: %.2f dB)\n",
                     worstLow, worstHigh, oldWorst);
        check (worstLow < 0.3, "within 0.3 dB up to 12 kHz");
        check (worstHigh < 1.0, "within 1 dB up to 18 kHz");
        // the reduction asked for is what the band takes at its centre
        const auto shape = splitBand (6200.0, 3.0);
        near (splitResponseDb (48000.0, 6200.0, 3.0, 5.0, shape.centreHz), -5.0, 0.05, "centre takes the reduction");
        near (splitResponseDb (48000.0, 6200.0, 3.0, 0.0, 3000.0), 0.0, 1.0e-9, "0 dB = flat");
        // width: the half-reduction points sit `width` octaves apart
        const double lo = splitResponseDb (192000.0, 6200.0, 2.0, 6.0, splitBand (6200.0, 2.0).lowHz, false);
        near (lo, -3.0, 0.15, "half the reduction at the lower edge");
        check (splitResponseDb (48000.0, 6200.0, 1.5, 6.0, 14000.0) > splitResponseDb (48000.0, 6200.0, 4.0, 6.0, 14000.0) + 2.0,
               "narrow leaves the air, wide takes it");
        const double under = splitResponseDb (48000.0, 6200.0, 4.0, 6.0, 1500.0);
        std::printf ("    widest band at 1.5 kHz: %.2f dB (6 dB at the centre)\n", under);
        check (splitBand (6200.0, maxWidthOct).lowHz >= splitLowestRatio * 6200.0 - 0.01 && under > -1.5,
               "even wide, the band's lower edge stays at 0.45 x freq (gentle skirt under it)");
    }

    void testStereo()
    {
        Test t ("stereo link 0..100 %, Stereo / Mid-Side / Mid / Side");
        const double fs = 48000.0;
        const auto take = makeTake (fs, 3.0);
        const int L = Engine::latencyFor (fs);
        std::vector<float> tone (take.x.size());
        for (size_t i = 0; i < tone.size(); ++i)
            tone[i] = 0.05f * (float) std::sin (juce::MathConstants<double>::twoPi * 300.0 * i / fs);
        const auto exactly = [&] (const std::vector<float>& out, const std::vector<float>& in) { return maxDiffDelayed (out, in, L) == 0.0f; };

        // S only on the left; the right carries a steady tone
        for (float link : { 0.0f, 0.5f, 1.0f })
        {
            Settings s;
            s.thresholdDb = -40.0f;
            s.mode = 1;
            s.stereoLink = link;
            E e (fs, s);
            const auto [l, r] = runStereo (e, take.x, tone);
            const double sLoss = rangesDb (take.x, take.sRanges, 0) - rangesDb (l, take.sRanges, L);
            const double rLoss = rangesDb (tone, take.sRanges, 0) - rangesDb (r, take.sRanges, L);
            std::printf ("    link %3.0f %%: left S -%.2f dB, right (no S) -%.2f dB during the S\n", link * 100.0f, sLoss, rLoss);
            check (sLoss > 3.0, "left S reduced (link " + juce::String (link) + ")");
            if (link == 0.0f)
                check (exactly (r, tone), "link 0 %: the right channel is untouched (bit-exact)");
            if (link == 1.0f)
                check (rLoss > 3.0, "link 100 %: the right channel dips with the left");
            if (link == 0.5f)
                check (rLoss > 0.3 && rLoss < sLoss - 1.0, "link 50 %: in between");
        }

        // S purely in the side (anti-phase), the vowels in the mid
        std::vector<float> sOnly (take.x.size(), 0.0f), vOnly (take.x.size(), 0.0f);
        for (auto [a, b] : take.sRanges)
            for (int i = a - (int) (fs * 0.012); i < b + (int) (fs * 0.012) && i < (int) sOnly.size(); ++i)
                sOnly[(size_t) i] = take.x[(size_t) i];
        for (size_t i = 0; i < vOnly.size(); ++i)
            vOnly[i] = take.x[i] - sOnly[i];
        std::vector<float> left (take.x.size()), right (take.x.size());
        for (size_t i = 0; i < left.size(); ++i)
        {
            left[i] = vOnly[i] + sOnly[i];
            right[i] = vOnly[i] - sOnly[i];
        }
        const auto sideLoss = [&] (const std::vector<float>& l, const std::vector<float>& r)
        {
            std::vector<float> side (l.size()), sideIn (l.size());
            for (size_t i = 0; i < l.size(); ++i)
            {
                side[i] = 0.5f * (l[i] - r[i]);
                sideIn[i] = 0.5f * (left[i] - right[i]);
            }
            return rangesDb (sideIn, take.sRanges, 0) - rangesDb (side, take.sRanges, L);
        };
        const auto midError = [&] (const std::vector<float>& l, const std::vector<float>& r)
        {
            float m = 0.0f;
            for (size_t i = (size_t) L; i < l.size(); ++i)
                m = std::max (m, std::abs (0.5f * (l[i] + r[i]) - vOnly[i - (size_t) L]));
            return m;
        };
        for (int mode : { 0, 1, 2, 3 })
        {
            Settings s;
            s.thresholdDb = -40.0f;
            s.stereoMode = mode;
            s.stereoLink = 0.0f;
            E e (fs, s);
            const auto [l, r] = runStereo (e, left, right);
            const double loss = sideLoss (l, r);
            const float mErr = midError (l, r);
            std::printf ("    %s: side S -%.2f dB, mid changed by at most %.6f\n", stereoChoices[mode].toRawUTF8(), loss, mErr);
            if (mode == 2)
                check (exactly (l, left) && exactly (r, right), "Mid: an S only in the side is left alone (bit-exact)");
            else
                check (loss > 3.0, stereoChoices[mode] + ": the side S is reduced");
            if (mode == 1 || mode == 3)
                check (mErr < 1.0e-5f, stereoChoices[mode] + ": the mid stays untouched");
        }

        // no reduction: bit-exact in every stereo mode and link
        for (int mode : { 0, 1, 2, 3 })
            for (float link : { 0.0f, 0.3f, 1.0f })
            {
                Settings s;
                s.stereoMode = mode;
                s.stereoLink = link;
                std::vector<float> a (tone), b (tone.size());
                for (size_t i = 0; i < b.size(); ++i)
                    b[i] = 0.7f * (float) std::sin (juce::MathConstants<double>::twoPi * 500.0 * i / fs);
                E e (fs, s);
                const auto [l, r] = runStereo (e, a, b);
                check (exactly (l, a) && exactly (r, b), "null: " + stereoChoices[mode] + ", link " + juce::String (link));
            }
    }

    void testListenBand()
    {
        Test t ("listen S band = what is processed (split band / detector band in wideband)");
        const double fs = 48000.0;
        const auto toneThrough = [&] (int mode, double hz)
        {
            std::vector<float> x ((size_t) fs);
            for (size_t i = 0; i < x.size(); ++i)
                x[i] = 0.1f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / fs);
            Settings s;
            s.listen = 1;
            s.mode = mode;
            E e (fs, s);
            const auto y = run (e, x);
            return energyDb (y, (int) (fs * 0.5), (int) fs) - energyDb (x, (int) (fs * 0.5), (int) fs);
        };
        const auto shape = splitBand (6200.0, defaultWidthOct);
        const double centre = toneThrough (0, shape.centreHz), low = toneThrough (0, 1000.0), wideLow = toneThrough (1, 3100.0),
                     wideHigh = toneThrough (1, 12400.0);
        std::printf ("    split: centre %.1f dB, 1 kHz %.1f dB | wideband: 3.1 kHz %.1f dB, 12.4 kHz %.1f dB\n", centre, low, wideLow, wideHigh);
        check (std::abs (centre) < 1.0, "split: the band centre is heard at its level");
        check (low < -40.0, "split: the voice under the band is gone");
        check (wideLow < -20.0 && std::abs (wideHigh) < 1.0, "wideband: the detector band (from Frequency up)");
    }

    void testSwitches()
    {
        Test t ("switching mode, listen, lookahead, stereo mode cross-fades (no clicks)");
        const double fs = 48000.0;
        // steady voice (2 kHz) under a steady S (9 kHz), plain relative detection: reduction on all the time
        const int n = (int) fs;
        std::vector<float> x ((size_t) n), x2 ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const double tt = i / fs;
            x[(size_t) i] = (float) (0.2 * std::sin (juce::MathConstants<double>::twoPi * 2000.0 * tt) + 0.3 * std::sin (juce::MathConstants<double>::twoPi * 9000.0 * tt));
            x2[(size_t) i] = (float) (0.15 * std::sin (juce::MathConstants<double>::twoPi * 2000.0 * tt + 1.0) + 0.1 * std::sin (juce::MathConstants<double>::twoPi * 9000.0 * tt));
        }
        Settings base;
        base.smart = false;
        base.thresholdDb = -50.0f;
        base.rangeDb = 12.0f;
        base.freq = 7000.0f;
        const int at = n / 2;
        // the 2 kHz amplitude around the switch: how much of its total change happens within 1 ms at most
        const auto steepest = [&] (const std::vector<float>& y)
        {
            const auto a = amplitudeAt (y, 2000.0, fs);
            const int ms = (int) (fs * 0.001);
            float total = std::abs (a[(size_t) (at + (int) (fs * 0.05))] - a[(size_t) (at - (int) (fs * 0.005))]), worst = 0.0f, bump = 0.0f;
            for (int i = at - ms * 5; i < at + ms * 40; ++i)
            {
                worst = std::max (worst, std::abs (a[(size_t) (i + ms)] - a[(size_t) i]));
                bump = std::max (bump, std::abs (a[(size_t) i] - a[(size_t) (at - ms * 5)]));
            }
            return std::make_tuple (worst, total, bump);
        };
        struct Case { const char* name; std::function<void (Settings&)> from, to; };
        const Case cases[] {
            { "split -> wideband", [] (Settings&) {}, [] (Settings& s) { s.mode = 1; } },
            { "wideband -> split", [] (Settings& s) { s.mode = 1; }, [] (Settings& s) { s.mode = 0; } },
            { "listen off -> delta (wideband)", [] (Settings& s) { s.mode = 1; }, [] (Settings& s) { s.mode = 1; s.listen = 2; } },
            { "listen delta -> S band (wideband)", [] (Settings& s) { s.mode = 1; s.listen = 2; }, [] (Settings& s) { s.mode = 1; s.listen = 1; } },
            { "stereo -> mid/side (wideband)", [] (Settings& s) { s.mode = 1; }, [] (Settings& s) { s.mode = 1; s.stereoMode = 1; } },
        };
        for (const auto& c : cases)
        {
            Settings a = base, b = base;
            c.from (a);
            c.to (b);
            E e (fs, a);
            const auto [l, r] = runStereo (e, x, x2, 64, at, &b);
            const auto [worst, total, bump] = steepest (l);
            const float ref = std::max (total, bump);
            std::printf ("    %-32s 2 kHz moves %.3f in all, at most %.3f per ms (%.0f %%)\n", c.name, ref, worst, ref > 0 ? 100.0f * worst / ref : 0.0f);
            check (ref > 0.02f, juce::String (c.name) + ": the switch changes the sound");
            check (worst < 0.2f * ref, juce::String (c.name) + ": spread over more than 5 ms");
        }

        // lookahead 0 -> 10 ms in the middle of S bursts: the applied reduction never jumps
        {
            const auto take = makeTake (fs, 2.0);
            Settings a;
            a.thresholdDb = -45.0f;
            a.lookaheadMs = 0.0f;
            a.mode = 1;
            Settings b = a;
            b.lookaheadMs = maxLookaheadMs;
            std::vector<float> trRef, trSw;
            E e1 (fs, a), e2 (fs, a);
            runStereo (e1, take.x, take.x, 1, -1, nullptr, &trRef);
            // just before an S starts: 10 ms ahead the envelope is already up
            const int sw = take.sRanges[3].first - (int) (fs * 0.015);
            runStereo (e2, take.x, take.x, 1, sw, &b, &trSw);
            float stepRef = 0.0f, stepSw = 0.0f;
            for (size_t i = 1; i < trRef.size(); ++i)
                stepRef = std::max (stepRef, std::abs (trRef[i] - trRef[i - 1]));
            for (int i = sw; i < sw + (int) (fs * 0.03); ++i)
                stepSw = std::max (stepSw, std::abs (trSw[(size_t) i] - trSw[(size_t) i - 1]));
            std::printf ("    lookahead 0 -> 10 ms during an S: largest step of the reduction %.3f dB per sample (without switching %.3f)\n", stepSw, stepRef);
            check (stepSw <= stepRef * 1.2f + 0.02f, "lookahead switch: no jump in the reduction");
        }
    }

    // 2026-11-24 (r2 research): the split band follows each S (three fixed bells, blended by a tracker)
    void testTracking()
    {
        Test t ("S tracking: the split band moves onto a low or a high S, null and switch stay clean");
        {
            float w[3];
            const float mid[3] { 1.0f, 1.0f, 1.0f }, lo[3] { 1.0f, 0.0f, 0.0f }, hi[3] { 0.0f, 0.0f, 1.0f }, none[3] {};
            trackWeights (mid, w);
            check (w[0] == 0.0f && w[1] == 1.0f && w[2] == 0.0f, "even energy -> the middle bell");
            trackWeights (lo, w);
            check (w[0] == 1.0f && w[1] == 0.0f, "low S -> the low bell");
            trackWeights (hi, w);
            check (w[2] == 1.0f && w[1] == 0.0f, "high S -> the high bell");
            trackWeights (none, w);
            check (w[1] == 1.0f, "silence -> the middle bell");
            const float a[3] { 0.2f, 0.5f, 0.3f };
            trackWeights (a, w);
            near (w[0] + w[1] + w[2], 1.0, 1.0e-6, "weights sum to 1");
        }
        const auto tb = trackBells (6200.0, 3.0);
        near (tb.widthOct, 2.0, 1.0e-9, "bells 1 octave narrower than the band at Width 3");
        near (tb.centreHz[1], splitBand (6200.0, 3.0).centreHz, 1.0e-6, "middle bell on the band centre");
        near (std::log2 (tb.centreHz[2] / tb.centreHz[0]), 2.0 * trackSpreadOct, 1.0e-9, "outer bells +- half an octave");
        near (trackBells (6200.0, 1.0).widthOct, (double) trackMinBellOct, 1.0e-9, "narrowest bell 1 octave");

        // A steady "S" (strong narrow noise) on a quiet white probe, constant 6 dB reduction (absolute detection):
        // where does the reduction sit? Tracked, it moves with the S; the fixed bell stays put.
        const double fs = 48000.0;
        const auto bandDb = [fs] (const std::vector<float>& v, double lo, double hi, int from)
        {
            Biq h1, h2, l1, l2;
            h1.b.setCoeffs (design::highPass (fs, lo, 0.54)); h2.b.setCoeffs (design::highPass (fs, lo, 1.31));
            l1.b.setCoeffs (design::lowPass (fs, hi, 0.54)); l2.b.setCoeffs (design::lowPass (fs, hi, 1.31));
            double s = 0.0;
            for (int i = 0; i < (int) v.size(); ++i)
            {
                const float y = l2.run (l1.run (h2.run (h1.run (v[(size_t) i]))));
                if (i >= from) s += (double) y * y;
            }
            return 10.0 * std::log10 (s + 1.0e-30);
        };
        const auto make = [fs] (double sHz)
        {
            juce::Random rnd (5);
            Biq b1, b2;
            b1.b.setCoeffs (design::highPass (fs, sHz / 1.2, 1.31));
            b2.b.setCoeffs (design::lowPass (fs, sHz * 1.2, 1.31));
            std::vector<float> x ((size_t) (fs * 2.0));
            for (auto& v : x)
            {
                const float n = rnd.nextFloat() * 2.0f - 1.0f;
                v = 0.3f * b2.run (b1.run (n)) + 0.003f * (rnd.nextFloat() * 2.0f - 1.0f);
            }
            return x;
        };
        double tilt[2][2];   // [S low / high][fixed / tracked]: loss at 4.5-6 kHz minus loss at 10-13 kHz
        for (int which = 0; which < 2; ++which)
        {
            const auto x = make (which == 0 ? 5200.0 : 11500.0);
            for (int tr = 0; tr < 2; ++tr)
            {
                Settings s;
                s.smart = false;
                s.relative = false;
                s.thresholdDb = -50.0f;
                s.rangeDb = 6.0f;
                s.trackS = tr == 1;
                E e (fs, s);
                const auto y = run (e, x);
                const int L = e.getLatencySamples(), from = (int) (fs * 0.5);
                std::vector<float> xd (x.size(), 0.0f);
                for (size_t i = (size_t) L; i < x.size(); ++i)
                    xd[i] = x[i - (size_t) L];
                tilt[which][tr] = (bandDb (xd, 4500.0, 6000.0, from) - bandDb (y, 4500.0, 6000.0, from))
                                - (bandDb (xd, 10000.0, 13000.0, from) - bandDb (y, 10000.0, 13000.0, from));
            }
        }
        std::printf ("    reduction 4.5-6 kHz minus 10-13 kHz: low S fixed %.2f / tracked %.2f dB, high S fixed %.2f / tracked %.2f dB\n",
                     tilt[0][0], tilt[0][1], tilt[1][0], tilt[1][1]);
        check (tilt[0][1] > tilt[0][0] + 1.0, "low S: the tracked band moves down");
        check (tilt[1][1] < tilt[1][0] - 1.0, "high S: the tracked band moves up");

        // null with tracking (no reduction = bit-exact), and switching tracking while running is click-free enough
        {
            auto take = makeTake (fs, 3.0);
            Settings s;
            s.rangeDb = 0.0f;
            E e (fs, s);
            check (maxDiffDelayed (run (e, take.x), take.x, e.getLatencySamples()) == 0.0f, "tracked, no reduction: bit-exact");
        }
    }

    void testAllround()
    {
        Test t ("Allround: de-esses inside a mix, where comparing against the voice body fails");
        const double fs = 48000.0;
        auto take = makeTake (fs, 6.0);
        // a loud bed under the voice (band-limited noise up to 2.5 kHz, about the level of the vowels)
        juce::Random rnd (21);
        Biq lp1, lp2, hp;
        lp1.b.setCoeffs (design::lowPass (fs, 2500.0, 0.54));
        lp2.b.setCoeffs (design::lowPass (fs, 2500.0, 1.31));
        hp.b.setCoeffs (design::highPass (fs, 60.0, 0.7));
        std::vector<float> mix = take.x;
        for (auto& v : mix)
            v += 1.0f * lp2.run (lp1.run (hp.run (rnd.nextFloat() * 2.0f - 1.0f)));
        double loss[2], vLoss[2];
        for (int allround = 0; allround < 2; ++allround)
        {
            Settings s;
            s.thresholdDb = -50.0f;   // in a mix the S sit lower against the full level
            s.allround = allround == 1;
            E e (fs, s);
            const auto y = run (e, mix);
            const int L = e.getLatencySamples();
            // the S judged in the S band (above 4 kHz): over the full band the loud bed under the S hides how much
            // of the S itself goes (2026-11-24: the tracked band also leaves more of the bed at 2.8-4 kHz alone)
            const auto sBand = [fs] (const std::vector<float>& v)
            {
                Biq h1, h2;
                h1.b.setCoeffs (design::highPass (fs, 4000.0, 0.54));
                h2.b.setCoeffs (design::highPass (fs, 4000.0, 1.31));
                std::vector<float> o (v);
                for (auto& x : o) x = h2.run (h1.run (x));
                return o;
            };
            loss[allround] = rangesDb (sBand (mix), take.sRanges, 0) - rangesDb (sBand (y), take.sRanges, L);
            vLoss[allround] = rangesDb (mix, take.vRanges, 0) - rangesDb (y, take.vRanges, L);
        }
        std::printf ("    in a mix: Smart S (above 4 kHz) -%.2f dB (vowels -%.2f) | Allround S -%.2f dB (vowels -%.2f)\n", loss[0], vLoss[0], loss[1], vLoss[1]);
        check (loss[1] > 3.0 && loss[1] > loss[0] + 2.0, "Allround takes the S down in the mix");
        check (vLoss[1] < 0.3, "Allround leaves the vowels");
        Settings solo;
        solo.allround = true;
        solo.thresholdDb = -36.0f;
        E e (fs, solo);
        const auto y = run (e, take.x);
        const int L = e.getLatencySamples();
        const double sSolo = rangesDb (take.x, take.sRanges, 0) - rangesDb (y, take.sRanges, L);
        const double vSolo = rangesDb (take.x, take.vRanges, 0) - rangesDb (y, take.vRanges, L);
        std::printf ("    solo vocal: Allround S -%.2f dB, vowels -%.2f dB\n", sSolo, vSolo);
        check (sSolo > 3.0 && vSolo < 0.3, "Allround also works on a solo vocal");
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
            check (! p.isPresetModified(), juce::String ("fresh preset not modified: ") + list[(size_t) i].name);
            for (const auto& [id, v] : list[(size_t) i].values)
                if (auto* raw = p.state.getRawParameterValue (id))
                    near (raw->load(), v, std::max (0.02, std::abs (v) * 0.002), juce::String (list[(size_t) i].name) + " " + id);
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

    // The factory library (2026-11-23, r2/ember-presets): Default on top, 5 categories in menu order, unique names,
    // a purpose text in both languages, every value a real setting inside its range, stepping in category order
    void testFactoryLibrary()
    {
        Test t ("factory preset library: categories, names, ranges, stepping");
        const auto& list = factoryPresets();
        check (list.size() == 29, "29 factory presets (Default + 28)");
        check (! list.empty() && juce::String (list[0].name) == "Default" && list[0].category == PresetCategory::none, "Default first");
        {
            EmberAudioProcessor fresh, p;
            p.loadFactoryPreset (0);
            check (sameValues (capture (fresh.state), capture (p.state)) && sameValues (capture (p.state), capture (fresh.state)),
                   "Default = the plugin's init state");
        }
        juce::StringArray names;
        int catPos = -1;
        std::map<int, int> perCategory;
        for (int i = 0; i < (int) list.size(); ++i)
        {
            const auto& fp = list[(size_t) i];
            const juce::String name (fp.name);
            check (name.isNotEmpty() && ! names.contains (name, true), "unique name: " + name);
            names.add (name);
            check (juce::String::fromUTF8 (fp.infoDe).endsWithChar ('.') && juce::String (fp.infoEn).endsWithChar ('.')
                       && juce::String (fp.infoEn).length() < 120 && juce::String::fromUTF8 (fp.infoDe).length() < 120,
                   name + ": one-sentence purpose in DE and EN");
            check (presetInfo (fp).isNotEmpty() && presetName (fp) == name, name + ": name / info in the interface");
            if (i > 0)
            {
                check (fp.category != PresetCategory::none, name + ": in a category");
                int pos = 0;
                for (const auto c : presetCategories)
                {
                    if (c == fp.category)
                        break;
                    ++pos;
                }
                check (pos >= catPos, name + ": sorted by category (menu and stepping order)");
                catPos = std::max (catPos, pos);
                ++perCategory[pos];
            }
            EmberAudioProcessor q;
            for (const auto& [id, v] : fp.values)
            {
                auto* rp = dynamic_cast<juce::RangedAudioParameter*> (q.state.getParameter (id));
                check (rp != nullptr && isSetting (id), name + ": " + id + " is a setting");
                if (rp != nullptr)
                {
                    const auto r = rp->getNormalisableRange();
                    check (std::isfinite (v) && v >= r.start && v <= r.end, name + ": " + id + " inside its range");
                }
            }
            // a voice button never takes over a preset's own threshold (it only moves thresholds on a voice value)
            if (i > 0)
                if (const auto it = fp.values.find (pid::threshold); it != fp.values.end())
                    check (it->second != voiceLowThrDb && it->second != voiceMidThrDb && it->second != voiceHighThrDb,
                           name + ": threshold off the voice values");
        }
        check ((int) perCategory.size() == (int) std::size (presetCategories), "every category holds presets");
        for (const auto c : presetCategories)
            check (categoryName (c).isNotEmpty(), "category label");
        // ‹ › steps through every factory preset in list (= category) order
        EmberAudioProcessor p;
        p.loadFactoryPreset (0);
        bool inOrder = true;
        for (int i = 1; i < (int) list.size(); ++i)
        {
            p.stepPreset (1);
            inOrder = inOrder && p.getFactoryIndex() == i && ! p.isPresetModified();
        }
        check (inOrder, "next steps through all factory presets in category order");
        p.loadFactoryPreset (0);
        p.stepPreset (-1);
        check (userPresets().isEmpty() ? p.getFactoryIndex() == (int) list.size() - 1 : p.getFactoryIndex() == -1,
               "previous from Default = the last preset (or the last user preset)");
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

    // UX fixes 2026-11-23 (docs/products/ember-fixes/UX.md): voice buttons, undo with the preset name, A/B in the
    // project and next to AUTO, preset stepping and files, state restored off the message thread
    void testUxFixes()
    {
        Test t ("ux fixes: voice buttons, undo name, A/B saved + AUTO, preset stepping, preset files, threaded state");
        const auto real = [] (EmberAudioProcessor& p, const char* id) { return p.state.getRawParameterValue (id)->load(); };
        const auto setReal = [] (EmberAudioProcessor& p, const char* id, float v)
        {
            if (auto* prm = p.state.getParameter (id))
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (prm->convertTo0to1 (v));
                prm->endChangeGesture();
            }
        };
        AutoResult fake;
        fake.status = AutoResult::Status::ok;
        fake.freq = 6500.0f;
        fake.thresholdDb = -29.3f;
        fake.rangeDb = 6.0f;
        using VR = EmberAudioProcessor::VoiceResult;

        // 1: one voice click = one undo step (frequency and threshold together)
        {
            EmberAudioProcessor p;
            p.prepareToPlay (48000.0, 512);
            check (p.setVoice (0) == VR::set, "LOW from the default");
            near (real (p, pid::freq), voiceLowHz, 1.0, "LOW frequency");
            near (real (p, pid::threshold), voiceLowThrDb, 0.05, "LOW threshold comes along");
            check (p.undo(), "undo the voice");
            near (real (p, pid::freq), voiceMidHz, 1.0, "frequency back in one step");
            near (real (p, pid::threshold), voiceMidThrDb, 0.05, "threshold back in the same step");
            check (! p.canUndo(), "and that was the only step");
            check (p.redo() && std::abs (real (p, pid::threshold) - voiceLowThrDb) < 0.05f, "redo brings both");
            p.setVoice (2);
            near (real (p, pid::threshold), voiceHighThrDb, 0.05, "from one voice value to the next");
        }
        // 2: a threshold from a preset, by hand or from AUTO stays; the AUTO protection ends with a preset load
        {
            EmberAudioProcessor p;
            p.prepareToPlay (48000.0, 512);
            p.loadFactoryPreset (3);   // Rap: -34.5 dB
            const float rapThr = real (p, pid::threshold);
            check (p.setVoice (2) == VR::setKeptThreshold, "HIGH on Rap: reported as threshold kept");
            near (real (p, pid::freq), voiceHighHz, 1.0, "HIGH frequency set");
            near (real (p, pid::threshold), rapThr, 0.05, "the preset's threshold stays");
            setReal (p, pid::threshold, -20.0f);
            p.setVoice (0);
            near (real (p, pid::threshold), -20.0, 0.05, "a hand-set threshold stays");

            p.applyAuto (fake);
            check (p.getVoiceRange() == 1, "AUTO at 6.5 kHz lights MID");
            check (p.setVoice (1) == VR::keptAutoFrequency, "MID again keeps AUTO's frequency");
            near (real (p, pid::freq), 6500.0, 1.0, "AUTO frequency kept");
            p.setVoice (0);
            near (real (p, pid::threshold), -29.3, 0.05, "AUTO's threshold stays on another voice");
            p.loadFactoryPreset (0);
            setReal (p, pid::freq, 6500.0f);   // by hand onto AUTO's old frequency
            p.setVoice (1);
            near (real (p, pid::freq), voiceMidHz, 1.0, "after a preset load MID sets its frequency (no stale AUTO protection)");
            // the protection belongs to the setting, not to the last AUTO run: it comes back with A/B and a project
            p.applyAuto (fake);
            juce::MemoryBlock mb;
            p.getStateInformation (mb);
            EmberAudioProcessor q;
            q.setStateInformation (mb.getData(), (int) mb.getSize());
            check (q.setVoice (1) == VR::keptAutoFrequency, "a project with an AUTO result: MID keeps AUTO's frequency");
            p.switchSlot (1 - p.getActiveSlot());
            setReal (p, pid::freq, 6500.0f);
            check (p.setVoice (1) != VR::keptAutoFrequency && std::abs (real (p, pid::freq) - voiceMidHz) < 1.0f, "the slot from before AUTO is not protected");
        }
        // 3: the borders between the voices lie away from every factory preset (and the voices themselves)
        {
            EmberAudioProcessor p;
            for (int i = 0; i < (int) factoryPresets().size(); ++i)
            {
                p.loadFactoryPreset (i);
                const float hz = real (p, pid::freq);
                check (std::abs (hz - EmberAudioProcessor::voiceLowMidHz) > 100.0f && std::abs (hz - EmberAudioProcessor::voiceMidHighHz) > 100.0f,
                       juce::String (factoryPresets()[(size_t) i].name) + ": " + juce::String (hz, 0) + " Hz clearly inside a voice range");
            }
            check (EmberAudioProcessor::voiceRangeOf (voiceLowHz) == 0 && EmberAudioProcessor::voiceRangeOf (voiceMidHz) == 1
                       && EmberAudioProcessor::voiceRangeOf (voiceHighHz) == 2, "each voice lights its own button");
            for (int v = 0; v < 3; ++v)
            {
                p.setVoice (v);
                check (p.getVoiceRange() == v, "voice " + juce::String (v) + " after the parameter round trip");
            }
        }
        // 5: undo of a preset load / AUTO brings the name, the "*" and the A/B slots back
        {
            EmberAudioProcessor p;
            p.prepareToPlay (48000.0, 512);
            p.loadFactoryPreset (2);
            const auto popName = p.getPresetName();
            p.loadFactoryPreset (8);
            check (p.undo() && p.getPresetName() == popName && ! p.isPresetModified(), "undo a preset: its name and no *");
            check (p.redo() && p.getPresetName() == presetName (factoryPresets()[8]), "redo: the other name");
            p.applyAuto (fake);
            check (p.getPresetName() == juce::String ("AUTO-Einstellung") && p.getActiveSlot() == 1, "AUTO on B");
            check (p.undo(), "undo AUTO");
            check (p.getPresetName() == presetName (factoryPresets()[8]) && ! p.isPresetModified(), "undo AUTO: the preset name before, no *");
            check (p.getActiveSlot() == 0, "undo AUTO: A/B as before");
            check (p.redo() && p.getActiveSlot() == 1 && p.getPresetName() == juce::String ("AUTO-Einstellung"), "redo AUTO: B and the AUTO name");
            setReal (p, pid::range, 11.0f);
            check (p.undo() && p.getActiveSlot() == 1, "an ordinary undo does not switch A/B");
        }
        // 6: AUTO never overwrites a setting parked on the other slot
        {
            EmberAudioProcessor p;
            p.prepareToPlay (48000.0, 512);
            p.switchSlot (1);
            setReal (p, pid::threshold, -44.0f);   // parked on B
            p.switchSlot (0);
            p.applyAuto (fake);
            check (p.getActiveSlot() == 0 && p.getAutoBeforeSlot() == -1, "B parked: the result goes into A");
            p.switchSlot (1);
            near (real (p, pid::threshold), -44.0, 0.05, "B kept the parked setting");
            p.switchSlot (0);
            near (real (p, pid::threshold), -29.3, 0.05, "A holds the AUTO result");
            p.undo();
            near (real (p, pid::threshold), voiceMidThrDb, 0.05, "the setting before is one undo away");
        }
        // 7: A/B and the active slot are saved with the project
        {
            EmberAudioProcessor p;
            p.prepareToPlay (48000.0, 512);
            p.loadFactoryPreset (1);
            p.switchSlot (1);
            p.loadFactoryPreset (4);
            setReal (p, pid::range, 9.0f);
            juce::MemoryBlock mb;
            p.getStateInformation (mb);
            EmberAudioProcessor q;
            q.setStateInformation (mb.getData(), (int) mb.getSize());
            check (q.getActiveSlot() == 1, "active slot B restored");
            near (real (q, pid::range), 9.0, 0.05, "B's values");
            check (q.getPresetName() == presetName (factoryPresets()[4]), "B's preset name");
            q.switchSlot (0);
            check (q.getPresetName() == presetName (factoryPresets()[1]) && ! q.isPresetModified(), "A: its preset, unchanged");
            near (real (q, pid::threshold), factoryPresets()[1].values.at (pid::threshold), 0.05, "A's values");
            q.switchSlot (1);
            near (real (q, pid::range), 9.0, 0.05, "and back to B");
            // the saved state carries no stray copy of the A/B data into the parameter tree
            juce::MemoryBlock again;
            q.getStateInformation (again);
            const auto xml = juce::AudioProcessor::getXmlFromBinary (again.getData(), (int) again.getSize());
            int abCount = 0;
            if (xml != nullptr)
                for (auto* e : xml->getChildIterator())
                    abCount += e->hasTagName ("EmberAB") ? 1 : 0;
            check (abCount == 1, "one A/B block after a round trip");
        }
        // 10: stepping from an AUTO setting starts at the first preset (forward) / the last (back)
        {
            EmberAudioProcessor p;
            p.prepareToPlay (48000.0, 512);
            p.applyAuto (fake);
            check (p.getFactoryIndex() < 0, "AUTO setting is no preset");
            p.stepPreset (1);
            check (p.getFactoryIndex() == 0, "next from AUTO = the first factory preset");
            p.applyAuto (fake);
            p.stepPreset (-1);
            const bool noUsers = userPresets().isEmpty();
            check (noUsers ? p.getFactoryIndex() == (int) factoryPresets().size() - 1 : p.getFactoryIndex() < 0, "previous from AUTO = the last preset");
        }
        // 14: rename / delete user preset files
        {
            const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ember_ux_presets");
            dir.deleteRecursively();
            EmberAudioProcessor p;
            check (p.saveUserPreset (dir.getChildFile ("Alt")), "save");
            const auto alt = dir.getChildFile (juce::String ("Alt") + presetExtension);
            check (p.saveUserPreset (dir.getChildFile ("Belegt")), "save a second");
            check (p.renameUserPreset (alt, "Belegt").isNotEmpty() && alt.existsAsFile(), "rename onto an existing preset is refused");
            check (p.renameUserPreset (alt, "   ").isNotEmpty(), "an empty name is refused");
            check (p.loadUserPreset (alt), "load Alt");
            check (p.renameUserPreset (alt, "Neu: Lead").isEmpty(), "rename (an illegal character is dropped)");
            check (! alt.existsAsFile(), "old file gone");
            const auto renamed = dir.findChildFiles (juce::File::findFiles, false, "Neu*");
            check (renamed.size() == 1 && p.getPresetName() == renamed[0].getFileNameWithoutExtension(), "the bar shows the new name");
            if (renamed.size() == 1)
            {
                check (p.deleteUserPreset (renamed[0]).isEmpty() && ! renamed[0].existsAsFile(), "delete");
                check (p.deleteUserPreset (renamed[0]).isEmpty(), "deleting a file that is gone is no error");
            }
            check (p.renameUserPreset (alt, "X").isNotEmpty(), "renaming a missing file says so");
            dir.deleteRecursively();
        }
        // 19: a state restored off the message thread: parameters at once, the rest on the message thread
        {
            EmberAudioProcessor src;
            src.prepareToPlay (48000.0, 512);
            src.loadFactoryPreset (5);
            src.switchSlot (1);
            src.loadFactoryPreset (2);
            juce::MemoryBlock mb;
            src.getStateInformation (mb);

            EmberAudioProcessor p;
            p.prepareToPlay (48000.0, 512);
            setReal (p, pid::range, 12.0f);   // an undo step that belongs to the old state
            std::thread worker ([&] { p.setStateInformation (mb.getData(), (int) mb.getSize()); });
            worker.join();
            near (real (p, pid::threshold), factoryPresets()[2].values.at (pid::threshold), 0.05, "parameters restored at once");
            juce::MemoryBlock saved;
            p.getStateInformation (saved);   // before the message thread took over: the restored A/B are saved
            EmberAudioProcessor q;
            q.setStateInformation (saved.getData(), (int) saved.getSize());
            check (q.getActiveSlot() == 1 && q.getPresetName() == presetName (factoryPresets()[2]), "pending meta is what gets saved");
            juce::MessageManager::getInstance()->runDispatchLoopUntil (300);   // the processor's timer takes it over
            check (p.getActiveSlot() == 1 && p.getPresetName() == presetName (factoryPresets()[2]), "taken over on the message thread");
            check (! p.canUndo(), "the old undo steps are gone");
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
    std::vector<float> monoOf (const juce::AudioBuffer<float>& b)
    {
        std::vector<float> m ((size_t) b.getNumSamples(), 0.0f);
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                m[(size_t) i] += b.getSample (ch, i) / (float) b.getNumChannels();
        return m;
    }

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

    // AUTO as the plugin does it: the capture (stereo, pauses cut) until 12 s of voice, from `startSeconds` on
    Settings cliBase;   // --process: research switches also reach AUTO
    AutoResult autoOn (const juce::AudioBuffer<float>& b, double fs, bool smart = true, double startSeconds = 0.0,
                       double voicedSeconds = autoEnoughSeconds)
    {
        const int from = std::min (b.getNumSamples(), (int) (startSeconds * fs));
        const float* ch[2] { b.getReadPointer (0, from), b.getReadPointer (std::min (1, b.getNumChannels() - 1), from) };
        Settings base = cliBase;
        base.smart = smart;
        return autoViaCapture (ch, std::min (2, b.getNumChannels()), b.getNumSamples() - from, fs, 512, base, voicedSeconds);
    }

    int render (const juce::File& in, const juce::File& dir)
    {
        double fs = 0.0;
        auto buf = loadWav (in, fs);
        if (buf == nullptr) { std::printf ("cannot read %s\n", in.getFullPathName().toRawUTF8()); return 1; }
        const auto r = autoOn (*buf, fs);
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
    //   freq thr range amount attack release la mix mode det(0 smart,1 rel,2 abs,3 allround) listen gain(dB in) block
    //   width (octaves) link (0..1) stereo (0 L/R, 1 M/S, 2 mid, 3 side)  noengine=1 (file I/O only, CPU bench)
    //   track (0 = one fixed split bell, 1 = the band follows each S)  out (output dB)
    //   levels=<wav> (detector level dB + smart weight, dry-aligned)
    //   auto=1 (AUTO first, as the plugin applies it) autosec=12 autostart=0  trace=<wav> (applied reduction, dB, dry-aligned)
    //   preset=<index or English name, '_' for spaces> (a factory preset as the plugin loads it; the keys above change it)
    int processCli (const juce::StringArray& args)
    {
        double fs = 0.0;
        auto buf = loadWav (juce::File (args[1]), fs);
        if (buf == nullptr) { std::printf ("cannot read\n"); return 1; }
        std::map<juce::String, float> kv;
        juce::String tracePath, levelsPath, presetKey;
        for (int i = 3; i < args.size(); ++i)
        {
            const auto k = args[i].upToFirstOccurrenceOf ("=", false, false), v = args[i].fromFirstOccurrenceOf ("=", false, false);
            if (k == "trace") tracePath = v; else if (k == "levels") levelsPath = v; else if (k == "preset") presetKey = v; else kv[k] = v.getFloatValue();
        }
        const auto has = [&] (const char* k) { return kv.count (k) > 0; };
        if (has ("gain"))
            buf->applyGain (dbToGain (kv["gain"]));
        Settings s;
        if (presetKey.isNotEmpty())
        {
            // a factory preset by index or name ('_' for spaces), loaded the way the plugin loads it; the other keys
            // then change single values on top of it
            const auto& list = factoryPresets();
            int index = presetKey.containsOnly ("0123456789") ? presetKey.getIntValue() : -1;
            for (int i = 0; i < (int) list.size() && index < 0; ++i)
                if (juce::String (list[(size_t) i].name).equalsIgnoreCase (presetKey.replaceCharacter ('_', ' ')))
                    index = i;
            if (! juce::isPositiveAndBelow (index, (int) list.size()))
            {
                std::printf ("unknown preset %s\n", presetKey.toRawUTF8());
                return 1;
            }
            EmberAudioProcessor p;
            p.loadFactoryPreset (index);
            s = readSettings (p.state);
            s.bypass = false;
            s.listen = 0;
            std::printf ("PRESET %d %s\n", index, list[(size_t) index].name);
        }
        // the user settings AUTO measures with (mode, attack, lookahead, width, tracking), as in the plugin
        if (has ("attack")) s.attackMs = kv["attack"];
        if (has ("mode")) s.mode = (int) kv["mode"];
        if (has ("la")) s.lookaheadMs = kv["la"];
        if (has ("width")) s.widthOct = kv["width"];
        if (has ("track")) s.trackS = kv["track"] > 0.5f;
        cliBase = s;
        if (has ("auto") && kv["auto"] > 0.5f)
        {
            const double start = has ("autostart") ? kv["autostart"] : 0.0;
            const double secs = has ("autosec") ? kv["autosec"] : autoEnoughSeconds;
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            const auto r = autoOn (*buf, fs, true, start, secs);
            std::printf ("AUTO status %d freq %.0f thr %.1f range %.1f peakHz %.0f target %.2f expected %.2f vowel %.3f vpres %.3f vair %.3f mixed %.3f mpres %.3f sVsV %.1f Sframes %d rej %d secs %.1f ch %d renders %d ms %.0f\n",
                         (int) r.status, r.freq, r.thresholdDb, r.rangeDb, r.peakHz, r.targetDb, r.expectedReductionDb, r.vowelReductionDb,
                         r.vowelPresenceDb, r.vowelAirDb, r.mixedReductionDb, r.mixedPresenceDb, r.sibilanceVsVowelDb, r.sibilantFrames,
                         r.rejectedFrames, r.seconds, r.numChannels, r.renders, juce::Time::getMillisecondCounterHiRes() - t0);
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
        if (has ("det")) { const int d = (int) kv["det"]; s.smart = d == 0 || d == 3; s.allround = d == 3; s.relative = d <= 1 || d == 3; }
        if (has ("width")) s.widthOct = kv["width"];
        if (has ("link")) s.stereoLink = kv["link"];
        if (has ("stereo")) s.stereoMode = (int) kv["stereo"];
        if (has ("out")) s.outputDb = kv["out"];
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
        const bool wantTrace = tracePath.isNotEmpty() || levelsPath.isNotEmpty();
        std::vector<float> trace (wantTrace ? (size_t) (N + L) : 0, 0.0f), levels (trace.size(), 0.0f), weights (trace.size(), 0.0f);
        const int stepBlock = wantTrace ? 1 : block;
        for (int start = 0; start < padded.getNumSamples(); start += stepBlock)
        {
            const int n = std::min (stepBlock, padded.getNumSamples() - start);
            float* ch[2] { padded.getWritePointer (0, start), padded.getWritePointer (nch > 1 ? 1 : 0, start) };
            if (! has ("noengine"))   // (CPU bench: file I/O alone)
                e.process (ch, nch, n);
            if (wantTrace)
            {
                trace[(size_t) start] = e.currentReductionDb.load();
                levels[(size_t) start] = e.detectorDb.load();
                weights[(size_t) start] = e.detectorWeight.load();
            }
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
        if (levelsPath.isNotEmpty())
        {
            // detector level (dB) and smart weight at input time (dry-aligned)
            juce::AudioBuffer<float> t (2, N);
            for (int i = 0; i < N; ++i)
            {
                t.setSample (0, i, levels[(size_t) i]);
                t.setSample (1, i, weights[(size_t) i]);
            }
            write (juce::File (levelsPath), t);
        }
        if (tracePath.isNotEmpty())
        {
            // the engine reports the reduction applied to the sample it outputs (dry sample i comes out at i + L)
            juce::AudioBuffer<float> t (1, N);
            for (int i = 0; i < N; ++i)
                t.setSample (0, i, trace[(size_t) (i + L)]);
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
        {
            std::weak_ptr<const MascotRig> gone;
            {
                const auto h = MascotRig::acquire();
                gone = h;
            }
            check (gone.expired(), "the rig (its pictures) is freed with the last window");
        }
        const auto held = MascotRig::acquire();
        const auto& rig = *held;
        check (&MascotRig::get() == held.get() && MascotRig::acquire() == held, "one rig, shared");
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
            else
                check (false, "EMBER_VOCALS: " + part.trim() + " does not exist");
        }
        // set but nothing found: that is a broken setup, not a pass
        check (! files.isEmpty(), "EMBER_VOCALS is set but holds no .wav file");
        int ok = 0, read = 0, engineOk = 0;
        // every factory preset as the plugin loads it; the strength ladder of the basics is checked on what they remove
        std::vector<Settings> presetSettings;
        for (int i = 0; i < (int) factoryPresets().size(); ++i)
        {
            EmberAudioProcessor p;
            p.loadFactoryPreset (i);
            presetSettings.push_back (readSettings (p.state));
        }
        const auto presetIndex = [] (const char* name)
        {
            for (int i = 0; i < (int) factoryPresets().size(); ++i)
                if (juce::String (factoryPresets()[(size_t) i].name) == name)
                    return i;
            return -1;
        };
        const int ladder[] { presetIndex ("Subtle Reduction"), presetIndex ("Gentle Vocal"), presetIndex ("Natural De-Essing"),
                             presetIndex ("Strong Sibilance"), presetIndex ("Vocal Recording Rescue"), presetIndex ("Extreme Sibilance") };
        double removed[std::size (ladder)] {};
        int presetRuns = 0, presetFinite = 0;
        for (const auto& f : files)
        {
            double fs = 0.0;
            auto buf = loadWav (f, fs);
            check (buf != nullptr, f.getFileName() + ": readable");
            if (buf == nullptr || buf->getNumSamples() < (int) (fs * 4.0))
                continue;
            ++read;
            const auto mono = monoOf (*buf);
            {
                // every factory preset on 20 s of the take: finite output; energy removed (dry delayed - wet)
                const int n = std::min ((int) mono.size(), (int) (fs * 20.0));
                const std::vector<float> part (mono.begin(), mono.begin() + n);
                for (int i = 0; i < (int) presetSettings.size(); ++i)
                {
                    E e (fs, presetSettings[(size_t) i]);
                    const auto y = run (e, part);
                    const int L = e.getLatencySamples();
                    bool finite = true;
                    double diff = 0.0;
                    for (int k = 0; k < (int) y.size(); ++k)
                    {
                        finite = finite && std::isfinite (y[(size_t) k]);
                        if (k >= L)
                            diff += juce::square ((double) part[(size_t) (k - L)] - (double) y[(size_t) k]);
                    }
                    ++presetRuns;
                    presetFinite += finite ? 1 : 0;
                    for (int l = 0; l < (int) std::size (ladder); ++l)
                        if (ladder[l] == i)
                            removed[l] += diff;
                }
            }
            {
                // the engine on the real take: finite, and no S nothing changes (the first 0.2 s before any voice)
                Settings st;
                E e (fs, st);
                const auto y = run (e, mono);
                bool finite = true;
                for (float v : y)
                    finite = finite && std::isfinite (v);
                check (finite, f.getFileName() + ": engine output finite");
                engineOk += finite ? 1 : 0;
            }
            const auto r = autoOn (*buf, fs);
            std::printf ("    %-16s %s freq %5.0f thr %6.1f range %4.1f S -%.2f of %.2f | V -%.2f pres -%.2f air -%.2f | M -%.2f pres -%.2f (S/V %.1f) peak %.0f, %d renders, %dch\n",
                         f.getFileName().substring (0, 16).toRawUTF8(), r.status == AutoResult::Status::ok ? "ok  " : "none",
                         r.freq, r.thresholdDb, r.rangeDb, r.expectedReductionDb, r.targetDb, r.vowelReductionDb, r.vowelPresenceDb, r.vowelAirDb,
                         r.mixedReductionDb, r.mixedPresenceDb, r.sibilanceVsVowelDb, r.peakHz, r.renders, r.numChannels);
            check (r.status == AutoResult::Status::ok, f.getFileName() + ": set up");
            if (r.status != AutoResult::Status::ok)
                continue;
            ++ok;
            // stability: other 12 s of the same take land near the same frequency
            if (buf->getNumSamples() > (int) (fs * 70.0))
            {
                float lo = r.peakHz, hi = r.peakHz;
                for (double from : { 25.0, 50.0 })
                {
                    const auto o = autoOn (*buf, fs, true, from);
                    if (o.status == AutoResult::Status::ok)
                    {
                        lo = std::min (lo, o.peakHz);
                        hi = std::max (hi, o.peakHz);
                    }
                }
                std::printf ("      excerpts 0/25/50 s: S centre %.0f-%.0f Hz (%.0f %%)\n", lo, hi, 100.0 * (hi / lo - 1.0));
                check (hi / lo < 1.30f, f.getFileName() + ": S centre stable over excerpts");
            }
            check (r.thresholdDb >= autoMinThresholdDb, f.getFileName() + ": threshold floor");
            check (r.expectedReductionDb >= r.targetDb - 0.5f || heldBack (r), f.getFileName() + ": target reached unless a voice limit holds it back");
            if (juce::SystemStats::getEnvironmentVariable ("EMBER_COMPARE", {}).isNotEmpty())
            {
                const auto p = autoOn (*buf, fs, false);
                std::printf ("      plain relative: thr %6.1f range %4.1f S -%.1f V -%.2f M -%.2f | smart: S -%.1f V -%.2f M -%.2f\n",
                             p.thresholdDb, p.rangeDb, p.expectedReductionDb, p.vowelReductionDb, p.mixedReductionDb,
                             r.expectedReductionDb, r.vowelReductionDb, r.mixedReductionDb);
            }
            check (r.freq >= autoMinCornerHz && r.freq <= autoMaxCornerHz, f.getFileName() + ": corner in range");
            check (r.vowelReductionDb <= autoVowelMaxDb + 0.01f && r.vowelPresenceDb <= autoVowelPresenceMaxDb + 0.01f
                       && r.vowelAirDb <= autoVowelAirMaxDb + 0.01f, f.getFileName() + ": vowels untouched (full band, presence, air)");
            check (r.mixedReductionDb <= autoMixedMaxDb + 0.01f && r.mixedPresenceDb <= autoMixedPresenceMaxDb + 0.01f,
                   f.getFileName() + ": S next to vowels keeps the vowel");
            check (r.rangeDb <= autoMaxRangeDb && r.expectedReductionDb <= autoMaxTargetDb + 0.5f, f.getFileName() + ": subtle");
        }
        std::printf ("    %d of %d takes set up (%d read)\n", ok, files.size(), read);
        check (read > 0, "EMBER_VOCALS: at least one take was analysed");
        check (engineOk == files.size(), "every take went through the engine");
        std::printf ("    factory presets: %d of %d runs finite (%d presets x %d takes)\n", presetFinite, presetRuns,
                     (int) presetSettings.size(), read);
        check (presetRuns == read * (int) presetSettings.size() && presetFinite == presetRuns, "every factory preset on every take: finite output");
        bool ladderOk = true;
        for (int l = 0; l < (int) std::size (ladder); ++l)
        {
            check (ladder[l] >= 0, "ladder preset exists");
            if (l > 0)
                ladderOk = ladderOk && removed[l] > removed[l - 1] * 1.05;
            std::printf ("      %-22s removed %.2f dB more than Subtle\n", ladder[l] >= 0 ? factoryPresets()[(size_t) ladder[l]].name : "?",
                         10.0 * std::log10 (std::max (1.0e-30, removed[l]) / std::max (1.0e-30, removed[0])));
        }
        check (ladderOk, "Subtle < Gentle < Natural < Strong < Rescue < Extreme (energy removed over all takes)");
    }
}

double emberThreadCpuMs();
double emberProcessCpuMs();
void emberRunQa (bool full, int& passed, int& failed);   // EmberQaTests.cpp

// Visual check for GUI optimisations (2026-11-23): the same frames of the SIMPLE view every run (seeded Ember, fixed
// time steps, the seal's clock set directly), through the screen's renderer (createComponentSnapshot), at 1x and 1.25x.
// Compare two runs pixel by pixel (before / after a change).
static int framesDump (const juce::File& dir, const juce::StringArray& args)
{
    dir.createDirectory();
    UserSettings::useFileForTests (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ember_frames.settings"));
    vocalypse::i18n::overrideSystemLanguage (vocalypse::i18n::Language::german);
    juce::String anim ("full");
    for (const auto& a : args)
        if (a.startsWith ("anim="))
            anim = a.fromFirstOccurrenceOf ("=", false, false);
    {
        UserSettings s;
        s.file().setValue ("animation", anim);
        s.file().saveIfNeeded();
    }
    EmberAudioProcessor p;
    p.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
    auto* ed = dynamic_cast<EmberEditor*> (base.get());
    if (ed == nullptr)
        return 1;
    ed->setSize (look::baseW, look::baseH);
    ed->setAdvanced (false);
    ed->tickForTest();   // texts, buttons
    auto& panel = ed->getPanel();
    auto& m = panel.getMascot();
    m.getBrain().seedForTest (12345);
    juce::PNGImageFormat png;
    const float dt = 1.0f / 60.0f;
    for (int k = 0; k <= 300; ++k)
    {
        MascotInput in;
        in.hasSignal = true;
        in.reductionDb = (k % 40) < 6 ? 6.0f : 0.0f;
        in.bpm = 120.0;
        m.setInput (in);
        m.stepForTest (dt);
        if (k % 15 != 0)
            continue;
        seal::Scene sc;
        sc.t = k * (double) dt;
        sc.sinceS = m.secondsSinceSwallow();
        sc.heat = in.reductionDb / 9.0f;
        sc.on = true;
        const auto kb = juce::Rectangle<float> (624, 118, 248, 230);
        panel.getSealOverlay().update (sc, true, juce::Rectangle<float> (92.0f, 92.0f).withCentre ({ kb.getCentreX(), kb.getY() + 90.0f }),
                                       { { 0, 0, 900, 60 }, { 24, 68, 852, 32 }, { 548, 112, 336, 410 }, { 16, 528, 868, 58 } });
        for (float scale : { 1.0f, 1.25f })
        {
            const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, scale);
            juce::FileOutputStream out (dir.getChildFile ("frame_" + juce::String (k).paddedLeft ('0', 3) + (scale > 1.0f ? "_125" : "_100") + ".png"));
            if (out.openedOk()) { out.setPosition (0); out.truncate(); png.writeImageToStream (img, out); }
        }
    }
    vocalypse::i18n::overrideSystemLanguage (std::nullopt);
    LiveMascotView::setAnimationLevel (AnimationLevel::full);
    std::printf ("frames: %s\n", dir.getFullPathName().toRawUTF8());
    return 0;
}


// Step 5 of FIGUR_ANIMIEREN.md: CPU with several instances. N plugins process a looped take in real time on one audio
// thread while their editors are open on the desktop (live Ember at 60 fps); CPU time is taken per thread, so audio
// and drawing (message thread, shared by all editors in a host) are reported separately.
static int bench (int seconds, const juce::StringArray& args)
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

    // options (key=value after the seconds): scales=1,1.25 counts=1,4 front=yes|no|both min=1 (windows minimised)
    juce::StringPairArray opt;
    for (const auto& a : args)
        if (a.contains ("="))
            opt.set (a.upToFirstOccurrenceOf ("=", false, false), a.fromFirstOccurrenceOf ("=", false, false));
    const auto numbers = [&] (const char* key, const char* fallback)
    {
        std::vector<float> out;
        for (const auto& t : juce::StringArray::fromTokens (opt.getValue (key, fallback), ",", ""))
            out.push_back (t.getFloatValue());
        return out;
    };
    const auto scales = numbers ("scales", "1,2");
    const auto counts = numbers ("counts", "1,2,4,8");
    const auto frontOpt = opt.getValue ("front", "both");
    std::vector<bool> fronts;
    if (frontOpt != "no") fronts.push_back (true);
    if (frontOpt != "yes") fronts.push_back (false);
    const bool minimised = opt.getValue ("min", "0") == "1";
    const bool hosted = opt.getValue ("hosted", "0") == "1";   // each editor a child window inside a host window, as in a DAW
    const auto anim = opt.getValue ("anim", "");
    LiveMascotView::fixedHzForTest = opt.getValue ("hz", "0").getIntValue();

    UserSettings::useFileForTests (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ember_bench.settings"));
    {
        UserSettings s;   // the editors read the animation setting when they open
        s.file().setValue ("animation", anim.isNotEmpty() ? anim : juce::String ("full"));
        s.file().saveIfNeeded();
    }
    const auto take = makeTake (48000.0, 10.0);
    std::printf ("Ember bench: %d s per run, 48 kHz / 256, editors open (live Ember)%s%s\n", seconds, minimised ? ", windows minimised" : "",
                 anim.isNotEmpty() ? (", animation " + anim).toRawUTF8() : "");
    std::printf ("  scale  inst | audio %% core  worst block | host UI thread %% core          | whole process %% core\n");
    for (bool inFront : fronts)
    for (float scale : scales)
    {
        LiveMascotView::alwaysInFront = inFront;
        if (scale == scales.front())
            std::printf (inFront ? "  -- cursor on the plugin (60 fps for a single window)\n" : "  -- cursor elsewhere (30 fps)\n");
        juce::Desktop::getInstance().setGlobalScaleFactor (scale);
        for (float countF : counts)
        {
            const int count = (int) countF;
            std::vector<std::unique_ptr<EmberAudioProcessor>> procs;
            std::vector<std::unique_ptr<juce::Component>> hosts;   // destroyed after the editors
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
                if (hosted)
                {
                    hosts.push_back (std::make_unique<juce::Component>());
                    auto& h = *hosts.back();
                    h.setBounds (40 + 30 * i, 40 + 30 * i, ed.getWidth(), ed.getHeight());
                    h.addToDesktop (juce::ComponentPeer::windowHasTitleBar | juce::ComponentPeer::windowHasMinimiseButton);
                    h.setVisible (true);
                    ed.setTopLeftPosition (0, 0);
                    ed.addToDesktop (0, h.getPeer()->getNativeHandle());
                }
                else
                {
                    ed.setTopLeftPosition (40 + 30 * i, 40 + 30 * i);
                    ed.addToDesktop (juce::ComponentPeer::windowHasTitleBar | juce::ComponentPeer::windowHasMinimiseButton);
                }
                ed.setVisible (true);
                loop.procs.push_back (procs.back().get());
            }
            juce::MessageManager::getInstance()->runDispatchLoopUntil (1500);   // settle (first decode, window creation)
            if (minimised)
            {
                for (auto& e : eds)
                    if (auto* peer = e->getPeer(); peer != nullptr && ! hosted)
                        peer->setMinimised (true);
                for (auto& h : hosts)
                    if (auto* peer = h->getPeer())
                        peer->setMinimised (true);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (1000);
            }
            LiveMascotView::benchFigureMs = 0; LiveMascotView::benchFloorMs = 0; LiveMascotView::benchRunesMs = 0;
            LiveMascotView::benchComposeMs = 0; LiveMascotView::benchFrames = 0;
            loop.startThread (juce::Thread::Priority::highest);
            const double m0 = emberThreadCpuMs(), p0 = emberProcessCpuMs(), w0 = juce::Time::getMillisecondCounterHiRes();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (seconds * 1000);
            const double wall = juce::Time::getMillisecondCounterHiRes() - w0, ui = emberThreadCpuMs() - m0, all = emberProcessCpuMs() - p0;
            loop.stopThread (2000);
            const double audioWall = loop.blocks.load() * 256 * 1000.0 / 48000.0;
            std::printf ("  %5.2fx %4d | %7.2f %%    %6.2f ms   | %6.1f %% (%4.1f %% per editor) | %6.1f %%\n", scale, count,
                         100.0 * loop.cpuMs.load() / std::max (1.0, audioWall), loop.worstBlockMs.load(),
                         100.0 * ui / wall, 100.0 * ui / wall / count, 100.0 * all / wall);
            if (const int fr = LiveMascotView::benchFrames.load(); fr > 0)
                std::printf ("            per frame: figure %.2f, seal floor %.2f, runes+fire %.2f, rest %.2f ms (%d frames, %.1f fps)\n",
                             LiveMascotView::benchFigureMs.load() / fr, LiveMascotView::benchFloorMs.load() / fr,
                             LiveMascotView::benchRunesMs.load() / fr, LiveMascotView::benchComposeMs.load() / fr, fr, fr * 1000.0 / wall / count);
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
        return bench (args.size() >= 2 ? juce::jmax (2, args[1].getIntValue()) : 8, args);
    if (args.size() >= 2 && args[0] == "--frames")
        return framesDump (juce::File (args[1]), args);
    if (args.size() >= 2 && args[0] == "--mascot-loops")
        return mascotLoops (juce::File (args[1]));

    if (args.size() >= 1 && (args[0] == "--qa" || args[0] == "--qa-full"))   // only the QA suite
    {
        emberRunQa (args[0] == "--qa-full", passed, failed);
        return failed == 0 ? 0 : 1;
    }
    if (args.size() >= 1 && args[0] == "--real-takes")   // only AUTO on the takes of EMBER_VOCALS
    {
        testRealTakes();
        std::printf ("\nEMBER real takes: %d passed, %d failed\n", passed, failed);
        return failed == 0 ? 0 : 1;
    }
    if (args.size() >= 1 && args[0] == "--auto-tests")   // only the AUTO tests (quick loop while tuning AUTO)
    {
        testAuto();
        testAutoTiming();
        testCapture();
        testAutoProcessor();
        testRealTakes();
        std::printf ("\nEMBER AUTO: %d passed, %d failed\n", passed, failed);
        return failed == 0 ? 0 : 1;
    }

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
    testAutoTiming();
    testCapture();
    testAutoProcessor();
    testProcessor();
    testLicence();
    testMascot();
    testSmart();
    testSIntoVowel();
    testSmoothBand();
    testBandShape();
    testStereo();
    testListenBand();
    testSwitches();
    testTracking();
    testAllround();
    testPresets();
    testFactoryLibrary();
    testUsability();
    testUxFixes();
    testMascotClips();
    testRig();
    testLiveBrain();
    testMascotFrames (snapshotDir);
    testEditor (snapshotDir);
    testRealTakes();
    emberRunQa (false, passed, failed);

    std::printf ("\nEMBER: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
