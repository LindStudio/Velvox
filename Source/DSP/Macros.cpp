#include "Macros.h"

namespace vocalypse::dsp
{
    namespace
    {
        float clamp (float v, float lo, float hi) noexcept { return juce::jlimit (lo, hi, v); }

        // Saturation drive: the single-band drive and (0.6) every multiband band move together
        void addSatDrive (SatSettings& sat, float db) noexcept
        {
            sat.driveDb += db;
            for (auto& b : sat.bands)
                b.driveDb += db;
        }

        // A module with a mix control: off in the base -> only the macro's share counts
        void addMix (bool& on, float& mix, float macroMix) noexcept
        {
            if (macroMix <= 0.0f)
                return;
            const float base = on ? mix : 0.0f;
            mix = clamp (base + macroMix, 0.0f, 1.0f);
            on = true;
        }

        // The same for a module with an additive Mix: the macro adds wet, but the dry gain of
        // the true mix law keeps following the user's own Mix (`lawMix`) - a macro never takes
        // the original vocal away (DSP/MixLaw.h)
        void addMix (bool& on, float& mix, float macroMix, float& lawMix) noexcept
        {
            if (macroMix > 0.0f && lawMix < 0.0f)
                lawMix = on ? mix : 0.0f;
            addMix (on, mix, macroMix);
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

        // A role band a down move may reduce: only one that exists and is on
        RoleBand existingRoleBand (EqSettings& eq, EqRole role) noexcept
        {
            const int i = eq.findRole (role);
            if (i < 0 || ! eq.bands[(size_t) i].on)
                return {};
            auto& b = eq.bands[(size_t) i];
            return { &b, b.gain };
        }

        // Down move on a mix module: only when it is on; at 0 it switches off
        void reduceMix (bool& on, float& mix, float delta) noexcept
        {
            if (! on)
                return;
            mix = clamp (mix + delta, 0.0f, 1.0f);
            if (mix <= 0.0f)
                on = false;
        }

        // Review 2026-10-28 (Jakob: Saturation "voller, nicht verzerrt"): the macros keep the saturation in the
        // warmth zone - more drive and more Mix only up to the THD limit; the drive gives way when the Mix
        // they add would pass it. A user setting already beyond the limit is not made any harsher by them.
        constexpr float macroThdLimit = SaturationModule::warmZoneThd * 0.85f;
        void limitSatToWarmZone (const SatSettings& base, SatSettings& s) noexcept
        {
            const auto thd = [] (int character, float driveDb, float mix)
            {
                SatSettings one;
                one.character = character;
                one.driveDb = driveDb;
                one.mix = mix;
                return SaturationModule::thdPercent (one, -12.0f);
            };
            const auto limit = [&] (float baseDb, float& db, int character)
            {
                const float allowed = std::max (macroThdLimit, base.enabled ? thd (character, baseDb, base.mix) : 0.0f);
                if (thd (character, db, s.mix) > allowed)
                    db = std::min (db, SaturationModule::maxDriveForThd (character, s.mix, allowed));
            };
            limit (base.driveDb, s.driveDb, s.character);
            // Multiband: the bands keep moving together - the smallest room of any band decides
            float added = 1.0e9f;
            for (size_t i = 0; i < s.bands.size(); ++i)
            {
                float db = s.bands[i].driveDb;
                limit (base.bands[i].driveDb, db, s.bands[i].character);
                added = std::min (added, db - base.bands[i].driveDb);
            }
            for (size_t i = 0; i < s.bands.size(); ++i)
                s.bands[i].driveDb = std::min (s.bands[i].driveDb, base.bands[i].driveDb + std::max (0.0f, added));

            // Tube colours even at 0 dB drive: then the Mix the macros added gives way, too
            const float allowed = std::max (macroThdLimit, base.enabled ? thd (s.character, base.driveDb, base.mix) : 0.0f);
            const float floorMix = base.enabled ? std::min (base.mix, s.mix) : 0.0f;
            if (! s.multiband && thd (s.character, s.driveDb, s.mix) > allowed)
            {
                float lo = floorMix, hi = s.mix;
                for (int i = 0; i < 16; ++i)
                {
                    const float m = 0.5f * (lo + hi);
                    (thd (s.character, s.driveDb, m) <= allowed ? lo : hi) = m;
                }
                s.mix = lo;
            }
        }

        // The clamps of applyMacros (parameter ranges)
        void clampMacroFields (ChainSettings& s, RoleBand& air, RoleBand& warmth) noexcept
        {
            air.clampGain();
            warmth.clampGain();
            s.deess.rangeDb      = clamp (s.deess.rangeDb, 0.0f, 24.0f);
            s.sat.driveDb        = clamp (s.sat.driveDb, 0.0f, 24.0f);
            for (auto& b : s.sat.bands)
                b.driveDb = clamp (b.driveDb, 0.0f, 24.0f);
            s.comp.thresholdDb   = clamp (s.comp.thresholdDb, -60.0f, 0.0f);
            s.comp.ratio         = clamp (s.comp.ratio, 1.0f, 20.0f);
            s.comp.attackMs      = clamp (s.comp.attackMs, 0.1f, 100.0f);
            s.comp.makeupDb      = clamp (s.comp.makeupDb, -12.0f, 24.0f);
            s.stereo.width       = clamp (s.stereo.width, 0.0f, StereoStage::maxWidth);
            s.reverbDuck.amountDb = clamp (s.reverbDuck.amountDb, 0.0f, 24.0f);
            s.delayDuck.amountDb  = clamp (s.delayDuck.amountDb, 0.0f, 24.0f);
        }
    }

