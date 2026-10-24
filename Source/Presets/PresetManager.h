#pragma once

#include "ParameterSnapshot.h"
#include "UndoHistory.h"

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
            juce::String category;
            bool isFactory = true;
            juce::File file; // user presets only
            juce::String description;
        };

        explicit PresetManager (juce::AudioProcessorValueTreeState& state,
                                juce::File userPresetDirectory = getDefaultUserPresetDirectory());

        static juce::File getDefaultUserPresetDirectory();
        static inline const juce::String fileExtension { ".vvpreset" };

        // Loading a preset is one undo step (set by the processor)
        void setUndoHistory (UndoHistory* h) noexcept { undo = h; }

        void refresh();
        const juce::Array<Entry>& getEntries() const noexcept { return entries; }

        bool loadPreset (int index);
        bool loadNext();
        bool loadPrevious();

        // Saves the current parameter values as a user preset (overwrites same name)
        bool saveUserPreset (const juce::String& name);
        bool deleteUserPreset (int index);
        // Renames a user preset file (factory presets cannot be renamed); false when the name is taken
        bool renameUserPreset (int index, const juce::String& newName);

        juce::String getCurrentPresetName() const;
        int getCurrentIndex() const;

        // True when a preset is loaded and any of its values (or the module
        // order) was changed since. The preset's values are read once per name.
        bool isCurrentPresetModified();

        juce::File getUserPresetDirectory() const { return userDirectory; }

        static inline const juce::Identifier presetNameProperty { "presetName" };

    private:
        void setCurrentPresetName (const juce::String& name);

        bool applyEntry (int index);

        juce::AudioProcessorValueTreeState& apvts;
        UndoHistory* undo = nullptr;
        juce::File userDirectory;
        juce::Array<Entry> entries;

        // Reference values of the current preset (for isCurrentPresetModified)
        juce::String referenceName;
        std::optional<ParameterSnapshot> reference;
    };
}
