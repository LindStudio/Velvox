#include "ConsistencyHarness.h"
#include "CheckEval.h"
#include "DSP/Instances.h"
#include "Parameters/EqBands.h"
#include <atomic>
#include <cmath>
#include <mutex>
#include <thread>

namespace vocalypse::test
{
    using automix::CapturedAudio;
    using automix::MixPlan;

    //==========================================================================
    // Fingerprint

    namespace
    {
        bool isEqBandSlotId (const juce::String& id)
        {
            return id.startsWith ("eqBand") && id.length() > 6 && juce::CharacterFunctions::isDigit (id[6]);
        }

        juce::String moduleOnId (dsp::ModuleId m, int instance)
        {
            return dsp::instanceParamId (juce::String (dsp::moduleKey (m)) + "On", instance);
        }
    }

    PlanFingerprint fingerprint (const MixPlan& plan, const std::map<juce::String, float>& prior)
    {
        // The state the plugin ends in: what the plan does not write keeps its prior value
        auto effective = prior;
        for (auto& [id, v] : plan.values)
            effective[id] = v;

        PlanFingerprint fp;
        for (auto& [id, v] : effective)
            if (! isEqBandSlotId (id) && automix::sessionParameters().count (id) == 0)   // session parameters are not part of the mix
                fp.values[id] = v;

        const auto access = eqbands::accessFor (effective);
        for (int instance = 0; instance < dsp::maxInstances (dsp::ModuleId::eq); ++instance)
            for (int band = 0; band < 16; ++band)
            {
                const auto b = eqbands::readBand (access, instance, band);
                if (b.bandRole() != dsp::EqRole::none)
                    fp.roleBands.emplace (std::make_pair (instance, b.role), b);
            }

        for (auto& slot : plan.routing)
            if (dsp::isChainModule (slot.id))
            {
                const auto on = effective.find (moduleOnId (slot.id, slot.instance));
                if (on != effective.end() && on->second > 0.5f)
                    fp.modulesOn.insert (juce::String (dsp::moduleKey (slot.id)) + "#" + juce::String ((int) slot.instance));
            }

        fp.routing = plan.routing.toString();
        return fp;
    }

    PlanFingerprint fingerprint (const MixPlan& plan)
    {
        return fingerprint (plan, defaultValues());
    }

    const std::vector<LowIntensity>& lowIntensityTable()
    {
        static const std::vector<LowIntensity> table {
            { "deess",   "deessRange",    2.5f },
            { "reverb",  "reverbMix",     8.0f },
            { "delay",   "delayMix",      8.0f },
            { "doubler", "doublerMix",    8.0f },
            { "smooth",  "smoothAmount", 15.0f },
            { "sat",     "satMix",       15.0f },
            { "smash",   "smashBlend",   10.0f },
            { "ride",    "rideAmount",   25.0f },
            { "denoise", "denoiseAmount", 25.0f },
            { "breath",  "breathAmount", 25.0f },
        };   // the compressor (and anything else not listed) is never "low"
        return table;
    }

    //==========================================================================
    // Comparison

    namespace
    {
        constexpr float eps = 1.0e-4f;

        juce::String num (float v) { return juce::String (v, 2); }

        bool withinRelOrAbs (float a, float b, float rel, float abs)
        {
            const float d = std::abs (a - b);
            return d <= abs + eps || d <= rel * std::max (std::abs (a), std::abs (b)) + eps;
        }
        bool withinRel (float a, float b, float rel)
        {
            return std::abs (a - b) <= rel * std::max (std::abs (a), std::abs (b)) + eps;
        }
        float octaves (float a, float b)
        {
            return a > 0.0f && b > 0.0f ? std::abs (std::log2 (a / b)) : 100.0f;
        }

        // "<moduleKey>#<instance>" of the module a parameter belongs to, empty for global ones
        juce::String moduleSlotOf (const juce::String& id)
        {
            if (const auto* spec = findParameterSpec (id))
                if (const auto m = moduleOfParameter (*spec))
                    return juce::String (dsp::moduleKey (*m)) + "#" + juce::String (dsp::splitInstanceParamId (id).second);
            return {};
        }

        bool endsWithAny (const juce::String& s, std::initializer_list<const char*> tails)
        {
            for (auto* t : tails)
                if (s.endsWith (t))
                    return true;
            return false;
        }

