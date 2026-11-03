// EQ 0.6: macros and the automatic mix write role bands (spec 8, Q5)

#include "EqTestHelpers.h"
#include "Parameters/EqBands.h"
#include "DSP/Macros.h"

using namespace vocalypse;
using namespace vocalypse::test;
using namespace vocalypse::test::eq;

class EqMacroTests final : public juce::UnitTest
{
public:
    EqMacroTests() : juce::UnitTest ("Vocalypse EQ macros", "Vocalypse") {}

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

//==============================================================================
#include "SyntheticVocal.h"
#include "AutoMix/AutoMixEngine.h"
#include "Text/I18n.h"

namespace
{
    std::map<juce::String, float> defaultValues()
    {
        std::map<juce::String, float> v;
        for (auto& s : getParameterSpecs())
            v[s.id] = s.defaultValue;
        return v;
    }

    // The band of `role` in a plan (over `current`), if the plan has one
    std::optional<dsp::EqBand> planBand (const automix::MixPlan& plan, const std::map<juce::String, float>& current, dsp::EqRole role, int instance = 0)
    {
        auto values = plan.values;
        const auto access = eqbands::accessFor (values, &current);
        const int i = eqbands::findBand (access, instance, role);
        if (i < 0)
            return std::nullopt;
        return eqbands::readBand (access, instance, i);
    }

    int countRoleBands (const automix::MixPlan& plan, const std::map<juce::String, float>& current)
    {
        auto values = plan.values;
        const auto access = eqbands::accessFor (values, &current);
        int n = 0;
        for (int i = 0; i < dsp::maxEqBands; ++i)
            if (eqbands::readBand (access, 0, i).role != 0)
                ++n;
        return n;
    }
}

class EqAutoMixTests final : public juce::UnitTest
{
public:
    EqAutoMixTests() : juce::UnitTest ("Vocalypse EQ auto-mix", "Vocalypse") {}

