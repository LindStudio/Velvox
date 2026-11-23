#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <map>

// Presets of EMBER De-Esser: factory presets (in code), user presets (XML files in
// Documents/Lind Audio/EMBER/Presets/*.emberpreset) and the A/B compare slots.
namespace ember
{
    using ParamValues = std::map<juce::String, float>;   // parameter ID -> real value

    struct FactoryPreset
    {
        const char* nameDe;
        const char* nameEn;
        ParamValues values;   // missing IDs = default
    };

    const std::vector<FactoryPreset>& factoryPresets();
    juce::String presetName (const FactoryPreset&);   // in the interface language

    // Bypass and listen (Delta / S range) are tools, not settings: presets, A/B, undo and the "modified" marker
    // never touch them (a preset never switches the plugin off or loads "only the S")
    bool isSetting (const juce::String& parameterId);
    // Every setting (see isSetting)
    ParamValues capture (const juce::AudioProcessorValueTreeState&);
    // Sets every parameter (missing ones to default) as host gestures
    void apply (juce::AudioProcessorValueTreeState&, const ParamValues&);
    ParamValues withDefaults (const juce::AudioProcessorValueTreeState&, const ParamValues&);
    bool sameValues (const ParamValues& a, const ParamValues& b);

    // User presets
    juce::File userPresetFolder();
    juce::Array<juce::File> userPresets();
    bool saveUserPreset (const juce::File&, const ParamValues&);
    bool loadUserPreset (const juce::File&, ParamValues& out);
    // Renames a preset file (same folder; the name made legal for a file). The error in the interface language.
    juce::Result renameUserPresetFile (const juce::File&, const juce::String& newName, juce::File& renamed);
    // Moves a preset file to the recycle bin (deletes it where there is none)
    juce::Result deleteUserPresetFile (const juce::File&);
    inline constexpr const char* presetExtension = ".emberpreset";

    // Per-user settings (language, editor size, first-run hint): %APPDATA%/Lind Audio/EMBER.settings
    class UserSettings
    {
    public:
        UserSettings();
        juce::PropertiesFile& file() { return *props; }
        juce::String getLanguage();                 // "system" / "de" / "en"
        void setLanguage (const juce::String&);
        int getEditorWidth (int fallback);
        void setEditorWidth (int);
        bool wasAutoUsed();
        void setAutoUsed();
        // Tests: settings in a temporary file
        static void useFileForTests (const juce::File&);
    private:
        std::unique_ptr<juce::PropertiesFile> props;
    };
}
