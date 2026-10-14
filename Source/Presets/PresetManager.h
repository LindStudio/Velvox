#pragma once

#include "ParameterSnapshot.h"

namespace velvox
{
    // Factory presets (compiled in) + user presets (XML files on disk).
    // All methods must be called on the message thread.
    class PresetManager
    {
    public:
        struct Entry
        {
            juce::String name;
            bool isFactory = true;
            juce::File file; // user presets only
        };

        explicit PresetManager (juce::AudioProcessorValueTreeState& state,
                                juce::File userPresetDirectory = getDefaultUserPresetDirectory());

        static juce::File getDefaultUserPresetDirectory();
        static inline const juce::String fileExtension { ".vvpreset" };

        void refresh();
        const juce::Array<Entry>& getEntries() const noexcept { return entries; }

        bool loadPreset (int index);
        bool loadNext();
        bool loadPrevious();

        // Saves the current parameter values as a user preset (overwrites same name)
        bool saveUserPreset (const juce::String& name);
        bool deleteUserPreset (int index);

        juce::String getCurrentPresetName() const;
        int getCurrentIndex() const;

        juce::File getUserPresetDirectory() const { return userDirectory; }

        static inline const juce::Identifier presetNameProperty { "presetName" };

    private:
        void setCurrentPresetName (const juce::String& name);

        juce::AudioProcessorValueTreeState& apvts;
        juce::File userDirectory;
        juce::Array<Entry> entries;
    };
}