        bool flipAllowed (const PlanFingerprint& whichHasItOn, const juce::String& slotKey)
        {
            const auto key = slotKey.upToFirstOccurrenceOf ("#", false, false);
            const int instance = slotKey.fromFirstOccurrenceOf ("#", false, false).getIntValue();
            for (auto& low : lowIntensityTable())
                if (key == low.module)
                {
                    const auto it = whichHasItOn.values.find (dsp::instanceParamId (low.param, instance));
                    return it != whichHasItOn.values.end() && it->second <= low.value + eps;
                }
            return false;
        }

        // Does `a` -> `b` violate the tolerance of this parameter? Returns the reason, empty = fine.
        // Parameters that no rule covers are not checked.
        juce::String valueViolation (const juce::String& id, float a, float b, Check check, float shiftDb, const Tolerances& t)
        {
            const auto base = dsp::splitInstanceParamId (id).first;
            const auto what = [&] { return id + ": " + num (a) + " -> " + num (b); };

            if (check == Check::gainShift)
            {
                if (base == pid::inputGain)
                    return std::abs (b - (a - shiftDb)) <= t.inputGainDb + eps ? juce::String() : what() + " (expected " + num (a - shiftDb) + ")";
                if (base == pid::outputGain)
                    return std::abs (b - (a + shiftDb)) <= t.outputGainDb + eps || std::abs (b - a) <= t.outputGainDb + eps
                               ? juce::String() : what() + " (expected " + num (a + shiftDb) + " or, with the output target clamped, " + num (a) + ")";
                return withinRelOrAbs (a, b, t.relative, t.absolute) ? juce::String() : what();
            }

            // perturbation
            if (base == pid::compThreshold)  return std::abs (a - b) <= t.compThresholdDb + eps ? juce::String() : what();
            if (base == pid::compRatio)      return withinRel (a, b, t.compRatioRel) ? juce::String() : what();
            if (base == pid::compAttack || base == pid::compRelease)
                return withinRel (a, b, t.compTimeRel) ? juce::String() : what();
            if (base == pid::deessRange)     return std::abs (a - b) <= t.deessRangeDb + eps ? juce::String() : what();
            if (base == pid::deessFreq)      return octaves (a, b) <= t.freqOctaves + eps ? juce::String() : what();
            if (base == pid::satDrive)       return std::abs (a - b) <= t.satDriveDb + eps ? juce::String() : what();
            if (endsWithAny (base, { "Mix", "Blend" }))
                return std::abs (a - b) <= t.mixPoints + eps ? juce::String() : what();
            return {};
        }

        juce::String bandName (const std::pair<int, int>& key)
        {
            return "eq#" + juce::String (key.first) + "/role" + juce::String (key.second);
        }

        juce::String bandText (const dsp::EqBand& b)
        {
            return juce::String (b.on ? "on " : "off ") + num (b.freq) + " Hz " + num (b.gain) + " dB Q " + num (b.q);
        }

        juce::String bandViolation (const std::pair<int, int>& key, const dsp::EqBand* a, const dsp::EqBand* b,
                                    Check check, const Tolerances& t)
        {
            const bool onA = a != nullptr && a->on, onB = b != nullptr && b->on;
            if (! onA && ! onB)
                return {};
            const auto name = bandName (key);
            if (onA != onB)
                return name + ": " + (onA ? bandText (*a) : juce::String ("absent")) + " -> " + (onB ? bandText (*b) : juce::String ("absent"));
            const auto what = [&] { return name + ": " + bandText (*a) + " -> " + bandText (*b); };
            if (a->type != b->type || a->slope != b->slope)
                return what();
            if (check == Check::gainShift)
                return withinRelOrAbs (a->freq, b->freq, t.relative, t.absolute) && withinRelOrAbs (a->gain, b->gain, t.relative, t.absolute)
                        && withinRelOrAbs (a->q, b->q, t.relative, t.absolute) ? juce::String() : what();
            return std::abs (a->gain - b->gain) <= t.eqBandGainDb + eps && octaves (a->freq, b->freq) <= t.freqOctaves + eps ? juce::String() : what();
        }
    }

