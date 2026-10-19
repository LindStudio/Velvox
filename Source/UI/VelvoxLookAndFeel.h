#pragma once

#include "Theme.h"

namespace velvox::ui
{
    class VelvoxLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        VelvoxLookAndFeel();

        // Slider properties set by Knob:
        //   hero    = large macro dial with a value-driven glow
        //   animPos = displayed (animated) position 0..1
        //   hover   = hover light 0..1
        static inline const juce::Identifier heroProperty { "hero" };
        static inline const juce::Identifier animPosProperty { "animPos" };
        static inline const juce::Identifier hoverProperty { "hover" };
        //   strong  = primary control: larger, brighter value read-out
        static inline const juce::Identifier strongProperty { "strong" };

        // Width of the value groove relative to the dial radius
        static constexpr float grooveRatio = 0.095f;

        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                               float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

        juce::Label* createSliderTextBox (juce::Slider&) override;

        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& background,
                                   bool highlighted, bool down) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

        void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                           int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

        juce::Font getPopupMenuFont() override;
        void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
        void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>&, const juce::String&) override;

        juce::Font getLabelFont (juce::Label&) override;

        void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
        juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                               juce::Rectangle<int> parentArea) override;

        juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

        void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;

    private:
        // Brushed-metal knob body for a dial radius (design units) at a physical pixel scale
        const juce::Image& metalBody (float radius, float physicalScale);

        juce::SharedResourcePointer<EmbeddedFonts> fonts;
        std::map<int, juce::Image> metalCache;   // key: diameter in physical pixels
    };
}
