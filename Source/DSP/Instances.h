#pragma once

#include "Routing.h"
#include <juce_core/juce_core.h>

namespace velvox::dsp
{
    // Repeated module instances (0.4).
    //
    // A chain may hold the same module several times (EQ -> Comp -> EQ ...). Hosts need
    // a fixed parameter list for automation, so every module has a fixed bank of
    // instances: instance 0 uses the original parameter IDs (projects from 0.1-0.3
    // load unchanged), instance k >= 1 uses "<id>_<k+1>" (e.g. "lowGain_2").
    //
    // The limits follow CPU and memory cost and what a vocal chain plausibly needs:
    // cheap tone / dynamics modules more often, effects with large buffers twice,
    // modules with a global role (check) once.
    inline constexpr int maxExtraInstances = 3;   // instances beyond the first, largest bank

    constexpr int maxInstances (ModuleId id) noexcept
    {
        switch (id)
        {
            case ModuleId::eq:      return 4;
            case ModuleId::comp:
            case ModuleId::deess:
            case ModuleId::sat:     return 3;
            case ModuleId::delay:
            case ModuleId::reverb:
            case ModuleId::smooth:
            case ModuleId::smash:
            case ModuleId::ride:
            case ModuleId::doubler: return 2;
            case ModuleId::check:
            case ModuleId::denoise:
            case ModuleId::breath:  return 1;
        }
        return 1;
    }

    // Every possible chain entry, all modules and instances together
    constexpr int totalInstanceCount() noexcept
    {
        int n = 0;
        for (int i = 0; i < numModules; ++i)
            n += maxInstances ((ModuleId) i);
        return n;
    }
    inline constexpr int maxChainSlots = totalInstanceCount();

    // Parameter ID of `baseId` for one instance
    inline juce::String instanceParamId (const juce::String& baseId, int instance)
    {
        return instance <= 0 ? baseId : baseId + "_" + juce::String (instance + 1);
    }

    // Splits "lowGain_3" into ("lowGain", 2); IDs without a valid suffix are instance 0
    inline std::pair<juce::String, int> splitInstanceParamId (const juce::String& id)
    {
        const int us = id.lastIndexOfChar ('_');
        if (us > 0)
        {
            const auto tail = id.substring (us + 1);
            if (tail.containsOnly ("0123456789") && tail.isNotEmpty())
            {
                const int k = tail.getIntValue() - 1;
                if (k >= 1 && k < 8)
                    return { id.substring (0, us), k };
            }
        }
        return { id, 0 };
    }

    // Selection code used by the UI (and stored as "uiSelection"): instance 0 keeps the
    // plain module number of earlier versions
    constexpr int selectionCode (ModuleId id, int instance) noexcept { return (int) id + 256 * instance; }
    constexpr ModuleId selectionModule (int code) noexcept { return (ModuleId) (code % 256); }
    constexpr int selectionInstance (int code) noexcept { return code / 256; }

    // "EQ", "EQ 2", ... for chain cards and parameter names
    inline juce::String instanceDisplayName (ModuleId id, int instance)
    {
        return instance <= 0 ? juce::String (moduleDisplayName (id)) : juce::String (moduleDisplayName (id)) + " " + juce::String (instance + 1);
    }
}
