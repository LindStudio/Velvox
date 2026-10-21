// Routing (module instances, order, send/insert), repeated instances, state migration
// and preset format 2.

#include "TestHelpers.h"
#include "StateKeys.h"
#include "Parameters/RoutingMigration.h"

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

    void setId (Harness& h, const juce::String& id, float value)
    {
        auto* p = h.proc.getState().getParameter (id);
        jassert (p != nullptr);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    float getId (Harness& h, const juce::String& id)
    {
        return h.proc.getState().getRawParameterValue (id)->load();
    }

    const char* const legacyV03Default = "eq,ride,comp,smash,deess,smooth,sat,doubler,choir,pitch,delay:send,reverb:send,chop,pulse,check";
}

class RoutingTests final : public juce::UnitTest
{
public:
    RoutingTests() : juce::UnitTest ("Headliner routing, instances & migration", "Headliner") {}

    void runTest() override
    {
        beginTest ("Routing model: string, edits, tolerant parsing");
        {
            const auto def = Routing::defaults();
            expect (def.isValid());
            expectEquals (def.toString(), juce::String ("v2,eq,comp,deess,delay:send,reverb:send"));
            expect (Routing::fromString (def.toString()) == def);

            // 0.2 / 0.3 strings: every module, 0.3 modules at their default places
            const auto full = Routing::legacyFull();
            expectEquals (full.size(), dsp::numModules);
            expect (Routing::fromString (legacyV03Default) == full);
            expect (Routing::fromString ("eq,comp,deess,sat,doubler,pitch,delay:send,reverb:send,chop") == full, "v0.2 default -> 0.3 default");
            const auto custom = Routing::fromString ("sat,eq,comp,deess,doubler,pitch,delay:insert,reverb:send,chop");
            expect (custom.isValid() && custom.isInsert (ModuleId::delay) && custom.isSend (ModuleId::reverb));
            expect (custom.positionOf (ModuleId::sat) == 0 && custom.positionOf (ModuleId::ride) == custom.positionOf (ModuleId::eq) + 1,
                    "custom order kept, ride placed after EQ");
            expect (custom.positionOf (ModuleId::check) == dsp::numModules - 1, "check stays last");

            // v2 strings are exact, with instances
            const auto multi = Routing::fromString ("v2,eq,comp,eq#2,deess,reverb#2:insert,delay:send");
            expectEquals (multi.size(), 6);
            expect (multi.contains (ModuleId::eq, 1) && multi.indexOf (ModuleId::eq, 1) == 2);
            expect (multi.isInsert (ModuleId::reverb, 1) && multi.isSend (ModuleId::delay, 0));
            expect (! multi.contains (ModuleId::reverb, 0));
            expect (Routing::fromString (multi.toString()) == multi);

            // Edits
            auto r = def;
            expect (r.insertAt (1, ModuleId::eq, 1));
            expect (! r.insertAt (0, ModuleId::eq, 1), "an instance appears only once");
            expect (! r.insertAt (0, ModuleId::eq, 9), "instance outside the bank");
            expectEquals (r.nextFreeInstance (ModuleId::eq), 2);
            expectEquals (r.countOf (ModuleId::eq), 2);
            expect (r.moveTo (ModuleId::eq, 1, 3));
            expectEquals (r.indexOf (ModuleId::eq, 1), 3);
            expect (r.placeBefore (ModuleId::deess, 0, ModuleId::eq, 0));
            expectEquals (r.indexOf (ModuleId::deess), 0);
            expect (r.remove (ModuleId::eq, 1) && ! r.contains (ModuleId::eq, 1));
            expect (r.setInsert (ModuleId::reverb, 0, true) && r.isInsert (ModuleId::reverb));
            expect (! r.setInsert (ModuleId::eq, 0, false), "only delay / reverb have a send mode");
            expect (r.isValid());

            // Every bank full: the routing holds every instance of every module
            Routing all;
            for (int m = 0; m < dsp::numModules; ++m)
                for (int k = 0; k < dsp::maxInstances ((ModuleId) m); ++k)
                    expect (all.insertAt (all.size(), (ModuleId) m, k));
            expectEquals (all.size(), Routing::capacity);
            expect (all.isValid());
            expectEquals (all.nextFreeInstance (ModuleId::eq), -1);

            // insertDefault: after the module's other instances, else at its default place
            auto d = Routing::defaults();
            d.insertDefault (ModuleId::sat, 0);
            expectEquals (d.indexOf (ModuleId::sat), d.indexOf (ModuleId::deess) + 1);
            d.insertDefault (ModuleId::ride, 0);
            expectEquals (d.indexOf (ModuleId::ride), d.indexOf (ModuleId::eq) + 1);
            d.insertDefault (ModuleId::eq, 1);
            expectEquals (d.indexOf (ModuleId::eq, 1), d.indexOf (ModuleId::eq, 0) + 1);
            d.insertDefault (ModuleId::reverb, 1);
            expect (d.isSend (ModuleId::reverb, 1), "effects join as sends");

            // Unknown keys, duplicates, bad instances
            const auto parsed = Routing::fromString ("v2,reverb:insert, banana, sat, sat, eq, eq#7, comp#0");
            expect (parsed.isValid());
            expectEquals (parsed.size(), 3);
            expect (parsed[0].id == ModuleId::reverb && parsed[0].insert && parsed[1].id == ModuleId::sat && parsed[2].id == ModuleId::eq);
            expect (Routing::fromString ("").isValid());
        }

        beginTest ("Parameter banks: every instance has its own parameters");
        {
            for (int m = 0; m < dsp::numModules; ++m)
                for (int k = 1; k < dsp::maxInstances ((ModuleId) m); ++k)
                    for (const auto& spec : getParameterSpecs())
                        if (moduleOfParameter (spec) == (ModuleId) m && dsp::splitInstanceParamId (spec.id).second == 0)
                            expect (findParameterSpec (dsp::instanceParamId (spec.id, k)) != nullptr,
                                    juce::String ("bank parameter ") + spec.id + " #" + juce::String (k + 1));
            expect (findParameterSpec ("lowGain_2") != nullptr && findParameterSpec ("lowGain_4") != nullptr && findParameterSpec ("lowGain_5") == nullptr);
            expect (findParameterSpec ("reverbDecay_2") != nullptr && findParameterSpec ("reverbDecay_3") == nullptr);
            expect (findParameterSpec ("pitchOn_2") == nullptr, "pitch has a single instance");
            expectEquals (juce::String (findParameterSpec ("compThreshold_2")->name), juce::String ("Compressor 2: Threshold"));

            Harness h;
            setId (h, "lowGain_2", 9.0f);
            expectWithinAbsoluteError (getId (h, "lowGain_2"), 9.0f, 0.01f);
            expectWithinAbsoluteError (h.get (pid::lowGain), 0.0f, 0.01f, "instance 1 untouched");
            const auto s = h.proc.readSettings();
            expectWithinAbsoluteError (s.extra[0].eq.lowGain, 9.0f, 0.01f, "bank 2 read");
            expectWithinAbsoluteError (s.eq.lowGain, 0.0f, 0.01f);
            expect (! s.extra[0].eq.hpfOn, "a new EQ instance starts without low cut");
        }

        beginTest ("Two EQ instances process independently (+9 dB and -9 dB cancel)");
        {
            Harness one, two;
            for (auto* h : { &one, &two })
            {
                h->neutral();
                h->set (pid::eqOn, 1.0f);
                h->set (pid::highGain, 9.0f);
                h->set (pid::highFreq, 4000.0f);
            }
            // second instance with the opposite shelf
            setId (two, "eqOn_2", 1.0f);
            setId (two, "highGain_2", -9.0f);
            setId (two, "highFreq_2", 4000.0f);
            auto r = two.proc.getRouting();
            r.insertAt (r.indexOf (ModuleId::eq) + 1, ModuleId::eq, 1);
            two.proc.setRouting (r);

            const auto in = one.noise (0.25f, 1.0, 51);
            const auto outOne = one.run (in);
            const auto outTwo = two.run (in);
            const double brightened = rmsDb (outOne, 4800, 38400) - rmsDb (in, 4800, 38400);
            double residual = 0.0;
            for (int i = 4800; i < in.getNumSamples(); ++i)
                residual = std::max (residual, (double) std::abs (outTwo.getSample (0, i) - in.getSample (0, i)));
            logMessage ("one EQ: +" + juce::String (brightened, 2) + " dB; two opposite EQs: max deviation " + juce::String (residual, 4));
            expectGreaterThan (brightened, 1.0);
            expectLessThan (residual, 0.06, "two independent instances: opposite shelves almost cancel");
        }

        beginTest ("Repeated instances in one chain: EQ -> Comp -> EQ -> De-esser");
        {
            Harness h;
            h.neutral();
            h.set (pid::eqOn, 1.0f);
            h.set (pid::lowGain, -4.0f);
            h.set (pid::compOn, 1.0f);
            h.set (pid::compThreshold, -30.0f);
            h.set (pid::compRatio, 4.0f);
            setId (h, "eqOn_2", 1.0f);
            setId (h, "highGain_2", 4.0f);
            h.set (pid::deessOn, 1.0f);
            auto r = Routing::fromString ("v2,eq,comp,eq#2,deess,delay:send,reverb:send");
            h.proc.setRouting (r);
            expect (h.proc.getRouting() == r);
            const auto in = h.noise (0.3f, 1.0, 53);
            const auto out = h.run (in);
            expect (allFinite (out));
            expect (h.proc.getChain().getActiveRouting() == r, "the audio thread runs the new chain");
        }

        beginTest ("Module order changes the result (comp->sat vs sat->comp)");
        {
            Harness a, b;
            compAndSat (a);
            compAndSat (b);
            expect (a.proc.getRouting().contains (ModuleId::sat), "switching saturation on adds it to the chain");
            auto r = b.proc.getRouting();
            r.moveTo (ModuleId::sat, 0, 0);
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
            auto r = b.proc.getRouting();
            r.setInsert (ModuleId::delay, 0, true);
            r.placeAtEnd (ModuleId::delay, 0);
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
            auto r = h.proc.getRouting();
            r.setInsert (ModuleId::reverb, 0, true);
            r.moveTo (ModuleId::reverb, 0, 0);
            h.proc.setRouting (r);
            const auto in = h.noise (0.3f, 1.0, 47);
            const auto out = h.run (in);
            const int L = h.proc.getLatencySamples();
            double err = 0.0;
            for (int i = 4800 + L; i < in.getNumSamples(); ++i)
                err = std::max (err, (double) std::abs (out.getSample (0, i) - in.getSample (0, i - L)));
            expectLessThan (err, 1.0e-5, "insert at 0 % mix must be transparent");
        }

        beginTest ("Routing changes while playing are click-free (reorder, add and remove instances)");
        {
            Harness h;
            compAndSat (h);
            setId (h, "eqOn_2", 1.0f);
            setId (h, "highGain_2", 3.0f);
            const auto in = h.sine (200.0, 0.5f, 3.0);
            juce::MidiBuffer midi;
            const int bs = 256;
            float maxStep = 0.0f, last = 0.0f;
            int switches = 0;

            for (int start = 0; start + bs <= in.getNumSamples(); start += bs)
            {
                if (start % 9600 == 0 && start > 0)
                {
                    auto r = h.proc.getRouting();
                    switch (switches++ % 4)
                    {
                        case 0: r.moveTo (ModuleId::sat, 0, 0); break;
                        case 1: r.insertAt (2, ModuleId::eq, 1); break;
                        case 2: r.moveTo (ModuleId::sat, 0, 3); break;
                        default: r.remove (ModuleId::eq, 1); break;
                    }
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
            // a hard switch between two chains would jump far more.
            logMessage ("Largest sample step across " + juce::String (switches) + " routing switches: " + juce::String (maxStep, 4));
            expectGreaterThan (switches, 6);
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

        beginTest ("Routing and repeated instances are saved with the project and restored");
        {
            const auto r = Routing::fromString ("v2,deess,eq,comp#2,eq#2,comp,delay:insert,reverb:send,reverb#2:send");

            juce::MemoryBlock block;
            {
                Harness h;
                h.proc.setRouting (r);
                setId (h, "compRatio_2", 7.5f);
                setId (h, "highGain_2", -3.0f);
                setId (h, "reverbDecay_2", 11.0f);
                h.proc.getStateInformation (block);
            }
            Harness h2;
            h2.proc.setStateInformation (block.getData(), (int) block.getSize());
            expect (h2.proc.getRouting() == r, h2.proc.getRouting().toString());
            expectWithinAbsoluteError (getId (h2, "compRatio_2"), 7.5f, 0.01f);
            expectWithinAbsoluteError (getId (h2, "highGain_2"), -3.0f, 0.01f);
            expectWithinAbsoluteError (getId (h2, "reverbDecay_2"), 11.0f, 0.01f);
            expectWithinAbsoluteError (h2.get (pid::compRatio), 3.0f, 0.01f, "instance 1 keeps its default");
        }

        beginTest ("Switching on an instance that is not in the chain adds it (automation never goes silent)");
        {
            Harness h;
            expect (! h.proc.getRouting().contains (ModuleId::doubler));
            h.set (pid::doublerOn, 1.0f);
            expect (h.proc.getRouting().contains (ModuleId::doubler), "doubler joined the chain");
            setId (h, "choirOn_2", 1.0f);
            expect (h.proc.getRouting().contains (ModuleId::choir, 1), "choir 2 joined the chain");
            h.set (pid::macroWidth, 60.0f);   // engages the doubler partially - already there
            expect (h.proc.getRouting().isValid());
        }

        beginTest ("Latency follows the saturation instances in the chain");
        {
            Harness h;
            h.neutral();
            const int none = h.proc.getLatencySamples();
            h.set (pid::satOn, 1.0f);
            const int one = h.proc.getLatencySamples();
            setId (h, "satOn_2", 1.0f);
            const int twoSat = h.proc.getLatencySamples();
            logMessage ("latency: " + juce::String (none) + " / " + juce::String (one) + " / " + juce::String (twoSat) + " samples");
            expectEquals (none, 0);
            expectGreaterThan (one, 0);
            expectEquals (twoSat, 2 * one);
            expectEquals (h.proc.getChain().getLatencyFor (h.proc.getRouting(), false), twoSat);
        }

        beginTest ("Migration: a v0.1 project resets newer parameters, routing keeps what is audible");
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
            auto r = h.proc.getRouting();
            r.insertAt (0, ModuleId::sat, 0);
            h.proc.setRouting (r);

            h.proc.setStateInformation (v1.getData(), (int) v1.getSize());
            expectWithinAbsoluteError (h.get (pid::compRatio), 5.0f, 0.01f, "saved value restored");
            expectWithinAbsoluteError (h.get (pid::delayToReverb), 0.0f, 0.01f, "missing parameter reset to default");
            expect (h.proc.getRouting() == Routing::defaults(), "v0.1: EQ, comp, de-esser, delay and reverb sends: " + h.proc.getRouting().toString());

            juce::MemoryBlock again;
            h.proc.getStateInformation (again);
            const auto xml = juce::AudioProcessor::getXmlFromBinary (again.getData(), (int) again.getSize());
            expectEquals (xml->getIntAttribute (statekeys::stateVersion.toString()), statekeys::currentStateVersion);
        }

        beginTest ("Migration: a 0.3 project keeps its order and every audible module; the sound is unchanged");
        {
            // 0.3 state: full routing string, saturation before EQ, chop on
            juce::MemoryBlock v03;
            {
                Harness h;
                h.set (pid::satOn, 1.0f);
                h.set (pid::satDrive, 12.0f);
                h.set (pid::chopOn, 1.0f);
                auto state = h.proc.getState().copyState();
                state.setProperty (statekeys::stateVersion, 3, nullptr);
                state.setProperty (statekeys::routing, "sat,eq,ride,comp,smash,deess,smooth,doubler,choir,pitch,delay:send,reverb:insert,chop,pulse,check", nullptr);
                const auto xml = state.createXml();
                juce::AudioProcessor::copyXmlToBinary (*xml, v03);
            }
            Harness h;
            h.proc.setStateInformation (v03.getData(), (int) v03.getSize());
            const auto r = h.proc.getRouting();
            expectEquals (r.toString(), juce::String ("v2,sat,eq,comp,deess,delay:send,reverb:insert,chop"));

            // The same chain with every module (0.3 behaviour) sounds identical
            Harness full;
            full.proc.setStateInformation (v03.getData(), (int) v03.getSize());
            full.proc.setRouting (Routing::fromString ("sat,eq,ride,comp,smash,deess,smooth,doubler,choir,pitch,delay:send,reverb:insert,chop,pulse,check"));
            const auto in = h.noise (0.2f, 1.0, 59);
            const double d = maxDiff (h.run (in), full.run (in), 0);
            logMessage ("pruned vs full 0.3 chain: max difference " + juce::String (d, 9));
            expectLessThan (d, 1.0e-6);
        }

        beginTest ("Presets: routing stored in format 2, format 1 loads the audible default routing");
        {
            Harness h;
            auto r = Routing::defaults();
            r.moveTo (ModuleId::comp, 0, 0);
            r.setInsert (ModuleId::reverb, 0, true);
            r.insertAt (2, ModuleId::eq, 1);
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
            expect (h.proc.getRouting() == Routing::defaults(), "format 1 preset -> default routing: " + h.proc.getRouting().toString());
        }

        beginTest ("A/B slots include routing and repeated instances");
        {
            Harness h;
            auto r = Routing::defaults();
            r.insertAt (1, ModuleId::eq, 1);
            h.proc.selectSlot (1);
            h.proc.setRouting (r);
            setId (h, "midGain_2", 5.0f);
            h.proc.selectSlot (0);
            expect (h.proc.getRouting() == Routing::defaults());
            expectWithinAbsoluteError (getId (h, "midGain_2"), 0.0f, 0.01f);
            h.proc.selectSlot (1);
            expect (h.proc.getRouting() == r);
            expectWithinAbsoluteError (getId (h, "midGain_2"), 5.0f, 0.01f);
        }
    }
};

static RoutingTests routingTests;
