// Headliner 0.6.1 chain items: module solo (spec item 4) and horizontal scrolling of the chain row (item 3).
// Solo is a monitoring state: every other module instance bypassed, latency unchanged, never saved, never an undo step.

#include "TestHelpers.h"
#include "DSP/Solo.h"

using namespace velvox;
using namespace velvox::test;
using dsp::ModuleId;

namespace
{
    // Every module instance switched on (the flags applySolo works on)
    dsp::ChainSettings allOn()
    {
        dsp::ChainSettings s;
        for (int k = 0; k <= dsp::maxExtraInstances; ++k)
        {
            auto b = s.instance (k);
            b.eqOn = b.compOn = b.deessOn = b.delayOn = b.reverbOn = true;
            b.sat.enabled = b.doubler.enabled = b.smash.enabled = b.choir.enabled = b.smooth.enabled = b.ride.enabled = true;
            if (k == 0)
                s.setInstance0 (b);
            else
                s.extra[(size_t) k - 1] = b;
        }
        s.pitch.enabled = s.chop.enabled = s.pulse.enabled = s.checkOn = true;
        s.pitch.mix = 0.8f;
        return s;
    }

    // On-state of one instance as applySolo sees it
    bool onOf (const dsp::ChainSettings& s, ModuleId id, int k)
    {
        const auto b = s.instance (k);
        switch (id)
        {
            case ModuleId::eq:      return b.eqOn;
            case ModuleId::comp:    return b.compOn;
            case ModuleId::deess:   return b.deessOn;
            case ModuleId::sat:     return b.sat.enabled;
            case ModuleId::doubler: return b.doubler.enabled;
            case ModuleId::delay:   return b.delayOn;
            case ModuleId::reverb:  return b.reverbOn;
            case ModuleId::smash:   return b.smash.enabled;
            case ModuleId::choir:   return b.choir.enabled;
            case ModuleId::smooth:  return b.smooth.enabled;
            case ModuleId::ride:    return b.ride.enabled;
            case ModuleId::pitch:   return s.pitch.enabled && s.pitch.mix > 0.0f;
            case ModuleId::chop:    return s.chop.enabled;
            case ModuleId::pulse:   return s.pulse.enabled;
            case ModuleId::check:   return s.checkOn;
        }
        return false;
    }
}

class ChainSoloTests final : public juce::UnitTest
{
public:
    ChainSoloTests() : juce::UnitTest ("Chain solo and scrolling (0.6.1)", "Velvox") {}

    void runTest() override
    {
        beginTest ("applySolo: only the soloed instance stays on; Check (read only) untouched");
        {
            for (int m = 0; m < dsp::numModules; ++m)
            {
                const auto soloId = (ModuleId) m;
                if (! dsp::canSolo (soloId))
                    continue;
                for (int k = 0; k < dsp::maxInstances (soloId); ++k)
                {
                    auto s = allOn();
                    dsp::applySolo (s, soloId, k);
                    for (int o = 0; o < dsp::numModules; ++o)
                        for (int j = 0; j < dsp::maxInstances ((ModuleId) o); ++j)
                        {
                            const auto id = (ModuleId) o;
                            const bool expected = (id == soloId && j == k) || id == ModuleId::check;
                            expect (onOf (s, id, j) == expected,
                                    juce::String ("solo ") + dsp::moduleKey (soloId) + "#" + juce::String (k + 1) + ": "
                                        + dsp::moduleKey (id) + "#" + juce::String (j + 1));
                        }
                }
            }
            expect (! dsp::canSolo (ModuleId::check), "Check does not change the sound: no solo");
        }

        beginTest ("applySolo: the pitch shifter keeps its latency (on, mix 0); the soloed module keeps its settings");
        {
            auto s = allOn();
            dsp::applySolo (s, ModuleId::comp, 0);
            expect (s.pitch.enabled, "pitch stays on (latency unchanged)");
            expectEquals (s.pitch.mix, 0.0f, "pitch heard dry");

            auto p = allOn();
            dsp::applySolo (p, ModuleId::pitch, 0);
            expect (p.pitch.enabled);
            expectEquals (p.pitch.mix, 0.8f, "soloed pitch unchanged");

            auto off = allOn();
            off.compOn = false;
            dsp::applySolo (off, ModuleId::comp, 0);
            expect (! off.compOn, "a soloed module that is switched off stays off");
        }

        runProcessorTests();
    }

