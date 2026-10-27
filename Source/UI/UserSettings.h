#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include "../Text/I18n.h"

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
    // 1.0 settings sheet
    inline constexpr const char* language = "language";         // "system" (computer language), "de", "en"; missing = never chosen
    inline constexpr const char* defaultMode = "defaultMode";   // "simple" / "advanced": view of a new instance
    inline constexpr const char* defaultScale = "defaultScale"; // window scale of a new instance (1 = 1000 x 600)
    inline constexpr const char* offerAnalysis = "offerAnalysis"; // bool (default true): analysis popup on new instances
    inline constexpr const char* infoCards = "infoCards";       // bool (default true): hover info cards on controls

    juce::String getString (const juce::String& key, const juce::String& fallback = {});
    bool getBool (const juce::String& key, bool fallback = false);
    void set (const juce::String& key, const juce::var& value);   // written to disk immediately

    // Interface language: the stored preference (system when never chosen), whether the user has
    // chosen at all (the first-start welcome asks), storing a choice (also applies it), and applying
    // the stored preference to i18n::current().
    i18n::Preference languagePreference();
    bool hasLanguageChoice();
    void setLanguagePreference (i18n::Preference);
    void confirmLanguageChoice();   // the welcome card was seen: keep the current preference
    void applyLanguage();

    // Tests and the snapshot renderer point the store at a scratch file
    // so they never read or change the real preferences.
    void useFileForTesting (const juce::File& file);
    juce::File getFile();
}
