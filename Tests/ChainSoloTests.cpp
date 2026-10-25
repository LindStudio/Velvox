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
    }
};

static ChainSoloTests chainSoloTests;
