#pragma once

#include "Widgets.h"

namespace velvox::ui
{
    // Segment group of the design (Rate 1/4 · 1/8 · 1/16 …): radio buttons on a #0D0F15 bar,
    // bound to a choice or bool parameter. Choice labels map onto the parameter's choices by
    // name (a label that is no choice name maps by position); a bool parameter takes two
    // labels (off, on). Host automation and presets update the selection.
    class Segments final : public juce::Component
    {
    public:
        Segments (APVTS& state, const char* paramId, juce::StringArray labels, juce::Colour accent);
        ~Segments() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        int getSelectedSegment() const noexcept { return selectedSegment; }   // -1: value has no segment
        int getChoiceForSegment (int segment) const;                         // parameter index of a segment
        void clickSegment (int segment);
        const juce::String& getParamId() const noexcept { return paramId; }

    private:
        void showValue (float denormalisedValue);

        juce::String paramId;
        juce::OwnedArray<juce::TextButton> buttons;
        std::vector<int> choiceIndex;
        std::unique_ptr<juce::ParameterAttachment> attachment;
        int selectedSegment = -1;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Segments)
    };
}
