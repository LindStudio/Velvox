#pragma once

// ============================================================================
//  FUTURE EXTENSION POINT - NOT IMPLEMENTED IN VERSION 1
//
//  Version 1 contains no AI, no speech input and no audio analysis. This file
//  only defines the contract a later "smart mixing" feature will plug into:
//
//    user text ("make it more aggressive")  ---\
//    (optional) analysis features of audio  ----> SuggestionProvider
//    current ParameterSnapshot             ---/          |
//                                                        v
//                                     ParameterSnapshot (proposal)
//                                                        |
//                       user reviews / accepts -> snapshot.applyTo (apvts)
//
//  The provider sees the parameter space through createParameterSchema()
//  (ids, ranges, units, descriptions) and answers with a ParameterSnapshot,
//  the same type used by presets and A/B slots - so applying a suggestion
//  is identical to loading a preset (undoable via A/B, automatable).
//  Long-running work (network, inference) must never run on the audio thread.
// ============================================================================

#include "../Presets/ParameterSnapshot.h"
#include <functional>

namespace velvox::mixing
{
    struct MixingRequest
    {
        juce::String description;          // free text from the user
        juce::String genreHint;            // optional, e.g. "Hypertechno"
        ParameterSnapshot current;         // current settings for context
    };

    struct MixingSuggestion
    {
        ParameterSnapshot proposal;        // values to apply
        juce::String explanation;          // human-readable reasoning
    };

    class SuggestionProvider
    {
    public:
        virtual ~SuggestionProvider() = default;

        // Must be asynchronous; the callback is invoked on the message thread.
        virtual void requestSuggestion (const MixingRequest& request,
                                        std::function<void (juce::Result, MixingSuggestion)> onDone) = 0;
    };
}
