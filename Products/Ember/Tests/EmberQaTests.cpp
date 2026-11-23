// EMBER QA suite (branch r2/ember-qa, 2026-11-23): the user's workflow through the real AudioProcessor / editor API
// and measured behaviour (latency, null, distortion, gain-reduction accuracy, clicks, automation, CPU, memory).
//   EmberTests --qa        only this suite (quick part; also part of the default full run)
//   EmberTests --qa-full   with the heavy measurements: the whole rate x block matrix, CPU, 10 minutes of audio,
//                          editor / instance loops (memory, handles, GDI), real takes (EMBER_VOCALS)
// Every measurement is printed ("    ..." lines); docs/products/ember-r2/TESTBERICHT.md reports them.
#include <juce_audio_formats/juce_audio_formats.h>
#include "EmberEngine.h"
#include "EmberParams.h"
#include "EmberProcessor.h"
#include "EmberEditor.h"
#include "EmberPresets.h"
#include "EmberLiveMascot.h"
#include "EmberTestSignals.h"
#include "Text/I18n.h"
#include <atomic>
#include <set>
#include <complex>
#include <thread>

void emberQaSysInfo (double& privateMB, double& workingSetMB, int& handles, int& gdiObjects, int& userObjects);   // EmberQaSys.cpp
double emberProcessCpuMs();                                                                                     // EmberBenchTime.cpp

