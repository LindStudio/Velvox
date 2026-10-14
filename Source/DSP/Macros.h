#pragma once

#include "VocalChain.h"

namespace velvox::dsp
{
    // Beginner macros, each 0..1.
    struct MacroValues
    {
        float presence = 0.0f, warmth = 0.0f, aggression = 0.0f, width = 0.0f, space = 0.0f, density = 0.0f;

        bool anyActive() const noexcept
        {
            return presence > 0.0f || warmth > 0.0f || aggression > 0.0f || width > 0.0f || space > 0.0f || density > 0.0f;
        }
    };

    // Turns base (Pro) settings + macros into the settings that are processed.
    // Pure function: no state, no allocation - safe on the audio thread, and
    // reusable by the UI (to show effective values) and by a future mixing
    // engine. Macros never overwrite the base parameters; they add offsets and
    // may switch a module on (partially) when it is off. All results are
    // clamped to the parameter ranges. With all macros at 0 the result equals
    // the base settings exactly.
    ChainSettings applyMacros (const ChainSettings& base, const MacroValues& macros) noexcept;

    // Which modules the macros currently switch on although they are off in the
    // base settings (for the UI). Indexed by ModuleId.
    std::array<bool, numModules> modulesEngagedByMacros (const ChainSettings& base, const MacroValues& macros) noexcept;
}
