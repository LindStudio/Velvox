// AUTO MIX CONSISTENCY (plan 2026-11-04-automix-konsistenz, Task 1): the harness that compares auto-mix plans
// (fingerprints) - exact for repeats, with tolerances for level shifts and small perturbations - and the
// determinism of the engine on a synthetic vocal.

#include "TestHelpers.h"
#include "SyntheticVocal.h"
#include "ConsistencyHarness.h"
#include "Parameters/EqBands.h"

using namespace vocalypse;
using namespace vocalypse::test;
using namespace vocalypse::automix;

namespace
{
    MixPlan basePlan()
    {
        MixPlan p;
        p.routing = dsp::Routing::defaults();
        p.set (pid::inputGain, 0.0f);
        p.set (pid::outputGain, 1.0f);
        p.set (pid::compOn, 1.0f);
        p.set (pid::compThreshold, -18.0f);
        p.set (pid::compRatio, 3.0f);
        p.set (pid::deessOn, 1.0f);
        p.set (pid::deessRange, 6.0f);
        p.set (pid::reverbOn, 0.0f);
        p.set (pid::reverbMix, 5.0f);
        return p;
    }

    void putBand (MixPlan& p, int slot, dsp::EqRole role, float freq, float gain)
    {
        const auto access = eqbands::accessFor (p.values);
        dsp::EqBand b;
        b.on = true;
        b.type = (int) dsp::EqBandType::bell;
        b.freq = freq;
        b.gain = gain;
        b.role = (int) role;
        eqbands::writeBand (access, 0, slot, b);
    }
}

class ConsistencyTests final : public juce::UnitTest
{
public:
    ConsistencyTests() : juce::UnitTest ("Auto mix consistency", "VOCALYPSE") {}