    void runTest() override
    {
        using dsp::EqRole;
        using automix::AutoMixEngine;

        // The decisions of the auto-mix for these inputs, recorded on purpose: a change of any of them has to be
        // deliberate (re-record with the logged lines). History: 0.5 fixed bands -> 0.6 role bands -> 0.6.1 auto EQ
        // on the 0.7 windows -> 2026-10-29 windows of finished vocals ->
        // 0.8 (spec 2026-11-02, "auf die Stimme angepasst"): the curve fit against the party target puts bells and
        // shelves where this voice differs (any broad role), so a golden lists the audible auto-mix bands by role.
        struct Band { EqRole role; int type; float freq, gain, q; };
        struct Golden
        {
            const char* name;
            VocalSpec spec;
            const char* chosen;
            const char* routing;
            float hpf; int hpfSlope;          // 0 = 12 dB/oct, 1 = 24 dB/oct
            float outputGain;
            std::vector<Band> bands;          // every audible auto-mix band besides the low cut
        };
        std::vector<Golden> goldens;
        { VocalSpec s; s.seconds = 12.0;
          goldens.push_back ({ "clean dry", s, "Effekte trockener + Mehr Raum (Large Hall)", "v2,eq,ride,comp,smash,deess,smooth,sat,doubler,delay:send,reverb:send", 100.0000f, 0, 3.7531f, { { EqRole::warmth, 0, 284.8685f, -3.0000f, 2.1000f }, { EqRole::autoMix, 0, 6451.6523f, 4.5000f, 0.7000f }, { EqRole::air, 0, 10404.1240f, 3.0000f, 2.0000f }, { EqRole::autoTone, 0, 1680.6729f, 4.0000f, 2.0000f }, } }); }
        { VocalSpec s; s.seconds = 12.0; s.tiltDb = -5.0f; s.f0 = 120.0f;
          goldens.push_back ({ "dark low", s, "Basisplan", "v2,eq,ride,comp,smash,deess,smooth,sat,doubler,delay:send,reverb:send", 70.0000f, 1, 3.5935f, { { EqRole::warmth, 1, 320.0000f, -3.0000f, 0.7071f }, { EqRole::autoMix, 0, 3715.8599f, 4.0000f, 0.7000f }, { EqRole::autoTone, 0, 507.9684f, -6.0000f, 4.0000f }, } }); }
        { VocalSpec s; s.seconds = 12.0; s.tiltDb = 4.0f; s.sibilance = 1.6f; s.f0 = 300.0f;
          goldens.push_back ({ "bright sibilant", s, "De-Esser st\xc3\xa4rker + Effekte trockener", "v2,eq,ride,comp,deess,smooth,doubler,delay:send,reverb:send", 136.4929f, 0, 4.5994f, { { EqRole::warmth, 0, 320.0000f, -3.0000f, 4.0000f }, { EqRole::autoMix, 0, 3621.8433f, -6.0000f, 0.7000f }, { EqRole::air, 0, 12901.5977f, -2.5000f, 4.0000f }, { EqRole::autoTone, 0, 2559.9988f, -4.0000f, 4.0000f }, } }); }
        { VocalSpec s; s.seconds = 12.0; s.channels = 2; s.stereoDifference = 0.5f; s.rumbleDb = -26.0f;
          goldens.push_back ({ "stereo with rumble", s, "De-Esser st\xc3\xa4rker", "v2,eq,ride,comp,smash,deess,smooth,sat,delay:send,reverb:send", 126.5934f, 1, 4.3043f, { { EqRole::warmth, 0, 295.9588f, -3.0000f, 2.1000f }, { EqRole::autoMix, 0, 6459.2046f, 4.0000f, 0.7000f }, { EqRole::autoTone, 0, 1589.8947f, 4.0000f, 1.0500f }, } }); }
        { VocalSpec s; s.seconds = 12.0; s.tiltDb = -3.0f; s.f0 = 150.0f; s.sampleRate = 44100.0;
          goldens.push_back ({ "dark 44.1 kHz", s, "Mehr Raum (Large Hall)", "v2,eq,ride,comp,smash,deess,smooth,sat,doubler,delay:send,reverb:send", 70.0000f, 0, 3.9755f, { { EqRole::warmth, 1, 320.0000f, -3.0000f, 0.7071f }, { EqRole::autoMix, 0, 3738.3115f, 4.0000f, 0.7000f }, { EqRole::autoTone, 0, 507.9684f, -3.5000f, 4.0000f }, } }); }

        const auto current = defaultValues();
        for (const auto& g : goldens)
        {
            beginTest (juce::String ("Auto-mix result unchanged, now in role bands: ") + g.name);
            const auto r = AutoMixEngine::run (makeVocal (g.spec), current);
            expect (r.ok, r.error);
            if (! r.ok)
                continue;
            // Candidate names are stored in both languages; the goldens are the German ones
            expectEquals (i18n::show (r.candidates[(size_t) r.chosenCandidate].name, i18n::Language::german), juce::String::fromUTF8 (g.chosen), "same candidate");
            expectEquals (r.plan.routing.toString(), juce::String (g.routing), "same chain");
            expectWithinAbsoluteError (r.plan.get (pid::outputGain), g.outputGain, 0.25f, "output level");

            // The recording line (paste it above when a change is deliberate)
            juce::String line;
            const auto lowCutNow = planBand (r.plan, current, EqRole::lowCut);
            line << "goldens.push_back ({ \"" << g.name << "\", s, \"" << i18n::show (r.candidates[(size_t) r.chosenCandidate].name, i18n::Language::german)
                 << "\", \"" << r.plan.routing.toString() << "\", " << juce::String (lowCutNow ? lowCutNow->freq : 0.0f, 4) << "f, "
                 << (lowCutNow && lowCutNow->slope == 3 ? 1 : 0) << ", " << juce::String (r.plan.get (pid::outputGain), 4) << "f, {";
            std::vector<std::pair<EqRole, dsp::EqBand>> audible;
            for (auto role : { EqRole::warmth, EqRole::autoMix, EqRole::air, EqRole::autoTone, EqRole::autoNotch1, EqRole::autoNotch2, EqRole::autoNotch3 })
                if (const auto b = planBand (r.plan, current, role); b.has_value() && b->on && std::abs (b->gain) > 1.0e-4f)
                {
                    audible.push_back ({ role, *b });
                    line << " { EqRole::" << (role == EqRole::warmth ? "warmth" : role == EqRole::autoMix ? "autoMix" : role == EqRole::air ? "air"
                                              : role == EqRole::autoTone ? "autoTone" : role == EqRole::autoNotch1 ? "autoNotch1"
                                              : role == EqRole::autoNotch2 ? "autoNotch2" : "autoNotch3")
                         << ", " << b->type << ", " << juce::String (b->freq, 4) << "f, " << juce::String (b->gain, 4) << "f, " << juce::String (b->q, 4) << "f },";
                }
            line << " } });";
            logMessage ("    RECORD " + line);

            const auto lowCut = planBand (r.plan, current, EqRole::lowCut);
            expect (lowCut.has_value() && lowCut->on && lowCut->bandType() == dsp::EqBandType::lowCut, "low cut band");
            if (lowCut)
            {
                expectWithinAbsoluteError (lowCut->freq, g.hpf, 0.01f);
                expectEquals (lowCut->slope, g.hpfSlope == 1 ? 3 : 1, "12 / 24 dB/oct");
            }

            expectEquals ((int) audible.size(), (int) g.bands.size(), "same number of auto-mix bands");
            for (auto& want : g.bands)
            {
                const auto b = planBand (r.plan, current, want.role);
                expect (b.has_value() && b->on && b->type == want.type, "band of role " + juce::String ((int) want.role));
                if (b)
                {
                    expectWithinAbsoluteError (b->freq, want.freq, 0.01f + 1.0e-4f * want.freq, "freq");
                    expectWithinAbsoluteError (b->gain, want.gain, 1.0e-3f, "gain");
                    expectWithinAbsoluteError (b->q, want.q, 1.0e-3f, "q");
                }
            }

            // only role bands: nothing is written to the hidden 0.5 fixed bands
            for (auto& [id, v] : r.plan.values)
                for (auto* fixed : { "hpf", "lowFreq", "lowGain", "lowQ", "lowType", "midFreq", "midGain", "midQ", "highFreq", "highGain", "highQ", "highType", "lpf" })
                    expect (! id.startsWith (fixed), "fixed band written: " + id);
        }

        beginTest ("Auto-mix keeps user bands, uses free slots, reuses its role bands");
        {
            auto withUser = current;
            for (int n = 1; n <= 3; ++n)   // the bands of the user in slots 1-3
            {
                withUser[pid::eqBandId (n, pid::eqband::on)] = 1.0f;
                withUser[pid::eqBandId (n, pid::eqband::freq)] = 500.0f * (float) n;
                withUser[pid::eqBandId (n, pid::eqband::gain)] = 2.0f;
            }
            VocalSpec s; s.seconds = 12.0; s.tiltDb = -5.0f; s.f0 = 120.0f;
            const auto profile = automix::VocalProfiler::analyse (makeVocal (s));
            const auto plan = automix::AutoMixPlanner::plan (profile, withUser);
            for (int n = 1; n <= 3; ++n)
                for (auto* field : { pid::eqband::on, pid::eqband::type, pid::eqband::freq, pid::eqband::gain, pid::eqband::q, pid::eqband::slope, pid::eqband::role })
                    expect (plan.values.count (pid::eqBandId (n, field)) == 0, "user band " + juce::String (n) + " untouched");
            auto values = plan.values;
            expectEquals (eqbands::findBand (eqbands::accessFor (values, &withUser), 0, EqRole::lowCut), 3, "first free slot after the user bands");

            auto mixed = withUser;
            for (auto& [id, v] : plan.values)
                mixed[id] = v;
            const auto again = automix::AutoMixPlanner::plan (profile, mixed);
            expectEquals (countRoleBands (again, mixed), countRoleBands (plan, withUser), "a second run adds no role bands");
        }

        beginTest ("The EQ 2 variant moves the air band behind the dynamics");
        {
            VocalSpec s; s.seconds = 12.0;
            const auto profile = automix::VocalProfiler::analyse (makeVocal (s));
            auto base = automix::AutoMixPlanner::plan (profile, current);
            base.set (pid::compOn, 1.0f);
            if (! base.routing.contains (dsp::ModuleId::comp))
                base.routing.insertAt (base.routing.size(), dsp::ModuleId::comp, 0);
            {
                auto values = base.values;
                eqbands::findOrCreateBand (eqbands::accessFor (values, &current), 0, EqRole::air, dsp::EqBandType::highShelf, 9000.0f, 3.0f, 0.7071f);
                for (auto& [id, v] : values)
                    base.set (id, v);
            }
            const auto cands = AutoMixEngine::candidatesFor (base, profile, current);
            const auto it = std::find_if (cands.begin(), cands.end(), [] (auto& c) { return c.second.routing.contains (dsp::ModuleId::eq, 1); });
            expect (it != cands.end(), "an EQ 2 variant exists");
            if (it != cands.end())
            {
                const auto air2 = planBand (it->second, current, EqRole::air, 1);
                expect (air2.has_value() && air2->on, "air band in EQ 2");
                if (air2)
                {
                    expectWithinAbsoluteError (air2->gain, 3.0f, 1.0e-4f);
                    expectWithinAbsoluteError (air2->freq, 9000.0f, 0.01f);
                }
                const auto air1 = planBand (it->second, current, EqRole::air, 0);
                expect (air1.has_value() && std::abs (air1->gain) < 1.0e-4f, "and left EQ 1");
            }
        }
    }
};

static EqAutoMixTests eqAutoMixTests;
