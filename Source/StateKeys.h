#pragma once

#include <juce_data_structures/juce_data_structures.h>

// Non-parameter properties stored on the APVTS state tree (persisted with the
// project). Never rename an existing key.
namespace velvox::statekeys
{
    // 1 (implicit, v0.1.x): parameters only
    // 2 (v0.2.x): + routing; missing parameters are reset to defaults on load
    // 3 (v0.3): + "AutoMix" child (report, previous state, first-open prompt)
    inline constexpr int currentStateVersion = 3;

    inline const juce::Identifier stateVersion { "stateVersion" };
    inline const juce::Identifier routing      { "routing" };
    inline const juce::Identifier uiMode       { "uiMode" };   // "beginner" / "pro" - view only, never affects audio
    inline const juce::Identifier uiHidden     { "uiHiddenModules" }; // module keys hidden from the chain view - view only
    inline const juce::Identifier uiTab        { "uiTab" };    // 0.6: "automix" / "check" - view only (uiMode then is the Simple / Advanced mode)
    // 0.6: mix law per module instance (dsp::MixLaws string). Absent = a project from 0.5 or
    // older: every instance on the legacy law. Does not need a new state version.
    inline const juce::Identifier mixLaw       { "mixLaw" };
}
