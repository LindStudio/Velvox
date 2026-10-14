#pragma once

#include <juce_data_structures/juce_data_structures.h>

// Non-parameter properties stored on the APVTS state tree (persisted with the
// project). Never rename an existing key.
namespace velvox::statekeys
{
    // 1 (implicit, v0.1.x): parameters only
    // 2 (v0.2.x): + routing; missing parameters are reset to defaults on load
    inline constexpr int currentStateVersion = 2;

    inline const juce::Identifier stateVersion { "stateVersion" };
    inline const juce::Identifier routing      { "routing" };
}
