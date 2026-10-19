#pragma once

#include "AssistantTypes.h"

namespace velvox::assistant
{
    // Turns recognised wishes into concrete, validated parameter changes.
    // Knows the Velvox modules and their real parameter IDs and ranges, nothing
    // about language. Works on a copy of the current values (AssistantContext),
    // so it is a pure function and can run on any thread.
    //
    // Rules:
    //  - only existing parameters, values clamped / snapped to their ranges
    //  - a module that is off gets an explicit "switch on" change
    //  - constraints are enforced last ("nicht schaerfer" removes anything
    //    that would raise the highs)
    //  - measurements (VOCAL CHECK, sidechain level) are quoted as measured
    //    facts only; nothing is claimed that was not measured
    //  - features that do not exist yet are reported as unavailable
    class AssistantPlanner
    {
    public:
        static Plan plan (const ParsedRequest& request, const AssistantContext& context);

        // Step size per intensity (slight / moderate / strong)
        static float factor (Intensity i) noexcept;
    };

    // Clamps and snaps a value to the parameter's range (bools / choices to integers)
    float legaliseValue (const juce::String& paramId, float value);
    // Value as shown to the user ("-3.0 dB", "An", "Shelf")
    juce::String displayValue (const juce::String& paramId, float value);
    // Module name as shown on the module cards
    juce::String moduleLabelFor (const juce::String& paramId);
}
