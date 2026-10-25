#include "MacroBake.h"
#include "SettingsReader.h"

namespace velvox::macrobake
{
    namespace
    {
        using dsp::ModuleId;

        struct Scalar
        {
            const char* id;
            float (*read) (const dsp::ChainSettings&);
        };

        // The macro-moved scalar fields and their parameters (real units), on-switches separately
        const std::vector<Scalar>& scalars()
        {
            static const std::vector<Scalar> s {
                { pid::deessRange,       [] (const dsp::ChainSettings& c) { return c.deess.rangeDb; } },
                { pid::satMix,           [] (const dsp::ChainSettings& c) { return c.sat.mix * 100.0f; } },
                { pid::satDrive,         [] (const dsp::ChainSettings& c) { return c.sat.driveDb; } },
                { pid::satLowDrive,      [] (const dsp::ChainSettings& c) { return c.sat.bands[0].driveDb; } },
                { pid::satMidDrive,      [] (const dsp::ChainSettings& c) { return c.sat.bands[1].driveDb; } },
                { pid::satHighDrive,     [] (const dsp::ChainSettings& c) { return c.sat.bands[2].driveDb; } },
                { pid::compThreshold,    [] (const dsp::ChainSettings& c) { return c.comp.thresholdDb; } },
                { pid::compRatio,        [] (const dsp::ChainSettings& c) { return c.comp.ratio; } },
                { pid::compAttack,       [] (const dsp::ChainSettings& c) { return c.comp.attackMs; } },
                { pid::compMakeup,       [] (const dsp::ChainSettings& c) { return c.comp.makeupDb; } },
                { pid::doublerMix,       [] (const dsp::ChainSettings& c) { return c.doubler.mix * 100.0f; } },
                { pid::doublerDry,       [] (const dsp::ChainSettings& c) { return c.doubler.dryDb; } },
                { pid::stereoWidth,      [] (const dsp::ChainSettings& c) { return c.stereo.width * 100.0f; } },
                { pid::reverbMix,        [] (const dsp::ChainSettings& c) { return c.reverbMix * 100.0f; } },
                { pid::delayMix,         [] (const dsp::ChainSettings& c) { return c.delayMix * 100.0f; } },
                { pid::reverbDuckAmount, [] (const dsp::ChainSettings& c) { return c.reverbDuck.amountDb; } },
                { pid::delayDuckAmount,  [] (const dsp::ChainSettings& c) { return c.delayDuck.amountDb; } },
            };
            return s;
        }

        struct Switch
        {
            const char* id;
            bool (*read) (const dsp::ChainSettings&);
        };

        const std::vector<Switch>& switches()
        {
            static const std::vector<Switch> s {
                { pid::eqOn,      [] (const dsp::ChainSettings& c) { return c.eqOn; } },
                { pid::deessOn,   [] (const dsp::ChainSettings& c) { return c.deessOn; } },
                { pid::satOn,     [] (const dsp::ChainSettings& c) { return c.sat.enabled; } },
                { pid::compOn,    [] (const dsp::ChainSettings& c) { return c.compOn; } },
                { pid::doublerOn, [] (const dsp::ChainSettings& c) { return c.doubler.enabled; } },
                { pid::reverbOn,  [] (const dsp::ChainSettings& c) { return c.reverbOn; } },
                { pid::delayOn,   [] (const dsp::ChainSettings& c) { return c.delayOn; } },
            };
            return s;
        }

        bool sameBand (const dsp::EqBand& a, const dsp::EqBand& b) noexcept
        {
            return a.on == b.on && a.type == b.type && a.freq == b.freq && a.gain == b.gain && a.q == b.q && a.slope == b.slope && a.role == b.role;
        }

        const char* macroIds[] { pid::macroPresence, pid::macroWarmth, pid::macroAggression, pid::macroWidth, pid::macroSpace, pid::macroDensity };
    }

