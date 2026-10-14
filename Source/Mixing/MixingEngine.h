#pragma once

// ============================================================================
//  FUTURE EXTENSION POINT - NO AI IN VERSION 0.x
//
//  Velvox contains no AI, no speech input and no audio analysis. This file
//  only defines the contract a later "smart mixing" feature will plug into:
//
//    user text ("make it more aggressive")  ---\
//    (optional) analysis features of audio  ----> SuggestionProvider
//    current ParameterSnapshot             ---/          |
//                                                        v
//                                     ParameterSnapshot (proposal)
//                                     = parameter values + module routing
//                                                        |
//            ProposalSession: compare with the original -> accept / reject
//
//  What already exists and is tested:
//   - createParameterSchema(): every parameter with id, module, range, unit
//     and a plain-language description (the "vocabulary" of a provider)
//   - ParameterSnapshot incl. routing: one format for presets, A/B, proposals
//   - applyMacros(): high-level intents (presence, warmth, ...) mapped onto the
//     real DSP parameters - a provider can also answer with macro values
//   - ProposalSession: apply, A/B against the original, accept or reject
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
