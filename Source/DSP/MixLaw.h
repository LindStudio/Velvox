#pragma once

#include "Routing.h"
#include "Instances.h"
#include <array>
#include <cstdint>

namespace velvox::dsp
{
    // How a module instance applies its Mix (0.6, spec Q1).
    //  legacy:     the law of 0.1-0.5. Delay / Reverb inserts, Doubler and Smash ADD their wet
    //              signal to a dry at unity (out = dry + wet * mix): Mix 100 % still has the dry.
    //  trueDryWet: Mix 0 % = dry only, Mix 100 % = wet only. For those additive modules the dry
    //              stays at unity up to 50 % and fades out linearly above ("dry-hold" law), so every
    //              setting up to 50 % sounds exactly as with the legacy law.
    // Every other module already crossfades truly (out = dry + (wet - dry) * mix) under both laws.
    enum class MixLaw { legacy = 0, trueDryWet = 1 };

    // Modules whose Mix depends on the law (delay / reverb only matter as inserts)
    constexpr bool hasAdditiveMix (ModuleId id) noexcept
    {
        return id == ModuleId::delay || id == ModuleId::reverb || id == ModuleId::doubler || id == ModuleId::smash;
    }

    // Dry gain of an additive module at `mix` (0..1); its wet gain is `mix` under both laws
    inline float additiveDryGain (float mix, MixLaw law) noexcept
    {
        if (law == MixLaw::legacy)
            return 1.0f;
        return juce::jlimit (0.0f, 1.0f, 2.0f * (1.0f - mix));
    }

    // The law of every module instance. Plain value type (copied lock-free to the audio thread).
    // Persisted as the hidden state property "mixLaw": "v1" followed by the instances that
    // still use the legacy law ("v1,delay,doubler#2"). A state without it is a project from
    // 0.5 or older: every instance legacy, so it sounds exactly as before.
    struct MixLaws
    {
        std::array<std::uint16_t, numModules> legacyBits {};   // bit k = instance k uses the legacy law

        static MixLaws allLegacy() noexcept;
        static MixLaws allTrue() noexcept { return {}; }

        bool isLegacy (ModuleId id, int instance) const noexcept;
        MixLaw lawOf (ModuleId id, int instance) const noexcept { return isLegacy (id, instance) ? MixLaw::legacy : MixLaw::trueDryWet; }
        void setLegacy (ModuleId id, int instance, bool legacy) noexcept;

        juce::String toString() const;
        static MixLaws fromString (const juce::String& text);   // tolerant, never fails

        bool operator== (const MixLaws& o) const noexcept { return legacyBits == o.legacyBits; }
        bool operator!= (const MixLaws& o) const noexcept { return ! (*this == o); }
    };
}
