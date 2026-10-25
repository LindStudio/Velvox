// Beginner macros: neutrality, real effect on the audio, clamping, no
// overwriting of base parameters, view mode never changes the sound.

#include "TestHelpers.h"
#include "MacroTestHelpers.h"
#include "StateKeys.h"

using namespace velvox;
using namespace velvox::test;

class MacroTests final : public juce::UnitTest
{
public:
    MacroTests() : juce::UnitTest ("Velvox macros", "Velvox") {}
    void moveTests();

    void runTest() override
    {
        moveTests();

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

        beginTest ("SPACE drag on a fresh instance: the insert -> send switch is part of the undo step and has no dip");
        {
            Harness h;   // fresh: delay / reverb are (unused) inserts
            expect (h.proc.getRouting().isInsert (dsp::ModuleId::reverb, 0) && h.proc.getRouting().isInsert (dsp::ModuleId::delay, 0));
            const auto routingBefore = h.proc.getRouting();
            const auto in = h.noise (0.25f, 2.0, 21);
            juce::AudioBuffer<float> out (in);
            juce::MidiBuffer midi;
            auto* space = h.proc.getState().getParameter (pid::macroSpace);
            const int bs = h.blockSize, numBlocks = in.getNumSamples() / bs;
            const int dragFrom = numBlocks / 3, dragTo = dragFrom + 20;
            for (int b = 0; b < numBlocks; ++b)
            {
                if (b == dragFrom)
                    space->beginChangeGesture();
                if (b >= dragFrom && b < dragTo)
                    space->setValueNotifyingHost ((float) (b - dragFrom + 1) / 20.0f);
                if (b == dragTo)
                    space->endChangeGesture();
                juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), out.getNumChannels(), b * bs, bs);
                h.proc.processBlock (view, midi);
            }
            expect (! h.proc.getRouting().isInsert (dsp::ModuleId::reverb, 0), "SPACE made the reverb a send");

            // Level in 1024-sample windows: never more than 1 dB below the level before the drag
            const int win = 1024;
            const auto levelAt = [&] (int start)
            {
                double sum = 0.0;
                for (int ch = 0; ch < out.getNumChannels(); ++ch)
                    for (int i = start; i < start + win; ++i)
                        sum += (double) out.getSample (ch, i) * out.getSample (ch, i);
                return 10.0 * std::log10 (sum / (win * out.getNumChannels()) + 1.0e-30);
            };
            double ref = 0.0;
            int refCount = 0;
            for (int s = dragFrom * bs - 8 * win; s < dragFrom * bs; s += win, ++refCount)
                ref += levelAt (s);
            ref /= refCount;
            double worst = 1.0e9;
            for (int s = dragFrom * bs; s + win <= (dragTo + 20) * bs; s += win / 2)
                worst = std::min (worst, levelAt (s) - ref);
            logMessage ("deepest dip during the SPACE drag: " + juce::String (worst, 2) + " dB");
            expectGreaterThan (worst, -1.0, "no routing dip");

            auto& undo = h.proc.getUndo();
            expectEquals (undo.getNumSteps(), 1, "the drag is one step");
            expect (undo.undo());
            expectWithinAbsoluteError (h.get (pid::macroSpace), 0.0f, 1.0e-4f);
            expect (h.proc.getRouting() == routingBefore, "undo brings the inserts back");
            expect (undo.redo());
            expect (! h.proc.getRouting().isInsert (dsp::ModuleId::reverb, 0), "redo: send again");
        }
    }
};

