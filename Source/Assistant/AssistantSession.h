#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "AssistantTypes.h"

namespace velvox::assistant
{
    // Applies a plan to the real plugin parameters and keeps an undo history.
    // Message thread only. Works exclusively through the APVTS parameters
    // (begin/endChangeGesture + setValueNotifyingHost), so the host records
    // automation, the module controls update through their attachments, and
    // presets / project state stay the single source of truth - the assistant
    // stores no values of its own.
    class AssistantSession
    {
    public:
        explicit AssistantSession (juce::AudioProcessorValueTreeState& state) : apvts (state) {}

        struct Result
        {
            enum class Status { applied, nothingSelected, stale, invalid, failed };
            Status status = Status::failed;
            int count = 0;                  // changes applied / restored
            juce::StringArray messages;     // shown to the user
        };

        // Validates every selected change first; applies all of them or none.
        // Returns `stale` (and changes nothing) when a value was edited since the
        // plan was made - the plan must then be rebuilt from the current state.
        Result apply (const Plan& plan);

        // Restores the values from before the last assistant action. A value that
        // was changed afterwards (by the user, a preset or automation) is kept.
        Result undo();
        bool canUndo() const noexcept { return ! history.empty(); }
        void clearHistory() { history.clear(); }

        // Problems that prevent applying (unknown id, out of range, not finite ...)
        juce::StringArray validate (const Plan& plan) const;
        bool isStale (const Plan& plan) const;

        // Current value of every parameter, in real units
        static std::map<juce::String, float> readValues (juce::AudioProcessorValueTreeState& state);

    private:
        float read (const juce::String& id) const;
        bool write (const juce::String& id, float value);

        struct Step { juce::String id; float before, after; };
        std::vector<std::vector<Step>> history;   // newest last
        static constexpr size_t maxHistory = 32;

        juce::AudioProcessorValueTreeState& apvts;
    };
}
