// Routing (module order, send/insert), state migration and preset format 2.

#include "TestHelpers.h"
#include "StateKeys.h"

using namespace velvox;
using namespace velvox::test;
using dsp::ModuleId;
using dsp::Routing;

namespace
{
    double maxDiff (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int start)
    {
        double m = 0.0;
        for (int ch = 0; ch < a.getNumChannels(); ++ch)
            for (int i = start; i < a.getNumSamples(); ++i)
                m = std::max (m, (double) std::abs (a.getSample (ch, i) - b.getSample (ch, i)));
        return m;
    }

    // Comp + saturation on, everything else neutral: order matters for this pair
    void compAndSat (Harness& h)
    {
        h.neutral();
        h.set (pid::compOn, 1.0f);
        h.set (pid::compThreshold, -30.0f);
        h.set (pid::compRatio, 6.0f);
        h.set (pid::compMakeup, 6.0f);
        h.set (pid::satOn, 1.0f);
        h.set (pid::satDrive, 18.0f);
    }
}

class RoutingTests final : public juce::UnitTest
{
public:
    RoutingTests() : juce::UnitTest ("Velvox routing & migration", "Velvox") {}

    void runTest() override
    {
        beginTest ("Routing model: string, packing, move, tolerant parsing");
        {
            const auto def = Routing::defaults();
            expect (def.isValidPermutation());
            expectEquals (def.toString(), juce::String ("eq,comp,deess,sat,doubler,pitch,delay:send,reverb:send,chop"));
            expect (Routing::fromString (def.toString()) == def);
            expect (Routing::unpack (def.pack()) == def);

            auto r = def;
            r.move (ModuleId::sat, 0);
            r.reverbInsert = true;
            expectEquals (r.positionOf (ModuleId::sat), 0);
            expectEquals (r.positionOf (ModuleId::eq), 1);
            expect (r.isValidPermutation());
            expect (Routing::fromString (r.toString()) == r);
            expect (Routing::unpack (r.pack()) == r);

            // placeBefore in both directions (used by drag & drop)
            auto p = def;
            p.placeBefore (ModuleId::sat, ModuleId::comp);       // move left
            expect (p.positionOf (ModuleId::sat) + 1 == p.positionOf (ModuleId::comp));
            p.placeBefore (ModuleId::eq, ModuleId::doubler);     // move right
            expect (p.positionOf (ModuleId::eq) + 1 == p.positionOf (ModuleId::doubler));
            p.placeAtEnd (ModuleId::comp);
            expectEquals (p.positionOf (ModuleId::comp), dsp::numModules - 1);
            expect (p.isValidPermutation());

            // Unknown keys, duplicates and missing modules
            const auto parsed = Routing::fromString ("reverb:insert, banana, sat, sat, eq");
            expect (parsed.isValidPermutation());
            expect (parsed.order[0] == ModuleId::reverb && parsed.reverbInsert);
            expect (parsed.order[1] == ModuleId::sat && parsed.order[2] == ModuleId::eq);
            expect (Routing::fromString ("").isValidPermutation());
            expect (Routing::unpack (0xffffffffffffffffull) == def, "invalid packed value falls back to default");
        }

        beginTest ("Module order changes the result (comp->sat vs sat->comp)");
        {
            Harness a, b;
            compAndSat (a);
            compAndSat (b);
            auto r = Routing::defaults();
            r.move (ModuleId::sat, 0);
            b.proc.setRouting (r);

            const auto in = a.noise (0.3f, 1.0, 41);
            const auto outA = a.run (in);
            const auto outB = b.run (in);
            expect (allFinite (outB));
            const double d = maxDiff (outA, outB, 4800);
            logMessage ("Max difference between orders: " + juce::String (d, 4));
            expectGreaterThan (d, 0.01);
        }

        beginTest ("Delay as last insert equals delay as send (reverb off)");
        {
            Harness a, b;
            for (auto* h : { &a, &b })
            {
                h->neutral();
                h->set (pid::delayOn, 1.0f);
                h->set (pid::delayMix, 60.0f);
                h->set (pid::delayFeedback, 40.0f);
                h->set (pid::delayTime, 50.0f);
            }
            auto r = Routing::defaults();
            r.delayInsert = true;
            r.move (ModuleId::delay, dsp::numModules - 1);
            b.proc.setRouting (r);

            const auto in = a.noise (0.3f, 1.0, 43);
            const auto outA = a.run (in);
            const auto outB = b.run (in);
            const double d = maxDiff (outA, outB, 0);
            logMessage ("Send vs last insert max difference: " + juce::String (d, 8));
            expectLessThan (d, 1.0e-6);
        }

        beginTest ("Reverb as first insert: dry exactly once, processed by later modules");
        {
            Harness h;
            h.neutral();
            h.set (pid::reverbOn, 1.0f);
            h.set (pid::reverbMix, 0.0f);
            auto r = Routing::defaults();
            r.reverbInsert = true;
            r.move (ModuleId::reverb, 0);
            h.proc.setRouting (r);
            const auto in = h.noise (0.3f, 1.0, 47);
            const auto out = h.run (in);
            const int L = h.proc.getLatencySamples();
            double err = 0.0;
            for (int i = 4800 + L; i < in.getNumSamples(); ++i)
                err = std::max (err, (double) std::abs (out.getSample (0, i) - in.getSample (0, i - L)));
            expectLessThan (err, 1.0e-5, "insert at 0 % mix must be transparent");
        }

        beginTest ("Routing change while playing is click-free");
        {
            Harness h;
            compAndSat (h);
            const auto in = h.sine (200.0, 0.5f, 2.0);
            juce::AudioBuffer<float> out (2, in.getNumSamples());
            juce::MidiBuffer midi;
            const int bs = 256;
            float maxStep = 0.0f, last = 0.0f;
            int switches = 0;

            for (int start = 0; start < in.getNumSamples(); start += bs)
            {
                if (start % 9600 == 0 && start > 0)
                {
                    auto r = h.proc.getRouting();
                    r.move (ModuleId::sat, (switches++ % 2) == 0 ? 0 : 3);
                    h.proc.setRouting (r);
                }
                juce::AudioBuffer<float> block (2, bs);
                for (int ch = 0; ch < 2; ++ch)
                    block.copyFrom (ch, 0, in, ch, start, bs);
                h.proc.processBlock (block, midi);
                for (int i = 0; i < bs; ++i)
                {
                    const float y = block.getSample (0, i);
                    if (start + i > 4800)
                        maxStep = std::max (maxStep, std::abs (y - last));
                    last = y;
                }
            }

            // A 200 Hz sine at this level changes by at most ~0.05 per sample;
            // a hard switch between the two orders would jump far more.
            logMessage ("Largest sample step across " + juce::String (switches) + " routing switches: " + juce::String (maxStep, 4));
            expectGreaterThan (switches, 3);
            expectLessThan (maxStep, 0.12f);
        }

        beginTest ("Delay into reverb adds the echoes to the reverb");
        {
            const auto tailEnergy = [] (float coupling)
            {
                Harness h;
                h.neutral();
                h.set (pid::delayOn, 1.0f);
                h.set (pid::delayMix, 100.0f);
                h.set (pid::delayFeedback, 0.0f);
                h.set (pid::delayTime, 400.0f);
                h.set (pid::reverbOn, 1.0f);
                h.set (pid::reverbMix, 50.0f);
                h.set (pid::reverbDecay, 0.4f);
                h.set (pid::delayToReverb, coupling);
                h.run (h.silence (0.3));
                auto in = h.silence (1.5);
                for (int i = 0; i < 480; ++i)
                    for (int ch = 0; ch < 2; ++ch)
                        in.setSample (ch, i, 0.5f);
                const auto out = h.run (in);
                // Window after the echo (400 ms) where only a reverb of the echo can exist
                return rmsDb (out, (int) (0.9 * h.fs), (int) (0.2 * h.fs));
            };

            const float without = tailEnergy (0.0f);
            const float with = tailEnergy (100.0f);
            logMessage ("Tail after echo: " + juce::String (without, 1) + " dB without, " + juce::String (with, 1) + " dB with coupling");
            expectGreaterThan (with - without, 10.0f);
        }

        beginTest ("Routing is saved with the project and restored");
        {
            auto r = Routing::defaults();
            r.move (ModuleId::deess, 0);
            r.delayInsert = true;

            juce::MemoryBlock block;
            {
                Harness h;
                h.proc.setRouting (r);
                h.proc.getStateInformation (block);
            }
            Harness h2;
            h2.proc.setStateInformation (block.getData(), (int) block.getSize());
            expect (h2.proc.getRouting() == r);
        }

        beginTest ("Migration: a v0.1 project resets newer parameters and routing");
        {
            // Build a state exactly like v0.1 wrote it: no stateVersion, no routing,
            // no parameters added in 0.2.
            juce::MemoryBlock v1;
            {
                Harness h;
                h.set (pid::compRatio, 5.0f);
                auto state = h.proc.getState().copyState();
                state.removeProperty (statekeys::stateVersion, nullptr);
                state.removeProperty (statekeys::routing, nullptr);
                for (int i = state.getNumChildren(); --i >= 0;)
                    if (state.getChild (i).getProperty ("id").toString() == pid::delayToReverb)
                        state.removeChild (i, nullptr);
                const auto xml = state.createXml();
                juce::AudioProcessor::copyXmlToBinary (*xml, v1);
            }

            Harness h;
            h.set (pid::delayToReverb, 70.0f);          // a running instance with changed values
            auto r = Routing::defaults();
            r.move (ModuleId::sat, 0);
            h.proc.setRouting (r);

            h.proc.setStateInformation (v1.getData(), (int) v1.getSize());
            expectWithinAbsoluteError (h.get (pid::compRatio), 5.0f, 0.01f, "saved value restored");
            expectWithinAbsoluteError (h.get (pid::delayToReverb), 0.0f, 0.01f, "missing parameter reset to default");
            expect (h.proc.getRouting() == Routing::defaults(), "missing routing reset to default");

            juce::MemoryBlock again;
            h.proc.getStateInformation (again);
            const auto xml = juce::AudioProcessor::getXmlFromBinary (again.getData(), (int) again.getSize());
            expectEquals (xml->getIntAttribute (statekeys::stateVersion.toString()), statekeys::currentStateVersion);
        }

        beginTest ("Presets: routing stored in format 2, format 1 loads default routing");
        {
            Harness h;
            auto r = Routing::defaults();
            r.move (ModuleId::comp, 0);
            r.reverbInsert = true;
            h.proc.setRouting (r);

            const auto snap = ParameterSnapshot::capture (h.proc.getState(), "x");
            const auto restored = ParameterSnapshot::fromValueTree (snap.toValueTree());
            expectEquals (restored.routing, r.toString());

            h.proc.setRouting (Routing::defaults());
            restored.applyTo (h.proc.getState());
            expect (h.proc.getRouting() == r, "format 2 preset restores routing");

            auto v1 = restored;
            v1.routing = {};
            v1.applyTo (h.proc.getState());
            expect (h.proc.getRouting() == Routing::defaults(), "format 1 preset -> default routing");
        }

        beginTest ("A/B slots include routing");
        {
            Harness h;
            auto r = Routing::defaults();
            r.move (ModuleId::sat, 1);
            h.proc.selectSlot (1);
            h.proc.setRouting (r);
            h.proc.selectSlot (0);
            expect (h.proc.getRouting() == Routing::defaults());
            h.proc.selectSlot (1);
            expect (h.proc.getRouting() == r);
        }
    }
};

static RoutingTests routingTests;