    PlanDiff compare (const PlanFingerprint& ref, const PlanFingerprint& other, Check check, float shiftDb, const Tolerances& tol)
    {
        PlanDiff d;

        // Module flips
        std::set<juce::String> flipped;
        for (auto& k : ref.modulesOn)
            if (other.modulesOn.count (k) == 0)
            {
                d.flips.push_back ("-" + k);
                flipped.insert (k);
                if (check != Check::perturbation || ! flipAllowed (ref, k))
                    d.overTolerance.push_back ("flip -" + k);
            }
        for (auto& k : other.modulesOn)
            if (ref.modulesOn.count (k) == 0)
            {
                d.flips.push_back ("+" + k);
                flipped.insert (k);
                if (check != Check::perturbation || ! flipAllowed (other, k))
                    d.overTolerance.push_back ("flip +" + k);
            }
        d.differing = (int) d.flips.size();

        // Values
        std::set<juce::String> ids;
        for (auto& kv : ref.values)   ids.insert (kv.first);
        for (auto& kv : other.values) ids.insert (kv.first);
        for (auto& id : ids)
        {
            const auto a = ref.values.find (id), b = other.values.find (id);
            const bool both = a != ref.values.end() && b != other.values.end();
            if (both && a->second == b->second)
                continue;
            ++d.differing;

            if (check == Check::exact)
            {
                d.overTolerance.push_back (both ? id + ": " + num (a->second) + " -> " + num (b->second)
                                                : id + ": " + (a == ref.values.end() ? "added" : "removed"));
                continue;
            }
            if (flipped.count (moduleSlotOf (id)) > 0)
                continue;   // the module is on in one run only: its settings are not comparable
            if (! both)
            {
                d.overTolerance.push_back (id + ": " + (a == ref.values.end() ? "added" : "removed"));
                continue;
            }
            const auto violation = valueViolation (id, a->second, b->second, check, shiftDb, tol);
            if (violation.isNotEmpty())
                d.overTolerance.push_back (violation);
        }

        // EQ role bands
        std::set<std::pair<int, int>> keys;
        for (auto& kv : ref.roleBands)   keys.insert (kv.first);
        for (auto& kv : other.roleBands) keys.insert (kv.first);
        for (auto& key : keys)
        {
            const auto a = ref.roleBands.find (key), b = other.roleBands.find (key);
            const auto* pa = a != ref.roleBands.end() ? &a->second : nullptr;
            const auto* pb = b != other.roleBands.end() ? &b->second : nullptr;
            if (pa != nullptr && pb != nullptr && *pa == *pb)
                continue;
            ++d.differing;
            if (check == Check::exact)
                d.overTolerance.push_back (bandName (key) + ": " + (pa ? bandText (*pa) : juce::String ("absent")) + " -> " + (pb ? bandText (*pb) : juce::String ("absent")));
            else if (const auto v = bandViolation (key, pa, pb, check, tol); v.isNotEmpty())
                d.overTolerance.push_back (v);
        }

        // The chain order: exact only (the other checks are about the settings and the modules that are on)
        if (ref.routing != other.routing)
        {
            ++d.differing;
            if (check == Check::exact)
                d.overTolerance.push_back ("routing: " + ref.routing + " -> " + other.routing);
        }

        d.pass = d.overTolerance.empty();
        return d;
    }

    //==========================================================================
    // Running the auto mix like the plugin

    bool captureLikePlugin (const CapturedAudio& file, double startSeconds, CapturedAudio& captured, bool withSongPosition)
    {
        using automix::CaptureBuffer;
        const int channels = juce::jlimit (1, 2, std::min (file.channels, (int) file.vocal.size()));
        const int total = file.numSamples();
        const int from = juce::jlimit (0, total, (int) std::llround (startSeconds * file.sampleRate));

        CaptureBuffer capture;
        capture.arm (file.sampleRate, channels, automix::CaptureConfig {});
        for (int start = from; start < total && capture.getState() != CaptureBuffer::State::complete; start += 512)
        {
            const int n = std::min (512, total - start);
            std::array<const float*, 2> p { file.vocal[0].data() + start, file.vocal[(size_t) channels - 1].data() + start };
            // Song position like a host at 120 BPM that started at 0 with the file: ppq = file time * 2
            const double ppq = (double) start / file.sampleRate * 2.0;
            capture.push (p.data(), channels, nullptr, 0, n, true, withSongPosition ? 120.0 : 0.0, withSongPosition ? ppq : -1.0);
        }
        if (capture.getState() != CaptureBuffer::State::complete && ! capture.finishNow())
            return false;
        captured = capture.copyOut();
        return true;
    }

