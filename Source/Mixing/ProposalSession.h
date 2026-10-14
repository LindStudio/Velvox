#pragma once

#include "../Presets/ParameterSnapshot.h"

namespace velvox::mixing
{
    // Compare-and-confirm workflow for a proposed setting (a preset today, a
    // suggestion from a future mixing assistant later):
    //
    //   begin (proposal)   remember the current sound, apply the proposal
    //   showOriginal()     temporarily switch back to compare
    //   showProposal()     switch to the proposal again
    //   accept()           keep the proposal
    //   reject()           restore the original exactly (values + routing)
    //
    // Message thread only. Contains no AI - it is the confirmation step any
    // proposal source must go through.
    class ProposalSession
    {
    public:
        explicit ProposalSession (juce::AudioProcessorValueTreeState& state) : apvts (state) {}

        void begin (const ParameterSnapshot& proposal);
        void showOriginal();
        void showProposal();
        void accept();
        void reject();

        bool isActive() const noexcept { return active; }
        bool isShowingProposal() const noexcept { return showingProposal; }

    private:
        juce::AudioProcessorValueTreeState& apvts;
        ParameterSnapshot original, proposed;
        bool active = false, showingProposal = false;
    };
}
