#pragma once

#include "VocalChain.h"

namespace velvox::dsp
{
    // The six Simple-mode sliders ("macros"; PUNCH = aggression, Density lives in the auto-mix view)
    enum class MacroId { presence = 0, warmth, aggression, width, space, density };
    inline constexpr int numMacros = 6;

    // Macro values / positions, each 0..1
    struct MacroValues
    {
        float presence = 0.0f, warmth = 0.0f, aggression = 0.0f, width = 0.0f, space = 0.0f, density = 0.0f;

        bool anyActive() const noexcept
        {
            return presence > 0.0f || warmth > 0.0f || aggression > 0.0f || width > 0.0f || space > 0.0f || density > 0.0f;
        }

        float get (MacroId id) const noexcept
        {
            switch (id)
            {
                case MacroId::presence:   return presence;
                case MacroId::warmth:     return warmth;
                case MacroId::aggression: return aggression;
                case MacroId::width:      return width;
                case MacroId::space:      return space;
                case MacroId::density:    return density;
            }
            return 0.0f;
        }

        void set (MacroId id, float v) noexcept
        {
            switch (id)
            {
                case MacroId::presence:   presence = v; break;
                case MacroId::warmth:     warmth = v; break;
                case MacroId::aggression: aggression = v; break;
                case MacroId::width:      width = v; break;
                case MacroId::space:      space = v; break;
                case MacroId::density:    density = v; break;
            }
        }
    };

    // 0.6.1: the Simple sliders SET the modules. Moving slider `id` from position `from` to `to` (0..1)
    // changes `base` - the module state when the move began - the way the 0.6.0 offset layer did, made
    // relative: from 0 the result equals applyMacros with that macro alone; to == from returns `base`
    // exactly. Up moves switch the modules they need on (and create the EQ role band, spec Q5); down
    // moves only reduce modules that are on - a mix brought to 0 switches its module off - and never
    // create anything. Results are clamped to the parameter ranges. Pure, no allocation.
    ChainSettings applyMacroMove (const ChainSettings& base, MacroId id, float from, float to) noexcept;

    // The 0.2 - 0.6.0 offset layer: base settings + macro values -> what those versions processed.
    // No longer in the audio path (0.6.1); kept unchanged to bake old projects / presets once on load
    // (Parameters/MacroBake.h) so they sound exactly as before. With all macros at 0 it returns `base`.
    ChainSettings applyMacros (const ChainSettings& base, const MacroValues& macros) noexcept;

    // Which modules the macros currently switch on although they are off in the
    // base settings (for the UI). Indexed by ModuleId.
    std::array<bool, numModules> modulesEngagedByMacros (const ChainSettings& base, const MacroValues& macros) noexcept;
}