    ChainSettings applyMacroMove (const ChainSettings& base, MacroId id, float fromRaw, float toRaw) noexcept
    {
        const float from = clamp (fromRaw, -1.0f, 1.0f), to = clamp (toRaw, -1.0f, 1.0f);   // bipolar since the review of 2026-10-28
        const float d = to - from;
        if (d == 0.0f)
            return base;

        ChainSettings s = base;
        RoleBand air, warmth;
        const bool up = d > 0.0f;

        // The amounts are those of applyMacros (keep both in step)
        switch (id)
        {
            case MacroId::presence:
                if (up)
                {
                    s.eqOn = true;
                    air = roleBand (s.eq, EqRole::air);
                    s.deessOn = true;
                }
                else if (toRaw < 0.0f)
                {
                    // Below the middle: a cut needs the band (review 2026-10-28, bipolar faders)
                    s.eqOn = true;
                    air = roleBand (s.eq, EqRole::air);
                }
                else if (s.eqOn)
                {
                    air = existingRoleBand (s.eq, EqRole::air);
                }
                air.add (5.0f * d);
                if (s.deessOn)
                    s.deess.rangeDb += 3.0f * d;
                break;

            case MacroId::warmth:
                if (up)
                {
                    s.eqOn = true;
                    warmth = roleBand (s.eq, EqRole::warmth);
                    air = roleBand (s.eq, EqRole::air);
                    addMix (s.sat.enabled, s.sat.mix, 0.5f * d);
                    addSatDrive (s.sat, 6.0f * d);
                }
                else
                {
                    if (toRaw < 0.0f)
                    {
                        // Below the middle: thinner - the warmth band cuts (created for it)
                        s.eqOn = true;
                        warmth = roleBand (s.eq, EqRole::warmth);
                        air = existingRoleBand (s.eq, EqRole::air);
                    }
                    else if (s.eqOn)
                    {
                        warmth = existingRoleBand (s.eq, EqRole::warmth);
                        air = existingRoleBand (s.eq, EqRole::air);
                    }
                    if (s.sat.enabled)
                        addSatDrive (s.sat, 6.0f * d);
                    reduceMix (s.sat.enabled, s.sat.mix, 0.5f * d);
                }
                warmth.add (3.0f * d);
                air.add (-1.5f * d);
                break;

            case MacroId::aggression:
            case MacroId::density:
            {
                const bool punch = id == MacroId::aggression;
                if (up)
                    s.compOn = true;
                if (s.compOn)
                {
                    s.comp.thresholdDb -= (punch ? 10.0f : 6.0f) * d;
                    s.comp.ratio += (punch ? 4.0f : 2.0f) * d;
                    if (punch)
                        s.comp.attackMs *= (1.0f - 0.6f * to) / (1.0f - 0.6f * from);
                    s.comp.makeupDb += (punch ? 5.0f : 3.0f) * d;
                }
                const float satShare = punch ? 0.4f : 0.25f;
                if (up)
                {
                    addMix (s.sat.enabled, s.sat.mix, satShare * d);
                    if (punch)
                        addSatDrive (s.sat, 8.0f * d);
                }
                else
                {
                    if (punch && s.sat.enabled)
                        addSatDrive (s.sat, 8.0f * d);
                    reduceMix (s.sat.enabled, s.sat.mix, satShare * d);
                }
                break;
            }

            case MacroId::width:
                if (up)
                    addMix (s.doubler.enabled, s.doubler.mix, 0.6f * d);
                else
                    reduceMix (s.doubler.enabled, s.doubler.mix, 0.6f * d);
                s.stereo.width += 0.5f * d;
                break;

            case MacroId::space:
                if (up)
                {
                    addMix (s.reverbOn, s.reverbMix, spaceReverbPerTravel * d);
                    addMix (s.delayOn, s.delayMix, spaceDelayPerTravel * d);
                    s.reverbDuck.amountDb += 6.0f * d;
                    s.delayDuck.amountDb += 6.0f * d;
                }
                else
                {
                    if (s.reverbOn)
                        s.reverbDuck.amountDb += 6.0f * d;
                    if (s.delayOn)
                        s.delayDuck.amountDb += 6.0f * d;
                    reduceMix (s.reverbOn, s.reverbMix, spaceReverbPerTravel * d);
                    reduceMix (s.delayOn, s.delayMix, spaceDelayPerTravel * d);
                }
                break;
        }

        clampMacroFields (s, air, warmth);
        limitSatToWarmZone (base.sat, s.sat);
        return s;
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
            addSatDrive (s.sat, 6.0f * m.warmth);
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
            addSatDrive (s.sat, 8.0f * m.aggression);
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
            addMix (s.doubler.enabled, s.doubler.mix, 0.6f * m.width, s.doubler.lawMix);
            s.stereo.width += 0.5f * m.width;
        }

