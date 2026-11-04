// AUTO MIX CONSISTENCY (plan 2026-11-04-automix-konsistenz, Task 2): the soft-threshold helpers and the
// structured per-module decision log.

#include "TestHelpers.h"
#include "SyntheticVocal.h"
#include "AutoMix/Soft.h"
#include "AutoMix/DecisionLog.h"
#include "AutoMix/AutoMixPlanner.h"
#include "AutoMix/VocalProfile.h"

using namespace vocalypse;
using namespace vocalypse::test;
using namespace vocalypse::automix;

class SoftDecisionTests final : public juce::UnitTest
{
public:
    SoftDecisionTests() : juce::UnitTest ("Soft thresholds and decision log", "VOCALYPSE") {}

    void runTest() override
    {
        beginTest ("soft: 0 / 0.5 / 1 at lo / mid / hi, monotonic, clamped");
        {
            expectWithinAbsoluteError (soft (0.0f, 2.0f, 6.0f), 0.0f, 1.0e-6f);
            expectWithinAbsoluteError (soft (4.0f, 2.0f, 6.0f), 0.5f, 1.0e-6f);
            expectWithinAbsoluteError (soft (6.0f, 2.0f, 6.0f), 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (soft (-50.0f, 2.0f, 6.0f), 0.0f, 1.0e-6f);
            expectWithinAbsoluteError (soft (50.0f, 2.0f, 6.0f), 1.0f, 1.0e-6f);
            float last = -1.0f;
            bool monotonic = true;
            for (int i = 0; i <= 100; ++i)
            {
                const float v = soft (2.0f + 0.04f * (float) i, 2.0f, 6.0f);
                monotonic = monotonic && v >= last;
                last = v;
            }
            expect (monotonic);
        }

        beginTest ("soft: lo > hi is falling");
        {
            expectWithinAbsoluteError (soft (6.0f, 6.0f, 2.0f), 0.0f, 1.0e-6f);
            expectWithinAbsoluteError (soft (4.0f, 6.0f, 2.0f), 0.5f, 1.0e-6f);
            expectWithinAbsoluteError (soft (2.0f, 6.0f, 2.0f), 1.0f, 1.0e-6f);
            expect (soft (5.0f, 6.0f, 2.0f) < soft (3.0f, 6.0f, 2.0f));
            expectWithinAbsoluteError (soft (1.0f, 3.0f, 3.0f), 0.0f, 1.0e-6f);   // degenerate: a step, no NaN
            expectWithinAbsoluteError (soft (4.0f, 3.0f, 3.0f), 1.0f, 1.0e-6f);
        }

        beginTest ("blend interpolates");
        {
            expectWithinAbsoluteError (blend (0.0f, 2.0f, 10.0f), 2.0f, 1.0e-6f);
            expectWithinAbsoluteError (blend (1.0f, 2.0f, 10.0f), 10.0f, 1.0e-6f);
            expectWithinAbsoluteError (blend (0.25f, 2.0f, 10.0f), 4.0f, 1.0e-6f);
        }

        beginTest ("chooseWithMargin keeps the favourite unless beaten by more than the margin");
        {
            const std::vector<float> scores { 1.0f, 1.4f, 1.2f };
            expectEquals (chooseWithMargin (scores, 0, 0.5f), 0);      // best beats it by 0.4 < 0.5
            expectEquals (chooseWithMargin (scores, 0, 0.3f), 1);      // 0.4 > 0.3
            expectEquals (chooseWithMargin (scores, 1, 5.0f), 1);      // the favourite is also the best
            expectEquals (chooseWithMargin (scores, 2, 0.1f), 1);
            expectEquals (chooseWithMargin (scores, -1, 0.5f), 1);     // no favourite: the best
            expectEquals (chooseWithMargin ({}, 0, 0.5f), -1);
            expectEquals (chooseWithMargin (scores, 9, 0.5f), 1);      // out of range: no favourite
        }

        beginTest ("toVar round-trips every field through JSON text");
        {
            ModuleLog l;
            l.module = dsp::ModuleId::deess;
            l.instance = 1;
            l.globalStage = false;
            l.active = true;
            l.problem = "sibilance";
            l.measurements = { { "sibilanceVsVowel", 2.5f, "dB" }, { "snr", 31.0f, "dB" } };
            l.need = 0.75f;
            l.intensity = 0.5f;
            l.parameters = { { "deessRange", 5.0f, "dB" } };
            l.result = { { "sibilanceAfter", -4.0f, "dB" } };
            l.skipReason = "Grund / reason";

            const auto text = juce::JSON::toString (toVar (l));
            const auto back = fromVar (juce::JSON::parse (text));
            expect (back.module == l.module);
            expectEquals (back.instance, 1);
            expect (back.globalStage == l.globalStage);
            expect (back.active == l.active);
            expectEquals (back.problem, l.problem);
            expectEquals (back.skipReason, l.skipReason);
            expectWithinAbsoluteError (back.need, 0.75f, 1.0e-6f);
            expectWithinAbsoluteError (back.intensity, 0.5f, 1.0e-6f);
            expectEquals ((int) back.measurements.size(), 2);
            expectEquals ((int) back.parameters.size(), 1);
            expectEquals ((int) back.result.size(), 1);
            if (back.measurements.size() == 2 && back.parameters.size() == 1 && back.result.size() == 1)
            {
                expectEquals (back.measurements[1].key, juce::String ("snr"));
                expectWithinAbsoluteError (back.measurements[0].value, 2.5f, 1.0e-6f);
                expectEquals (back.measurements[0].unit, juce::String ("dB"));
                expectEquals (back.parameters[0].key, juce::String ("deessRange"));
                expectEquals (back.result[0].key, juce::String ("sibilanceAfter"));
            }

            ModuleLog g;
            g.globalStage = true;
            expect (fromVar (juce::JSON::parse (juce::JSON::toString (toVar (g)))).globalStage);
        }

        beginTest ("logFor creates one entry per module instance and finds it again");
        {
            MixPlan p;
            auto& a = p.logFor (dsp::ModuleId::comp);
            a.problem = "x";
            expectEquals ((int) p.log.size(), 1);
            expectEquals (p.logFor (dsp::ModuleId::comp).problem, juce::String ("x"));
            p.logFor (dsp::ModuleId::comp, 1);
            p.logFor (dsp::ModuleId::eq);
            expectEquals ((int) p.log.size(), 3);
        }

        beginTest ("The planner logs every module it decides on, with its parameters and skip reasons");
        {
            const auto profile = VocalProfiler::analyse (makeVocal ({}));
            const auto plan = AutoMixPlanner::plan (profile, {});
            using M = dsp::ModuleId;
            for (auto m : { M::denoise, M::breath, M::eq, M::ride, M::comp, M::smash, M::deess, M::smooth, M::sat, M::doubler, M::reverb, M::delay })
            {
                int n = 0;
                const ModuleLog* entry = nullptr;
                for (auto& l : plan.log)
                    if (! l.globalStage && l.module == m && l.instance == 0)
                    {
                        ++n;
                        entry = &l;
                    }
                expectEquals (n, 1, juce::String (dsp::moduleKey (m)));
                if (entry == nullptr)
                    continue;
                const bool wanted = m == M::denoise ? plan.on (pid::denoiseOn) : m == M::breath ? plan.on (pid::breathOn)
                                                                                 : plan.activeModules().count (m) > 0;
                expect (entry->active == wanted, juce::String (dsp::moduleKey (m)) + " active flag");
                if (m == M::deess || m == M::comp)   // continuous since Task 6
                    expect (entry->need >= 0.0f && entry->need <= 1.0f, juce::String (dsp::moduleKey (m)) + " need");
                else
                    expect (entry->need == (entry->active ? 1.0f : 0.0f), juce::String (dsp::moduleKey (m)) + " need");
                if (entry->active)
                    expect (! entry->parameters.empty(), juce::String (dsp::moduleKey (m)) + " parameters");
                else
                    expect (entry->skipReason.isNotEmpty(), juce::String (dsp::moduleKey (m)) + " skip reason");
            }
            // the log's parameters are the plan's values
            for (auto& l : plan.log)
                for (auto& pr : l.parameters)
                    expect (plan.values.count (pr.key) == 1 && plan.values.at (pr.key) == pr.value, pr.key);
            int outputStage = 0;
            for (auto& l : plan.log)
                outputStage += l.globalStage ? 1 : 0;
            expectEquals (outputStage, 1);
        }

        runDynamicsTests();
    }

    static const ModuleLog* logOf (const MixPlan& plan, dsp::ModuleId m)
    {
        for (auto& l : plan.log)
            if (! l.globalStage && l.module == m && l.instance == 0)
                return &l;
        return nullptr;
    }

    static float effectiveRange (const MixPlan& plan) { return plan.on (pid::deessOn) ? plan.get (pid::deessRange) : 0.0f; }

    static VocalProfile denseProfile()
    {
        VocalProfile p;
        p.enoughMaterial = true;
        p.voicedSeconds = 20.0f;
        p.voiceRmsDb = -18.0f;
        p.dynamicsValid = true;
        p.crestDb = 14.0f;
        return p;
    }

    void runDynamicsTests()
    {
        // ---- De-esser: range follows the sibilance continuously
        beginTest ("De-esser range grows monotonically with the sibilance, no step over 1.5 dB");
        {
            // The synthetic vocal's S/vowel moves by up to 5 dB per sibilance step, so the bound is checked per dB
            // of measurement as well (the curve's steepest slope is 8 dB x 1.5 / 9 dB = 1.33 dB per dB)
            std::vector<float> range, need, measured;
            juce::String line;
            for (int i = 0; i < 9; ++i)
            {
                VocalSpec spec;
                spec.sibilance = 0.1f + 0.1f * (float) i;
                const auto profile = VocalProfiler::analyse (makeVocal (spec));
                const auto plan = AutoMixPlanner::plan (profile, {});
                range.push_back (effectiveRange (plan));
                measured.push_back (profile.sibilanceVsVowelDb);
                const auto* l = logOf (plan, dsp::ModuleId::deess);
                need.push_back (l != nullptr ? l->need : -1.0f);
                line << juce::String (profile.sibilanceVsVowelDb, 1) << "->" << juce::String (range.back(), 2) << " ";
            }
            logMessage ("de-ess sweep (S/vowel -> range): " + line);
            float maxStep = 0.0f;
            for (size_t i = 1; i < range.size(); ++i)
            {
                expect (range[i] >= range[i - 1] - 1.0e-4f, "monotonic at " + juce::String ((int) i));
                const float dMeasured = measured[i] - measured[i - 1];
                if (range[i - 1] > 0.0f && dMeasured > 0.1f)   // both on (the step off -> on is the minimum audible range)
                    maxStep = std::max (maxStep, (range[i] - range[i - 1]) / dMeasured);
                else
                    expect (range[i] - range[i - 1] <= 8.0f);
            }
            expect (maxStep <= 1.5f / 1.0f + 1.0e-3f, "max slope " + juce::String (maxStep, 2) + " dB per dB");
            expect (range.back() > range.front() + 1.0f, "the sweep does move the range");
            for (size_t i = 0; i < need.size(); ++i)
                expect (need[i] >= 0.0f && need[i] <= 1.0f);
        }

        beginTest ("De-esser: on only when range >= 1.5 dB, range = 8 dB x need, threshold between vowel and S level");
        {
            auto p = denseProfile();
            p.sibilanceValid = true;
            p.sibBandVowelP90Db = -40.0f;
            p.sibBandSibilantP50Db = -30.0f;
            p.deessDetectorHz = 6000.0f;
            float last = -1.0f;
            bool onSeen = false;
            for (int i = 0; i <= 120; ++i)
            {
                p.sibilanceVsVowelDb = -14.0f + 0.1f * (float) i;
                const auto plan = AutoMixPlanner::plan (p, {});
                const float need = soft (p.sibilanceVsVowelDb, -6.0f - 6.0f, -6.0f + 3.0f);
                const bool on = plan.on (pid::deessOn);
                if (std::abs (8.0f * need - 1.5f) > 0.05f)
                    expect (on == (8.0f * need >= 1.5f), "on flag at " + juce::String (p.sibilanceVsVowelDb, 1));
                if (on)
                {
                    expectWithinAbsoluteError (plan.get (pid::deessRange), 8.0f * need, 0.26f);
                    const float thr = plan.get (pid::deessThreshold);
                    expect (thr > -40.0f && thr < -30.0f + 0.01f, "threshold between vowel and S");
                    onSeen = true;
                }
                const float eff = effectiveRange (plan);
                if (last >= 0.0f)
                    expect (eff >= last - 0.26f && eff - last <= 1.75f);
                last = eff;
            }
            expect (onSeen);
        }

        // ---- Compressor: ratio and threshold follow the phrase spread continuously
        beginTest ("Compressor ratio and threshold are monotonic in the phrase range, steps <= 0.4 / 1.5 dB");
        {
            std::vector<float> ratio, thr;
            juce::String line;
            for (float range = 4.0f; range <= 16.01f; range += 1.0f)
            {
                VocalSpec spec;
                spec.phraseRangeDb = range;
                const auto profile = VocalProfiler::analyse (makeVocal (spec));
                const auto plan = AutoMixPlanner::plan (profile, {});
                expect (plan.on (pid::compOn));
                ratio.push_back (plan.get (pid::compRatio));
                thr.push_back (plan.get (pid::compThreshold));
                line << juce::String (profile.dynamicsDb, 1) << "->" << juce::String (ratio.back(), 2) << "/" << juce::String (thr.back(), 1) << " ";
            }
            logMessage ("comp sweep (dynamics -> ratio/threshold): " + line);
            for (size_t i = 1; i < ratio.size(); ++i)
            {
                expect (ratio[i] >= ratio[i - 1] - 1.0e-4f, "ratio monotonic at " + juce::String ((int) i));
                expect (ratio[i] - ratio[i - 1] <= 0.4f + 1.0e-3f, "ratio step " + juce::String (ratio[i] - ratio[i - 1], 2));
                expect (std::abs (thr[i] - thr[i - 1]) <= 1.5f + 1.0e-3f, "threshold step " + juce::String (thr[i] - thr[i - 1], 2));
            }
        }

        beginTest ("Compressor on a hand-made profile: ratio on the continuous curve, threshold monotonic in the spread");
        {
            auto p = denseProfile();
            float lastRatio = 0.0f, lastThr = 1000.0f;
            for (int i = 0; i <= 120; ++i)
            {
                p.dynamicsDb = 4.0f + 0.1f * (float) i;
                const auto plan = AutoMixPlanner::plan (p, {});
                const float ratio = plan.get (pid::compRatio), thr = plan.get (pid::compThreshold);
                if (i > 0)
                {
                    expect (ratio >= lastRatio - 1.0e-4f, "ratio at " + juce::String (p.dynamicsDb, 1));
                    expect (ratio - lastRatio <= 0.1f, "ratio step at " + juce::String (p.dynamicsDb, 1));
                    expect (thr <= lastThr + 1.0e-4f, "threshold monotonic at " + juce::String (p.dynamicsDb, 1));
                }
                expect (ratio >= 1.5f - 1.0e-4f && ratio <= 5.0f + 1.0e-4f);
                lastRatio = ratio;
                lastThr = thr;
            }
        }

        beginTest ("Compressor: the dense-material blend has no jump over 1 ratio across dynamicsDb 5.5 .. 6.5");
        {
            auto p = denseProfile();
            p.crestDb = 9.5f;
            float lastRatio = 0.0f, lastThr = 0.0f;
            for (int i = 0; i <= 100; ++i)
            {
                p.dynamicsDb = 5.5f + 0.01f * (float) i;
                const auto plan = AutoMixPlanner::plan (p, {});
                const float ratio = plan.get (pid::compRatio), thr = plan.get (pid::compThreshold);
                if (i > 0)
                {
                    expect (std::abs (ratio - lastRatio) <= 1.0f, "ratio jump at " + juce::String (p.dynamicsDb, 2));
                    expect (std::abs (thr - lastThr) <= 1.5f, "threshold jump at " + juce::String (p.dynamicsDb, 2));
                }
                lastRatio = ratio;
                lastThr = thr;
            }
            // the ends of the blend: the whole 5.5 -> 6.5 step as one
            p.dynamicsDb = 5.5f;
            const float a = AutoMixPlanner::plan (p, {}).get (pid::compRatio);
            p.dynamicsDb = 6.5f;
            expect (std::abs (AutoMixPlanner::plan (p, {}).get (pid::compRatio) - a) <= 1.0f);
            // dense material gets the gentle end
            p.dynamicsDb = 4.0f;
            p.crestDb = 8.0f;
            expect (AutoMixPlanner::plan (p, {}).get (pid::compRatio) <= 1.6f);
        }

        beginTest ("Log entries of the de-esser and compressor carry need, intensity and parameters");
        {
            VocalSpec spec;
            spec.sibilance = 0.8f;
            const auto profile = VocalProfiler::analyse (makeVocal (spec));
            const auto plan = AutoMixPlanner::plan (profile, {});
            for (auto m : { dsp::ModuleId::deess, dsp::ModuleId::comp })
            {
                const auto* l = logOf (plan, m);
                expect (l != nullptr);
                if (l == nullptr)
                    continue;
                if (l->active)
                {
                    expect (l->need > 0.0f && l->need <= 1.0f && l->intensity > 0.0f && l->intensity <= 1.0f);
                    expect (! l->parameters.empty());
                }
                else
                    expect (l->skipReason.isNotEmpty());
            }
            const auto* d = logOf (plan, dsp::ModuleId::deess);
            if (d != nullptr && d->active)
                expectWithinAbsoluteError (d->intensity, plan.get (pid::deessRange) / 8.0f, 1.0e-4f);
        }
    }
};

static SoftDecisionTests softDecisionTests;
