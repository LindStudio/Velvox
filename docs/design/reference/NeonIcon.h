#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
// Leuchtendes Line-Art-Icon (die SVGs aus /Resources/icons).
// Die SVGs sind weiß gezeichnet und werden hier in die Modulfarbe umgefärbt.
//
//   icon.setSvg (BinaryData::reverb_svg, BinaryData::reverb_svgSize, hv::colours::reverb);
//==============================================================================
class NeonIcon : public juce::Component
{
public:
    NeonIcon()
    {
        setInterceptsMouseClicks (false, false);
        setComponentEffect (&glow);
    }

    void setSvg (const void* data, size_t size, juce::Colour colour)
    {
        // Weiß im SVG-Text durch die Modulfarbe ersetzen – funktioniert mit JUCE 7, 8 und 9
        auto svg = juce::String::createStringFromData (data, (int) size)
                       .replace ("#FFFFFF", "#" + colour.toDisplayString (false));
        const auto utf8 = svg.toUTF8();
        drawable = juce::Drawable::createFromImageData (utf8.getAddress(), utf8.sizeInBytes() - 1);

        glow.setGlowProperties (6.0f, colour.withAlpha (0.85f));
        repaint();
    }

    void setDimmed (bool shouldBeDimmed)   { setAlpha (shouldBeDimmed ? 0.3f : 1.0f); }

    void paint (juce::Graphics& g) override
    {
        if (drawable != nullptr)
            drawable->drawWithin (g, getLocalBounds().toFloat().reduced (6.0f),
                                  juce::RectanglePlacement::centred, 1.0f);
    }

private:
    std::unique_ptr<juce::Drawable> drawable;
    juce::GlowEffect glow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeonIcon)
};
