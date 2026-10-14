// Beginner macros: neutrality, real effect on the audio, clamping, no
// overwriting of base parameters, view mode never changes the sound.

#include "TestHelpers.h"
#include "StateKeys.h"

using namespace velvox;
using namespace velvox::test;

class MacroTests final : public juce::UnitTest
{
public:
    MacroTests() : juce::UnitTest ("Velvox macros", "Velvox") {}

    void runTest() override
    {
        beginTest ("All macros at 0: effective settings equal the base settings");
        {
            Harness h;
            for (auto& p : getFactoryPresets())
                if (p.name == "Hypertechno Lead")
                    p.applyTo (h.proc.getState());
            const auto base = h.proc.readSettings();
            const auto eff = h.proc.readEffectiveSettings();
            expectEquals (eff.comp.thresholdDb, base.comp.thresholdDb);
            expectEquals (eff.reverbMix, base.reverbMix);
            expectEquals (eff.stereo.width, base.stereo.width);
            expect (eff.doubler.enabled == base.doubler.enabled);
        }

        beginTest ("Space switches on reverb and delay with ducking");
        {
            Harness h;
            h.neutral();
            h.set (pid::macroSpace, 100.0f);
            const auto eff = h.proc.readEffectiveSettings();
            expect (eff.reverbOn && eff.delayOn);
            expectWithinAbsoluteError (eff.reverbMix, 0.30f, 1.0e-5f);
            expectWithinAbsoluteError (eff.delayMix, 0.15f, 1.0e-5f);
            expectWithinAbsoluteError (eff.reverbDuck.amountDb, 6.0f, 1.0e-4f);
            const auto engaged = dsp::modulesEngagedByMacros (h.proc.readSettings(), h.proc.readMacros());
            expect (engaged[(size_t) dsp::ModuleId::reverb] && engaged[(size_t) dsp::ModuleId::delay]);
            expect (! engaged[(size_t) dsp::ModuleId::eq]);

            // Audible: a reverb tail appears after the signal stops
            auto in = h.noise (0.3f, 0.5, 3);
            juce::AudioBuffer<float> sig (2, (int) (1.5 * h.fs));
            sig.clear();
            for (int ch = 0; ch < 2; ++ch)
                sig.copyFrom (ch, 0, in, ch, 0, in.getNumSamples());
            const auto out = h.run (sig);
            expectGreaterThan (rmsDb (out, (int) (0.7 * h.fs), (int) (0.3 * h.fs)), -60.0f, "tail after the input stopped");
        }

        beginTest ("Presence lifts the top end, Aggression adds compression");
        {
            const auto levelAt16k = [] (float presence)
            {
                Harness h;
                h.neutral();
                h.set (pid::macroPresence, presence);
                const auto out = h.run (h.sine (16000.0, 0.005f, 1.0)); // below de-esser threshold
                return rmsDb (out, 24000, 24000);
            };
            const float lift = levelAt16k (100.0f) - levelAt16k (0.0f);
            logMessage ("Presence 100 % at 16 kHz: +" + juce::String (lift, 2) + " dB");
            expectGreaterThan (lift, 3.5f);

            const auto gr = [] (float aggression)
            {
                Harness h;
                h.neutral();
                h.set (pid::compOn, 1.0f);
                h.set (pid::macroAggression, aggression);
                h.run (h.noise (0.2f, 1.0, 5));
                return h.proc.getChain().getCompressor().gainReductionDb.pull();
            };
            const float g0 = gr (0.0f), g100 = gr (100.0f);
            logMessage ("Compressor GR: aggression 0 % " + juce::String (g0, 1) + " dB, 100 % " + juce::String (g100, 1) + " dB");
            expectGreaterThan (g100, g0 + 5.0f);
        }

        beginTest ("Width widens via doubler and stereo width");
        {
            const auto sideEnergy = [] (float width)
            {
                Harness h;
                h.neutral();
                h.set (pid::macroWidth, width);
                auto in = h.noise (0.3f, 1.0, 7);
                in.copyFrom (1, 0, in, 0, 0, in.getNumSamples()); // identical channels = mono source
                const auto out = h.run (in);
                double side = 0.0;
                for (int i = 24000; i < out.getNumSamples(); ++i)
                    side += std::pow (out.getSample (0, i) - out.getSample (1, i), 2.0);
                return side;
            };
            expectLessThan (sideEnergy (0.0f), 1.0e-6);
            expectGreaterThan (sideEnergy (100.0f), 1.0);
        }

        beginTest ("Extreme macros stay within parameter ranges and produce valid audio");
        {
            Harness h;
            for (auto& p : getFactoryPresets())
                if (p.name == "Aggressive Vocal")
                    p.applyTo (h.proc.getState());
            for (auto* id : { pid::macroPresence, pid::macroWarmth, pid::macroAggression, pid::macroWidth, pid::macroSpace, pid::macroDensity })
                h.set (id, 100.0f);
            const auto eff = h.proc.readEffectiveSettings();
            expect (eff.comp.ratio <= 20.0f && eff.comp.thresholdDb >= -60.0f && eff.comp.makeupDb <= 24.0f);
            expect (eff.sat.driveDb <= 24.0f && eff.sat.mix <= 1.0f && eff.stereo.width <= 2.0f);
            expect (eff.eq.highGain <= 18.0f && eff.eq.lowGain <= 18.0f);
            const auto out = h.run (h.noise (0.5f, 2.0, 9));
            expect (allFinite (out));
        }

        beginTest ("Macros never overwrite the base (Pro) parameters");
        {
            Harness h;
            h.neutral();
            const auto before = ParameterSnapshot::capture (h.proc.getState());
            h.set (pid::macroAggression, 80.0f);
            h.run (h.noise (0.3f, 0.5, 11));
            const auto after = ParameterSnapshot::capture (h.proc.getState());
            for (const auto& [id, v] : before.values)
                if (! id.startsWith ("macro"))
                    expectEquals (after.values.at (id), v, id);
        }

        beginTest ("Beginner / Pro view mode never changes the sound and is saved");
        {
            const auto render = [] (const juce::String& mode)
            {
                Harness h;
                for (auto& p : getFactoryPresets())
                    if (p.name == "Modern Pop")
                        p.applyTo (h.proc.getState());
                h.proc.getState().state.setProperty (statekeys::uiMode, mode, nullptr);
                return h.run (h.noise (0.3f, 1.0, 13));
            };
            const auto a = render ("beginner"), b = render ("pro");
            double d = 0.0;
            for (int i = 0; i < a.getNumSamples(); ++i)
                d = std::max (d, (double) std::abs (a.getSample (0, i) - b.getSample (0, i)));
            expectEquals (d, 0.0);

            juce::MemoryBlock block;
            {
                Harness h;
                h.proc.getState().state.setProperty (statekeys::uiMode, "pro", nullptr);
                h.proc.getStateInformation (block);
            }
            Harness h2;
            h2.proc.setStateInformation (block.getData(), (int) block.getSize());
            expectEquals (h2.proc.getState().state.getProperty (statekeys::uiMode).toString(), juce::String ("pro"));
        }
    }
};

static MacroTests macroTests;