    const juce::StringArray& touchedParamIds()
    {
        static const juce::StringArray ids = []
        {
            juce::StringArray a;
            for (const auto& s : scalars())
                a.add (s.id);
            for (const auto& s : switches())
                a.add (s.id);
            for (int band = 0; band < dsp::maxEqBands; ++band)
                for (auto* field : { pid::eqband::on, pid::eqband::type, pid::eqband::freq, pid::eqband::gain,
                                     pid::eqband::q, pid::eqband::slope, pid::eqband::role })
                    a.add (eqbands::bandParamId (0, band, field));
            return a;
        }();
        return ids;
    }

    dsp::ChainSettings readSettings (const eqbands::Access& access, const dsp::Routing& routing)
    {
        OfflineParameters p;
        for (const auto& spec : getParameterSpecs())
            p.set (spec.id, access.get (spec.id));
        return p.read (routing);
    }

    dsp::MacroValues readMacros (const eqbands::Access& access)
    {
        dsp::MacroValues m;
        for (int i = 0; i < dsp::numMacros; ++i)
            m.set ((dsp::MacroId) i, access.get (macroIds[i]) * 0.01f);
        return m;
    }

    void writeMacroFields (const eqbands::Access& access, const dsp::ChainSettings& from, const dsp::ChainSettings& to)
    {
        for (const auto& s : scalars())
            if (const float v = s.read (to); v != s.read (from))
                access.set (s.id, v);
        for (int band = 0; band < dsp::maxEqBands; ++band)
            if (! sameBand (from.eq.bands[(size_t) band], to.eq.bands[(size_t) band]))
                eqbands::writeBand (access, 0, band, to.eq.bands[(size_t) band]);
        for (const auto& s : switches())
            if (const bool on = s.read (to); on != s.read (from))
                access.set (s.id, on ? 1.0f : 0.0f);
    }

    bool keepDry (const dsp::ChainSettings& before, dsp::ChainSettings& after, const dsp::Routing& routing, dsp::MixLaws& laws)
    {
        bool changed = false;
        const auto keep = [&] (ModuleId id, bool beforeOn, float beforeMix, bool afterOn, float afterMix, float* dryDb)
        {
            if (! afterOn || laws.isLegacy (id, 0) || afterMix <= beforeMix)
                return;
            const float dryBefore = beforeOn ? dsp::additiveDryGain (beforeMix, dsp::MixLaw::trueDryWet) : 1.0f;
            if (dsp::additiveDryGain (afterMix, dsp::MixLaw::trueDryWet) == dryBefore)
                return;
            laws.setLegacy (id, 0, true);   // dry at unity from now on, wet = Mix as before
            if (dryDb != nullptr && dryBefore < 1.0f)
                *dryDb = dryBefore <= 0.0f ? -60.0f
                                           : juce::jlimit (-60.0f, 6.0f, *dryDb + juce::Decibels::gainToDecibels (dryBefore, -100.0f));
            changed = true;
        };
        keep (ModuleId::doubler, before.doubler.enabled, before.doubler.mix, after.doubler.enabled, after.doubler.mix, &after.doubler.dryDb);
        if (routing.isInsert (ModuleId::delay, 0))
            keep (ModuleId::delay, before.delayOn, before.delayMix, after.delayOn, after.delayMix, nullptr);
        if (routing.isInsert (ModuleId::reverb, 0))
            keep (ModuleId::reverb, before.reverbOn, before.reverbMix, after.reverbOn, after.reverbMix, nullptr);
        return changed;
    }

    bool bake (const eqbands::Access& access, const dsp::Routing& routing, dsp::MixLaws* laws)
    {
        const auto macros = readMacros (access);
        if (! macros.anyActive())
            return false;
        const auto base = readSettings (access, routing);
        auto baked = dsp::applyMacros (base, macros);
        if (laws != nullptr)
            keepDry (base, baked, routing, *laws);
        writeMacroFields (access, base, baked);
        return true;
    }
}
