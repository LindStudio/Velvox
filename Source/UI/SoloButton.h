#pragma once

#include "Theme.h"

namespace velvox::ui
{
    // Module solo (0.6.1): the "S" on a chain tile and in the panel header. Solo yellow, the classic
    // mixer colour; it only ever means "solo" in Headliner (bypass is amber, warnings red).
    inline const juce::Colour soloColour { 0xffffd84d };

    // Round "S": outline (alpha = how visible it is, e.g. the tile's hover fade), filled while soloed
    inline void drawSoloGlyph (juce::Graphics& g, juce::Rectangle<float> r, bool soloed, float hover, float alpha = 1.0f)
    {
        const float size = std::min (r.getWidth(), r.getHeight());
        r = r.withSizeKeepingCentre (size, size);
        if (soloed)
        {
            drawGlow (g, r.getCentre(), size * 1.0f, 0.35f * alpha, soloColour);
            g.setColour (soloColour.withAlpha (alpha));
            g.fillEllipse (r);
        }
        else
        {
            g.setColour (colours::background.withAlpha (0.9f * alpha));
            g.fillEllipse (r);
            g.setColour ((hover > 0.5f ? soloColour.withAlpha (0.8f) : colours::line.brighter (0.45f)).withMultipliedAlpha (alpha));
            g.drawEllipse (r.reduced (0.6f), 1.2f);
        }
        g.setColour (soloed ? colours::windowBg.withAlpha (alpha)
                            : (hover > 0.5f ? soloColour : colours::textSoft).withAlpha (alpha));
        g.setFont (semiBold (size * 0.62f));
        g.drawText ("S", r.translated (0.0f, 0.3f), juce::Justification::centred, false);
    }

    // Header button: shows / toggles the processor's solo state for one instance (wired by the panel)
    class SoloButton final : public juce::Button
    {
    public:
        SoloButton() : juce::Button ("Solo")
        {
            setClickingTogglesState (false);
            setTooltip (juce::String::fromUTF8 ("Solo: nur dieses Modul hören, alle anderen umgangen (nochmal klicken: aus). "
                                                "In der Kette: S auf der Kachel oder Alt+Klick"));
        }

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            drawSoloGlyph (g, getLocalBounds().toFloat().reduced (2.0f), getToggleState(), highlighted || down ? 1.0f : 0.0f);
        }
    };
}
