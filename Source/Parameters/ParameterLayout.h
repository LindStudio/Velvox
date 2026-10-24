#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ParameterIDs.h"
#include "../DSP/Routing.h"
#include <optional>

namespace velvox
{
    // Machine-readable description of one parameter. This table is the single
    // source of truth for the APVTS layout, preset validation and (later) for a
    // mixing engine that needs to know what it is allowed to change.
    struct ParameterSpec
    {
        enum class Type { Float, Bool, Choice };

        const char* id;
        const char* name;
        const char* module;       // "Global", "EQ", "Compressor", ...
        Type type;
        float minValue = 0.0f;
        float maxValue = 1.0f;
        float defaultValue = 0.0f;
        float skewCentre = 0.0f;  // 0 = linear, otherwise value at the knob centre
        const char* unit = "";    // "dB", "Hz", "ms", "%", "s", ":1"
        juce::StringArray choices {};
        const char* description = "";
        float interval = 0.0f;    // > 0: values snap to this step (e.g. 1 semitone)
        bool automatable = true;  // false: the host may not automate it (internal state, legacy IDs)
        bool hidden = false;      // not shown in the UI (still saved, still read by the DSP where used)
    };

    // Every parameter: the original table (instance 1 of every module), followed by the
    // generated banks of instances 2..n (see DSP/Instances.h)
    const std::vector<ParameterSpec>& getParameterSpecs();
    const ParameterSpec* findParameterSpec (const juce::String& id);

    // Module a parameter belongs to (nullopt for global / output / macro parameters)
    std::optional<dsp::ModuleId> moduleOfParameter (const ParameterSpec& spec);

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // JSON schema of all parameters (id, module, range, unit, description).
    // Intended as the contract for a future suggestion engine; no AI in v1.
    juce::var createParameterSchema();

    // Tempo-synced delay divisions
    namespace delaydiv
    {
        const juce::StringArray& names();
        double beatsForIndex (int index); // length in quarter notes
    }

    // Formats a value the same way the host sees it
    juce::String formatValue (const ParameterSpec& spec, float value);

    inline constexpr int parameterVersion = 1;
}
