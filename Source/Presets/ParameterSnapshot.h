#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <map>

namespace velvox
{
    // A named set of parameter values in REAL units (dB, Hz, ms, %; bools 0/1;
    // choices as index). Used by factory presets, user presets, the A/B slots
    // and - in a future version - by a mixing engine that proposes settings.
    struct ParameterSnapshot
    {
        juce::String name;
        juce::String category;
        juce::String description;
        std::map<juce::String, float> values;

        static ParameterSnapshot capture (juce::AudioProcessorValueTreeState& apvts, const juce::String& name = {});

        // Applies values with begin/endChangeGesture so hosts record automation
        // correctly. Parameters missing from the snapshot are reset to their
        // default when resetMissingToDefault is true (makes presets deterministic).
        // Global bypass is never touched by a snapshot.
        void applyTo (juce::AudioProcessorValueTreeState& apvts, bool resetMissingToDefault = true) const;

        // Returns a list of problems (unknown IDs, out-of-range values)
        juce::StringArray validate() const;

        juce::ValueTree toValueTree() const;
        static ParameterSnapshot fromValueTree (const juce::ValueTree& tree);

        static inline const juce::Identifier treeType { "VelvoxPreset" };
    };
}
