#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "HeadlinerLookAndFeel.h"

//==============================================================================
// Fertiger Regler-Baustein wie im Design:
//
//        ( Knopf )
//          12 ms        <- Wert (Roboto Mono, 11.5 px, #E8EBF2)
//        PRE-DELAY      <- Name (Montserrat SemiBold, 9 px, #6B7489)
//
// Verbindet sich direkt mit einem APVTS-Parameter.
//
//   knob = std::make_unique<HeadlinerKnob> (apvts, "reverb_predelay", "PRE-DELAY",
//                                           hv::colours::reverb, " ms");
//==============================================================================
class HeadlinerKnob : public juce::Component
{
public:
    HeadlinerKnob (juce::AudioProcessorValueTreeState& state,
                   const juce::String& parameterID,
                   const juce::String& labelText,
                   juce::Colour accent,
                   const juce::String& unitSuffix = {},
                   int decimals = 0)
        : name (labelText), suffix (unitSuffix), numDecimals (decimals)
    {
        HeadlinerLookAndFeel::styleKnob (slider, accent);
        addAndMakeVisible (slider);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, parameterID, slider);

        if (auto* p = state.getParameter (parameterID))
            slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));

        slider.onValueChange = [this] { repaint(); };
    }

    void setKnobDiameter (int d)   { knobDiameter = d; resized(); }

    void paint (juce::Graphics& g) override
    {
        auto* lf = dynamic_cast<HeadlinerLookAndFeel*> (&getLookAndFeel());
        auto area = getLocalBounds().withTrimmedTop (knobDiameter + 4);

        g.setColour (hv::colours::text);
        if (lf != nullptr) g.setFont (lf->monoFont (11.5f)); else g.setFont (11.5f);
        g.drawText (juce::String (slider.getValue(), numDecimals) + suffix,
                    area.removeFromTop (16), juce::Justification::centred);

        g.setColour (juce::Colour (0xff6b7489));
        if (lf != nullptr) g.setFont (lf->uiFont (9.0f, true)); else g.setFont (9.0f);
        g.drawText (name, area.removeFromTop (14), juce::Justification::centred);
    }

    void resized() override
    {
        slider.setBounds (getLocalBounds().removeFromTop (knobDiameter)
                                          .withSizeKeepingCentre (knobDiameter, knobDiameter));
    }

    /** Empfohlene Gesamtgröße für das Layout. */
    juce::Rectangle<int> preferredSize() const { return { 0, 0, juce::jmax (88, knobDiameter + 20), knobDiameter + 38 }; }

    juce::Slider slider;

private:
    juce::String name, suffix;
    int numDecimals = 0;
    int knobDiameter = 52;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeadlinerKnob)
};