void MacroTests::moveTests()
{
    using dsp::MacroId;
    const MacroId all[] { MacroId::presence, MacroId::warmth, MacroId::aggression, MacroId::width, MacroId::space, MacroId::density };

    // Module states to start from: a fresh plugin, two factory presets, every module off
    std::vector<std::pair<juce::String, dsp::ChainSettings>> bases;
    {
        Harness h;
        bases.push_back ({ "fresh", h.proc.readSettings() });
        for (auto* name : { "Hypertechno Lead", "Aggressive Vocal" })
        {
            Harness p;
            for (auto& preset : getFactoryPresets())
                if (preset.name == name)
                    preset.applyTo (p.proc.getState());
            bases.push_back ({ name, p.proc.readSettings() });
        }
        Harness n;
        n.neutral();
        bases.push_back ({ "neutral", n.proc.readSettings() });
    }

    beginTest ("Simple move from 0 = the 0.6.0 macro mapping of that macro alone");
    for (const auto& [name, base] : bases)
        for (auto id : all)
            for (float to : { 0.25f, 0.6f, 1.0f })
            {
                dsp::MacroValues m;
                m.set (id, to);
                const auto diff = macroFieldDifference (dsp::applyMacroMove (base, id, 0.0f, to), dsp::applyMacros (base, m));
                expect (diff.isEmpty(), name + ", macro " + juce::String ((int) id) + " -> " + juce::String (to) + ": " + diff);
            }

    beginTest ("Simple move back to its start restores the module state exactly");
    for (const auto& [name, base] : bases)
        for (auto id : all)
            for (float at : { 0.0f, 0.4f, 1.0f })
            {
                const auto diff = macroFieldDifference (dsp::applyMacroMove (base, id, at, at), base);
                expect (diff.isEmpty(), name + ", macro " + juce::String ((int) id) + " at " + juce::String (at) + ": " + diff);
            }

    beginTest ("Simple move down: less of the effect, a mix module at 0 switches off, nothing is created");
    {
        const auto neutral = bases.back().second;
        const auto up = dsp::applyMacroMove (neutral, MacroId::space, 0.0f, 0.5f);
        expect (up.reverbOn && up.delayOn);
        expectWithinAbsoluteError (up.reverbMix, 0.15f, 1.0e-6f);
        expectWithinAbsoluteError (up.reverbDuck.amountDb, 3.0f, 1.0e-5f);

        const auto half = dsp::applyMacroMove (up, MacroId::space, 0.5f, 0.25f);
        expect (half.reverbOn);
        expectWithinAbsoluteError (half.reverbMix, 0.075f, 1.0e-6f);
        expectWithinAbsoluteError (half.reverbDuck.amountDb, 1.5f, 1.0e-5f);

        const auto down = dsp::applyMacroMove (up, MacroId::space, 0.5f, 0.0f);
        expect (! down.reverbOn && ! down.delayOn, "a mix brought to 0 switches its module off");
        expectWithinAbsoluteError (down.reverbMix, 0.0f, 1.0e-7f);
        expectWithinAbsoluteError (down.reverbDuck.amountDb, 0.0f, 1.0e-6f);

        // Off modules stay off and untouched when a slider goes down; no role band is created
        const auto offDown = dsp::applyMacroMove (neutral, MacroId::space, 0.8f, 0.2f);
        expect (! offDown.reverbOn && ! offDown.delayOn);
        expectEquals (offDown.reverbMix, neutral.reverbMix);
        auto noRoles = neutral;
        for (auto& b : noRoles.eq.bands)
            b = {};
        const auto presDown = dsp::applyMacroMove (noRoles, MacroId::presence, 0.7f, 0.1f);
        expect (macroFieldDifference (presDown, noRoles).isEmpty(), "switched-off modules are left alone by a down move");
        for (const auto& b : presDown.eq.bands)
            expect (! b.on && b.role == 0, "no role band created by a down move");

        // Lower PUNCH: threshold up, ratio down, attack longer, clamped to the ranges
        auto comp = neutral;
        comp.compOn = true;
        comp.comp.thresholdDb = -5.0f;
        comp.comp.ratio = 2.0f;
        comp.comp.attackMs = 10.0f;
        const auto softer = dsp::applyMacroMove (comp, MacroId::aggression, 1.0f, 0.0f);
        expectEquals (softer.comp.thresholdDb, 0.0f, "clamped at 0 dB");
        expectEquals (softer.comp.ratio, 1.0f, "clamped at 1:1");
        expectWithinAbsoluteError (softer.comp.attackMs, 25.0f, 1.0e-3f, "attack / (1 - 0.6)");
    }

    beginTest ("Simple move never touches user EQ bands");
    {
        auto base = bases.back().second;
        for (auto& b : base.eq.bands)
            b = {};
        base.eq.bands[0] = { true, (int) dsp::EqBandType::highShelf, 9000.0f, 3.0f, 0.7f, 1, 0 };   // the user's own shelf
        const auto t = dsp::applyMacroMove (base, MacroId::presence, 0.0f, 1.0f);
        expectEquals (t.eq.bands[0].gain, 3.0f, "user band unchanged");
        const int air = t.eq.findRole (dsp::EqRole::air);
        expect (air > 0, "a role band in a free slot");
        if (air > 0)
            expectWithinAbsoluteError (t.eq.bands[(size_t) air].gain, 5.0f, 1.0e-5f);
    }
}

static MacroTests macroTests;