    void runTest() override
    {
        const Tolerances tol;

        beginTest ("A fingerprint compared with itself passes the exact check");
        {
            auto p = basePlan();
            putBand (p, 2, dsp::EqRole::presence, 3500.0f, 2.0f);
            const auto fp = fingerprint (p);
            const auto d = compare (fp, fp, Check::exact, 0.0f, tol);
            expect (d.pass);
            expectEquals (d.differing, 0);
            expect (fp.modulesOn.count ("deess#0") == 1);
            expect (fp.modulesOn.count ("reverb#0") == 0);
            expect (fp.roleBands.size() == 1);
        }

        beginTest ("A changed deessOn is a flip");
        {
            auto a = basePlan(), b = basePlan();
            b.set (pid::deessOn, 0.0f);
            const auto d = compare (fingerprint (a), fingerprint (b), Check::exact, 0.0f, tol);
            expect (! d.pass);
            expectEquals ((int) d.flips.size(), 1);
            expect (d.flips[0].contains ("deess"));
        }

        beginTest ("An EQ band moved to another slot with the same role is not a difference");
        {
            auto a = basePlan(), b = basePlan();
            putBand (a, 0, dsp::EqRole::presence, 3500.0f, 2.0f);
            putBand (b, 7, dsp::EqRole::presence, 3500.0f, 2.0f);
            expect (fingerprint (a).values == fingerprint (b).values);   // slots are not part of the values
            const auto d = compare (fingerprint (a), fingerprint (b), Check::exact, 0.0f, tol);
            expect (d.pass);
            expectEquals (d.differing, 0);

            putBand (b, 7, dsp::EqRole::presence, 3500.0f, 3.0f);        // same role, another gain: a difference
            expect (! compare (fingerprint (a), fingerprint (b), Check::exact, 0.0f, tol).pass);
        }

        beginTest ("Gain shift: the compensation is checked, the thresholds stay put");
        {
            auto a = basePlan(), b = basePlan();
            b.set (pid::inputGain, -2.0f);
            b.set (pid::outputGain, 3.0f);
            expect (compare (fingerprint (a), fingerprint (b), Check::gainShift, 2.0f, tol).pass);

            auto g = b;
            g.set (pid::compThreshold, -20.0f);                             // a threshold that follows the shift is no longer allowed
            expect (! compare (fingerprint (a), fingerprint (g), Check::gainShift, 2.0f, tol).pass);

            auto c = b;
            c.set (pid::inputGain, -1.0f);                                  // compensation missing
            expect (! compare (fingerprint (a), fingerprint (c), Check::gainShift, 2.0f, tol).pass);

            auto e = b;
            e.set (pid::compRatio, 4.0f);                                   // another value moved by 33 %
            expect (! compare (fingerprint (a), fingerprint (e), Check::gainShift, 2.0f, tol).pass);

            auto f = b;
            f.set (pid::deessOn, 0.0f);                                     // no module flip allowed
            expect (! compare (fingerprint (a), fingerprint (f), Check::gainShift, 2.0f, tol).pass);
        }

        beginTest ("Perturbation: flips only at low intensity, values within their tolerances");
        {
            auto a = basePlan(), b = basePlan();
            a.set (pid::reverbOn, 1.0f);                                    // reverb mix 5 % <= 8 %: may flip
            expect (compare (fingerprint (a), fingerprint (b), Check::perturbation, 0.0f, tol).pass);
            a.set (pid::reverbMix, 15.0f);                                  // 15 % > 8 %: must not
            expect (! compare (fingerprint (a), fingerprint (b), Check::perturbation, 0.0f, tol).pass);

            auto c = basePlan(), d = basePlan();
            d.set (pid::compOn, 0.0f);                                      // a compressor never flips
            expect (! compare (fingerprint (c), fingerprint (d), Check::perturbation, 0.0f, tol).pass);

            auto e = basePlan(), f = basePlan();
            f.set (pid::compThreshold, -16.5f);                             // 1.5 dB <= 2 dB
            f.set (pid::deessRange, 7.5f);
            expect (compare (fingerprint (e), fingerprint (f), Check::perturbation, 0.0f, tol).pass);
            f.set (pid::compThreshold, -15.0f);                             // 3 dB > 2 dB
            expect (! compare (fingerprint (e), fingerprint (f), Check::perturbation, 0.0f, tol).pass);

            auto g = basePlan(), h = basePlan();
            putBand (g, 0, dsp::EqRole::air, 12000.0f, 2.0f);
            putBand (h, 4, dsp::EqRole::air, 12000.0f * 1.1f, 3.0f);        // 1 dB, 0.14 octave
            expect (compare (fingerprint (g), fingerprint (h), Check::perturbation, 0.0f, tol).pass);
            putBand (h, 4, dsp::EqRole::air, 12000.0f * 1.3f, 3.0f);        // 0.38 octave
            expect (! compare (fingerprint (g), fingerprint (h), Check::perturbation, 0.0f, tol).pass);
        }

        beginTest ("A parameter the plan leaves unwritten shows the prior state it leaks from");
        {
            const auto plan = basePlan();                       // never writes satDrive
            expect (plan.values.count (pid::satDrive) == 0);
            auto priorA = defaultValues(), priorB = defaultValues();
            priorB[pid::satDrive] = 11.0f;
            const auto d = compare (fingerprint (plan, priorA), fingerprint (plan, priorB), Check::exact, 0.0f, tol);
            expect (! d.pass);
            expectEquals (d.differing, 1);
            expect (compare (fingerprint (plan, priorA), fingerprint (plan, priorA), Check::exact, 0.0f, tol).pass);
        }

        beginTest ("An empty file gives a failed result, not a crash");
        expect (! mixFile (CapturedAudio {}, 0.0, false, defaultValues(), MixIntent {}).ok);

        beginTest ("Prior states: defaults and seeded random values are legal and reproducible");
        {
            const auto d = defaultValues();
            expect (d.size() > 100);
            const auto s1 = scrambledValues (1), s1b = scrambledValues (1), s2 = scrambledValues (2);
            expect (s1 == s1b);
            expect (s1 != s2);
            expect (s1.size() == d.size());
            for (auto& [id, v] : s1)
            {
                const auto* spec = findParameterSpec (id);
                expect (spec != nullptr && v >= spec->minValue - 1.0e-4f && v <= spec->maxValue + 1.0e-4f, id);
            }
        }

        beginTest ("10 runs from the default state give identical fingerprints (synthetic vocal, 16 s)");
        {
            const auto vocal = makeVocal ({});
            const auto start = defaultValues();
            std::optional<PlanFingerprint> first;
            int identical = 0;
            for (int i = 0; i < 10; ++i)
            {
                const auto r = mixFile (vocal, 0.0, false, start, MixIntent {});
                expect (r.ok, r.error);
                if (! r.ok)
                    break;
                const auto fp = fingerprint (r.plan);
                if (! first.has_value())
                    first = fp;
                const auto d = compare (*first, fp, Check::exact, 0.0f, tol);
                if (d.pass)
                    ++identical;
                else
                    logMessage ("run " + juce::String (i) + " differs: " + juce::StringArray (d.overTolerance.data(), (int) d.overTolerance.size()).joinIntoString ("; "));
            }
            expectEquals (identical, 10);
        }

        // ---- Task 3 (U5): every auto mix starts from the defaults and writes every parameter
        VocalSpec spec;
        spec.seconds = 14.0;
        const auto vocal = makeVocal (spec);

        beginTest ("The plan writes every parameter except the session ones (bypass, level match window / range, Check)");
        {
            const auto plan = mixFile (vocal, 0.0, false, defaultValues(), MixIntent {});
            expect (plan.ok, plan.error);
            expectEquals ((int) sessionParameters().size(), 5);
            for (auto& s : getParameterSpecs())
            {
                const bool session = sessionParameters().count (s.id) > 0;
                expect (plan.plan.values.count (s.id) == (session ? 0u : 1u), juce::String (session ? "session parameter written: " : "parameter not written: ") + s.id);
            }
            for (auto id : { juce::String (pid::inputGain), juce::String (pid::outputGain), juce::String (pid::macroPresence),
                             eqbands::bandParamId (0, 15, pid::eqband::freq), eqbands::bandParamId (1, 0, pid::eqband::gain) })
                expect (plan.plan.values.count (id) == 1, id);
        }

        beginTest ("A plan from any prior state equals the plan from the defaults (exact)");
        {
            const auto tolerances = Tolerances {};
            const auto fromDefaults = mixFile (vocal, 0.0, false, defaultValues(), MixIntent {});
            expect (fromDefaults.ok, fromDefaults.error);
            for (juce::uint32 seed : { 1u, 2u })
            {
                const auto prior = scrambledValues (seed);
                const auto r = mixFile (vocal, 0.0, false, prior, MixIntent {});
                expect (r.ok, r.error);
                expect (r.plan.values == fromDefaults.plan.values, "values, seed " + juce::String ((int) seed));
                expect (r.plan.routing.toString() == fromDefaults.plan.routing.toString(), "routing, seed " + juce::String ((int) seed));
                const auto d = compare (fingerprint (fromDefaults.plan), fingerprint (r.plan), Check::exact, 0.0f, tolerances);
                expect (d.pass, "fingerprint, seed " + juce::String ((int) seed));
                expectEquals (d.differing, 0);
            }
        }

        beginTest ("Three runs in a row, each from the previous result, are identical to the first");
        {
            auto state = scrambledValues (3);
            std::optional<PlanFingerprint> first;
            for (int i = 0; i < 3; ++i)
            {
                const auto r = mixFile (vocal, 0.0, false, state, MixIntent {});
                expect (r.ok, r.error);
                if (! r.ok)
                    break;
                const auto fp = fingerprint (r.plan, state);
                if (! first.has_value())
                    first = fp;
                const auto d = compare (*first, fp, Check::exact, 0.0f, tol);
                expect (d.pass, "run " + juce::String (i) + " differs");
                for (auto& [id, v] : r.plan.values)
                    state[id] = v;
            }
        }

        beginTest ("applyResult writes every non-session parameter, also those at their default; a module outside the routing is off");
        {
            Harness h;
            auto& c = h.proc.getAutoMix();
            for (auto& [id, v] : scrambledValues (4))
                if (sessionParameters().count (id) == 0 && h.proc.getState().getParameter (id) != nullptr)
                    h.set (id.toRawUTF8(), v);
            // The five session parameters (nothing the auto mix decides): set to non-default values, they survive the run
            const std::map<juce::String, float> session { { pid::bypass, 1.0f }, { pid::levelMatchWindow, 2.0f }, { pid::levelMatchRange, 7.0f },
                                                          { pid::checkOn, 0.0f }, { pid::checkMode, 1.0f } };
            expectEquals ((int) session.size(), (int) sessionParameters().size());
            for (auto& [id, v] : session)
            {
                expect (sessionParameters().count (id) == 1, id);
                h.set (id.toRawUTF8(), v);
            }
            const auto r = c.runSynchronously (vocal);
            expect (r.ok, r.error);
            c.applyResult (r);
            for (auto& [id, v] : session)
                expectEquals (h.get (id.toRawUTF8()), v, "session parameter after applyResult: " + id);

            const auto sliders = sliderPositions (r.plan.values);   // the SIMPLE faders show what the mix did (positions only)
            int checked = 0;
            for (auto& [id, v] : r.plan.values)
            {
                auto* param = h.proc.getState().getParameter (id);
                if (param == nullptr)
                    continue;
                const bool fader = sliders.count (id) > 0;
                const float expected = fader ? sliders.at (id) : v;
                const float actual = param->convertFrom0to1 (param->getValue());
                expectWithinAbsoluteError (actual, expected, 1.0e-3f * std::max (1.0f, std::abs (expected)), id);
                ++checked;
            }
            expect (checked > 400, "parameters compared: " + juce::String (checked));
            expectWithinAbsoluteError (h.get (pid::inputGain), r.plan.get (pid::inputGain), 1.0e-3f, "input gain is part of the mix");

            for (auto id : { dsp::ModuleId::eq, dsp::ModuleId::ride, dsp::ModuleId::comp, dsp::ModuleId::smash, dsp::ModuleId::deess, dsp::ModuleId::smooth,
                             dsp::ModuleId::sat, dsp::ModuleId::doubler, dsp::ModuleId::delay, dsp::ModuleId::reverb, dsp::ModuleId::denoise, dsp::ModuleId::breath })
                for (int instance = 0; instance < dsp::maxInstances (id); ++instance)
                    if (! r.plan.routing.contains (id, instance))
                        expectEquals (h.get (dsp::instanceParamId (juce::String (dsp::moduleKey (id)) + "On", instance).toRawUTF8()), 0.0f,
                                      juce::String (dsp::moduleKey (id)) + " is not in the routing but on");
            expect (h.proc.getRouting().toString() == r.plan.routing.toString(), "routing from scratch");
        }

        beginTest ("A preset loaded before the second run changes nothing");
        {
            Harness h;
            auto& c = h.proc.getAutoMix();
            const auto first = c.runSynchronously (vocal);
            expect (first.ok, first.error);
            c.applyResult (first);
            for (int preset : { 1, 2 })
            {
                h.proc.getPresetManager().loadPreset (preset);
                const auto again = c.runSynchronously (vocal);
                expect (again.ok, again.error);
                expect (again.plan.values == first.plan.values, "values after preset " + juce::String (preset));
                expect (again.plan.routing.toString() == first.plan.routing.toString(), "routing after preset " + juce::String (preset));
            }
        }

        // ---- Task 4 (U3): the auto mix analyses and plans at a fixed working level
        // The reference take scaled by `shiftDb` exactly (as the harness does: makeVocal's own normalisation would
        // change the waveform a little with the level)
        const auto mixShifted = [&vocal] (float shiftDb)
        {
            auto shifted = vocal;
            const float g = juce::Decibels::decibelsToGain (shiftDb);
            for (auto& ch : shifted.vocal)
                for (auto& x : ch)
                    x *= g;
            // straight into the engine: the capture's own start / stop thresholds are absolute levels (not what is tested here)
            return AutoMixEngine::run (shifted, defaultValues(), {}, {}, MixIntent {});
        };
        const auto mixAtLevel = [] (float levelDb, float clipGain = 0.0f)
        {
            VocalSpec s;
            s.seconds = 14.0;
            s.levelDb = levelDb;
            s.clipGain = clipGain;
            return AutoMixEngine::run (makeVocal (s), defaultValues(), {}, {}, MixIntent {});
        };
        // The synthetic take peaks 1.4 dB under full scale and sits at -18.7 LUFS: the reference is 2.5 dB lower, so +-2 dB
        // stays a legal recording (under full scale) and inside the output stage's -24 .. -14 LUFS range
        constexpr float referenceDb = -2.5f;
        const auto reference = mixShifted (referenceDb);
        expect (reference.ok, reference.error);

        beginTest ("Level shifts of +-0.5 / 1 / 2 dB: the gain check passes, nothing else moves (no rounding flip)");
        for (float shift : { -2.0f, -1.0f, -0.5f, 0.5f, 1.0f, 2.0f })
        {
            const auto r = mixShifted (referenceDb + shift);
            expect (r.ok, r.error);
            const auto d = compare (fingerprint (reference.plan), fingerprint (r.plan), Check::gainShift, shift, tol);
            expect (d.pass, "shift " + juce::String (shift) + ": " + juce::StringArray (d.overTolerance.data(), (int) d.overTolerance.size()).joinIntoString ("; "));
            expect (d.flips.empty(), "shift " + juce::String (shift) + " flips a module");
            expectWithinAbsoluteError (r.plan.get (pid::inputGain), reference.plan.get (pid::inputGain) - shift, 0.05f, "inputGain, shift " + juce::String (shift));

            // Every value but the two gains is the same (within float noise of the scaled copy): not even a rounded one flips
            int moved = 0;
            for (auto& [id, v] : reference.plan.values)
            {
                if (id == pid::inputGain || id == pid::outputGain)
                    continue;
                const auto it = r.plan.values.find (id);
                if (it == r.plan.values.end() || std::abs (it->second - v) > 1.0e-3f * std::max (1.0f, std::abs (v)))
                {
                    ++moved;
                    logMessage ("shift " + juce::String (shift) + ": " + id + " " + juce::String (v) + " -> " + (it == r.plan.values.end() ? juce::String ("-") : juce::String (it->second)));
                }
            }
            expectEquals (moved, 0, "values moved by a shift of " + juce::String (shift) + " dB");
            expect (r.plan.routing.toString() == reference.plan.routing.toString(), "routing, shift " + juce::String (shift));
        }

        beginTest ("A quiet take (12 dB under the reference) gets 12 dB more input gain; the output gain takes it back");
        {
            const auto r = mixShifted (referenceDb - 12.0f);
            expect (r.ok, r.error);
            expectWithinAbsoluteError (r.plan.get (pid::inputGain), reference.plan.get (pid::inputGain) + 12.0f, 0.05f);
            expect (r.plan.get (pid::inputGain) < 24.0f, "unclamped");
            // everything but the two gains stays; the output gain of such a quiet take is not "original + shift": the output
            // stage aims at the loudness of the take, kept within -24 .. -14 LUFS (an absolute target, by design)
            for (auto& [id, v] : reference.plan.values)
                if (id != pid::inputGain && id != pid::outputGain && r.plan.values.count (id) == 1)
                    expect (std::abs (r.plan.values.at (id) - v) <= 1.0e-3f * std::max (1.0f, std::abs (v)), id + ": " + juce::String (v) + " -> " + juce::String (r.plan.values.at (id)));
            logMessage ("quiet take: inputGain " + juce::String (reference.plan.get (pid::inputGain), 2) + " -> " + juce::String (r.plan.get (pid::inputGain), 2)
                        + ", outputGain " + juce::String (reference.plan.get (pid::outputGain), 2) + " -> " + juce::String (r.plan.get (pid::outputGain), 2));
        }

        beginTest ("A take that needs more than +24 dB is clamped to +24 dB, and the decision log says so");
        {
            const auto r = mixShifted (referenceDb - 24.0f);
            if (r.ok)
            {
                expectEquals (r.plan.get (pid::inputGain), 24.0f);
                int inputStages = 0;
                for (auto& l : r.plan.log)
                    if (l.globalStage && l.instance == 1)
                    {
                        ++inputStages;
                        expectEquals (l.problem, juce::String ("level"));
                        expect (l.parameters.size() == 1 && l.parameters[0].key == pid::inputGain && l.parameters[0].value == 24.0f);
                        float wanted = 0.0f, clamped = 0.0f;
                        for (auto& m : l.measurements)
                            if (m.key == "wantedGain")
                                wanted = m.value;
                        for (auto& m : l.result)
                            if (m.key == "clamped")
                                clamped = m.value;
                        expect (wanted > 24.0f, "wanted gain " + juce::String (wanted));
                        expectEquals (clamped, 1.0f);
                    }
                expectEquals (inputStages, 1);
                expect (! r.plan.notes.empty());
            }
            else
                expect (false, "a take 24 dB under the reference should still mix (" + r.error + ")");
        }

        beginTest ("The decision log carries the input stage: working level, measured level, the gain");
        {
            int inputStages = 0, outputStages = 0;
            for (auto& l : reference.plan.log)
                if (l.globalStage)
                    (l.instance == 1 ? inputStages : outputStages)++;
            expectEquals (inputStages, 1);
            expectEquals (outputStages, 1);
            for (auto& l : reference.plan.log)
                if (l.globalStage && l.instance == 1)
                {
                    expect (l.parameters.size() == 1 && l.parameters[0].key == pid::inputGain);
                    float voice = 0.0f, working = 0.0f;
                    for (auto& m : l.measurements)
                    {
                        if (m.key == "voiceRms")      voice = m.value;
                        if (m.key == "workingLevel")  working = m.value;
                    }
                    expectEquals (working, workingLevelDb);
                    expectWithinAbsoluteError (voice + reference.plan.get (pid::inputGain), workingLevelDb, 1.0e-3f);
                }
        }

        beginTest ("A clipped take still reports its clipping (the profile looks at the unscaled audio)");
        {
            const auto r = mixAtLevel (-18.0f, 6.0f);
            expect (r.ok, r.error);
            expect (r.profile.clipRuns > 0, "clipRuns " + juce::String (r.profile.clipRuns));
            expect (r.profile.peakDb > -0.1f, "peak " + juce::String (r.profile.peakDb));
            expect (r.profile.hasIssue ("clipping"));
            expect (r.original.clipRuns > 0);
            expect (r.profile.distortionLikely);
            expect (r.plan.get (pid::inputGain) < -1.0f, "a loud, clipped take is brought down: " + juce::String (r.plan.get (pid::inputGain)));
        }

        beginTest ("VocalProfiler::analyse with scaleDb: peak, clipping and hot input refer to the audio before the scaling");
        {
            VocalSpec s;
            s.seconds = 14.0;
            s.clipGain = 6.0f;
            auto raw = makeVocal (s);
            const auto direct = VocalProfiler::analyse (raw);
            auto scaled = raw;
            const float gain = juce::Decibels::decibelsToGain (-9.0f);
            for (auto& ch : scaled.vocal)
                for (auto& x : ch)
                    x *= gain;
            const auto viaScale = VocalProfiler::analyse (scaled, 5.0f, {}, -9.0f);
            expect (direct.clipRuns > 0);
            expectEquals (viaScale.clipRuns, direct.clipRuns);
            expectWithinAbsoluteError (viaScale.peakDb, direct.peakDb, 1.0e-3f);
            expect (viaScale.hasIssue ("clipping") == direct.hasIssue ("clipping"));
            expect (VocalProfiler::analyse (scaled).clipRuns == 0, "unscaled the clipping would be invisible");

            // a hot, unclipped take at -0.2 dBFS peak
            VocalSpec hot;
            hot.seconds = 14.0;
            auto hotAudio = makeVocal (hot);
            float peak = 0.0f;
            for (auto x : hotAudio.vocal[0])
                peak = std::max (peak, std::abs (x));
            const float up = juce::Decibels::decibelsToGain (-0.2f) / peak;
            for (auto& x : hotAudio.vocal[0])
                x *= up;
            auto hotScaled = hotAudio;
            for (auto& x : hotScaled.vocal[0])
                x *= gain;
            expect (VocalProfiler::analyse (hotAudio).hasIssue ("hotInput"));
            expect (VocalProfiler::analyse (hotScaled, 5.0f, {}, -9.0f).hasIssue ("hotInput"));
            expect (! VocalProfiler::analyse (hotScaled).hasIssue ("hotInput"));
        }
    }
};

static ConsistencyTests consistencyTests;