    automix::AutoMixResult mixFile (const CapturedAudio& file, double startSeconds, bool withSongPosition,
                                    const std::map<juce::String, float>& prior, const automix::MixIntent& intent)
    {
        if (file.vocal.empty() || file.numSamples() <= 0)
        {
            automix::AutoMixResult r;
            r.error = "no audio";
            return r;
        }
        CapturedAudio captured;
        if (! captureLikePlugin (file, startSeconds, captured, withSongPosition))
        {
            automix::AutoMixResult r;
            r.error = "not enough signal";
            return r;
        }
        return automix::AutoMixEngine::run (captured, prior, {}, {}, intent);
    }

    // defaultValues() is defined in CheckEval.cpp (the same map)

    std::map<juce::String, float> scrambledValues (juce::uint32 seed)
    {
        juce::Random rnd ((juce::int64) seed);
        std::map<juce::String, float> v;
        for (auto& s : getParameterSpecs())
        {
            float value;
            if (s.type == ParameterSpec::Type::Float)
            {
                value = s.minValue + rnd.nextFloat() * (s.maxValue - s.minValue);
                if (s.interval > 0.0f)
                    value = s.minValue + std::round ((value - s.minValue) / s.interval) * s.interval;
            }
            else
                value = (float) juce::jlimit ((int) s.minValue, (int) s.maxValue,
                                              (int) s.minValue + rnd.nextInt ((int) (s.maxValue - s.minValue) + 1));
            v[s.id] = juce::jlimit (s.minValue, s.maxValue, value);
        }
        return v;
    }

    //==========================================================================
    // Command line

    namespace
    {
        struct Row
        {
            juce::String take, variant, check;
            int differing = 0;
            juce::StringArray flips, over;
            bool pass = true;
            bool info = false;   // reported, not counted (offsets without a song position)
        };

        juce::String expandEnvironment (juce::String text)
        {
            for (int start = text.indexOfChar ('%'); start >= 0; start = text.indexOfChar (start + 1, '%'))
            {
                const int end = text.indexOfChar (start + 1, '%');
                if (end < 0)
                    break;
                const auto name = text.substring (start + 1, end);
                const auto value = juce::SystemStats::getEnvironmentVariable (name, {});
                if (name.isNotEmpty() && value.isNotEmpty())
                {
                    text = text.substring (0, start) + value + text.substring (end + 1);
                    start += value.length() - 1;
                }
            }
            return text;
        }

