#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "HeadlinerColours.h"

//==============================================================================
// HEADLINER VOCAL – LookAndFeel
//
//  - drawRotarySlider:   matter 3D-Knopf, vertiefte Kappe, leuchtender Punkt,
//                        Strichkranz der bis zum Wert in der Akzentfarbe leuchtet
//  - drawToggleButton:   kleiner Switch (26 x 14) + Label rechts
//  - TextButtons:        Segment-Buttons (Rate, Mode, Room …) mit Toggle-State
//
// Akzentfarbe pro Regler:  slider.setColour (Slider::rotarySliderFillColourId, farbe)
// Akzentfarbe pro Switch:  button.setColour (ToggleButton::tickColourId, farbe)
// Akzentfarbe pro Segment: button.setColour (TextButton::buttonOnColourId, farbe)
//==============================================================================
class HeadlinerLookAndFeel : public juce::LookAndFeel_V4
{
public:
    HeadlinerLookAndFeel();

    /** Optional: eingebettete Fonts setzen (Montserrat + Roboto Mono, siehe README). */
    void setTypefaces (juce::Typeface::Ptr sansRegular,
                       juce::Typeface::Ptr sansSemiBold,
                       juce::Typeface::Ptr monoRegular);

    juce::Font uiFont   (float height, bool semiBold = false) const;
    juce::Font monoFont (float height) const;

    /** Stellt einen Slider auf das Headliner-Knob-Verhalten ein (270°, kein Textfeld). */
    static void styleKnob (juce::Slider& slider, juce::Colour accent);

    /** Hintergrund einer Segment-Gruppe (die Buttons liegen darauf). */
    static void drawSegmentGroupBackground (juce::Graphics& g, juce::Rectangle<float> area);

    //==============================================================================
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getLabelFont (juce::Label&) override;

private:
    juce::Typeface::Ptr sans, sansSemi, mono;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeadlinerLookAndFeel)
};