        // ---- Space: reverb and delay, ducked so the vocal stays clear
        if (m.space > 0.0f)
        {
            addMix (s.reverbOn, s.reverbMix, 0.3f * m.space, s.reverbLawMix);
            addMix (s.delayOn, s.delayMix, 0.15f * m.space, s.delayLawMix);
            s.reverbDuck.amountDb += 6.0f * m.space;
            s.delayDuck.amountDb += 6.0f * m.space;
        }

        // ---- Clamp to the parameter ranges
        air.clampGain();
        warmth.clampGain();
        s.deess.rangeDb      = clamp (s.deess.rangeDb, 0.0f, 24.0f);
        s.sat.driveDb        = clamp (s.sat.driveDb, 0.0f, 24.0f);
        for (auto& b : s.sat.bands)
            b.driveDb = clamp (b.driveDb, 0.0f, 24.0f);
        s.comp.thresholdDb   = clamp (s.comp.thresholdDb, -60.0f, 0.0f);
        s.comp.ratio         = clamp (s.comp.ratio, 1.0f, 20.0f);
        s.comp.attackMs      = clamp (s.comp.attackMs, 0.1f, 100.0f);
        s.comp.makeupDb      = clamp (s.comp.makeupDb, -12.0f, 24.0f);
        s.stereo.width       = clamp (s.stereo.width, 0.0f, StereoStage::maxWidth);
        s.reverbDuck.amountDb = clamp (s.reverbDuck.amountDb, 0.0f, 24.0f);
        s.delayDuck.amountDb  = clamp (s.delayDuck.amountDb, 0.0f, 24.0f);
        limitSatToWarmZone (base.sat, s.sat);
        return s;
    }
}