        juce::Array<juce::File> listTakes (const juce::File& takes)
        {
            juce::Array<juce::File> files;
            if (takes.isDirectory())
            {
                files = takes.findChildFiles (juce::File::findFiles, false, "*.wav");
                std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName() < b.getFileName(); });
            }
            else
                for (auto line : juce::StringArray::fromLines (takes.loadFileAsString()))
                {
                    line = line.trim();
                    if (line.isNotEmpty() && ! line.startsWith ("#"))
                        files.add (juce::File (expandEnvironment (line)));
                }
            return files;
        }

        std::vector<Row> runTake (const juce::File& takeFile, const automix::MixIntent& intent, int repeats)
        {
            std::vector<Row> rows;
            const auto name = takeFile.getFileNameWithoutExtension();
            const Tolerances tol;

            bool informational = false;
            const auto add = [&] (const juce::String& variant, Check check, const PlanDiff& d)
            {
                Row r;
                r.info = informational;
                r.take = name;
                r.variant = variant;
                r.check = check == Check::exact ? "exact" : check == Check::gainShift ? "gainShift" : "perturbation";
                r.differing = d.differing;
                r.flips.addArray (juce::StringArray (d.flips.data(), (int) d.flips.size()));
                r.over.addArray (juce::StringArray (d.overTolerance.data(), (int) d.overTolerance.size()));
                r.pass = d.pass;
                rows.push_back (std::move (r));
            };
            const auto fail = [&] (const juce::String& variant, Check check, const juce::String& why)
            {
                PlanDiff d;
                d.differing = 1;
                d.overTolerance.push_back (why);
                d.pass = false;
                add (variant, check, d);
            };

            const auto file = loadVocal (takeFile);
            if (! file.has_value())
            {
                fail ("load", Check::exact, "cannot read " + takeFile.getFullPathName());
                return rows;
            }

            const auto defaults = defaultValues();
            const auto reference = mixFile (*file, 0.0, false, defaults, intent);
            if (! reference.ok)
            {
                fail ("reference", Check::exact, "auto mix failed: " + reference.error);
                return rows;
            }
            const auto refFp = fingerprint (reference.plan, defaults);

            // One variant: mix, compare with the reference (or another fingerprint)
            const auto variant = [&] (const juce::String& label, Check check, float shiftDb, const CapturedAudio& audio, double start,
                                      const std::map<juce::String, float>& prior, const PlanFingerprint& against,
                                      bool withPosition = false) -> std::optional<automix::AutoMixResult>
            {
                auto r = mixFile (audio, start, withPosition, prior, intent);
                if (! r.ok)
                {
                    fail (label, check, "auto mix failed: " + r.error);
                    return std::nullopt;
                }
                add (label, check, compare (against, fingerprint (r.plan, prior), check, shiftDb, tol));
                return r;
            };

            // The first of the repeats is the reference run
            for (int i = 2; i <= repeats; ++i)
                variant ("repeat" + juce::String (i), Check::exact, 0.0f, *file, 0.0, defaults, refFp);

            for (float shift : { -2.0f, -1.0f, -0.5f, 0.5f, 1.0f, 2.0f })
            {
                auto shifted = *file;
                const float gain = juce::Decibels::decibelsToGain (shift);
                for (auto& ch : shifted.vocal)
                    for (auto& s : ch)
                        s *= gain;
                variant ("gain" + juce::String (shift > 0.0f ? "+" : "") + juce::String (shift, 1), Check::gainShift, shift, shifted, 0.0, defaults, refFp);
            }

            {
                auto noisy = *file;
                juce::Random rnd (70);
                const float amp = juce::Decibels::decibelsToGain (-70.0f) * std::sqrt (3.0f);   // uniform noise: RMS = amp / sqrt (3)
                for (auto& ch : noisy.vocal)
                    for (auto& s : ch)
                        s += amp * (rnd.nextFloat() * 2.0f - 1.0f);
                variant ("noise-70", Check::perturbation, 0.0f, noisy, 0.0, defaults, refFp);
            }

            // Start points. With a song position (the reference runs with the same position from 0) the segments are
            // the same wherever the capture began; without one the segments follow the first stored sample, so these
            // rows are reported (informational), not counted.
            {
                const auto positioned = mixFile (*file, 0.0, true, defaults, intent);
                if (positioned.ok)
                {
                    const auto positionedFp = fingerprint (positioned.plan, defaults);
                    for (double start : { 3.0, 6.0 })
                        variant ("offsetPos" + juce::String ((int) start), Check::perturbation, 0.0f, *file, start, defaults, positionedFp, true);
                }
                else
                    fail ("offsetPos", Check::perturbation, "auto mix with song position failed: " + positioned.error);
                informational = true;
                for (double start : { 3.0, 6.0 })
                    variant ("offsetNoPos" + juce::String ((int) start), Check::perturbation, 0.0f, *file, start, defaults, refFp);
                informational = false;
            }

            // Sequential: every run starts from the plugin state the previous one left
            {
                auto prior = defaults;
                for (auto& [id, v] : reference.plan.values)
                    prior[id] = v;
                for (int i = 2; i <= 3; ++i)
                {
                    const auto r = variant ("sequential" + juce::String (i), Check::exact, 0.0f, *file, 0.0, prior, refFp);
                    if (! r.has_value())
                        break;
                    for (auto& [id, v] : r->plan.values)
                        prior[id] = v;
                }
            }

            for (juce::uint32 seed : { 1u, 2u })
                variant ("scrambled" + juce::String ((int) seed), Check::exact, 0.0f, *file, 0.0, scrambledValues (seed), refFp);

            return rows;
        }

        juce::String csvField (const juce::String& s) { return "\"" + s.replace ("\"", "\"\"") + "\""; }

        juce::String groupOf (const juce::String& variant)
        {
            int i = 0;
            while (i < variant.length() && juce::CharacterFunctions::isLetter (variant[i]))
                ++i;
            return variant.substring (0, i);
        }
    }

    int runConsistency (const juce::File& takes, const juce::File& outDir, const automix::MixIntent& intent, int repeats)
    {
        const auto files = listTakes (takes);
        if (files.isEmpty())
        {
            std::printf ("No takes in %s\n", takes.getFullPathName().toRawUTF8());
            return 1;
        }
        repeats = std::max (1, repeats);
        outDir.createDirectory();
        defaultValues();   // builds the parameter table once, before the workers start

        // One take per thread. The engine path (AutoMixEngine::run, VocalProfiler, render) was checked for shared
        // mutable statics: it has none (the only function statics build the parameter table once, above).
        const int numThreads = std::max (1, std::min (files.size(), juce::SystemStats::getNumCpus() - 1));
        std::printf ("consistency: %d takes, %d repeats, %d threads\n", files.size(), repeats, numThreads);
        std::fflush (stdout);

        std::vector<std::vector<Row>> perTake ((size_t) files.size());
        std::atomic<int> next { 0 };
        std::mutex printLock;
        std::vector<std::thread> workers;
        for (int t = 0; t < numThreads; ++t)
            workers.emplace_back ([&]
            {
                for (int i = next++; i < files.size(); i = next++)
                {
                    perTake[(size_t) i] = runTake (files[i], intent, repeats);
                    int failed = 0;
                    for (auto& r : perTake[(size_t) i])
                        failed += r.pass || r.info ? 0 : 1;
                    const std::lock_guard<std::mutex> lock (printLock);
                    std::printf ("[%d/%d] %s: %d checks, %d failed\n", i + 1, files.size(), files[i].getFileName().toRawUTF8(),
                                 (int) perTake[(size_t) i].size(), failed);
                    std::fflush (stdout);
                }
            });
        for (auto& w : workers)
            w.join();

        // consistency.csv - in take order, so the output does not depend on the thread timing
        juce::String csv ("take,variant,check,differing,flips,overTolerance,pass\n");
        int total = 0, failed = 0;
        std::vector<juce::String> groups;
        for (auto& rows : perTake)
            for (auto& r : rows)
            {
                csv << csvField (r.take) << "," << csvField (r.variant) << "," << r.check << "," << r.differing << ","
                    << csvField (r.flips.joinIntoString ("; ")) << "," << csvField (r.over.joinIntoString ("; ")) << "," << (r.pass ? 1 : 0) << "\n";
                if (! r.info)
                {
                    ++total;
                    failed += r.pass ? 0 : 1;
                }
                const auto g = groupOf (r.variant);
                if (std::find (groups.begin(), groups.end(), g) == groups.end())
                    groups.push_back (g);
            }

        // consistency_summary.txt - passed / total per check group and take
        juce::String summary;
        summary << "Auto mix consistency - " << files.size() << " takes, " << repeats << " repeats\n"
                << "genre " << (int) intent.genre << ", vibe " << (int) intent.vibe << "\n\n";
        summary << juce::String ("check").paddedRight (' ', 12);
        for (auto& f : files)
            summary << " " << f.getFileNameWithoutExtension().paddedRight (' ', 18).substring (0, 18);
        summary << "  total\n";
        for (auto& g : groups)
        {
            summary << g.paddedRight (' ', 12);
            int gp = 0, gt = 0;
            for (size_t i = 0; i < perTake.size(); ++i)
            {
                int p = 0, n = 0;
                for (auto& r : perTake[i])
                    if (groupOf (r.variant) == g)
                    {
                        ++n;
                        p += r.pass ? 1 : 0;
                    }
                summary << " " << (juce::String (p) + "/" + juce::String (n)).paddedRight (' ', 18);
                gp += p;
                gt += n;
            }
            summary << "  " << gp << "/" << gt << "\n";
        }
        summary << "\n(offsetNoPos: no song position - informational, not counted)\n";
        summary << "\n" << (total - failed) << " of " << total << " checks passed" << (failed == 0 ? "" : " - FAILED") << "\n";

        const bool written = outDir.getChildFile ("consistency.csv").replaceWithText (csv, false, false, "\n")
                          && outDir.getChildFile ("consistency_summary.txt").replaceWithText (summary, false, false, "\n");
        std::printf ("\n%s\n", summary.toRawUTF8());
        return failed == 0 && written ? 0 : 1;
    }
}
