#pragma once

#include <juce_data_structures/juce_data_structures.h>

// Per-user preferences that belong to the person, not to a project:
// whether the first-start tutorial was finished, and a personal default
// module layout. Stored in the user's application-data folder
// (Velvox/Velvox.settings). Message thread only, never touched by audio.
namespace velvox::ui::settings
{
    inline constexpr const char* tutorialDone  = "tutorialDone";   // bool
    inline constexpr const char* myLayoutOrder = "myLayoutOrder";  // dsp::Routing string
    inline constexpr const char* myLayoutHidden = "myLayoutHidden"; // comma separated module keys
    inline constexpr const char* assistantAutoApply = "assistantAutoApply"; // bool: apply proposals right away
    inline constexpr const char* presetFavourites = "presetFavourites";   // favourite presets, one per line: "f:Name" / "u:Name"

    juce::String getString (const juce::String& key, const juce::String& fallback = {});
    bool getBool (const juce::String& key, bool fallback = false);
    void set (const juce::String& key, const juce::var& value);   // written to disk immediately

    // Tests and the snapshot renderer point the store at a scratch file
    // so they never read or change the real preferences.
    void useFileForTesting (const juce::File& file);
    juce::File getFile();
}
