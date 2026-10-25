#pragma once

#include "DetailPanels.h"

namespace velvox::ui
{
    // "Link" of the Saturation bands (satLink, panel behaviour - the DSP never reads it): while Link is
    // on and the user edits one band's Drive (any control: a gesture is open on it), the other two
    // bands follow by the same amount, relative to where they were when the gesture began and
    // limited at the range ends (dsp::linkedBandDrives). Their changes run inside gestures of their
    // own, which the undo history folds into the user's step. Host automation (no gesture) and the
    // single-band Drive never link. One object per panel, for its instance; message thread only.
    class SatLink final : private juce::AudioProcessorParameter::Listener
    {
    public:
        SatLink (APVTS& state, int instance);
        ~SatLink() override;

    private:
        void parameterValueChanged (int parameterIndex, float newValue) override;
        void parameterGestureChanged (int parameterIndex, bool gestureIsStarting) override;
        int bandOf (int parameterIndex) const noexcept;

        std::array<juce::RangedAudioParameter*, 3> drives {};
        juce::RangedAudioParameter* link = nullptr;
        std::array<float, 3> start {};
        int driving = -1;      // band whose user gesture is open (-1: none)
        bool writing = false;  // our own begin / set / end calls

        JUCE_DECLARE_NON_COPYABLE (SatLink)
    };
}
