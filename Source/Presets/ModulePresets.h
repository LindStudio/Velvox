#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../DSP/Routing.h"

namespace velvox
{
    // Starting points for a single module. Applying one only touches that
    // module's parameters (everything else in the chain stays as it is) and
    // switches the module on. Values are in real units, like ParameterSnapshot.
    struct ModulePreset
    {
        juce::String name;
        juce::String description;
        struct Value { juce::String id; float value; };
        std::vector<Value> values;
    };

    const std::vector<ModulePreset>& getModulePresets (dsp::ModuleId module);

    // Applies with begin/endChangeGesture so hosts record it like a user edit
    // Applies the preset to one instance of its module (0 = the original parameters)
    void applyModulePreset (const ModulePreset& preset, juce::AudioProcessorValueTreeState& apvts, int instance = 0);
}
