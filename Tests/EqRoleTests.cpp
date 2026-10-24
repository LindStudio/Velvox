// EQ 0.6: macros and the automatic mix write role bands (spec 8, Q5)

#include "EqTestHelpers.h"
#include "Parameters/EqBands.h"
#include "DSP/Macros.h"

using namespace velvox;
using namespace velvox::test;
using namespace velvox::test::eq;

class EqMacroTests final : public juce::UnitTest
{
public:
    EqMacroTests() : juce::UnitTest ("Velvox EQ macros", "Velvox") {}

    void runTest() override
    {
        using dsp::EqRole;

        const auto withMacros = [] (const dsp::ChainSettings& base, float presence, float warmth)
        {
            dsp::MacroValues m;
            m.presence = presence;
            m.warmth = warmth;
            return dsp::applyMacros (base, m);
        };

        beginTest ("Migrated 0.5 state: Presence / Warmth move bands 4 / 2 exactly like the 0.5 fixed bands");
        for (const auto& o : legacyCases())
        {
            dsp::ChainSettings base;
            base.eq = migrated (o);
            const auto eff = withMacros (base, 0.6f, 0.4f);

            // the 0.5 macro arithmetic on the fixed bands
            auto expected = o;
            expected.highGain += 5.0f * 0.6f;
            expected.lowGain += 3.0f * 0.4f;
            expected.highGain -= 1.5f * 0.4f;
            expected.highGain = juce::jlimit (-18.0f, 18.0f, expected.highGain);
            expected.lowGain = juce::jlimit (-18.0f, 18.0f, expected.lowGain);

            expectEquals (eff.eq.bands[3].gain, expected.highGain, "air band");
            expectEquals (eff.eq.bands[1].gain, expected.lowGain, "warmth band");
            expect (eff.eqOn);
            for (int i = 4; i < 16; ++i)
                expect (eff.eq.bands[(size_t) i] == base.eq.bands[(size_t) i], "other bands unchanged");

            legacy::v05::EqualizerModule ref;
            ref.setSettings (expected);
            ref.prepare (48000.0, 256);
            dsp::EqualizerModule eq;
            eq.setSettings (eff.eq);
            eq.prepare (48000.0, 256);
            const auto in = noiseBuffer (2, 24000, 0.2f, 31);
            auto a = in, b = in;
            runModule (ref, a);
            runModule (eq, b);
            expect (maxAbsDiff (a, b) == 0.0, "macro sound bit-identical to 0.5: " + juce::String (diffDb (a, b), 1) + " dB");
        }

        beginTest ("Fresh state: the macros create their bands (0.5 defaults: 10 kHz / 150 Hz shelves)");
        {
            const dsp::ChainSettings base;
            const auto eff = withMacros (base, 1.0f, 0.5f);
            const int air = eff.eq.findRole (EqRole::air), warmth = eff.eq.findRole (EqRole::warmth);
            expectEquals (air, 0);
            expectEquals (warmth, 1);
            const auto& a = eff.eq.bands[(size_t) std::max (0, air)];
            expect (a.on && a.bandType() == dsp::EqBandType::highShelf);
            expectWithinAbsoluteError (a.freq, 10000.0f, 1.0e-3f);
            expectWithinAbsoluteError (a.q, 0.7071f, 1.0e-4f);
            expectWithinAbsoluteError (a.gain, 5.0f - 0.75f, 1.0e-5f);
            const auto& w = eff.eq.bands[(size_t) std::max (0, warmth)];
            expect (w.on && w.bandType() == dsp::EqBandType::lowShelf);
            expectWithinAbsoluteError (w.freq, 150.0f, 1.0e-3f);
            expectWithinAbsoluteError (w.gain, 1.5f, 1.0e-5f);
            expect (withMacros (base, 0.0f, 0.0f).eq.findRole (EqRole::air) < 0, "no macro, no band");
        }

        beginTest ("User bands are never moved by macros; no free slot -> the EQ is left alone");
        {
            dsp::ChainSettings base;
            base.eq.bands[0] = makeBand (EqBandType::highShelf, 10000.0f, 2.0f, 0.7071f);   // the user's own air shelf
            auto eff = withMacros (base, 1.0f, 1.0f);
            expect (eff.eq.bands[0] == base.eq.bands[0], "user band untouched");
            expect (eff.eq.findRole (EqRole::air) == 1 && eff.eq.findRole (EqRole::warmth) == 2);

            dsp::ChainSettings full;
            for (auto& b : full.eq.bands)
                b = makeBand (EqBandType::bell, 1000.0f, 1.0f, 1.0f);
            eff = withMacros (full, 1.0f, 1.0f);
            for (int i = 0; i < 16; ++i)
                expect (eff.eq.bands[(size_t) i] == full.eq.bands[(size_t) i]);
        }

        beginTest ("A switched-off role band is switched on with the macro share only; gains stay in range");
        {
            dsp::ChainSettings base;
            auto air = makeBand (EqBandType::highShelf, 12000.0f, 6.0f, 0.7071f);
            air.role = (int) EqRole::air;
            air.on = false;
            base.eq.bands[5] = air;
            auto eff = withMacros (base, 0.5f, 0.0f);
            expect (eff.eq.bands[5].on);
            expectWithinAbsoluteError (eff.eq.bands[5].gain, 2.5f, 1.0e-5f);
            expectWithinAbsoluteError (eff.eq.bands[5].freq, 12000.0f, 1.0e-3f);

            base.eq.bands[5].on = true;
            base.eq.bands[5].gain = 17.0f;
            expectEquals (withMacros (base, 1.0f, 0.0f).eq.bands[5].gain, 18.0f, "clamped like 0.5");
            base.eq.bands[5].gain = 25.0f;   // beyond the 0.5 range (set by the user): not pulled down
            expectEquals (withMacros (base, 1.0f, 0.0f).eq.bands[5].gain, 25.0f);
        }

        beginTest ("Presence through the plugin lifts the highs (fresh instance)");
        {
            Harness h;
            h.neutral();
            h.set (pid::eqOn, 1.0f);
            h.proc.setRouting (dsp::Routing::fromString ("v2,eq"));   // the de-esser it engages is not in the chain
            h.set (pid::macroPresence, 100.0f);
            const auto out = h.run (h.sine (16000.0, 0.05f, 0.5));
            const double lift = juce::Decibels::gainToDecibels (toneLevel (out, 12000, 12000, 16000.0, h.fs) / 0.05);
            expect (lift > 4.0 && lift < 5.5, "presence 100 % at 16 kHz: " + juce::String (lift, 2) + " dB");
        }
    }
};

static EqMacroTests eqMacroTests;
