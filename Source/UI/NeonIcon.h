#pragma once

#include "Theme.h"

namespace velvox::ui
{
    // Glowing line-art module icon (Resources/Icons, white strokes recoloured into the module
    // colour), as in the HEADLINER VOCAL design.
    class NeonIcon final : public juce::Component
    {
    public:
        NeonIcon()
        {
            setInterceptsMouseClicks (false, false);
            setComponentEffect (&glow);
        }

        void setModule (dsp::ModuleId id) { setSvg (moduleIconSvg (id), moduleColour (id)); }

        void setSvg (const juce::String& svgText, juce::Colour colour)
        {
            const auto svg = svgText.replace ("#FFFFFF", "#" + colour.toDisplayString (false));
            const auto utf8 = svg.toUTF8();
            drawable = juce::Drawable::createFromImageData (utf8.getAddress(), utf8.sizeInBytes() - 1);
            glow.setGlowProperties (6.0f, colour.withAlpha (0.85f));
            repaint();
        }

        void setDimmed (bool shouldBeDimmed) { setAlpha (shouldBeDimmed ? 0.3f : 1.0f); }

        void paint (juce::Graphics& g) override
        {
            if (drawable != nullptr)
                drawable->drawWithin (g, getLocalBounds().toFloat().reduced (6.0f), juce::RectanglePlacement::centred, 1.0f);
        }

    private:
        std::unique_ptr<juce::Drawable> drawable;
        juce::GlowEffect glow;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeonIcon)
    };
}
