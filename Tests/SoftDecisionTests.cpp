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
    }
};

static SoftDecisionTests softDecisionTests;
