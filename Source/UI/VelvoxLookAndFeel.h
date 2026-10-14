#pragma once

#include "Theme.h"

namespace velvox::ui
{
    class VelvoxLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        VelvoxLookAndFeel();

        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                               float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

        juce::Label* createSliderTextBox (juce::Slider&) override;

        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& background,
                                   bool highlighted, bool down) override;
        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

        void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                           int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

        juce::Font getPopupMenuFont() override;
        void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;

        juce::Font getLabelFont (juce::Label&) override;
    };
}
