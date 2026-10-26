// Host conformance: loading a state brings back every parameter's normalised value, as the host
// reads it (pluginval "Plugin state restoration"). A host writes raw normalised values - a switch
// can hold 0.37 - and JUCE's bool parameter keeps that raw value; the APVTS only writes a parameter
// whose snapped value changes, so a switch already on the loaded side kept the host's raw value.

#include "TestHelpers.h"

using namespace velvox;
using namespace velvox::test;

class StateRestoreTests final : public juce::UnitTest
{
public:
    StateRestoreTests() : juce::UnitTest ("Velvox state restoration", "Velvox") {}

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
    }
};

static StateRestoreTests stateRestoreTests;
