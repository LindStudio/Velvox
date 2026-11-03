// Host conformance: loading a state brings back every parameter's normalised value, as the host
// reads it (pluginval "Plugin state restoration"). A host writes raw normalised values - a switch
// can hold 0.37 - and JUCE's bool parameter keeps that raw value; the APVTS only writes a parameter
// whose snapped value changes, so a switch already on the loaded side kept the host's raw value.

#include "TestHelpers.h"
#include "ProductInfo.h"
#include "Presets/ParameterSnapshot.h"

using namespace vocalypse;
using namespace vocalypse::test;

class StateRestoreTests final : public juce::UnitTest
{
public:
    StateRestoreTests() : juce::UnitTest ("Vocalypse state restoration", "Vocalypse") {}

    void runTest() override
    {
        beginTest ("Random host values, then the saved state: every parameter reads its saved value");
        {
            Harness h;
            juce::MemoryBlock saved;
            h.proc.getStateInformation (saved);

            const auto& params = h.proc.getParameters();
            std::vector<float> original;
            for (auto* p : params)
                original.push_back (p->getValue());

            for (int seed : { 1, 2, 3, 0x1c14db6 })
            {
                juce::Random r (seed);
                for (auto* p : params)
                    p->setValueNotifyingHost (r.nextFloat());

                h.proc.setStateInformation (saved.getData(), (int) saved.getSize());

                int wrong = 0;
                for (int i = 0; i < params.size(); ++i)
                {
                    if (std::abs (params[i]->getValue() - original[(size_t) i]) > 1.0e-6f)
                    {
                        if (++wrong <= 5)
                            logMessage ("seed " + juce::String (seed) + ": " + params[i]->getName (64) + " "
                                        + juce::String (params[i]->getValue()) + " vs " + juce::String (original[(size_t) i]));
                    }
                }
                expectEquals (wrong, 0, "parameters not restored, seed " + juce::String (seed));
            }
        }

        beginTest ("A switch holding a raw host value reads snapped after a load that keeps it");
        {
            Harness h;
            h.set (pid::reverbOn, 0.0f);
            juce::MemoryBlock saved;
            h.proc.getStateInformation (saved);

            auto* p = h.proc.getState().getParameter (pid::reverbOn);
            p->setValueNotifyingHost (0.37f);   // still "off"
            h.proc.setStateInformation (saved.getData(), (int) saved.getSize());
            expectEquals (p->getValue(), 0.0f);
        }

        // Rename to VOCALYPSE (2026-11-03): projects and presets saved by Velvox / Headliner still load
        beginTest ("A project saved before the rename (state \"VelvoxState\") loads into VOCALYPSE");
        {
            Harness before;
            before.set (pid::reverbMix, 37.0f);
            before.set (pid::compThreshold, -27.0f);
            juce::MemoryBlock saved;
            before.proc.getStateInformation (saved);
            auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
            expect (xml != nullptr && xml->hasTagName ("VocalypseState"), "the new state name is written");
            xml->setTagName (product::legacyStateType);   // as Headliner 0.8 wrote it
            juce::MemoryBlock old;
            juce::AudioProcessor::copyXmlToBinary (*xml, old);

            Harness after;
            after.proc.setStateInformation (old.getData(), (int) old.getSize());
            expectWithinAbsoluteError (after.get (pid::reverbMix), 37.0f, 0.01f);
            expectWithinAbsoluteError (after.get (pid::compThreshold), -27.0f, 0.01f);
            expect (after.proc.getState().state.hasType ("VocalypseState"), "the loaded state keeps the new name");
        }

        beginTest ("A preset saved before the rename (\"VelvoxPreset\") loads");
        {
            Harness h;
            h.set (pid::reverbMix, 23.0f);
            auto tree = ParameterSnapshot::capture (h.proc.getState(), "old").toValueTree();
            expect (tree.hasType ("VocalypsePreset"));
            juce::ValueTree legacy (ParameterSnapshot::legacyTreeType);
            legacy.copyPropertiesAndChildrenFrom (tree, nullptr);
            const auto snap = ParameterSnapshot::fromValueTree (legacy);
            expectEquals (snap.name, juce::String ("old"));
            h.set (pid::reverbMix, 5.0f);
            snap.applyTo (h.proc.getState());
            expectWithinAbsoluteError (h.get (pid::reverbMix), 23.0f, 0.01f);
        }
    }
};

static StateRestoreTests stateRestoreTests;