namespace ember::qa
{
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
            Test (const char* name) { currentTest = juce::String ("qa: ") + name; std::printf ("qa: %s\n", name); }
        };

        using Sig = std::vector<float>;
        using namespace vocalypse::dsp;
        constexpr double twoPi = juce::MathConstants<double>::twoPi;
        const double allRates[] { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };
        const int allBlocks[] { 1, 7, 32, 64, 128, 256, 512, 1024, 2048, 4096 };

        float gainOf (double db) { return (float) std::pow (10.0, db / 20.0); }
        double toDb (double g) { return 20.0 * std::log10 (std::max (1.0e-30, g)); }

        // ---------------------------------------------------------------- signals
        Sig sine (double fs, double seconds, double hz, double db, double phase = 0.0)
        {
            Sig x ((size_t) (fs * seconds));
            const double a = gainOf (db);
            for (size_t i = 0; i < x.size(); ++i)
                x[i] = (float) (a * std::sin (twoPi * hz * (double) i / fs + phase));
            return x;
        }

        Sig sweep (double fs, double seconds, double f0, double f1, double db)
        {
            Sig x ((size_t) (fs * seconds));
            const double a = gainOf (db), k = std::log (f1 / f0), n = (double) x.size();
            for (size_t i = 0; i < x.size(); ++i)
            {
                const double t = (double) i / fs, T = n / fs;
                x[i] = (float) (a * std::sin (twoPi * f0 * T / k * (std::exp (t / T * k) - 1.0)));
            }
            return x;
        }

        Sig noise (double fs, double seconds, double peakDb, unsigned seed)
        {
            Sig x ((size_t) (fs * seconds));
            juce::Random rnd ((juce::int64) seed);
            const float a = gainOf (peakDb);
            for (auto& v : x)
                v = a * (rnd.nextFloat() * 2.0f - 1.0f);
            return x;
        }

        Sig plus (Sig a, const Sig& b)
        {
            for (size_t i = 0; i < a.size() && i < b.size(); ++i)
                a[i] += b[i];
            return a;
        }

        Sig scaled (Sig a, float g)
        {
            for (auto& v : a)
                v *= g;
            return a;
        }

        Sig fadeIn (Sig a, double fs, double ms = 20.0)
        {
            const int n = std::min ((int) a.size(), (int) (fs * ms * 0.001));
            for (int i = 0; i < n; ++i)
                a[(size_t) i] *= (float) i / (float) n;
            return a;
        }

        float peakOf (const Sig& x, int from = 0)
        {
            float p = 0.0f;
            for (size_t i = (size_t) std::max (0, from); i < x.size(); ++i)
                p = std::max (p, std::abs (x[i]));
            return p;
        }

        float maxStep (const Sig& x, int from = 1)
        {
            float m = 0.0f;
            for (size_t i = (size_t) std::max (1, from); i < x.size(); ++i)
                m = std::max (m, std::abs (x[i] - x[i - 1]));
            return m;
        }

        // A stereo pair from a mono signal: the right one later and quieter (a wide double)
        Sig rightOf (const Sig& x, int delay = 7, float gain = 0.8f)
        {
            Sig r (x.size(), 0.0f);
            for (size_t i = (size_t) delay; i < x.size(); ++i)
                r[i] = gain * x[i - (size_t) delay];
            return r;
        }

        struct Stats
        {
            bool finite = true, denormal = false;
            float peak = 0.0f, step = 0.0f;
            int zeroRun = 0;   // longest run of exact zeros
        };

        Stats statsOf (const Sig& y, int from = 0)
        {
            Stats s;
            int run = 0;
            for (size_t i = (size_t) std::max (0, from); i < y.size(); ++i)
            {
                const float v = y[i];
                if (! std::isfinite (v))
                {
                    s.finite = false;
                    continue;
                }
                if (v != 0.0f && std::abs (v) < std::numeric_limits<float>::min())
                    s.denormal = true;
                s.peak = std::max (s.peak, std::abs (v));
                if (i > 0 && std::isfinite (y[i - 1]))
                    s.step = std::max (s.step, std::abs (v - y[i - 1]));
                run = v == 0.0f ? run + 1 : 0;
                s.zeroRun = std::max (s.zeroRun, run);
            }
            return s;
        }

        // Fits a sine of a known frequency over [from, to): amplitude and the residual (THD+N) in dB below it
        struct Fit { double amplitude = 0.0, thdnDb = 0.0; };
        Fit fitSine (const Sig& y, double fs, double hz, int from, int to)
        {
            double ss = 0, cc = 0, sc = 0, ys = 0, yc = 0;
            for (int i = from; i < to; ++i)
            {
                const double w = twoPi * hz * i / fs, s = std::sin (w), c = std::cos (w), v = y[(size_t) i];
                ss += s * s; cc += c * c; sc += s * c; ys += v * s; yc += v * c;
            }
            const double det = ss * cc - sc * sc;
            const double a = (ys * cc - yc * sc) / det, b = (yc * ss - ys * sc) / det;
            double res = 0, sig = 0;
            for (int i = from; i < to; ++i)
            {
                const double w = twoPi * hz * i / fs, f = a * std::sin (w) + b * std::cos (w);
                res += (y[(size_t) i] - f) * (y[(size_t) i] - f);
                sig += f * f;
            }
            return { std::sqrt (a * a + b * b), 10.0 * std::log10 (std::max (1.0e-30, res) / std::max (1.0e-30, sig)) };
        }

        double biquadMag (const BiquadCoeffs& c, double fs, double hz)
        {
            const double w = twoPi * hz / fs;
            const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
            return std::abs ((c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2));
        }

        // The detector band's response (4th-order Butterworth high-pass at the frequency) and its tilt: the level the
        // absolute detector reads for a steady tone of amplitude a at hz
        double absoluteLevelDb (double fs, double freq, double a, double hz)
        {
            const double f = std::min (freq, fs * 0.45);
            const double h = biquadMag (design::highPass (fs, f, 0.54119610), fs, hz) * biquadMag (design::highPass (fs, f, 1.30656296), fs, hz);
            return toDb (a * h) + detectorTiltDbPerOct * std::log2 (f / detectorTiltRefHz);
        }

        // Expected reduction of the static curve (soft knee 4:1, range, Amount, the 24 dB ceiling)
        double expectedReduction (double levelDb, const Settings& s)
        {
            return std::min ((double) maxReductionDb, std::min ((double) s.rangeDb, (double) staticReductionDb ((float) levelDb, s.thresholdDb)) * amountScale (s.amount));
        }

        // 8th-order Butterworth high-pass: what lies far above every tone of a test signal (clicks are broadband)
        struct Hp8
        {
            Biquad b[4];
            Hp8 (double fs, double hz)
            {
                const double q[] { 0.50979558, 0.60134489, 0.89997622, 2.56291545 };
                for (int k = 0; k < 4; ++k)
                    b[k].setCoeffs (design::highPass (fs, hz, q[k]));
            }
            Sig run (const Sig& x)
            {
                Sig y (x);
                for (auto& v : y)
                    for (auto& s : b)
                        v = s.process (v, 0);
                return y;
            }
        };

        // Clicks: the high-passed output in 1 ms windows, the loudest window in [from, to) in dB against the signal's RMS
        double hfPeakDb (const Sig& y, double fs, int from, int to, double hpHz, double refRms)
        {
            Hp8 hp (fs, hpHz);
            const auto h = hp.run (y);
            const int w = std::max (1, (int) (fs * 0.001));
            double worst = 0.0;
            for (int i = std::max (0, from); i + w <= std::min ((int) h.size(), to); i += w / 2)
            {
                double e = 0.0;
                for (int k = 0; k < w; ++k)
                    e += (double) h[(size_t) (i + k)] * h[(size_t) (i + k)];
                worst = std::max (worst, e / w);
            }
            return 10.0 * std::log10 (std::max (1.0e-30, worst)) - toDb (refRms);
        }

        double rmsOf (const Sig& x, int from, int to)
        {
            double e = 0.0;
            int n = 0;
            for (int i = std::max (0, from); i < std::min ((int) x.size(), to); ++i, ++n)
                e += (double) x[(size_t) i] * x[(size_t) i];
            return std::sqrt (e / std::max (1, n));
        }

        // ---------------------------------------------------------------- the plugin as a host drives it
        juce::RangedAudioParameter& param (EmberAudioProcessor& p, const char* id) { return *p.state.getParameter (id); }

        // A change from the editor (one gesture)
        void setReal (EmberAudioProcessor& p, const char* id, float v)
        {
            auto& prm = param (p, id);
            prm.beginChangeGesture();
            prm.setValueNotifyingHost (prm.convertTo0to1 (v));
            prm.endChangeGesture();
        }

        // Host automation as the JUCE wrappers deliver it (audio thread: setValue + the listeners)
        void automate (juce::RangedAudioParameter& prm, float normalised)
        {
            prm.setValue (normalised);
            prm.sendValueChangedMessageToListeners (normalised);
        }

        float raw (EmberAudioProcessor& p, const char* id) { return p.state.getRawParameterValue (id)->load(); }

        std::unique_ptr<EmberAudioProcessor> makePlugin (double fs, int block, int channels = 2)
        {
            auto p = std::make_unique<EmberAudioProcessor>();
            if (channels == 1)
            {
                juce::AudioProcessor::BusesLayout mono;
                mono.inputBuses.add (juce::AudioChannelSet::mono());
                mono.outputBuses.add (juce::AudioChannelSet::mono());
                p->setBusesLayout (mono);
            }
            p->prepareToPlay (fs, block);
            return p;
        }

        using BlockSizes = std::function<int (int blockIndex)>;
        using Hook = std::function<void (int blockIndex, int start, int n)>;

        BlockSizes fixedBlocks (int n) { return [n] (int) { return n; }; }
        BlockSizes varyingBlocks (unsigned seed, int maxBlock = 2048)
        {
            auto rnd = std::make_shared<juce::Random> ((juce::int64) seed);
            return [rnd, maxBlock] (int)
            {
                // mostly host-like sizes, sometimes tiny or odd ones
                const int k = rnd->nextInt (10);
                return k == 0 ? 1 + rnd->nextInt (8) : (k < 3 ? 1 + rnd->nextInt (maxBlock) : 32 + rnd->nextInt (std::min (maxBlock, 1024)));
            };
        }

        // In place through processBlock (mono: one channel, else two)
        void processInPlace (EmberAudioProcessor& p, Sig& l, Sig* r, const BlockSizes& blocks, const Hook& hook = {})
        {
            juce::MidiBuffer midi;
            const int len = (int) l.size(), nch = r != nullptr ? 2 : 1;
            for (int start = 0, b = 0; start < len; ++b)
            {
                const int n = std::min (std::max (1, blocks (b)), len - start);
                if (hook)
                    hook (b, start, n);
                float* ptr[2] { l.data() + start, r != nullptr ? r->data() + start : nullptr };
                juce::AudioBuffer<float> buf (ptr, nch, n);
                p.processBlock (buf, midi);
                start += n;
            }
        }

        std::pair<Sig, Sig> processStereo (EmberAudioProcessor& p, Sig l, Sig r, const BlockSizes& blocks, const Hook& hook = {})
        {
            processInPlace (p, l, &r, blocks, hook);
            return { std::move (l), std::move (r) };
        }

        Sig processMono (EmberAudioProcessor& p, Sig x, const BlockSizes& blocks, const Hook& hook = {})
        {
            processInPlace (p, x, nullptr, blocks, hook);
            return x;
        }

        Sig delayed (const Sig& x, int d)
        {
            Sig y (x.size(), 0.0f);
            for (size_t i = (size_t) d; i < x.size(); ++i)
                y[i] = x[i - (size_t) d];
            return y;
        }

        float maxDiff (const Sig& a, const Sig& b, int from = 0)
        {
            float m = 0.0f;
            for (size_t i = (size_t) std::max (0, from); i < a.size() && i < b.size(); ++i)
                m = std::max (m, std::abs (a[i] - b[i]));
            return m;
        }

        // Sample for sample the same value (+0 and -0 count as the same sample)
        bool identical (const Sig& a, const Sig& b)
        {
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); ++i)
                if (! (a[i] == b[i]))
                    return false;
            return true;
        }

        // Every setting as a normalised value (tests compare states with it)
        std::map<juce::String, float> normalisedValues (EmberAudioProcessor& p)
        {
            std::map<juce::String, float> m;
            for (auto* prm : p.getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                    m[rp->getParameterID()] = rp->getValue();
            return m;
        }

        juce::File tempDir (const juce::String& name)
        {
            auto d = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name);
            d.deleteRecursively();
            d.createDirectory();
            return d;
        }

        // The voice + S test tone of the switching tests: 200 Hz body and a steady 7 kHz "S" (reduction on all the time)
        std::pair<Sig, Sig> steadyVoiceAndS (double fs, double seconds)
        {
            auto l = fadeIn (plus (sine (fs, seconds, 200.0, -20.0), sine (fs, seconds, 7000.0, -12.0)), fs);
            auto r = fadeIn (plus (sine (fs, seconds, 200.0, -22.0, 0.7), sine (fs, seconds, 7000.0, -15.0, 1.3)), fs);
            return { l, r };
        }

        // =============================================================== PART 4: workflow
        void testParametersReachEngine()
        {
            Test t ("every parameter reaches the engine (host values -> processBlock -> engine settings)");
            auto p = makePlugin (48000.0, 256);
            juce::Random rnd (11);
            Sig l (256, 0.0f), r (256, 0.0f);
            int mismatches = 0;
            for (int trial = 0; trial < 60; ++trial)
            {
                for (auto* prm : p->getParameters())
                    automate (*dynamic_cast<juce::RangedAudioParameter*> (prm), trial == 0 ? 0.0f : (trial == 1 ? 1.0f : rnd.nextFloat()));
                processStereo (*p, l, r, fixedBlocks (256));
                const auto want = readSettings (p->state);
                const auto& got = p->engine.getSettings();
                const auto same = [] (float a, float b) { return std::abs (a - b) <= 1.0e-4f * std::max (1.0f, std::abs (b)); };
                const bool ok = same (got.freq, want.freq) && same (got.thresholdDb, want.thresholdDb) && same (got.rangeDb, want.rangeDb)
                             && same (got.amount, want.amount) && same (got.attackMs, want.attackMs) && same (got.releaseMs, want.releaseMs)
                             && same (got.lookaheadMs, want.lookaheadMs) && same (got.mix, want.mix) && same (got.outputDb, want.outputDb)
                             && same (got.widthOct, want.widthOct) && same (got.stereoLink, want.stereoLink)
                             && got.mode == want.mode && got.listen == want.listen && got.bypass == want.bypass && got.stereoMode == want.stereoMode
                             && got.smart == want.smart && got.relative == want.relative && got.allround == want.allround;
                mismatches += ok ? 0 : 1;
            }
            check (mismatches == 0, "engine settings == parameter values after one block (" + juce::String (mismatches) + " of 60 differ)");
            // the detection choice means what its name says
            const struct { int det; bool smart, relative, allround; } map[] { { 0, true, true, false }, { 1, false, true, false }, { 2, false, false, false }, { 3, true, true, true } };
            for (const auto& m : map)
            {
                setReal (*p, pid::detection, (float) m.det);
                const auto s = readSettings (p->state);
                check (s.smart == m.smart && s.relative == m.relative && s.allround == m.allround,
                       "detection " + detectionChoices[m.det] + " -> smart/relative/allround flags");
            }
        }

        void testPresetFiles()
        {
            Test t ("presets: factory + user files, corrupt files, read-only, stepping");
            const auto dir = tempDir ("ember_qa_presets");
            setUserPresetFolderForTests (dir);
            auto p = makePlugin (48000.0, 512);
            const auto& list = factoryPresets();
            Sig l (512, 0.0f), r (512, 0.0f);
            for (int i = 0; i < (int) list.size(); ++i)
            {
                p->loadFactoryPreset (i);
                processStereo (*p, l, r, fixedBlocks (512));
                const auto want = readSettings (p->state);
                check (std::abs (p->engine.getSettings().thresholdDb - want.thresholdDb) < 1.0e-4f && std::abs (p->engine.getSettings().freq - want.freq) < 0.01f
                           && p->engine.getSettings().mode == want.mode,
                       juce::String ("factory preset reaches the engine: ") + list[(size_t) i].nameEn);
                check (p->getPresetName() == presetName (list[(size_t) i]) && ! p->isPresetModified(), juce::String ("name + not modified: ") + list[(size_t) i].nameEn);
            }

            // own presets
            p->loadFactoryPreset (2);
            setReal (*p, pid::range, 9.5f);
            check (p->saveUserPreset (dir.getChildFile ("Lead")), "save 'Lead'");
            check (p->saveUserPreset (dir.getChildFile (juce::String::fromUTF8 ("Stimme ÄÖÜ ß – 2"))), "save a name with umlauts and a dash");
            check (userPresets().size() == 2, "both files listed");
            p->loadFactoryPreset (0);
            check (p->loadUserPreset (dir.getChildFile (juce::String ("Lead") + presetExtension)) && std::abs (raw (*p, pid::range) - 9.5f) < 0.01f
                       && p->getPresetName() == "Lead" && ! p->isPresetModified(), "load 'Lead': values, name, not modified");
            check (p->loadUserPreset (dir.getChildFile (juce::String::fromUTF8 ("Stimme ÄÖÜ ß – 2") + presetExtension))
                       && p->getPresetName() == juce::String::fromUTF8 ("Stimme ÄÖÜ ß – 2"), "umlaut name round trip");

            // broken files: refused, nothing changes
            const auto write = [&] (const char* name, const juce::String& text) { auto f = dir.getChildFile (juce::String (name) + presetExtension); f.replaceWithText (text); return f; };
            const auto empty = write ("X1 empty", {});
            const auto wrongRoot = write ("X2 wrong root", "<SomethingElse><Param id=\"emRange\" value=\"3\"/></SomethingElse>");
            const auto truncated = write ("X3 truncated", "<EmberPreset version=\"1\"><Param id=\"emRange\" val");
            auto garbage = dir.getChildFile (juce::String ("X4 garbage") + presetExtension);
            {
                juce::MemoryBlock mb (4096);
                juce::Random rnd (5);
                rnd.fillBitsRandomly (mb.getData(), mb.getSize());
                garbage.replaceWithData (mb.getData(), mb.getSize());
            }
            const auto beforeValues = capture (p->state);
            const auto beforeName = p->getPresetName();
            for (const auto& f : { empty, wrongRoot, truncated, garbage })
            {
                check (! p->loadUserPreset (f), f.getFileName() + ": refused");
                check (sameValues (beforeValues, capture (p->state)) && p->getPresetName() == beforeName, f.getFileName() + ": settings and name unchanged");
            }
            check (! p->loadUserPreset (dir.getChildFile ("does not exist.emberpreset")), "a missing file is refused");

            // odd but readable files: out-of-range values are clamped, unknown ids ignored, missing ids = default
            const auto odd = write ("Y odd", "<EmberPreset version=\"1\"><Param id=\"emRange\" value=\"1000\"/><Param id=\"emFreq\" value=\"-5\"/>"
                                             "<Param id=\"emMode\" value=\"7\"/><Param id=\"emThreshold\" value=\"1e30\"/><Param id=\"emUnknown\" value=\"3\"/>"
                                             "<Param id=\"emMix\" value=\"inf\"/></EmberPreset>");
            check (p->loadUserPreset (odd), "out-of-range preset loads");
            {
                bool inRange = true;
                for (auto* prm : p->getParameters())
                    if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                    {
                        const float v = raw (*p, rp->getParameterID().toRawUTF8());
                        const auto& rg = rp->getNormalisableRange();
                        inRange = inRange && std::isfinite (v) && v >= rg.start - 1.0e-4f && v <= rg.end + 1.0e-4f;
                    }
                check (inRange, "every value finite and inside its range");
                near (raw (*p, pid::range), 24.0, 1.0e-3, "range clamped to 24 dB");
                near (raw (*p, pid::freq), 2000.0, 0.5, "frequency clamped to 2 kHz");
                near (raw (*p, pid::mix), 100.0, 1.0e-3, "inf mix skipped -> default");
                near (raw (*p, pid::attack), 0.5, 1.0e-3, "missing attack -> default");
            }

            // stepping over factory + user presets: every step loads something, broken files are skipped
            {
                const int numFactory = (int) list.size();
                p->loadFactoryPreset (0);
                juce::StringArray seen;
                int stuck = 0;
                for (int k = 0; k < numFactory + userPresets().size() + 2; ++k)
                {
                    const auto before = p->getPresetName();
                    p->stepPreset (1);
                    stuck += p->getPresetName() == before ? 1 : 0;
                    seen.addIfNotAlreadyThere (p->getPresetName());
                }
                check (stuck == 0, "stepping forward never gets stuck on a broken file (" + juce::String (stuck) + " steps did nothing)");
                check (seen.contains ("Lead") && seen.contains ("Y odd") && ! seen.contains ("X1 empty"), "user presets visited, broken ones not");
                p->loadFactoryPreset (0);
                stuck = 0;
                for (int k = 0; k < numFactory + userPresets().size() + 2; ++k)
                {
                    const auto before = p->getPresetName();
                    p->stepPreset (-1);
                    stuck += p->getPresetName() == before ? 1 : 0;
                }
                check (stuck == 0, "stepping back never gets stuck (" + juce::String (stuck) + ")");
            }

            // a read-only file and a folder that cannot be made: saving fails cleanly, the bar keeps its name
            {
                const auto ro = dir.getChildFile (juce::String ("ReadOnly") + presetExtension);
                check (p->saveUserPreset (ro), "save ReadOnly");
                ro.setReadOnly (true);
                const auto text = ro.loadFileAsString();
                p->loadFactoryPreset (3);
                setReal (*p, pid::range, 2.0f);
                const auto name = p->getPresetName();
                check (! p->saveUserPreset (ro), "saving over a read-only file reports failure");
                check (ro.loadFileAsString() == text && p->getPresetName() == name, "file untouched, name unchanged");
                check (p->renameUserPreset (ro, "Renamed RO").isEmpty() || ro.existsAsFile(), "rename of a read-only file: done or reported");
                for (auto& f : dir.findChildFiles (juce::File::findFiles, false, "*"))
                    f.setReadOnly (false);
                const auto blocker = dir.getChildFile ("not a folder");
                blocker.replaceWithText ("x");
                check (! p->saveUserPreset (blocker.getChildFile ("Inside")), "saving below a file (no folder possible) reports failure");
            }
            setUserPresetFolderForTests ({});
            dir.deleteRecursively();
        }

        void testUndoFuzz()
        {
            Test t ("undo / redo: 30 random edits back and forth restore every value exactly");
            auto p = makePlugin (48000.0, 512);
            juce::Random rnd (17);
            const auto start = normalisedValues (*p);
            std::vector<std::map<juce::String, float>> states { start };
            juce::StringArray ids;
            for (auto* prm : p->getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm); rp != nullptr && isSetting (rp->getParameterID()))
                    ids.add (rp->getParameterID());
            int lastIdx = -1;
            for (int k = 0; k < 30; ++k)
            {
                int idx = rnd.nextInt (ids.size());
                if (idx == lastIdx)
                    idx = (idx + 1) % ids.size();   // the same control again within a second is the same step
                lastIdx = idx;
                auto& prm = param (*p, ids[idx].toRawUTF8());
                float v = rnd.nextFloat();
                if (std::abs (prm.convertFrom0to1 (v) - prm.convertFrom0to1 (prm.getValue())) < 1.0e-3f)
                    v = v > 0.5f ? 0.0f : 1.0f;
                prm.beginChangeGesture();
                prm.setValueNotifyingHost (v);
                prm.endChangeGesture();
                states.push_back (normalisedValues (*p));
            }
            const auto same = [] (const std::map<juce::String, float>& a, const std::map<juce::String, float>& b)
            {
                for (const auto& [id, v] : a)
                    if (id != pid::listen && id != pid::bypass && std::abs (b.at (id) - v) > 1.0e-6f)
                        return false;
                return true;
            };
            int wrong = 0;
            for (int k = 29; k >= 0; --k)
            {
                p->undo();
                wrong += same (states[(size_t) k], normalisedValues (*p)) ? 0 : 1;
            }
            check (wrong == 0 && ! p->canUndo(), "30 undos walk back through every state (" + juce::String (wrong) + " wrong)");
            wrong = 0;
            for (int k = 1; k <= 30; ++k)
            {
                p->redo();
                wrong += same (states[(size_t) k], normalisedValues (*p)) ? 0 : 1;
            }
            check (wrong == 0 && ! p->canRedo(), "30 redos walk forward again (" + juce::String (wrong) + " wrong)");
            p->undo();
            setReal (*p, pid::range, 1.0f);
            check (! p->canRedo(), "a new edit after undo clears redo");
        }

        // Two saved states hold the same: parameter values within float precision of the skewed ranges (1e-6 relative,
        // e.g. 6199.9995 / 6199.9985 Hz), everything else identical
        bool sameState (const juce::XmlElement& a, const juce::XmlElement& b)
        {
            juce::XmlElement ca (a), cb (b);
            for (auto* e : a.getChildWithTagNameIterator ("PARAM"))
            {
                const auto* o = b.getChildByAttribute ("id", e->getStringAttribute ("id"));
                const double va = e->getDoubleAttribute ("value"), vb = o != nullptr ? o->getDoubleAttribute ("value") : 1.0e30;
                if (std::abs (va - vb) > 1.0e-6 * std::max (1.0, std::abs (va)))
                    return false;
            }
            ca.deleteAllChildElementsWithTagName ("PARAM");
            cb.deleteAllChildElementsWithTagName ("PARAM");
            return ca.isEquivalentTo (&cb, false);
        }

        // A random but complete user situation: values, a preset name, A/B with two different settings
        void randomSituation (EmberAudioProcessor& p, juce::Random& rnd, const juce::File& presetDir)
        {
            const int kind = rnd.nextInt (3);
            if (kind == 0)
                p.loadFactoryPreset (rnd.nextInt ((int) factoryPresets().size()));
            const auto randomEdits = [&]
            {
                for (auto* prm : p.getParameters())
                    if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm); rp != nullptr && isSetting (rp->getParameterID()) && rnd.nextFloat() < 0.6f)
                    {
                        rp->beginChangeGesture();
                        rp->setValueNotifyingHost (rnd.nextFloat());
                        rp->endChangeGesture();
                    }
            };
            randomEdits();
            if (kind == 1)
                p.saveUserPreset (presetDir.getChildFile ("Situation " + juce::String (rnd.nextInt (1000))));
            if (rnd.nextBool())
            {
                p.switchSlot (1);
                randomEdits();
                if (rnd.nextBool())
                    p.switchSlot (0);
            }
            setReal (p, pid::bypass, rnd.nextFloat() < 0.3f ? 1.0f : 0.0f);
        }

        void testStateRoundTrip()
        {
            Test t ("state: random situations round-trip into a fresh instance (other rate / block, unprepared)");
            const auto dir = tempDir ("ember_qa_state_presets");
            setUserPresetFolderForTests (dir);
            juce::Random rnd (23);
            int valueErrors = 0, metaErrors = 0, xmlErrors = 0, slotErrors = 0;
            for (int trial = 0; trial < 25; ++trial)
            {
                auto src = makePlugin (44100.0, 1024);
                randomSituation (*src, rnd, dir);
                setReal (*src, pid::listen, (float) rnd.nextInt (3));
                juce::MemoryBlock mb;
                src->getStateInformation (mb);

                std::unique_ptr<EmberAudioProcessor> dst;
                if (trial % 3 == 0)
                    dst = std::make_unique<EmberAudioProcessor>();      // the host restores before prepareToPlay
                else
                    dst = makePlugin (trial % 3 == 1 ? 96000.0 : 192000.0, trial % 3 == 1 ? 64 : 4096);
                dst->setStateInformation (mb.getData(), (int) mb.getSize());
                const auto a = normalisedValues (*src), b = normalisedValues (*dst);
                for (const auto& [id, v] : a)
                    if (id == pid::listen)
                        valueErrors += b.at (id) == 0.0f ? 0 : 1;   // listening never comes back with a project
                    else
                        valueErrors += std::abs (b.at (id) - v) <= 1.0e-6f ? 0 : 1;
                const bool metaOk = src->getActiveSlot() == dst->getActiveSlot() && src->getPresetName() == dst->getPresetName()
                                 && src->isPresetModified() == dst->isPresetModified() && src->getFactoryIndex() == dst->getFactoryIndex();
                if (! metaOk)
                    std::printf ("    trial %d: slot %d/%d, name \"%s\"/\"%s\", modified %d/%d, factory %d/%d\n", trial, src->getActiveSlot(), dst->getActiveSlot(),
                                 src->getPresetName().toRawUTF8(), dst->getPresetName().toRawUTF8(), (int) src->isPresetModified(), (int) dst->isPresetModified(),
                                 src->getFactoryIndex(), dst->getFactoryIndex());
                metaErrors += metaOk ? 0 : 1;
                // saving the restored instance again gives the same state (listening off on both sides)
                setReal (*src, pid::listen, 0.0f);
                juce::MemoryBlock m1, m2;
                src->getStateInformation (m1);
                dst->getStateInformation (m2);
                const auto x1 = juce::AudioProcessor::getXmlFromBinary (m1.getData(), (int) m1.getSize());
                const auto x2 = juce::AudioProcessor::getXmlFromBinary (m2.getData(), (int) m2.getSize());
                const bool xmlOk = x1 != nullptr && x2 != nullptr && sameState (*x1, *x2);
                if (! xmlOk && x1 != nullptr && x2 != nullptr)
                {
                    const auto dbg = juce::File::getSpecialLocation (juce::File::tempDirectory);
                    dbg.getChildFile ("ember_qa_state_a.xml").replaceWithText (x1->toString());
                    dbg.getChildFile ("ember_qa_state_b.xml").replaceWithText (x2->toString());
                    std::printf ("    trial %d: saved states differ (ember_qa_state_a/b.xml in the temp folder)\n", trial);
                }
                xmlErrors += xmlOk ? 0 : 1;
                // the other A/B slot holds the same on both
                const int other = 1 - src->getActiveSlot();
                src->switchSlot (other);
                dst->switchSlot (other);
                slotErrors += (sameValues (capture (src->state), capture (dst->state)) && src->getPresetName() == dst->getPresetName()) ? 0 : 1;
            }
            check (valueErrors == 0, "every parameter identical after the round trip (" + juce::String (valueErrors) + " differ)");
            check (metaErrors == 0, "A/B slot, preset name, * marker, factory index identical (" + juce::String (metaErrors) + " trials differ)");
            check (xmlErrors == 0, "the restored instance saves the same state again (" + juce::String (xmlErrors) + " differ)");
            check (slotErrors == 0, "the other A/B slot restored as well (" + juce::String (slotErrors) + " differ)");
            // saving and reopening 50 times does not walk the values away
            {
                auto p = makePlugin (48000.0, 512);
                setReal (*p, pid::freq, 6789.0f);
                setReal (*p, pid::attack, 0.73f);
                for (int k = 0; k < 50; ++k)
                {
                    juce::MemoryBlock mb;
                    p->getStateInformation (mb);
                    auto q = std::make_unique<EmberAudioProcessor>();
                    q->setStateInformation (mb.getData(), (int) mb.getSize());
                    p = std::move (q);
                }
                std::printf ("    50 save / reopen cycles: frequency %.4f Hz (set 6789), attack %.5f ms (set 0.73)\n", raw (*p, pid::freq), raw (*p, pid::attack));
                check (std::abs (raw (*p, pid::freq) - 6789.0f) < 0.05f && std::abs (raw (*p, pid::attack) - 0.73f) < 1.0e-4f, "no drift over 50 save / reopen cycles");
            }
            setUserPresetFolderForTests ({});
            dir.deleteRecursively();
        }

        void testCorruptState()
        {
            Test t ("state: corrupt / hostile project data never breaks the plugin");
            auto good = makePlugin (48000.0, 512);
            good->loadFactoryPreset (4);
            juce::MemoryBlock valid;
            good->getStateInformation (valid);
            const auto validXml = juce::AudioProcessor::getXmlFromBinary (valid.getData(), (int) valid.getSize());

            const auto makeBlob = [] (const juce::XmlElement& xml) { juce::MemoryBlock mb; juce::AudioProcessor::copyXmlToBinary (xml, mb); return mb; };
            const auto withParam = [&] (const char* id, const juce::String& value)
            {
                auto x = std::make_unique<juce::XmlElement> (*validXml);
                for (auto* e : x->getChildWithTagNameIterator ("PARAM"))
                    if (e->getStringAttribute ("id") == id)
                        e->setAttribute ("value", value);
                return makeBlob (*x);
            };
            std::vector<std::pair<juce::String, juce::MemoryBlock>> blobs;
            {
                juce::MemoryBlock rnd (2000);
                juce::Random r (3);
                r.fillBitsRandomly (rnd.getData(), rnd.getSize());
                blobs.push_back ({ "random bytes", rnd });
                blobs.push_back ({ "truncated", juce::MemoryBlock (valid.getData(), valid.getSize() / 2) });
                blobs.push_back ({ "empty", juce::MemoryBlock() });
                juce::XmlElement other ("NotEmber");
                blobs.push_back ({ "other root tag", makeBlob (other) });
                blobs.push_back ({ "NaN threshold", withParam (pid::threshold, "nan") });
                blobs.push_back ({ "inf frequency", withParam (pid::freq, "inf") });
                blobs.push_back ({ "range 1000 dB", withParam (pid::range, "1000") });
                blobs.push_back ({ "mode 7", withParam (pid::mode, "7") });
                blobs.push_back ({ "text value", withParam (pid::mix, "loud") });
                auto noParams = std::make_unique<juce::XmlElement> (*validXml);
                noParams->deleteAllChildElementsWithTagName ("PARAM");
                blobs.push_back ({ "no parameters", makeBlob (*noParams) });
                auto manySlots = std::make_unique<juce::XmlElement> (*validXml);
                if (auto* ab = manySlots->getChildByName ("EmberAB"))
                {
                    ab->setAttribute ("active", 99);
                    for (int i = 0; i < 500; ++i)
                        ab->createNewChildElement ("Slot")->setAttribute ("used", true);
                }
                blobs.push_back ({ "500 A/B slots, active 99", makeBlob (*manySlots) });
            }
            Sig l = sine (48000.0, 0.2, 7000.0, -10.0), r = l;
            for (const auto& [name, blob] : blobs)
            {
                auto p = makePlugin (48000.0, 512);
                p->setStateInformation (blob.getData(), (int) blob.getSize());
                bool inRange = true;
                for (auto* prm : p->getParameters())
                    if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                    {
                        const float v = raw (*p, rp->getParameterID().toRawUTF8());
                        const auto& rg = rp->getNormalisableRange();
                        inRange = inRange && std::isfinite (v) && v >= rg.start - 1.0e-4f && v <= rg.end + 1.0e-4f && std::isfinite (rp->getValue());
                    }
                check (inRange, name + ": every parameter finite and inside its range");
                const auto [yl, yr] = processStereo (*p, l, r, fixedBlocks (512));
                const auto st = statsOf (yl);
                check (st.finite && st.peak > 0.01f, name + ": audio keeps flowing");
                check (p->getActiveSlot() == 0 || p->getActiveSlot() == 1, name + ": A/B slot valid");
                juce::MemoryBlock again;
                p->getStateInformation (again);
                check (again.getSize() > 0, name + ": saves again");
            }
        }

        void testThreadedState()
        {
            Test t ("state restored on another thread while the audio thread processes and the message thread runs");
            auto p = makePlugin (48000.0, 256);
            juce::MemoryBlock states[2];
            {
                auto a = makePlugin (48000.0, 256);
                a->loadFactoryPreset (3);
                a->switchSlot (1);
                a->loadFactoryPreset (7);
                a->getStateInformation (states[0]);
                auto b = makePlugin (48000.0, 256);
                b->loadFactoryPreset (9);
                setReal (*b, pid::range, 11.0f);
                b->getStateInformation (states[1]);
            }
            std::atomic<bool> stop { false }, audioOk { true };
            std::atomic<int> blocks { 0 };
            const auto take = testsig::makeTake (48000.0, 2.0);
            std::thread audio ([&]
            {
                juce::AudioBuffer<float> buf (2, 256);
                juce::MidiBuffer midi;
                for (int pos = 0; ! stop.load(); pos = (pos + 256) % ((int) take.x.size() - 256))
                {
                    for (int ch = 0; ch < 2; ++ch)
                        buf.copyFrom (ch, 0, take.x.data() + pos, 256);
                    p->processBlock (buf, midi);
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < 256; ++i)
                            if (! std::isfinite (buf.getSample (ch, i)) || std::abs (buf.getSample (ch, i)) > 4.0f)
                                audioOk.store (false);
                    blocks.fetch_add (1);
                }
            });
            std::atomic<int> done { 0 };
            std::thread restorer ([&]
            {
                for (int k = 0; k < 120; ++k)
                {
                    p->setStateInformation (states[k % 2].getData(), (int) states[k % 2].getSize());
                    std::this_thread::sleep_for (std::chrono::milliseconds (2));
                }
                p->setStateInformation (states[1].getData(), (int) states[1].getSize());   // the last word
                done.fetch_add (1);
            });
            std::thread saver ([&]
            {
                for (int k = 0; k < 120; ++k)
                {
                    juce::MemoryBlock mb;
                    p->getStateInformation (mb);
                    if (juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize()) == nullptr)
                        audioOk.store (false);
                    std::this_thread::sleep_for (std::chrono::milliseconds (2));
                }
                done.fetch_add (1);
            });
            while (done.load() < 2)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
            restorer.join();
            saver.join();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (300);   // the processor's timer takes the meta over
            stop.store (true);
            audio.join();
            std::printf ("    %d audio blocks while 121 states were restored and 120 saved\n", blocks.load());
            check (audioOk.load(), "audio finite and bounded, every saved state readable");
            near (raw (*p, pid::range), 11.0, 0.05, "the last restored state holds");
            check (p->getFactoryIndex() == 9 && p->isPresetModified(), "its preset (Mix / Bus, modified) taken over on the message thread");
            check (p->getActiveSlot() == 0, "its A/B slot");
        }

        void testAutomationStress (bool full)
        {
            Test t ("host automation: every parameter (incl. mode / detection / listen / stereo / bypass) changes per block");
            juce::Random seedGen (31);
            std::vector<double> rates = full ? std::vector<double> (std::begin (allRates), std::end (allRates)) : std::vector<double> { 44100.0, 96000.0 };
            std::vector<int> blocks = full ? std::vector<int> (std::begin (allBlocks), std::end (allBlocks)) : std::vector<int> { 1, 64, 512, 4096 };
            blocks.push_back (-1);   // varying sizes
            float worstJump = 0.0f, worstPeak = 0.0f;
            int runs = 0, bad = 0, dropouts = 0;
            for (double fs : rates)
                for (int block : blocks)
                    for (int withListen = 0; withListen < 2; ++withListen)
                    {
                        auto p = makePlugin (fs, block > 0 ? block : 2048);
                        const auto [x, xr] = steadyVoiceAndS (fs, 1.5);
                        const auto take = testsig::makeTake (fs, 1.5, -10.0f);
                        const Sig in = plus (x, scaled (take.x, 0.7f)), inR = plus (xr, scaled (take.x, 0.5f));
                        juce::Random rnd (seedGen.nextInt());
                        const int every = block > 0 && block < 32 ? 32 / block : 1;   // tiny blocks: a change every 32 samples
                        const auto hook = [&] (int b, int, int)
                        {
                            if (b % every != 0)
                                return;
                            for (auto* prm : p->getParameters())
                            {
                                auto& rp = *dynamic_cast<juce::RangedAudioParameter*> (prm);
                                const auto id = rp.getParameterID();
                                const bool discrete = rp.getNumSteps() < 100;
                                if (id == pid::listen && withListen == 0)
                                    continue;
                                if (! discrete || rnd.nextFloat() < 0.3f)
                                    automate (rp, rnd.nextFloat());
                            }
                        };
                        const auto [yl, yr] = processStereo (*p, in, inR, block > 0 ? fixedBlocks (block) : varyingBlocks ((unsigned) seedGen.nextInt()), hook);
                        const int L = p->getLatencySamples();
                        // +12 dB output at most, the split band can never boost: 4x the input, a little headroom for the bell's skirt
                        const float inPeak = std::max (peakOf (in), peakOf (inR)), inStep = std::max (maxStep (in), maxStep (inR));
                        for (const auto* y : { &yl, &yr })
                        {
                            const auto st = statsOf (*y);
                            const float jump = st.step / (4.0f * inStep), pk = st.peak / (4.0f * inPeak);
                            worstJump = std::max (worstJump, jump);
                            worstPeak = std::max (worstPeak, pk);
                            const bool ok = st.finite && ! st.denormal && pk <= 1.25f && jump <= 1.25f;
                            bad += ok ? 0 : 1;
                            if (! ok)
                                std::printf ("    %.1f kHz block %d listen %d: finite %d denormal %d peak %.2f x step %.2f x\n", fs / 1000.0, block, withListen,
                                             (int) st.finite, (int) st.denormal, pk, jump);
                            if (withListen == 0)
                            {
                                const auto tail = statsOf (*y, L + (int) (fs * 0.05));
                                dropouts += tail.zeroRun > 8 ? 1 : 0;
                            }
                        }
                        ++runs;
                    }
            std::printf ("    %d runs: largest sample step %.2f of the bound (4 x the input's), largest peak %.2f of 4 x input\n", runs, worstJump, worstPeak);
            check (bad == 0, "finite, no denormals, bounded output, no jumps (" + juce::String (bad) + " channel runs failed)");
            check (dropouts == 0, "no dropouts (runs of zeros) while automating without listen (" + juce::String (dropouts) + ")");
        }

        void testSwitchClicks()
        {
            Test t ("clicks: high-passed output around mode / bypass / listen / stereo / detection / preset switches");
            const double fs = 96000.0, hpHz = 24000.0;   // the tones sit at 200 Hz and 7 kHz: everything above 24 kHz is a click
            const int block = 64, at = (int) (fs * 0.6);
            const auto [x, xr] = steadyVoiceAndS (fs, 1.2);
            const double ref = rmsOf (x, at - (int) fs / 10, at);
            // calibration: what a hard switch of the same kind would measure (the gain jumps by 5 dB, or dry -> band)
            Sig hard = x;
            for (int i = at; i < (int) hard.size(); ++i)
                hard[(size_t) i] *= gainOf (-5.0);
            const double hardDb = hfPeakDb (hard, fs, at - 2000, at + 2000, hpHz, ref);
            const double quietDb = hfPeakDb (x, fs, at - 20000, at + 20000, hpHz, ref);
            // what the de-esser does at the start of every S: the reduction comes in with the attack (threshold -10 -> -50 dB)
            double onsetDb = 0.0;
            {
                auto p = makePlugin (fs, block);
                setReal (*p, pid::threshold, -10.0f);
                p->prepareToPlay (fs, block);
                const auto [yl, yr] = processStereo (*p, x, xr, fixedBlocks (block), [&] (int, int start, int n) { if (at >= start && at < start + n) setReal (*p, pid::threshold, -50.0f); });
                onsetDb = std::max (hfPeakDb (yl, fs, at - 1000, at + p->getLatencySamples() + (int) (fs * 0.05), hpHz, ref),
                                    hfPeakDb (yr, fs, at - 1000, at + p->getLatencySamples() + (int) (fs * 0.05), hpHz, ref));
            }
            std::printf ("    reference: the input itself %.1f dB, a hard 5 dB gain step %.1f dB, the reduction setting in as on an S %.1f dB\n"
                         "    (loudest 1 ms of the output above 24 kHz at 96 kHz, against the signal's RMS)\n", quietDb, hardDb, onsetDb);
            check (hardDb > quietDb + 40.0, "the detector sees a hard switch");
            // a click: louder than the reduction coming in on an S (+3 dB) and than 30 dB under a hard step
            const double clickDb = std::max (onsetDb + 3.0, hardDb - 30.0);

            struct Case { juce::String name; std::function<void (EmberAudioProcessor&)> from, to; };
            const auto setp = [] (const char* id, float v) { return [id, v] (EmberAudioProcessor& p) { setReal (p, id, v); }; };
            const auto none = [] (EmberAudioProcessor&) {};
            std::vector<Case> cases {
                { "bypass on", none, setp (pid::bypass, 1.0f) },
                { "bypass off", setp (pid::bypass, 1.0f), setp (pid::bypass, 0.0f) },
                { "listen off -> S band", none, setp (pid::listen, 1.0f) },
                { "listen off -> delta", none, setp (pid::listen, 2.0f) },
                { "listen delta -> off", setp (pid::listen, 2.0f), setp (pid::listen, 0.0f) },
                { "listen S band -> delta", setp (pid::listen, 1.0f), setp (pid::listen, 2.0f) },
                { "split -> wideband", none, setp (pid::mode, 1.0f) },
                { "wideband -> split", setp (pid::mode, 1.0f), setp (pid::mode, 0.0f) },
                { "stereo L/R -> M/S", none, setp (pid::stereo, 1.0f) },
                { "stereo L/R -> Mid", none, setp (pid::stereo, 2.0f) },
                { "stereo Side -> L/R", setp (pid::stereo, 3.0f), setp (pid::stereo, 0.0f) },
                { "stereo L/R -> M/S (listen S band)", setp (pid::listen, 1.0f), setp (pid::stereo, 1.0f) },
                { "detection Smart -> Absolute", none, setp (pid::detection, 2.0f) },
                { "detection Relative -> Allround", setp (pid::detection, 1.0f), setp (pid::detection, 3.0f) },
                { "lookahead 5 -> 0 ms", none, setp (pid::lookahead, 0.0f) },
                { "lookahead 0 -> 10 ms", setp (pid::lookahead, 0.0f), setp (pid::lookahead, 10.0f) },
                { "link 100 -> 0 %", none, setp (pid::link, 0.0f) },
                { "frequency 4 -> 9 kHz", setp (pid::freq, 4000.0f), setp (pid::freq, 9000.0f) },
                { "width 1 -> 4 oct", setp (pid::width, 1.0f), setp (pid::width, 4.0f) },
                { "mix 100 -> 0 %", none, setp (pid::mix, 0.0f) },
                { "output 0 -> +12 dB", none, setp (pid::output, 12.0f) },
                { "range 5 -> 24 dB", none, setp (pid::range, 24.0f) },
                { "Amount 50 -> 0 %", none, setp (pid::amount, 0.0f) },
                { "undo of a range edit", none, [] (EmberAudioProcessor& p) { setReal (p, pid::range, 20.0f); p.undo(); } },
                { "A/B to a different setting", [] (EmberAudioProcessor& p) { p.switchSlot (1); setReal (p, pid::mode, 1.0f); setReal (p, pid::range, 12.0f); p.switchSlot (0); },
                                                [] (EmberAudioProcessor& p) { p.switchSlot (1); } },
            };
            for (int i = 0; i + 1 < (int) factoryPresets().size(); ++i)
                cases.push_back ({ juce::String ("preset ") + factoryPresets()[(size_t) i].nameEn + " -> " + factoryPresets()[(size_t) i + 1].nameEn,
                                   [i] (EmberAudioProcessor& p) { p.loadFactoryPreset (i); }, [i] (EmberAudioProcessor& p) { p.loadFactoryPreset (i + 1); } });
            double worst = -200.0;
            juce::String worstName;
            int clicks = 0;
            for (const auto& c : cases)
            {
                auto p = makePlugin (fs, block);
                c.from (*p);
                p->prepareToPlay (fs, block);   // "from" is where the session starts (no fade at the start)
                const auto [yl, yr] = processStereo (*p, x, xr, fixedBlocks (block), [&] (int, int start, int n)
                {
                    if (at >= start && at < start + n)
                        c.to (*p);
                });
                const int L = p->getLatencySamples();
                const double db = std::max (hfPeakDb (yl, fs, at - 1000, at + L + (int) (fs * 0.05), hpHz, ref),
                                            hfPeakDb (yr, fs, at - 1000, at + L + (int) (fs * 0.05), hpHz, ref));
                const double base = std::max (hfPeakDb (yl, fs, at - (int) (fs * 0.2), at - 1000, hpHz, ref), hfPeakDb (yr, fs, at - (int) (fs * 0.2), at - 1000, hpHz, ref));
                const bool click = db > clickDb;
                std::printf ("    %-48s %6.1f dB (before the switch %6.1f)%s\n", c.name.toRawUTF8(), db, base, click ? "  <-- CLICK" : "");
                clicks += click ? 1 : 0;
                if (db > worst) { worst = db; worstName = c.name; }
            }
            std::printf ("    loudest: %s %.1f dB = %.1f dB under a hard 5 dB step (click limit %.1f dB)\n", worstName.toRawUTF8(), worst, hardDb - worst, clickDb);
            check (clicks == 0, "no switch clicks (" + juce::String (clicks) + " do)");
        }

        void testDelta()
        {
            Test t ("delta = input - output, sample-aligned, in every mode");
            const double fs = 48000.0;
            const auto take = testsig::makeTake (fs, 2.5, -10.0f);
            const Sig l = take.x, r = rightOf (take.x);
            struct Case { const char* name; std::vector<std::pair<const char*, float>> set; };
            const Case cases[] {
                { "default", {} },
                { "wideband", { { pid::mode, 1.0f } } },
                { "mix 40 %", { { pid::mix, 40.0f } } },
                { "M/S", { { pid::stereo, 1.0f } } },
                { "Mid", { { pid::stereo, 2.0f } } },
                { "Side", { { pid::stereo, 3.0f } } },
                { "link 30 %", { { pid::link, 30.0f } } },
                { "Allround, narrow, range 20", { { pid::detection, 3.0f }, { pid::width, 1.0f }, { pid::range, 20.0f }, { pid::threshold, -45.0f } } },
                { "absolute, Amount 100 %", { { pid::detection, 2.0f }, { pid::amount, 100.0f }, { pid::threshold, -40.0f } } },
            };
            for (const auto& c : cases)
            {
                auto a = makePlugin (fs, 333), d = makePlugin (fs, 333);
                for (auto& [id, v] : c.set) { setReal (*a, id, v); setReal (*d, id, v); }
                setReal (*d, pid::listen, 2.0f);
                a->prepareToPlay (fs, 333);
                d->prepareToPlay (fs, 333);
                const auto [al, ar] = processStereo (*a, l, r, fixedBlocks (333));
                const auto [dl, dr] = processStereo (*d, l, r, varyingBlocks (5));
                const int L = a->getLatencySamples();
                const auto dlIn = delayed (l, L), drIn = delayed (r, L);
                float err = 0.0f, removed = 0.0f;
                for (size_t i = 0; i < al.size(); ++i)
                {
                    err = std::max (err, std::max (std::abs (dl[i] - (dlIn[i] - al[i])), std::abs (dr[i] - (drIn[i] - ar[i]))));
                    removed = std::max (removed, std::abs (dl[i]));
                }
                std::printf ("    %-28s delta peak %.4f, |delta - (in - out)| <= %.2e\n", c.name, removed, err);
                check (removed > 1.0e-3f && err < 2.0e-6f, juce::String (c.name) + ": delta == delayed input - output");
            }
            // with output gain the delta is what is removed at the output level: delta = g x in - out
            {
                auto a = makePlugin (fs, 512), d = makePlugin (fs, 512);
                for (auto* p : { a.get(), d.get() })
                    setReal (*p, pid::output, 6.0f);
                setReal (*d, pid::listen, 2.0f);
                a->prepareToPlay (fs, 512);
                d->prepareToPlay (fs, 512);
                const auto [al, ar] = processStereo (*a, l, r, fixedBlocks (512));
                const auto [dl, dr] = processStereo (*d, l, r, fixedBlocks (512));
                const auto in = delayed (l, a->getLatencySamples());
                const float g = gainOf (6.0);
                float err = 0.0f;
                for (size_t i = 0; i < al.size(); ++i)
                    err = std::max (err, std::abs (dl[i] - (g * in[i] - al[i])));
                check (err < 4.0e-6f, "output +6 dB: delta == g * input - output (" + juce::String (err, 8) + ")");
            }
        }

        // ---------------------------------------------------------------- info cards
        struct InfoEntry { juce::String kind, title, value, text; };

        juce::String kindOf (juce::Component* c)
        {
            if (dynamic_cast<vocalypse::ui::Knob*> (c) != nullptr) return "knob " + dynamic_cast<vocalypse::ui::Knob*> (c)->getParamId();
            if (dynamic_cast<vocalypse::ui::Segments*> (c) != nullptr) return "segments " + dynamic_cast<vocalypse::ui::Segments*> (c)->getParamId();
            if (dynamic_cast<ReductionMeter*> (c) != nullptr) return "reduction meter";
            if (dynamic_cast<DetectorMeter*> (c) != nullptr) return "S level meter";
            if (dynamic_cast<BandDisplay*> (c) != nullptr) return "spectrum";
            if (dynamic_cast<LiveMascotView*> (c) != nullptr) return "Ember";
            if (auto* b = dynamic_cast<juce::Button*> (c)) return "button " + b->getName() + "/" + b->getButtonText();
            if (dynamic_cast<juce::Slider*> (c) != nullptr) return "slider";
            return {};
        }

        // The controls of one view and their info cards, in a fixed order (the same in both languages)
        std::vector<InfoEntry> infoCards (EmberEditor& ed, int& uncovered, juce::StringArray& missing)
        {
            auto& panel = ed.getPanel();
            std::vector<juce::Component*> all;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
            {
                for (auto* ch : c.getChildren())
                {
                    if (! ch->isVisible())
                        continue;   // hidden overlays (licence card, about) and the other view
                    all.push_back (ch);
                    if (dynamic_cast<vocalypse::ui::Knob*> (ch) == nullptr && dynamic_cast<vocalypse::ui::Segments*> (ch) == nullptr)
                        walk (*ch);   // a knob's slider and a segment's buttons share their parent's card
                }
            };
            walk (panel);
            std::vector<InfoEntry> out;
            for (auto* c : all)
            {
                const auto kind = kindOf (c);
                if (kind.isEmpty() || c->getBounds().isEmpty())
                    continue;
                const vocalypse::ui::HoverInfo* info = nullptr;
                for (auto* a = c; a != nullptr && a != &panel && info == nullptr; a = a->getParentComponent())
                    for (auto* h : vocalypse::ui::HoverInfo::getAllForTests())
                        if (&h->getAnchor() == a)
                            info = h;
                if (info == nullptr)
                {
                    ++uncovered;
                    missing.add (kind);
                    continue;
                }
                out.push_back ({ kind, info->getTitle(), info->getValue(), info->getDescription() });
            }
            return out;
        }

        bool looksGerman (const juce::String& s)
        {
            const auto t = " " + s.toLowerCase() + " ";
            for (const char* w : { " und ", " der ", " die ", " das ", " nicht ", " wird ", " werden ", " ist ", " mit ", " sie ", " ein ", " eine " })
                if (t.contains (w))
                    return true;
            return t.containsAnyOf (juce::String::fromUTF8 ("äöüßÄÖÜ„"));
        }

        bool looksEnglish (const juce::String& s)
        {
            const auto t = " " + s.toLowerCase() + " ";
            for (const char* w : { " the ", " and ", " you ", " is ", " of ", " with ", " only ", " from " })
                if (t.contains (w))
                    return true;
            return false;
        }

        void testInfoCards()
        {
            Test t ("info cards: every control of both views explains itself, in German and in English");
            UserSettings::useFileForTests (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ember_qa.settings"));
            auto p = makePlugin (48000.0, 512);
            std::map<juce::String, std::vector<InfoEntry>> byLang[2];
            for (int lang = 0; lang < 2; ++lang)
            {
                vocalypse::i18n::overrideSystemLanguage (lang == 0 ? vocalypse::i18n::Language::german : vocalypse::i18n::Language::english);
                std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
                auto* ed = dynamic_cast<EmberEditor*> (base.get());
                if (ed == nullptr)
                {
                    check (false, "editor");
                    return;
                }
                ed->setSize (look::baseW, look::baseH);
                for (bool adv : { false, true })
                {
                    ed->setAdvanced (adv);
                    ed->tickForTest();
                    int uncovered = 0;
                    juce::StringArray missing;
                    auto cards = infoCards (*ed, uncovered, missing);
                    const juce::String where = juce::String (lang == 0 ? "DE " : "EN ") + (adv ? "ADVANCED" : "SIMPLE");
                    check (uncovered == 0, where + ": every control has an info card (missing: " + missing.joinIntoString (", ") + ")");
                    check (cards.size() >= (adv ? 20u : 12u), where + ": " + juce::String ((int) cards.size()) + " controls found");
                    std::printf ("    %s: %d controls, each with an info card\n", where.toRawUTF8(), (int) cards.size());
                    int emptyTitle = 0, emptyText = 0, wrongLang = 0, badValue = 0;
                    for (const auto& c : cards)
                    {
                        emptyTitle += c.title.trim().isEmpty() ? 1 : 0;
                        emptyText += c.text.trim().length() < 12 ? 1 : 0;
                        const bool wrong = lang == 1 ? looksGerman (c.text) || looksGerman (c.title) : (looksEnglish (c.text) && ! looksGerman (c.text));
                        if (wrong)
                            std::printf ("    %s %s: \"%s\"\n", where.toRawUTF8(), c.kind.toRawUTF8(), c.text.substring (0, 80).toRawUTF8());
                        wrongLang += wrong ? 1 : 0;
                        badValue += (c.value.containsIgnoreCase ("nan") || c.value.containsIgnoreCase ("inf")) ? 1 : 0;
                        // units in the read-outs follow the language too ("3,00 Okt.", not "3,00 oct")
                        if (lang == 0 && c.value.contains ("oct"))
                        {
                            std::printf ("    %s %s: value \"%s\"\n", where.toRawUTF8(), c.kind.toRawUTF8(), c.value.toRawUTF8());
                            ++wrongLang;
                        }
                    }
                    check (emptyTitle == 0 && emptyText == 0, where + ": no empty title / text (" + juce::String (emptyTitle) + " / " + juce::String (emptyText) + ")");
                    check (wrongLang == 0, where + ": every text in the interface language (" + juce::String (wrongLang) + " not)");
                    check (badValue == 0, where + ": value read-outs are numbers");
                    byLang[lang][adv ? "adv" : "simple"] = cards;
                    // button texts in English carry no German
                    if (lang == 1)
                    {
                        juce::StringArray german;
                        for (const auto& c : cards)
                            if (c.kind.startsWith ("button") && looksGerman (c.kind))
                                german.add (c.kind);
                        check (german.isEmpty(), where + ": button texts in English (" + german.joinIntoString (", ") + ")");
                    }
                }
            }
            vocalypse::i18n::overrideSystemLanguage (std::nullopt);
            for (const char* view : { "simple", "adv" })
            {
                const auto& de = byLang[0][view];
                const auto& en = byLang[1][view];
                check (de.size() == en.size(), juce::String (view) + ": the same controls in both languages");
                int untranslated = 0;
                for (size_t i = 0; i < std::min (de.size(), en.size()); ++i)
                    if (de[i].text == en[i].text)
                    {
                        ++untranslated;
                        std::printf ("    untranslated (%s): %s\n", view, de[i].kind.toRawUTF8());
                    }
                check (untranslated == 0, juce::String (view) + ": every description translated (" + juce::String (untranslated) + " identical)");
            }
        }

        void testParameterText()
        {
            Test t ("parameter ranges, display strings and typed values (host text -> value) in both languages");
            auto p = makePlugin (48000.0, 512);
            for (auto lang : { vocalypse::i18n::Language::german, vocalypse::i18n::Language::english })
            {
                vocalypse::i18n::overrideSystemLanguage (lang);
                const juce::String L = lang == vocalypse::i18n::Language::german ? "DE " : "EN ";
                for (auto* prm : p->getParameters())
                {
                    auto& rp = *dynamic_cast<juce::RangedAudioParameter*> (prm);
                    const auto id = rp.getParameterID();
                    check (rp.getName (64).isNotEmpty(), L + id + ": name");
                    const auto& rg = rp.getNormalisableRange();
                    check (rp.getDefaultValue() >= 0.0f && rp.getDefaultValue() <= 1.0f, L + id + ": default inside the range");
                    int worst = 0;
                    juce::String example;
                    const int steps = 41;
                    for (int k = 0; k < steps; ++k)
                    {
                        const float n = rg.snapToLegalValue (rg.convertFrom0to1 ((float) k / (steps - 1)));
                        const float norm = rp.convertTo0to1 (n);
                        const auto text = rp.getText (norm, 64);
                        if (text.isEmpty() || text.containsIgnoreCase ("nan"))
                            worst = std::max (worst, 3);
                        const float back = rp.convertFrom0to1 (rp.getValueForText (text));
                        // the read-out is rounded: it must come back within its own last digit
                        const float tol = id == pid::freq ? 5.0f
                                        : (id == pid::threshold || id == pid::range || id == pid::output) ? 0.05f
                                        : (id == pid::release || id == pid::mix || id == pid::amount || id == pid::link) ? 0.5f
                                        : (id == pid::attack || id == pid::lookahead || id == pid::width) ? 0.005f
                                        : std::max (rg.interval, 1.0e-4f);
                        if (std::abs (back - n) > tol + 1.0e-4f)
                        {
                            if (worst < 2)
                                example = "\"" + text + "\" -> " + juce::String (back, 3) + " (was " + juce::String (n, 3) + ")";
                            worst = std::max (worst, 2);
                        }
                    }
                    check (worst < 3, L + id + ": every value has a read-out");
                    check (worst < 2, L + id + ": typed read-out gives the value back " + example);
                    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (&rp))
                        for (int i = 0; i < c->choices.size(); ++i)
                            check (std::lround (rp.convertFrom0to1 (rp.getValueForText (c->choices[i]))) == i, L + id + ": choice name " + c->choices[i]);
                }
            }
            vocalypse::i18n::overrideSystemLanguage (std::nullopt);
            // typed values a user enters in a host's generic editor
            const struct { const char* id; const char* text; float want; } typed[] {
                { pid::freq, "7 kHz", 7000.0f }, { pid::freq, "6,5 kHz", 6500.0f }, { pid::freq, "5500", 5500.0f }, { pid::freq, "5500 Hz", 5500.0f },
                { pid::threshold, "-40", -40.0f }, { pid::threshold, "\xe2\x88\x92" "35,5 dB", -35.5f }, { pid::threshold, "-27.3 dB", -27.3f },
                { pid::attack, "2,5 ms", 2.5f }, { pid::mix, "50 %", 50.0f }, { pid::output, "+3 dB", 3.0f }, { pid::width, "1,5 oct", 1.5f }, { pid::width, "2,25 Okt.", 2.25f },
            };
            for (const auto& c : typed)
            {
                auto& rp = param (*p, c.id);
                const float got = rp.convertFrom0to1 (rp.getValueForText (juce::String::fromUTF8 (c.text)));
                near (got, c.want, std::max (0.051, std::abs (c.want) * 0.001), juce::String (c.id) + ": typed \"" + juce::String::fromUTF8 (c.text) + "\"");
            }
            // every parameter at its extremes at once: the audio stays sane
            for (int extreme = 0; extreme < 2; ++extreme)
            {
                auto q = makePlugin (48000.0, 256);
                for (auto* prm : q->getParameters())
                    if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm); rp != nullptr && rp->getParameterID() != pid::bypass && rp->getParameterID() != pid::listen)
                        automate (*rp, (float) extreme);
                q->prepareToPlay (48000.0, 256);
                const auto take = testsig::makeTake (48000.0, 2.0, -8.0f);
                const auto [yl, yr] = processStereo (*q, take.x, rightOf (take.x), fixedBlocks (256));
                const auto st = statsOf (yl);
                std::printf ("    all parameters at %s: output peak %.3f (input %.3f)\n", extreme ? "max" : "min", st.peak, peakOf (take.x));
                check (st.finite && ! st.denormal && st.peak <= 4.1f * peakOf (take.x) && st.peak > 0.0f, juce::String ("all at ") + (extreme ? "max" : "min") + ": finite, bounded");
            }
        }

        // =============================================================== PART 5: measurements
        void testLatency (bool full)
        {
            Test t ("latency: reported == measured (impulse), every rate, every lookahead, both modes, bypass");
            int wrong = 0, cases = 0;
            std::printf ("    rate      reported  measured (lookahead 0 / 1 / 2.5 / 5 / 7.5 / 10 ms, split / wide / bypass)\n");
            for (double fs : allRates)
            {
                juce::String line;
                const int reported = makePlugin (fs, 512)->getLatencySamples();
                std::set<int> measured;
                for (float la : { 0.0f, 1.0f, 2.5f, 5.0f, 7.5f, 10.0f })
                    for (int mode = 0; mode < 3; ++mode)
                    {
                        if (! full && la != 0.0f && la != 10.0f && la != 5.0f)
                            continue;
                        auto p = makePlugin (fs, 512);
                        setReal (*p, pid::lookahead, la);
                        setReal (*p, pid::mode, mode == 1 ? 1.0f : 0.0f);
                        setReal (*p, pid::bypass, mode == 2 ? 1.0f : 0.0f);
                        p->prepareToPlay (fs, 512);
                        Sig x ((size_t) (fs * 0.05), 0.0f);
                        x[100] = 0.5f;
                        const auto [yl, yr] = processStereo (*p, x, x, fixedBlocks (512));
                        int arg = 0;
                        for (int i = 0; i < (int) yl.size(); ++i)
                            if (std::abs (yl[(size_t) i]) > std::abs (yl[(size_t) arg]))
                                arg = i;
                        measured.insert (arg - 100);
                        wrong += (arg - 100 != reported || p->getLatencySamples() != reported) ? 1 : 0;
                        ++cases;
                    }
                for (int m : measured)
                    line << m << " ";
                std::printf ("    %6.1f k  %6d    %s\n", fs / 1000.0, reported, line.toRawUTF8());
                check (reported == (int) std::lround (0.01 * fs), "reported = 10 ms at " + juce::String (fs));
            }
            check (wrong == 0, "measured impulse delay == reported latency in " + juce::String (cases) + " cases (" + juce::String (wrong) + " differ)");
        }

        void testNullAndDistortion()
        {
            Test t ("null without reduction (bit-exact) and THD+N under reduction");
            // no reduction: tones below the threshold, quiet signals, bypass with overs, Amount 0 % -> bit-exact
            int notNull = 0, nullCases = 0;
            for (double fs : allRates)
            {
                const struct { const char* name; Sig x; std::vector<std::pair<const char*, float>> set; } cases[] {
                    { "sweep 20 Hz-20 kHz -24 dBFS, absolute, thr -10", sweep (fs, 2.0, 20.0, std::min (20000.0, fs * 0.45), -24.0), { { pid::detection, 2.0f }, { pid::threshold, -10.0f } } },
                    { "noise -80 dBFS (under the relative floor)", noise (fs, 1.0, -80.0, 3), {} },
                    { "voice+S Amount 0 %", testsig::makeTake (fs, 1.5).x, { { pid::amount, 0.0f } } },
                    { "voice+S +6 dBFS bypass", scaled (testsig::makeTake (fs, 1.5).x, 2.0f / std::max (1.0e-6f, peakOf (testsig::makeTake (fs, 1.5).x))), { { pid::bypass, 1.0f } } },
                    { "low tones only (300 Hz + 1 kHz), smart", fadeIn (plus (sine (fs, 1.5, 300.0, -6.0), sine (fs, 1.5, 1000.0, -12.0)), fs), {} },
                };
                for (const auto& c : cases)
                    for (int mode = 0; mode < 2; ++mode)
                    {
                        auto p = makePlugin (fs, 512);
                        for (auto& [id, v] : c.set)
                            setReal (*p, id, v);
                        setReal (*p, pid::mode, (float) mode);
                        p->prepareToPlay (fs, 512);
                        const auto [yl, yr] = processStereo (*p, c.x, c.x, fixedBlocks (512));
                        const bool ok = identical (yl, delayed (c.x, p->getLatencySamples()));
                        if (! ok)
                            std::printf ("    not null: %.1f kHz %s %s (max diff %.2e)\n", fs / 1000.0, c.name, mode ? "wideband" : "split", maxDiff (yl, delayed (c.x, p->getLatencySamples())));
                        notNull += ok ? 0 : 1;
                        ++nullCases;
                    }
            }
            check (notNull == 0, "bit-exact null in " + juce::String (nullCases) + " cases (" + juce::String (notNull) + " not)");

            // after an S has been reduced, the audio goes back to exactly the input (no residual gain error); Relative
            // detection keeps the gate open, so only the envelope's release brings the gain back to 1
            for (int mode = 0; mode < 4; ++mode)
            {
                const double fs = 48000.0;
                auto p = makePlugin (fs, 512);
                setReal (*p, pid::mode, (float) (mode % 2));
                setReal (*p, pid::detection, mode >= 2 ? 1.0f : 0.0f);
                setReal (*p, pid::threshold, -45.0f);
                p->prepareToPlay (fs, 512);
                auto x = testsig::makeTake (fs, 1.0).x;
                const auto low = fadeIn (plus (sine (fs, 4.0, 300.0, -6.0), sine (fs, 4.0, 1000.0, -12.0)), fs, 50.0);
                x.insert (x.end(), low.begin(), low.end());
                const auto [yl, yr] = processStereo (*p, x, x, fixedBlocks (512));
                const auto ref = delayed (x, p->getLatencySamples());
                int firstExact = -1;
                for (int i = (int) ref.size() - 1; i >= 0; --i)
                    if (yl[(size_t) i] != ref[(size_t) i]) { firstExact = i + 1; break; }
                const double after = (firstExact - (int) (fs * 1.0) - p->getLatencySamples()) / fs;
                const juce::String name = juce::String (mode % 2 ? "wideband" : "split") + (mode >= 2 ? " / Relative" : " / Smart");
                std::printf ("    %s: bit-exact again %.2f s after the last S (largest deviation 0.2 s after it %.2e)\n", name.toRawUTF8(), std::max (0.0, after),
                             maxDiff (yl, ref, (int) (fs * 1.2) + p->getLatencySamples()));
                check (after < 1.5, name + ": back to a bit-exact null within 1.5 s after the last S");
            }

            // THD+N while reducing (absolute detection, steady tones above the threshold)
            std::printf ("    THD+N under reduction (48 kHz, absolute, thr -30, tone at thr + 12 dB, range 12):\n");
            std::printf ("      mode      tone      reduction   THD+N (default times)   THD+N (attack 0.1 / release 400 ms)\n");
            const double fs = 48000.0;
            double worstDefault = -200.0;
            for (int mode = 0; mode < 2; ++mode)
                for (double hz : { 7000.0, 9000.0, 11000.0, 16000.0 })
                {
                    double thd[2] {}, red = 0.0;
                    for (int times = 0; times < 2; ++times)
                    {
                        Engine e;
                        Settings s;
                        s.smart = false;
                        s.relative = false;
                        s.freq = 6500.0f;
                        s.thresholdDb = -30.0f;
                        s.rangeDb = 12.0f;
                        s.mode = mode;
                        if (times == 1) { s.attackMs = 0.1f; s.releaseMs = 400.0f; }
                        e.setSettings (s);
                        e.prepare (fs, 512);
                        const double level = absoluteLevelDb (fs, 6500.0, 1.0, hz);
                        auto x = sine (fs, 1.0, hz, -30.0 + 12.0 - level);
                        Sig l = x, r = x;
                        for (int st = 0; st < (int) l.size(); st += 512)
                        {
                            float* ch[2] { l.data() + st, r.data() + st };
                            e.process (ch, 2, std::min (512, (int) l.size() - st));
                        }
                        const auto f = fitSine (l, fs, hz, (int) (fs * 0.5), (int) (fs * 0.95));
                        thd[times] = f.thdnDb;
                        if (times == 0)
                            red = toDb (gainOf (-30.0 + 12.0 - level)) - toDb (f.amplitude);
                    }
                    worstDefault = std::max (worstDefault, thd[0]);
                    std::printf ("      %-9s %5.0f Hz  %6.2f dB    %7.1f dB              %7.1f dB\n", mode ? "wideband" : "split", hz, red, thd[0], thd[1]);
                }
            check (worstDefault < -40.0, "THD+N under 9 dB of reduction stays below -40 dB (1 %)");
        }

        void testReductionAccuracy (bool full)
        {
            Test t ("gain-reduction accuracy: steady tone -> measured reduction vs the threshold / ratio / knee / range / Amount curve");
            const double fs = 48000.0, freq = 6500.0;
            const double wideHz = 9000.0;
            const auto shape = splitBand (freq, 3.0);
            const double splitHz = shape.centreHz;
            struct Row { int mode; double thr, over, range, amount, expect, measured, meter, detector, level; };
            std::vector<Row> rows;
            const std::vector<double> thrs = full ? std::vector<double> { -50.0, -40.0, -30.0, -20.0, -10.0 } : std::vector<double> { -40.0, -20.0 };
            for (int mode = 0; mode < 2; ++mode)
                for (double thr : thrs)
                    for (double over : { -6.0, -3.0, 0.0, 3.0, 6.0, 12.0, 20.0 })
                        for (double range : { 3.0, 6.0, 12.0, 24.0 })
                            for (double amount : { 0.25, 0.5, 1.0 })
                            {
                                if (! full && (range == 3.0 || amount == 0.25) && over != 12.0)
                                    continue;
                                const double hz = mode == 0 ? splitHz : wideHz;
                                const double lvl1 = absoluteLevelDb (fs, freq, 1.0, hz);
                                const double ampDb = thr + over - lvl1;
                                if (ampDb > 0.0)
                                    continue;   // the tone would be over full scale
                                Engine e;
                                Settings s;
                                s.smart = false;
                                s.relative = false;
                                s.freq = (float) freq;
                                s.thresholdDb = (float) thr;
                                s.rangeDb = (float) range;
                                s.amount = (float) amount;
                                s.mode = mode;
                                s.attackMs = 0.1f;   // the static curve: the envelope rides the tone's crest
                                s.releaseMs = 400.0f;
                                e.setSettings (s);
                                e.prepare (fs, 512);
                                const auto x = sine (fs, 0.6, hz, ampDb);
                                Sig l = x, r = x;
                                float meter = 0.0f, det = -200.0f;
                                for (int st = 0; st < (int) l.size(); st += 512)
                                {
                                    float* ch[2] { l.data() + st, r.data() + st };
                                    e.process (ch, 2, std::min (512, (int) l.size() - st));
                                    const float m = e.gainReductionDb.pull();
                                    if (st > (int) (fs * 0.3))
                                    {
                                        meter = std::max (meter, m);
                                        det = std::max (det, e.detectorDb.load());
                                    }
                                }
                                const auto f = fitSine (l, fs, hz, (int) (fs * 0.35), (int) (fs * 0.58));
                                const double measuredAtTone = toDb (gainOf (ampDb)) - toDb (f.amplitude);
                                const double expectR = expectedReduction (thr + over, s);
                                // split: the band's response at the tone for that reduction (the band's own shape)
                                const double expect = mode == 0 ? -splitResponseDb (fs, freq, 3.0, expectR, hz) : expectR;
                                rows.push_back ({ mode, thr, over, range, amount, expect, measuredAtTone, meter, det, thr + over });
                            }
            double worst[2] {}, worstMeter[2] {}, worstDet = 0.0, worstDepth = 0.0;
            for (const auto& r : rows)
            {
                worst[r.mode] = std::max (worst[r.mode], std::abs (r.measured - r.expect));
                // the meter shows the reduction the curve asks for (in split: the band's nominal depth)
                Settings nominalSettings;
                nominalSettings.thresholdDb = (float) r.thr;
                nominalSettings.rangeDb = (float) r.range;
                nominalSettings.amount = (float) r.amount;
                const double nominal = expectedReduction (r.level, nominalSettings);
                worstMeter[r.mode] = std::max (worstMeter[r.mode], std::abs (r.meter - (r.mode == 0 ? nominal : r.measured)));
                if (r.mode == 0)
                    worstDepth = std::max (worstDepth, nominal - r.measured);
                worstDet = std::max (worstDet, std::abs (r.detector - r.level));
            }
            std::printf ("    split: the band's centre loses up to %.2f dB less than its nominal depth (deep reductions: the blend past the fixed -12 dB cut)\n", worstDepth);
            std::printf ("    %d cases: |measured - expected| <= %.3f dB split, %.3f dB wideband\n", (int) rows.size(), worst[0], worst[1]);
            std::printf ("    reduction meter vs measured: <= %.3f dB split, %.3f dB wideband; S-level meter vs tone level <= %.3f dB\n", worstMeter[0], worstMeter[1], worstDet);
            std::printf ("      mode  thr   over  range Amount  expected measured  meter\n");
            for (const auto& r : rows)
                if (r.thr == -20.0 && (r.over == 0.0 || r.over == 3.0 || r.over == 12.0) && (r.range == 6.0 || r.range == 24.0) && r.amount != 0.25)
                    std::printf ("      %-5s %4.0f %+5.0f %5.0f %5.0f %%  %7.2f %8.2f %7.2f\n", r.mode ? "wide" : "split", r.thr, r.over, r.range, r.amount * 100.0, r.expect, r.measured, r.meter);
            check (worst[0] < 0.35 && worst[1] < 0.35, "static curve within 0.35 dB (split at the band centre, wideband)");
            check (worstMeter[0] < 0.35 && worstMeter[1] < 0.35, "reduction meter within 0.35 dB (wideband: of the measured reduction, split: of the band's depth)");
            check (worstDet < 0.35, "S-level meter within 0.35 dB of the detector level of the tone");

            // default times: the 0.5 ms attack lets the envelope sit a little under the crest of a steady tone
            {
                Engine e;
                Settings s;
                s.smart = false; s.relative = false; s.freq = (float) freq; s.thresholdDb = -30.0f; s.rangeDb = 24.0f; s.mode = 1;
                e.setSettings (s);
                e.prepare (fs, 512);
                const double ampDb = -30.0 + 12.0 - absoluteLevelDb (fs, freq, 1.0, wideHz);
                Sig l = sine (fs, 0.6, wideHz, ampDb), r = l;
                for (int st = 0; st < (int) l.size(); st += 512) { float* ch[2] { l.data() + st, r.data() + st }; e.process (ch, 2, std::min (512, (int) l.size() - st)); }
                const double m = toDb (gainOf (ampDb)) - toDb (fitSine (l, fs, wideHz, (int) (fs * 0.35), (int) (fs * 0.58)).amplitude);
                std::printf ("    default attack 0.5 / release 60 ms, wideband 12 dB over: measured %.2f dB, curve %.2f dB\n", m, expectedReduction (-18.0, s));
                check (std::abs (m - expectedReduction (-18.0, s)) < 1.0, "default times within 1 dB of the curve");
            }
            // mix: what the meter shows against what the tone loses
            {
                std::printf ("    mix (wideband, 9 dB reduction): meter vs measured\n");
                double worstMix = 0.0;
                for (float mix : { 1.0f, 0.75f, 0.5f, 0.25f, 0.0f })
                {
                    Engine e;
                    Settings s;
                    s.smart = false; s.relative = false; s.freq = (float) freq; s.thresholdDb = -30.0f; s.rangeDb = 24.0f; s.mode = 1;
                    s.attackMs = 0.1f; s.releaseMs = 400.0f; s.mix = mix;
                    e.setSettings (s);
                    e.prepare (fs, 512);
                    const double ampDb = -30.0 + 12.0 - absoluteLevelDb (fs, freq, 1.0, wideHz);
                    Sig l = sine (fs, 0.6, wideHz, ampDb), r = l;
                    float meter = 0.0f;
                    for (int st = 0; st < (int) l.size(); st += 512)
                    {
                        float* ch[2] { l.data() + st, r.data() + st };
                        e.process (ch, 2, std::min (512, (int) l.size() - st));
                        const float mm = e.gainReductionDb.pull();
                        if (st > (int) (fs * 0.3)) meter = std::max (meter, mm);
                    }
                    const double m = toDb (gainOf (ampDb)) - toDb (fitSine (l, fs, wideHz, (int) (fs * 0.35), (int) (fs * 0.58)).amplitude);
                    std::printf ("      mix %3.0f %%: meter %.2f dB, measured %.2f dB\n", mix * 100.0f, meter, m);
                    worstMix = std::max (worstMix, std::abs (meter - m));
                }
                check (worstMix < 0.35, "the reduction meter follows Mix (shows what is really taken off)");
            }
            // relative detection: the S-level meter reads band / full level + reference
            {
                Engine e;
                Settings s;
                s.smart = false; s.relative = true; s.freq = (float) freq; s.thresholdDb = -10.0f;
                e.setSettings (s);
                e.prepare (fs, 512);
                const double a = 0.1, b = 0.3;   // 9 kHz tone and a 300 Hz body three times louder
                auto x = plus (sine (fs, 0.6, wideHz, toDb (a)), sine (fs, 0.6, 300.0, toDb (b)));
                Sig l = x, r = x;
                float det = -200.0f;
                for (int st = 0; st < (int) l.size(); st += 512)
                {
                    float* ch[2] { l.data() + st, r.data() + st };
                    e.process (ch, 2, std::min (512, (int) l.size() - st));
                    if (st > (int) (fs * 0.3)) det = std::max (det, e.detectorDb.load());
                }
                const double want = absoluteLevelDb (fs, freq, a, wideHz) - toDb (peakOf (x, (int) (fs * 0.3))) + referenceDb;
                std::printf ("    relative detection: S-level meter %.2f dB, expected %.2f dB\n", det, want);
                check (std::abs (det - want) < 0.5, "relative S level = band against the full level + reference");
            }
        }

        void testMatrix (bool full)
        {
            Test t ("matrix: rates x block sizes x mono / stereo x signals (finite, bounded, silence, block-size invariance)");
            const std::vector<double> rates = full ? std::vector<double> (std::begin (allRates), std::end (allRates)) : std::vector<double> { 44100.0, 192000.0 };
            const std::vector<int> blocks = full ? std::vector<int> (std::begin (allBlocks), std::end (allBlocks)) : std::vector<int> { 1, 7, 512, 4096 };
            int runs = 0, bad = 0, variant = 0, silenceBad = 0;
            float worstVariance = 0.0f, worstGrowth = 0.0f;
            for (double fs : rates)
            {
                const auto takeStrong = testsig::makeTake (fs, 1.2, -6.0f, -12.0f);
                const auto takeSubtle = testsig::makeTake (fs, 1.2, -26.0f, -12.0f, true, 9);
                const float loudGain = 2.0f / peakOf (takeStrong.x);   // +6 dBFS float overs
                const std::vector<std::pair<const char*, Sig>> signals {
                    { "silence", Sig ((size_t) (fs * 0.5), 0.0f) },
                    { "-80 dBFS noise", noise (fs, 0.5, -80.0, 1) },
                    { "-60 dBFS sibilance", scaled (takeStrong.x, gainOf (-60.0) / peakOf (takeStrong.x)) },
                    { "0 dBFS sibilance", scaled (takeStrong.x, 1.0f / peakOf (takeStrong.x)) },
                    { "+6 dBFS sibilance", scaled (takeStrong.x, loudGain) },
                    { "DC 0.5", Sig ((size_t) (fs * 0.5), 0.5f) },
                    { "sweep", sweep (fs, 1.0, 20.0, fs * 0.45, -6.0) },
                    { "white noise -6 dBFS", noise (fs, 0.5, -6.0, 2) },
                    { "strong sibilance", takeStrong.x },
                    { "subtle sibilance", takeSubtle.x },
                };
                for (int channels = 1; channels <= 2; ++channels)
                    for (const auto& [name, x] : signals)
                    {
                        Sig refL;
                        for (int bi = -1; bi < (int) blocks.size(); ++bi)
                        {
                            const bool vary = bi < 0;
                            auto p = makePlugin (fs, vary ? 4096 : blocks[(size_t) bi], channels);
                            const auto bs = vary ? varyingBlocks ((unsigned) (fs + channels), 4096) : fixedBlocks (blocks[(size_t) bi]);
                            Sig yl, yr;
                            if (channels == 1)
                                yl = processMono (*p, x, bs);
                            else
                                std::tie (yl, yr) = processStereo (*p, x, rightOf (x), bs);
                            const float inPeak = peakOf (x);
                            for (const auto* y : { &yl, &yr })
                            {
                                if (y->empty())
                                    continue;
                                const auto st = statsOf (*y);
                                // a cut can raise the peak of broadband noise a little (phase), never more than 2 dB
                                const bool ok = st.finite && ! st.denormal && st.peak <= 1.26f * inPeak + 1.0e-9f;
                                if (inPeak > 0.0f)
                                    worstGrowth = std::max (worstGrowth, st.peak / inPeak);
                                if (! ok)
                                    std::printf ("    %.1f kHz %dch block %d %s: finite %d denormal %d peak %.3f (in %.3f)\n", fs / 1000.0, channels,
                                                 vary ? -1 : blocks[(size_t) bi], name, (int) st.finite, (int) st.denormal, st.peak, inPeak);
                                bad += ok ? 0 : 1;
                            }
                            if (juce::String (name) == "silence")
                                silenceBad += peakOf (yl) == 0.0f ? 0 : 1;
                            // the same audio for every block size (bit-exact)
                            if (refL.empty())
                                refL = yl;
                            else
                            {
                                const float d = maxDiff (yl, refL);
                                worstVariance = std::max (worstVariance, d);
                                variant += d == 0.0f ? 0 : 1;
                            }
                            ++runs;
                        }
                    }
            }
            std::printf ("    %d runs, largest difference between block sizes %.2e, output peak at most %.3f x the input peak\n", runs, worstVariance, worstGrowth);
            check (bad == 0, "finite, no denormals, peak never 2 dB over the input's (" + juce::String (bad) + " failed)");
            check (silenceBad == 0, "silence in = silence out");
            check (variant == 0, "bit-identical output for every block size, also varying ones (" + juce::String (variant) + " differ)");

            // a host that sends bigger blocks than it announced in prepareToPlay (FL Studio does): same audio
            {
                const double fs = 48000.0;
                const auto x = testsig::makeTake (fs, 1.5).x;
                auto small = makePlugin (fs, 64), ref = makePlugin (fs, 512);
                const auto [bl, br] = processStereo (*small, x, rightOf (x), fixedBlocks (8192));
                const auto [rl, rr] = processStereo (*ref, x, rightOf (x), fixedBlocks (512));
                check (identical (bl, rl) && identical (br, rr), "blocks of 8192 after prepareToPlay (48 kHz, 64): identical audio");
            }
            // mono in a mono layout = the left channel of a stereo instance fed the same signal twice
            {
                const double fs = 48000.0;
                const auto x = testsig::makeTake (fs, 1.5).x;
                auto m = makePlugin (fs, 512, 1), s = makePlugin (fs, 512, 2);
                const auto ym = processMono (*m, x, fixedBlocks (512));
                const auto [sl, sr] = processStereo (*s, x, x, fixedBlocks (512));
                check (maxDiff (ym, sl) < 1.0e-6f && identical (sl, sr), "mono = dual mono (" + juce::String (maxDiff (ym, sl), 8) + ")");
            }
        }

        void testDeterminism()
        {
            Test t ("determinism: the same input twice -> bit-identical, also after prepareToPlay again");
            const double fs = 48000.0;
            const auto take = testsig::makeTake (fs, 3.0);
            const Sig l = take.x, r = rightOf (take.x);
            const auto session = [&] (EmberAudioProcessor& p)
            {
                // automation and preset switches at fixed blocks: part of "the same input"
                return processStereo (p, l, r, varyingBlocks (77), [&p] (int b, int, int)
                {
                    if (b % 40 == 10) p.loadFactoryPreset ((b / 40) % (int) factoryPresets().size());
                    if (b % 25 == 5) automate (param (p, pid::freq), (float) ((b * 37) % 100) / 100.0f);
                });
            };
            auto a = makePlugin (fs, 2048), b = makePlugin (fs, 2048);
            const auto ya = session (*a), yb = session (*b);
            check (identical (ya.first, yb.first) && identical (ya.second, yb.second), "two fresh instances: bit-identical");
            a->loadFactoryPreset (0);
            a->prepareToPlay (fs, 2048);
            auto c = makePlugin (fs, 2048);
            c->loadFactoryPreset (0);
            c->prepareToPlay (fs, 2048);
            const auto ya2 = session (*a), yc = session (*c);
            check (identical (ya2.first, yc.first) && identical (ya2.second, yc.second), "an instance prepared again == a fresh one");
        }

        void testCpu()
        {
            Test t ("CPU: processBlock per instance (48 kHz / 512 stereo), 3 runs, median");
            const double fs = 48000.0;
            const int block = 512;
            const auto take = testsig::makeTake (fs, 10.0);
            const Sig r = rightOf (take.x);
            struct Case { const char* name; std::function<void (EmberAudioProcessor&)> set; bool automateFreq; };
            const Case cases[] {
                { "default (split, Smart)", [] (EmberAudioProcessor&) {}, false },
                { "wideband", [] (EmberAudioProcessor& p) { setReal (p, pid::mode, 1.0f); }, false },
                { "link 50 % (each channel + linked)", [] (EmberAudioProcessor& p) { setReal (p, pid::link, 50.0f); }, false },
                { "worst: M/S, link 50, Allround, S band, freq automated", [] (EmberAudioProcessor& p)
                  { setReal (p, pid::stereo, 1.0f); setReal (p, pid::link, 50.0f); setReal (p, pid::detection, 3.0f); setReal (p, pid::listen, 1.0f); }, true },
                { "bypass", [] (EmberAudioProcessor& p) { setReal (p, pid::bypass, 1.0f); }, false },
            };
            std::printf ("    load: %s\n", juce::SystemStats::getCpuModel().toRawUTF8());
            for (const auto& c : cases)
            {
                std::vector<double> us;
                for (int rep = 0; rep < 3; ++rep)
                {
                    auto p = makePlugin (fs, block);
                    c.set (*p);
                    p->prepareToPlay (fs, block);
                    juce::AudioBuffer<float> buf (2, block);
                    juce::MidiBuffer midi;
                    const int n = (int) take.x.size() / block;
                    int count = 0;
                    const auto t0 = juce::Time::getHighResolutionTicks();
                    for (int pass = 0; pass < 3; ++pass)
                        for (int k = 0; k < n; ++k, ++count)
                        {
                            buf.copyFrom (0, 0, take.x.data() + k * block, block);
                            buf.copyFrom (1, 0, r.data() + k * block, block);
                            if (c.automateFreq)
                                automate (param (*p, pid::freq), (float) (k % 50) / 50.0f);
                            p->processBlock (buf, midi);
                        }
                    const double sec = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
                    us.push_back (sec * 1.0e6 / count);
                }
                std::sort (us.begin(), us.end());
                const double blockUs = block / fs * 1.0e6;
                std::printf ("    %-55s %7.1f us per block (runs %.1f / %.1f / %.1f) = %.2f %% of real time\n", c.name, us[1], us[0], us[1], us[2], 100.0 * us[1] / blockUs);
                check (us[1] < 0.05 * blockUs, juce::String (c.name) + ": under 5 % of real time");
            }

            // the whole process with the editor open (GUI on the message thread) against the editor closed
            {
                UserSettings::useFileForTests (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ember_qa.settings"));
                for (int open = 0; open < 2; ++open)
                {
                    auto p = makePlugin (fs, block);
                    std::unique_ptr<juce::AudioProcessorEditor> ed;
                    if (open)
                    {
                        ed.reset (p->createEditor());
                        ed->addToDesktop (juce::ComponentPeer::windowHasTitleBar);
                        ed->setVisible (true);
                    }
                    std::atomic<bool> stop { false };
                    std::thread audio ([&]
                    {
                        juce::AudioBuffer<float> buf (2, block);
                        juce::MidiBuffer midi;
                        const double blockMs = block / fs * 1000.0;
                        double next = juce::Time::getMillisecondCounterHiRes();
                        for (int k = 0; ! stop.load(); ++k)
                        {
                            const int pos = (k * block) % ((int) take.x.size() - block);
                            buf.copyFrom (0, 0, take.x.data() + pos, block);
                            buf.copyFrom (1, 0, r.data() + pos, block);
                            p->processBlock (buf, midi);
                            next += blockMs;
                            const double wait = next - juce::Time::getMillisecondCounterHiRes();
                            if (wait > 0.0)
                                std::this_thread::sleep_for (std::chrono::microseconds ((int) (wait * 1000.0)));
                        }
                    });
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (500);
                    const double c0 = emberProcessCpuMs(), w0 = juce::Time::getMillisecondCounterHiRes();
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (4000);
                    const double cpu = emberProcessCpuMs() - c0, wall = juce::Time::getMillisecondCounterHiRes() - w0;
                    stop.store (true);
                    audio.join();
                    std::printf ("    whole process, real-time audio, editor %s: %.1f %% of one core\n", open ? "OPEN (SIMPLE, animation full)" : "closed", 100.0 * cpu / wall);
                    ed.reset();
                }
            }
        }

        void testMemory()
        {
            Test t ("memory: 10 minutes of audio, editor open / close, instances created / destroyed");
            double priv = 0, ws = 0;
            int handles = 0, gdi = 0, user = 0;
            emberQaSysInfo (priv, ws, handles, gdi, user);
            std::printf ("    start: private %.1f MB, working set %.1f MB, handles %d, GDI %d, USER %d\n", priv, ws, handles, gdi, user);
            // 1) ten minutes of audio through one instance (presets, automation, AUTO listening now and then)
            {
                const double fs = 48000.0;
                const int block = 512;
                auto p = makePlugin (fs, block);
                const auto take = testsig::makeTake (fs, 10.0);
                juce::AudioBuffer<float> buf (2, block);
                juce::MidiBuffer midi;
                const int perMinute = (int) (fs * 60.0 / block);
                double firstMinute = 0.0, fifth = 0.0, last = 0.0;
                std::printf ("    audio minute:");
                for (int k = 0; k < perMinute * 10; ++k)
                {
                    const int pos = (k * block) % ((int) take.x.size() - block);
                    for (int ch = 0; ch < 2; ++ch)
                        buf.copyFrom (ch, 0, take.x.data() + pos, block);
                    if (k % 2000 == 100) p->loadFactoryPreset ((k / 2000) % (int) factoryPresets().size());
                    if (k % 97 == 0) automate (param (*p, pid::range), (float) (k % 13) / 13.0f);
                    if (k % 20000 == 500) p->startAuto();
                    if (k % 20000 == 2500) p->cancelAuto();
                    p->processBlock (buf, midi);
                    if ((k + 1) % perMinute == 0)
                    {
                        juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
                        emberQaSysInfo (priv, ws, handles, gdi, user);
                        if ((k + 1) / perMinute == 1)
                            firstMinute = priv;
                        if ((k + 1) / perMinute == 5)
                            fifth = priv;
                        last = priv;
                        std::printf (" %d: %.1f MB", (k + 1) / perMinute, priv);
                    }
                }
                std::printf ("\n");
                check (last - fifth < 0.5, "private bytes flat over the last 5 minutes of audio (" + juce::String (last - fifth, 2) + " MB; minute 1 -> 10: "
                                               + juce::String (last - firstMinute, 2) + " MB)");
            }
            // 2) editor open / close (on the desktop, painting) 120 times; without the desktop window 60 times
            for (int onDesktop = 1; onDesktop >= 0; --onDesktop)
            {
                UserSettings::useFileForTests (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ember_qa.settings"));
                auto p = makePlugin (48000.0, 512);
                const int loops = onDesktop ? 120 : 60;
                double p0 = 0; int h0 = 0, g0 = 0, u0 = 0;
                std::printf ("    editor open/close%s, private MB:", onDesktop ? " (window on the desktop)" : " (no window)");
                for (int k = 0; k < loops; ++k)
                {
                    {
                        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
                        if (onDesktop)
                        {
                            ed->addToDesktop (juce::ComponentPeer::windowHasTitleBar);
                            ed->setVisible (true);
                        }
                        if (auto* e = dynamic_cast<EmberEditor*> (ed.get()))
                            e->setAdvanced (k % 2 == 1);
                        juce::MessageManager::getInstance()->runDispatchLoopUntil (onDesktop ? 60 : 5);
                    }
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
                    if (k == 9)
                        emberQaSysInfo (p0, ws, h0, g0, u0);
                    if ((k + 1) % 20 == 0)
                    {
                        emberQaSysInfo (priv, ws, handles, gdi, user);
                        std::printf (" %d: %.1f", k + 1, priv);
                    }
                }
                juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
                emberQaSysInfo (priv, ws, handles, gdi, user);
                std::printf ("\n      after 10: %.1f MB, %d handles, %d GDI, %d USER -> after %d: %.1f MB, %d, %d, %d\n",
                             p0, h0, g0, u0, loops, priv, handles, gdi, user);
                const double perOpen = (priv - p0) / (loops - 10);
                check (perOpen < 0.1, juce::String ("editor loop") + (onDesktop ? " (window)" : " (no window)") + ": private bytes stay ("
                                          + juce::String (perOpen, 3) + " MB per open)");
                check (handles - h0 < 30 && gdi - g0 < 10 && user - u0 < 10, "editor loop: no handle / GDI / USER object leak");
            }
            // 3) instances created and destroyed 150 times
            {
                double p0 = 0; int h0 = 0, g0 = 0, u0 = 0;
                Sig l (512 * 20), r (512 * 20);
                for (int k = 0; k < 150; ++k)
                {
                    auto p = makePlugin (k % 2 ? 44100.0 : 96000.0, 512);
                    processStereo (*p, l, r, fixedBlocks (512));
                    if (k == 9)
                    {
                        p.reset();
                        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
                        emberQaSysInfo (p0, ws, h0, g0, u0);
                    }
                }
                juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
                emberQaSysInfo (priv, ws, handles, gdi, user);
                std::printf ("    150 instances: after 10: %.1f MB, %d handles -> after 150: %.1f MB, %d handles\n", p0, h0, priv, handles);
                check (priv - p0 < 3.0 && handles - h0 < 10, "instance loop: no leak (" + juce::String (priv - p0, 2) + " MB, " + juce::String (handles - h0) + " handles)");
            }
        }

        std::unique_ptr<juce::AudioBuffer<float>> readWav (const juce::File& f, double& fs)
        {
            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
            if (rd == nullptr)
                return nullptr;
            fs = rd->sampleRate;
            auto b = std::make_unique<juce::AudioBuffer<float>> ((int) std::min (2u, rd->numChannels), (int) rd->lengthInSamples);
            rd->read (b.get(), 0, (int) rd->lengthInSamples, 0, true, b->getNumChannels() > 1);
            return b;
        }

        void testRealTakes()
        {
            const auto env = juce::SystemStats::getEnvironmentVariable ("EMBER_VOCALS", {});
            if (env.isEmpty())
                return;
            Test t ("real takes (EMBER_VOCALS): presets switched while playing, automation, delta, determinism");
            juce::Array<juce::File> files;
            for (const auto& part : juce::StringArray::fromTokens (env, ";", ""))
            {
                const juce::File f (part.trim());
                if (f.isDirectory())
                    files.addArray (f.findChildFiles (juce::File::findFiles, true, "*.wav"));
                else if (f.existsAsFile())
                    files.add (f);
            }
            check (! files.isEmpty(), "takes found");
            int ok = 0;
            for (const auto& f : files)
            {
                double fs = 0.0;
                auto buf = readWav (f, fs);
                if (buf == nullptr)
                {
                    check (false, f.getFileName() + ": readable");
                    continue;
                }
                const int n = std::min (buf->getNumSamples(), (int) (fs * 40.0));
                Sig l (buf->getReadPointer (0), buf->getReadPointer (0) + n);
                Sig r (buf->getReadPointer (buf->getNumChannels() - 1), buf->getReadPointer (buf->getNumChannels() - 1) + n);
                const float inPeak = std::max (peakOf (l), peakOf (r)), inStep = std::max (maxStep (l), maxStep (r));
                // presets every 0.4 s, Amount and frequency automated, varying blocks
                auto p = makePlugin (fs, 2048);
                const int every = (int) (fs * 0.4);
                int nextSwitch = every;
                const auto [yl, yr] = processStereo (*p, l, r, varyingBlocks ((unsigned) n), [&] (int b, int start, int)
                {
                    if (start >= nextSwitch)
                    {
                        p->loadFactoryPreset ((start / every) % (int) factoryPresets().size());
                        nextSwitch += every;
                    }
                    if (b % 7 == 0)
                        automate (param (*p, pid::amount), (float) ((b * 13) % 100) / 100.0f);
                });
                const auto sl = statsOf (yl), sr = statsOf (yr);
                const bool sane = sl.finite && sr.finite && ! sl.denormal && ! sr.denormal && std::max (sl.peak, sr.peak) <= 1.05f * inPeak
                               && std::max (sl.step, sr.step) <= 1.5f * inStep;
                // delta with the default setting, and the same run twice
                auto a = makePlugin (fs, 512), d = makePlugin (fs, 512), a2 = makePlugin (fs, 512);
                setReal (*d, pid::listen, 2.0f);
                d->prepareToPlay (fs, 512);
                const auto [al, ar] = processStereo (*a, l, r, fixedBlocks (512));
                const auto [dl, dr] = processStereo (*d, l, r, fixedBlocks (512));
                const auto [bl, br] = processStereo (*a2, l, r, fixedBlocks (512));
                const auto in = delayed (l, a->getLatencySamples());
                float err = 0.0f;
                for (size_t i = 0; i < al.size(); ++i)
                    err = std::max (err, std::abs (dl[i] - (in[i] - al[i])));
                const bool same = identical (al, bl) && identical (ar, br);
                std::printf ("    %-28s %5.1f kHz %dch: peak %.3f -> %.3f, step %.3f -> %.3f, delta err %.1e, deterministic %s\n",
                             f.getFileName().substring (0, 28).toRawUTF8(), fs / 1000.0, buf->getNumChannels(), inPeak, std::max (sl.peak, sr.peak),
                             inStep, std::max (sl.step, sr.step), err, same ? "yes" : "NO");
                check (sane, f.getFileName() + ": finite, bounded, no jumps while presets switch");
                check (err < 2.0e-6f, f.getFileName() + ": delta == input - output");
                check (same, f.getFileName() + ": deterministic");
                ok += sane && same && err < 2.0e-6f ? 1 : 0;
            }
            std::printf ("    %d of %d takes clean\n", ok, files.size());
        }
    }
}

// Runs the suite; adds its counts to the caller's
void emberRunQa (bool full, int& passedTotal, int& failedTotal)
{
    using namespace ember::qa;
    passed = failed = 0;
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    testParametersReachEngine();
    testPresetFiles();
    testUndoFuzz();
    testStateRoundTrip();
    testCorruptState();
    testThreadedState();
    testDelta();
    testInfoCards();
    testParameterText();
    testSwitchClicks();
    testAutomationStress (full);
    testLatency (full);
    testNullAndDistortion();
    testReductionAccuracy (full);
    testMatrix (full);
    testDeterminism();
    if (full)
    {
        testCpu();
        testMemory();
        testRealTakes();
    }
    std::printf ("\nEMBER QA%s: %d passed, %d failed (%.1f s)\n", full ? " (full)" : "", passed, failed, (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0);
    passedTotal += passed;
    failedTotal += failed;
}
