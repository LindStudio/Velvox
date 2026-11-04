#pragma once

#include <juce_core/juce_core.h>
#include <map>
#include <vector>
#include "Soft.h"
#include "../DSP/Routing.h"

// The structured decision log of the automatic mix (plan 2026-11-04-automix-konsistenz): per module what was
// measured, how much the problem weighs, what the module received and what the render showed afterwards.
namespace vocalypse::automix
{
    struct Measured
    {
        juce::String key;
        float value = 0.0f;
        juce::String unit;
    };

    struct ModuleLog
    {
        dsp::ModuleId module = dsp::ModuleId::eq;
        int instance = 0;
        bool globalStage = false;             // input / output gain entries (no module)
        bool active = false;
        juce::String problem;                 // stable key ("sibilance", "phraseSpread", ...) or "" when none
        std::vector<Measured> measurements;   // what was measured for this decision
        float need = 0.0f;                    // 0..1 soft weight of the problem
        float intensity = 0.0f;               // 0..1 share of the module's allowed maximum
        std::vector<Measured> parameters;     // every value this module received
        std::vector<Measured> result;         // after the render (e.g. measured GR, S/vowel after)
        juce::String skipReason;              // DE/EN via txt::both when !active
    };

    juce::var toVar (const ModuleLog&);       // JSON
    ModuleLog fromVar (const juce::var&);

    // Reads every parameter of each module entry (instance 0, or the output gain for the global stage) from
    // `values` into `parameters`, with the parameter's unit. Called when planning is complete.
    void fillParameters (std::vector<ModuleLog>& log, const std::map<juce::String, float>& values);

    // Adds or replaces (by key) an entry of a list
    void setMeasured (std::vector<Measured>& list, const juce::String& key, float value, const juce::String& unit);
}
