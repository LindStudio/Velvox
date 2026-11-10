// EMBER De-Esser test runner.
//   EmberTests                          all checks (exit code 1 on a failure)
//   EmberTests --render <wav> <dir>     AUTO on the take, then render: <name>_ember.wav, <name>_delta.wav + report
//   EmberTests --snapshots <dir>        PNGs of SIMPLE and ADVANCED (both languages)
//   env EMBER_VOCALS=<dir or ;-list>    also runs AUTO on these real takes (report + sanity checks)
#include <juce_audio_formats/juce_audio_formats.h>
#include "EmberEngine.h"
#include "EmberAuto.h"
#include "EmberParams.h"
#include "EmberProcessor.h"
#include "EmberEditor.h"
#include "EmberMascot.h"
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
        near (s.freq, 6500.0, 0.5, "default freq");
        near (s.thresholdDb, -30.0, 0.01, "default threshold");
        near (s.rangeDb, 5.0, 0.01, "default range (gentle)");
        near (s.amount, 0.5, 1e-4, "default Amount 50 %");
        check (s.relative && s.mode == 0 && s.listen == 0 && ! s.bypass, "default modes");
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
            near (maxGr, 4.0 * amount * 2.0, 0.05, "max reduction = range x Amount x 2 (" + juce::String (amount) + ")");
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
            check (r.targetDb >= autoMinTargetDb && r.targetDb <= autoMaxTargetDb, "target within the subtle window");
            check (r.expectedReductionDb > 0.3f && r.expectedReductionDb <= r.targetDb + 0.6f, "S lose at most the target (rendered)");
            check (r.mixedReductionDb <= autoMixedMaxDb + 0.05f, "vowel side of each S kept");
            check (r.vowelReductionDb <= autoVowelMaxDb + 0.02f, "vowels <= 0.1 dB");
            check (r.rangeDb <= autoMaxRangeDb, "range <= 6 dB");

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
        check (s.mode == 0 && s.relative && std::abs (s.amount - 0.5f) < 1e-3f, "split, relative, Amount 50 %");
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
        Test t ("mascot states");
        MascotBrain b;
        const double dt = 1.0 / 30.0;
        const auto runFor = [&] (MascotInput in, double seconds) { for (double s = 0; s < seconds; s += dt) b.update (in, dt); };
        MascotInput in;
        runFor (in, 1.0);
        check (b.getState() == MascotState::idle, "idle without signal");
        in.hasSignal = true;
        runFor (in, 1.2);
        check (b.getState() == MascotState::singing, "singing");
        in.reductionDb = 1.0f;
        runFor (in, 1.2);
        check (b.getState() == MascotState::snapping, "snapping");
        in.reductionDb = 5.0f;
        runFor (in, 1.2);
        check (b.getState() == MascotState::fire, "fire");
        check (b.getIntensity() > 0.3f, "fire intensity follows GR");
        in.reductionDb = 11.0f;
        runFor (in, 1.5);
        check (b.getState() == MascotState::overheated, "overheated after a while above 9 dB");
        in.reductionDb = 0.0f;
        runFor (in, 0.3);
        check (b.getState() == MascotState::overheated, "stays hot a moment");
        runFor (in, 2.5);
        check (b.getState() == MascotState::singing, "cools down");

        // Calm: a GR that flickers every 100 ms does not make him flicker
        {
            MascotBrain c;
            MascotInput fl;
            fl.hasSignal = true;
            for (double s = 0; s < 1.5; s += dt) c.update (fl, dt);
            int changes = 0;
            auto last = c.getState();
            for (int i = 0; i < 90; ++i)   // 3 s
            {
                fl.reductionDb = (i / 3) % 2 == 0 ? 2.5f : 0.0f;
                c.update (fl, dt);
                if (c.getState() != last) { ++changes; last = c.getState(); }
            }
            check (changes <= 3, "flickering GR: at most 3 mood changes in 3 s (got " + juce::String (changes) + ")");
        }
        in.autoListening = true;
        runFor (in, 0.1);
        check (b.getState() == MascotState::listening, "listening");
        in.autoListening = false;
        in.autoJustFinished = true;
        b.update (in, dt);
        in.autoJustFinished = false;
        runFor (in, 1.0);
        check (b.getState() == MascotState::proud, "proud after AUTO");
        runFor (in, 2.0);
        check (b.getState() != MascotState::proud, "proud ends");
        in.bypass = true;
        runFor (in, 0.1);
        check (b.getState() == MascotState::sleeping, "sleeping on bypass");
        for (int s = 0; s < numMascotStates; ++s)
            check (juce::String (mascotStateName ((MascotState) s)).isNotEmpty(), "state name");
    }

    void testMascotClips()
    {
        Test t ("mascot clips: out of the old mood, into the new one, then its loop");
        MascotView view;
        view.setSize (500, 400);
        auto& f = view.getFramesForTest();
        f.clear();
        juce::Image img (juce::Image::ARGB, 8, 8, true);
        const auto addN = [&] (MascotState s, MascotClip c, int n) { for (int i = 0; i < n; ++i) f.add (s, c, img); };
        addN (MascotState::singing, MascotClip::loop, 24);
        addN (MascotState::singing, MascotClip::out, 12);    // 0.5 s at 24 fps
        addN (MascotState::fire, MascotClip::in, 12);
        addN (MascotState::fire, MascotClip::loop, 24);
        const double dt = 1.0 / 30.0;
        MascotInput in;
        in.hasSignal = true;
        view.setInput (in);
        for (int i = 0; i < 60; ++i) view.stepForTest (dt);
        check (view.getShownState() == MascotState::singing && view.getShownClip() == MascotClip::loop, "singing loop");
        in.reductionDb = 5.0f;
        view.setInput (in);
        bool sawOut = false, sawIn = false;
        for (int i = 0; i < 90; ++i)
        {
            view.stepForTest (dt);
            sawOut = sawOut || (view.getShownState() == MascotState::singing && view.getShownClip() == MascotClip::out);
            sawIn = sawIn || (view.getShownState() == MascotState::fire && view.getShownClip() == MascotClip::in);
        }
        check (sawOut, "played singing_out");
        check (sawIn, "played fire_in");
        check (view.getShownState() == MascotState::fire && view.getShownClip() == MascotClip::loop, "ends in the fire loop");
    }

    void testMascotFrames (const juce::File& snapshotDir)
    {
        Test t ("mascot pictures built in (Jakob's Ember, 2026-11-10)");
        MascotFrames frames;
        for (int s = 0; s < numMascotStates; ++s)
        {
            const auto st = (MascotState) s;
            check (frames.has (st), juce::String ("picture for ") + mascotStateName (st));
            if (frames.has (st))
                check (frames.frame (st, MascotClip::loop, 0).getWidth() >= 512 && frames.frame (st, MascotClip::loop, 0).hasAlphaChannel(),
                       juce::String ("size + alpha ") + mascotStateName (st));
        }
        if (snapshotDir == juce::File())
            return;

        // One picture per mood, as the plugin shows it (500 x 400)
        const std::pair<const char*, MascotInput> moods[] {
            { "idle", {} },
            { "singing", { false, true, false, false, 0.0f } },
            { "snapping", { false, true, false, false, 0.8f } },
            { "fire", { false, true, false, false, 8.0f } },
            { "overheated", { false, true, false, false, 12.0f } },
            { "sleeping", { true, false, false, false, 0.0f } },
            { "listening", { false, true, true, false, 0.0f } },
        };
        snapshotDir.createDirectory();
        for (const auto& [name, in] : moods)
        {
            MascotView view;
            view.setSize (500, 400);
            view.setInput (in);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (700);
            juce::Image img (juce::Image::ARGB, 500, 400, true);
            {
                juce::Graphics g (img);
                g.fillAll (juce::Colour (0xff05060a));
                view.paintEntireComponent (g, true);
            }
            juce::PNGImageFormat png;
            juce::FileOutputStream out (snapshotDir.getChildFile (juce::String ("mascot_") + name + ".png"));
            if (out.openedOk()) { out.setPosition (0); out.truncate(); png.writeImageToStream (img, out); }
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

    void testEditor (const juce::File& snapshotDir)
    {
        Test t ("editor: both views, scaling, languages");
        EmberAudioProcessor p;
        p.prepareToPlay (48000.0, 512);
        for (auto lang : { vocalypse::i18n::Language::german, vocalypse::i18n::Language::english })
        {
            vocalypse::i18n::overrideSystemLanguage (lang);
            std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
            auto* ed = dynamic_cast<EmberEditor*> (base.get());
            check (ed != nullptr, "editor type");
            if (ed == nullptr)
                return;
            const juce::String suffix = lang == vocalypse::i18n::Language::german ? "de" : "en";
            for (bool adv : { false, true })
            {
                ed->setAdvanced (adv);
                const auto img = snapshot (*ed);
                check (img.isValid() && img.getWidth() == look::baseW, "snapshot " + juce::String (adv ? "advanced" : "simple"));
                if (snapshotDir != juce::File())
                {
                    snapshotDir.createDirectory();
                    juce::PNGImageFormat png;
                    juce::FileOutputStream out (snapshotDir.getChildFile (juce::String (adv ? "advanced_" : "simple_") + suffix + ".png"));
                    if (out.openedOk()) { out.setPosition (0); out.truncate(); png.writeImageToStream (img, out); }
                }
            }
            ed->setSize (look::baseW * 3 / 2, look::baseH * 3 / 2);
            check (ed->getWidth() == look::baseW * 3 / 2, "resizes");
            check (ed->isAdvanced(), "view kept");
        }
        vocalypse::i18n::overrideSystemLanguage (std::nullopt);
        check ((bool) p.state.state.getProperty ("advanced", false), "view stored in the state");
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
    AutoResult autoOn (const std::vector<float>& mono, double fs)
    {
        AutoCapture c;
        c.prepare (fs);
        c.start();
        for (int start = 0; start < (int) mono.size() && c.isCapturing(); start += 512)
        {
            const float* ch[1] { mono.data() + start };
            c.push (ch, 1, std::min (512, (int) mono.size() - start));
        }
        return analyse (c.data(), c.getNumSamples(), fs);
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
            check (r.freq >= autoMinCornerHz && r.freq <= autoMaxCornerHz, f.getFileName() + ": corner in range");
            check (r.vowelReductionDb <= 0.15f, f.getFileName() + ": vowels untouched");
            check (r.mixedReductionDb <= autoMixedMaxDb + 0.1f, f.getFileName() + ": S next to vowels keeps the vowel");
            check (r.rangeDb <= autoMaxRangeDb && r.expectedReductionDb <= autoMaxTargetDb + 0.5f, f.getFileName() + ": subtle");
        }
        std::printf ("    %d of %d takes set up\n", ok, files.size());
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (juce::String::fromUTF8 (argv[i]));

    if (args.size() >= 3 && args[0] == "--render")
        return render (juce::File (args[1]), juce::File (args[2]));

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
    testRobust();
    testAuto();
    testCapture();
    testProcessor();
    testMascot();
    testMascotClips();
    testMascotFrames (snapshotDir);
    testEditor (snapshotDir);
    testRealTakes();

    std::printf ("\nEMBER: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