    // A chain with tone, dynamics, colour, latency and a send: EQ, Comp, Sat, Smash, Pitch (off), Reverb send
    static void setupBusyChain (Harness& h, bool pitchOn)
    {
        h.proc.setRouting (dsp::Routing::fromString ("v2,eq,comp,sat,smash,pitch,reverb:send"));
        h.set (pid::eqOn, 1.0f);
        h.set (pid::compOn, 1.0f);
        h.set (pid::satOn, 1.0f);
        h.set (pid::smashOn, 1.0f);
        h.set (pid::pitchOn, pitchOn ? 1.0f : 0.0f);
        h.set (pid::reverbOn, 1.0f);
        h.set (pid::deessOn, 0.0f);
        h.set (pid::delayOn, 0.0f);
    }

    // Largest second difference (a click shows as a spike of curvature)
    static float maxCurvature (const juce::AudioBuffer<float>& b, int from)
    {
        float m = 0.0f;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = std::max (2, from); i < b.getNumSamples(); ++i)
                m = std::max (m, std::abs (b.getSample (ch, i) - 2.0f * b.getSample (ch, i - 1) + b.getSample (ch, i - 2)));
        return m;
    }

    void runProcessorTests()
    {
        const int comp = dsp::selectionCode (ModuleId::comp, 0);
        const int sat = dsp::selectionCode (ModuleId::sat, 0);

        beginTest ("Processor solo: one at a time, click again releases, Check and absent instances cannot be soloed");
        {
            Harness h;
            setupBusyChain (h, false);
            expectEquals (h.proc.getSolo(), -1, "no solo by default");
            expect (h.proc.toggleSolo (comp), "solo on");
            expect (h.proc.isSoloed (comp));
            expect (h.proc.toggleSolo (sat), "another solo replaces it");
            expect (h.proc.isSoloed (sat) && ! h.proc.isSoloed (comp), "only one solo at a time");
            expect (! h.proc.toggleSolo (sat), "click again releases");
            expectEquals (h.proc.getSolo(), -1);

            h.proc.setSolo (dsp::selectionCode (ModuleId::comp, 1));
            expectEquals (h.proc.getSolo(), -1, "an instance not in the chain cannot be soloed");
            auto r = h.proc.getRouting();
            r.insertDefault (ModuleId::check, 0);
            h.proc.setRouting (r);
            h.proc.setSolo ((int) ModuleId::check);
            expectEquals (h.proc.getSolo(), -1, "Check cannot be soloed");
        }

        beginTest ("Processor solo: only the soloed module is heard (= the chain with every other module off)");
        {
            Harness a, b;
            setupBusyChain (a, false);
            setupBusyChain (b, false);
            for (auto* id : { pid::eqOn, pid::satOn, pid::smashOn, pid::reverbOn })
                b.set (id, 0.0f);
            a.proc.setSolo (comp);
            const auto in = a.noise (0.25f, 2.0, 7);
            const auto outA = a.run (in);
            const auto outB = b.run (in);
            const int from = (int) (0.8 * a.fs);
            float diff = 0.0f;
            for (int ch = 0; ch < outA.getNumChannels(); ++ch)
                for (int i = from; i < outA.getNumSamples(); ++i)
                    diff = std::max (diff, std::abs (outA.getSample (ch, i) - outB.getSample (ch, i)));
            expect (diff < 1.0e-3f, "solo comp == comp alone, max diff " + juce::String (diff, 6));
        }

        beginTest ("Processor solo: latency unchanged (saturation in the chain, pitch shifter on)");
        {
            Harness h;
            setupBusyChain (h, true);
            h.proc.updateHostLatency();
            h.run (h.sine (220.0, 0.2f, 0.3));
            const int hostBefore = h.proc.getLatencySamples(), chainBefore = h.proc.getChain().getLatencySamples();
            expect (hostBefore > 0 && chainBefore == hostBefore, "latent chain");
            const int switchesBefore = h.proc.getChain().meters.structureSwitches.load();
            for (int code : { comp, sat, (int) ModuleId::smash, (int) ModuleId::pitch, (int) ModuleId::reverb, -1 })
            {
                h.proc.setSolo (code);
                h.proc.updateHostLatency();
                h.run (h.sine (220.0, 0.2f, 0.3));
                expectEquals (h.proc.getLatencySamples(), hostBefore, "host latency, solo " + juce::String (code));
                expectEquals (h.proc.getChain().getLatencySamples(), chainBefore, "chain latency, solo " + juce::String (code));
            }
            expectEquals (h.proc.getChain().meters.structureSwitches.load(), switchesBefore, "no routing fade (dip) for a solo");
        }

        beginTest ("Processor solo: switching is click-free");
        {
            // Reference: the largest curvature of every static solo state
            const int codes[] { -1, (int) ModuleId::eq, comp, sat, (int) ModuleId::smash, (int) ModuleId::reverb, (int) ModuleId::pitch };
            float reference = 0.0f;
            for (int code : codes)
            {
                Harness h;
                setupBusyChain (h, true);
                h.proc.setSolo (code);
                const auto out = h.run (h.sine (180.0, 0.3f, 1.5));
                reference = std::max (reference, maxCurvature (out, (int) (0.7 * h.fs)));
            }

            // Solo switched every 0.25 s (whole sine periods, so the input itself is continuous)
            const auto switching = [&] (bool pitchOn, std::function<void (Harness&, int)> apply)
            {
                Harness h;
                setupBusyChain (h, pitchOn);
                h.run (h.sine (180.0, 0.3f, 0.7));
                float worst = 0.0f;
                for (int round = 0; round < 3; ++round)
                    for (int code : codes)
                    {
                        apply (h, code);
                        worst = std::max (worst, maxCurvature (h.run (h.sine (180.0, 0.3f, 0.25)), 0));
                    }
                return worst;
            };
            const float solo = switching (true, [] (Harness& h, int code) { h.proc.setSolo (code); });
            const float soloNoPitch = switching (false, [] (Harness& h, int code) { h.proc.setSolo (code); });
            // The same changes made by hand with the power switches (pitch off: its switch changes the latency)
            const auto byHand = [] (Harness& h, int code)
            {
                for (auto [id, param] : { std::pair { ModuleId::eq, pid::eqOn }, { ModuleId::comp, pid::compOn }, { ModuleId::sat, pid::satOn },
                                          { ModuleId::smash, pid::smashOn }, { ModuleId::reverb, pid::reverbOn } })
                    h.set (param, code < 0 || code == (int) id ? 1.0f : 0.0f);
                // the pitch shifter's mix knob turned like the solo does (its switch would change the latency)
                h.set (pid::pitchMix, code < 0 || code == (int) ModuleId::pitch ? 100.0f : 0.0f);
            };
            const float handNoPitch = switching (false, byHand), handPitch = switching (true, byHand);
            logMessage ("    curvature: solo " + juce::String (solo, 5) + " / pitch off " + juce::String (soloNoPitch, 5) + "; by hand "
                        + juce::String (handPitch, 5) + " / " + juce::String (handNoPitch, 5) + "; static " + juce::String (reference, 5));
            expect (soloNoPitch <= handNoPitch * 1.1f + 1.0e-5f, "as smooth as switching the modules by hand");
            expect (solo <= handPitch * 1.1f + 1.0e-5f, "as smooth as switching by hand, pitch shifter on");
            expect (solo < 0.01f, "far from a click (a 1 % step)");
        }

        beginTest ("Processor solo: a monitoring state - never saved, never an undo step, never marks parameters");
        {
            Harness h;
            setupBusyChain (h, false);
            const auto stateBefore = h.proc.getState().copyState().toXmlString();
            const int stepsBefore = h.proc.getUndo().getNumSteps();
            h.proc.setSolo (comp);
            h.run (h.sine (220.0, 0.2f, 0.1));
            expectEquals (h.proc.getState().copyState().toXmlString(), stateBefore, "state untouched");
            expectEquals (h.proc.getUndo().getNumSteps(), stepsBefore, "no undo step");
            expect (h.get (pid::eqOn) > 0.5f, "the other modules' switches stay on");

            juce::MemoryBlock saved;
            h.proc.getStateInformation (saved);
            Harness other;
            other.proc.setStateInformation (saved.getData(), (int) saved.getSize());
            expectEquals (other.proc.getSolo(), -1, "a loaded project never starts soloed");
            h.proc.setStateInformation (saved.getData(), (int) saved.getSize());
            expectEquals (h.proc.getSolo(), -1, "loading a project ends the solo");
        }

        beginTest ("Processor solo: the soloed instance leaving the chain ends the solo");
        {
            Harness h;
            setupBusyChain (h, false);
            h.proc.setSolo (comp);
            auto r = h.proc.getRouting();
            r.remove (ModuleId::comp, 0);
            h.proc.setRouting (r);
            expectEquals (h.proc.getSolo(), -1);
        }
    }
};

static ChainSoloTests chainSoloTests;
