#include "Macros.h"

namespace velvox::dsp
{
    namespace
    {
        float clamp (float v, float lo, float hi) noexcept { return juce::jlimit (lo, hi, v); }

        // A module with a mix control: off in the base -> only the macro's share counts
        void addMix (bool& on, float& mix, float macroMix) noexcept
        {
            if (macroMix <= 0.0f)
                return;
            const float base = on ? mix : 0.0f;
            mix = clamp (base + macroMix, 0.0f, 1.0f);
            on = true;
        }

        // The EQ band a macro moves (spec 8, Q5): the band with its role, or - when there is none (a
        // fresh state, or the user deleted it) - a new one in a free slot with the 0.5 default of the
        // fixed band the macro used to move. A switched-off role band counts as 0 dB. User bands are
        // never touched; without a free slot the macro leaves the EQ alone.
        struct RoleBand
        {
            EqBand* band = nullptr;
            float baseGain = 0.0f;

            void add (float db) noexcept
            {
                if (band != nullptr)
                    band->gain += db;
            }

            // The 0.5 clamp (+-18 dB), but a user setting beyond it is not pulled back
            void clampGain() noexcept
            {
                if (band != nullptr)
                    band->gain = clamp (band->gain, std::min (-18.0f, baseGain), std::max (18.0f, baseGain));
            }
        };

        RoleBand roleBand (EqSettings& eq, EqRole role) noexcept
        {
            int i = eq.findRole (role);
            if (i >= 0)
            {
                auto& b = eq.bands[(size_t) i];
                if (! b.on)
                {
                    b.on = true;
                    b.gain = 0.0f;
                }
                return { &b, b.gain };
            }
            i = eq.findFree();
            if (i < 0)
                return {};
            auto& b = eq.bands[(size_t) i];
            b = {};
            b.on = true;
            b.type = (int) (role == EqRole::air ? EqBandType::highShelf : EqBandType::lowShelf);
            b.freq = role == EqRole::air ? 10000.0f : 150.0f;
            b.q = 0.7071f;
            b.role = (int) role;
            return { &b, 0.0f };
        }
    }

    ChainSettings applyMacros (const ChainSettings& base, const MacroValues& raw) noexcept
    {
        if (! raw.anyActive())
            return base;

        MacroValues m;
        m.presence   = clamp (raw.presence, 0.0f, 1.0f);
        m.warmth     = clamp (raw.warmth, 0.0f, 1.0f);
        m.aggression = clamp (raw.aggression, 0.0f, 1.0f);
        m.width      = clamp (raw.width, 0.0f, 1.0f);
        m.space      = clamp (raw.space, 0.0f, 1.0f);
        m.density    = clamp (raw.density, 0.0f, 1.0f);

        ChainSettings s = base;
        RoleBand air, warmth;

        // ---- Presence: brighter top end (the "air" band, 0.5: high shelf), de-esser keeps the extra S in check
        if (m.presence > 0.0f)
        {
            s.eqOn = true;
            air = roleBand (s.eq, EqRole::air);
            air.add (5.0f * m.presence);
            s.deessOn = true;
            s.deess.rangeDb += 3.0f * m.presence;
        }

        // ---- Warmth: fuller lows, slightly softer top, gentle saturation
        if (m.warmth > 0.0f)
        {
            s.eqOn = true;
            warmth = roleBand (s.eq, EqRole::warmth);
            warmth.add (3.0f * m.warmth);
            if (air.band == nullptr)
                air = roleBand (s.eq, EqRole::air);
            air.add (-1.5f * m.warmth);
            addMix (s.sat.enabled, s.sat.mix, 0.5f * m.warmth);
            s.sat.driveDb += 6.0f * m.warmth;
        }

        // ---- Aggression: harder, faster compression and more drive
        if (m.aggression > 0.0f)
        {
            s.compOn = true;
            s.comp.thresholdDb -= 10.0f * m.aggression;
            s.comp.ratio += 4.0f * m.aggression;
            s.comp.attackMs *= 1.0f - 0.6f * m.aggression;
            s.comp.makeupDb += 5.0f * m.aggression;
            addMix (s.sat.enabled, s.sat.mix, 0.4f * m.aggression);
            s.sat.driveDb += 8.0f * m.aggression;
        }

        // ---- Density: more compression for a thicker, steadier voice
        if (m.density > 0.0f)
        {
            s.compOn = true;
            s.comp.thresholdDb -= 6.0f * m.density;
            s.comp.ratio += 2.0f * m.density;
            s.comp.makeupDb += 3.0f * m.density;
            addMix (s.sat.enabled, s.sat.mix, 0.25f * m.density);
        }

        // ---- Width: doubled voices and a wider output (centre untouched)
        if (m.width > 0.0f)
        {
            addMix (s.doubler.enabled, s.doubler.mix, 0.6f * m.width);
            s.stereo.width += 0.5f * m.width;
        }

        // ---- Space: reverb and delay, ducked so the vocal stays clear
        if (m.space > 0.0f)
        {
            addMix (s.reverbOn, s.reverbMix, 0.3f * m.space);
            addMix (s.delayOn, s.delayMix, 0.15f * m.space);
            s.reverbDuck.amountDb += 6.0f * m.space;
            s.delayDuck.amountDb += 6.0f * m.space;
        }

        // ---- Clamp to the parameter ranges
        air.clampGain();
        warmth.clampGain();
        s.deess.rangeDb      = clamp (s.deess.rangeDb, 0.0f, 24.0f);
        s.sat.driveDb        = clamp (s.sat.driveDb, 0.0f, 24.0f);
        s.comp.thresholdDb   = clamp (s.comp.thresholdDb, -60.0f, 0.0f);
        s.comp.ratio         = clamp (s.comp.ratio, 1.0f, 20.0f);
        s.comp.attackMs      = clamp (s.comp.attackMs, 0.1f, 100.0f);
        s.comp.makeupDb      = clamp (s.comp.makeupDb, -12.0f, 24.0f);
        s.stereo.width       = clamp (s.stereo.width, 0.0f, StereoStage::maxWidth);
        s.reverbDuck.amountDb = clamp (s.reverbDuck.amountDb, 0.0f, 24.0f);
        s.delayDuck.amountDb  = clamp (s.delayDuck.amountDb, 0.0f, 24.0f);
        return s;
    }

    std::array<bool, numModules> modulesEngagedByMacros (const ChainSettings& base, const MacroValues& m) noexcept
    {
        const auto eff = applyMacros (base, m);
        std::array<bool, numModules> e {};
        e[(size_t) ModuleId::eq]      = eff.eqOn && ! base.eqOn;
        e[(size_t) ModuleId::comp]    = eff.compOn && ! base.compOn;
        e[(size_t) ModuleId::deess]   = eff.deessOn && ! base.deessOn;
        e[(size_t) ModuleId::sat]     = eff.sat.enabled && ! base.sat.enabled;
        e[(size_t) ModuleId::doubler] = eff.doubler.enabled && ! base.doubler.enabled;
        e[(size_t) ModuleId::delay]   = eff.delayOn && ! base.delayOn;
        e[(size_t) ModuleId::reverb]  = eff.reverbOn && ! base.reverbOn;
        return e;
    }
}
