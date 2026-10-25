#pragma once

#include "Widgets.h"

namespace velvox::ui
{
    // Segment group of the design (Rate 1/4 · 1/8 · 1/16 …): radio buttons on a #0D0F15 bar,
    // bound to a choice or bool parameter. Choice labels map onto the parameter's choices by
    // name (a label that is no choice name maps by position); a bool parameter takes two
    // labels (off, on). Host automation and presets update the selection.
    // With `withOverflow`, a last "…" segment opens a menu with every choice; when the value
    // has no segment of its own, that segment lights up and shows the value's name.
    // Gestures (spec 2.6, like every control): click = choose, double-click or Alt+click = default,
    // wheel = previous / next segment, right-click = menu (reset, copy, paste, module items),
    // hover = info card with name, description and value.
    class Segments final : public juce::Component
    {
    public:
        Segments (APVTS& state, const char* paramId, juce::StringArray labels, juce::Colour accent, bool withOverflow = false);
        ~Segments() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        int getSelectedSegment() const noexcept { return selectedSegment; }   // -1: value has no segment
        int getChoiceForSegment (int segment) const;                         // parameter index of a segment
        int getNumSegments() const noexcept { return (int) choiceIndex.size(); }
        void clickSegment (int segment);
        void setAccent (juce::Colour accent);
        const juce::String& getParamId() const noexcept { return paramId; }

        // Optional tile look (Saturation character tiles): each segment is painted by this function
        // instead of the text segment; the gestures stay the same. Bounds are the segment's local area.
        using TilePainter = std::function<void (juce::Graphics&, int segment, juce::Rectangle<float> bounds, bool selected, bool hover)>;
        void setTilePainter (TilePainter painter);
        bool hasTilePainter() const noexcept { return tilePainter != nullptr; }

        int getOverflowSegment() const noexcept { return overflow != nullptr ? (int) choiceIndex.size() : -1; }
        juce::String getOverflowText() const { return overflow != nullptr ? overflow->getButtonText() : juce::String(); }
        void chooseFromOverflow (int choice);

        // Gestures (also called by the segment buttons)
        void resetToDefault();
        void step (int direction);   // +1 = next segment, -1 = previous
        void showMenu (juce::Component& target);

    private:
        class SegmentButton;
        void showValue (float denormalisedValue);
        void showOverflowMenu();

        juce::String paramId;
        juce::RangedAudioParameter* parameter = nullptr;
        juce::StringArray allChoices;
        juce::OwnedArray<juce::TextButton> buttons;
        juce::TextButton* overflow = nullptr;   // owned by buttons (last)
        std::vector<int> choiceIndex;
        std::unique_ptr<juce::ParameterAttachment> attachment;
        int selectedSegment = -1;
        std::unique_ptr<HoverInfo> info;
        TilePainter tilePainter;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Segments)
    };
}
