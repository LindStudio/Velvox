#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../DSP/Routing.h"

// Visual tokens (v0.3 "deep dark" identity).
// Monochrome: near-black canvas, glass surfaces made of faint white, and
// white as the only accent - lit with soft glows instead of colour. Red is
// reserved for real problems (clipping, phase cancellation).
namespace velvox::ui
{
    namespace colours
    {
        inline const juce::Colour background { 0xff07080a };
        inline const juce::Colour panel      { 0xff0e0f12 };   // glass panels (opaque equivalent)
        inline const juce::Colour raised     { 0xff15161a };   // controls, tiles
        inline const juce::Colour raisedHi   { 0xff1c1d22 };
        inline const juce::Colour line       { 0xff24262c };   // hairlines
        inline const juce::Colour lineSoft   { 0xff1a1b20 };
        inline const juce::Colour text       { 0xfff2f3f5 };
        inline const juce::Colour textDim    { 0xff8b8e97 };
        inline const juce::Colour textFaint  { 0xff4d5058 };
        inline const juce::Colour accent     { 0xffffffff };
        inline const juce::Colour warning    { 0xffff5c63 };
        inline const juce::Colour io         { 0xffc9ccd3 };

        // Atmospheric accent: "aurora", teal fading into blue
        inline const juce::Colour teal       { 0xff2ee6c5 };
        inline const juce::Colour blue       { 0xff3b82f6 };
    }

    // Position on the aurora gradient (0 = teal, 1 = blue)
    inline juce::Colour aurora (float t, float alpha = 1.0f)
    {
        return colours::teal.interpolatedWith (colours::blue, juce::jlimit (0.0f, 1.0f, t)).withAlpha (alpha);
    }

    inline juce::ColourGradient auroraGradient (juce::Point<float> from, juce::Point<float> to, float alpha = 1.0f)
    {
        return juce::ColourGradient (aurora (0.0f, alpha), from, aurora (1.0f, alpha), to, false);
    }

    // Every module shares the white accent; the chain position is shown by layout
    inline juce::Colour moduleColour (dsp::ModuleId) { return colours::accent; }

    // Exponential approach used by every animation (called once per frame)
    struct Smoothed
    {
        float current = 0.0f, target = 0.0f;

        // Returns true while still moving
        bool tick (float rate)
        {
            const float d = target - current;
            if (std::abs (d) < 1.0e-3f)
            {
                const bool moved = current != target;
                current = target;
                return moved;
            }
            current += d * rate;
            return true;
        }
        void snap (float v) { current = target = v; }
    };

    //==========================================================================
    // Embedded typefaces (Inter, SIL OFL 1.1). Held alive by a
    // SharedResourcePointer in the editor so they are created once per session.
    struct EmbeddedFonts
    {
        EmbeddedFonts();
        juce::Typeface::Ptr regular, medium, semibold, light;
    };

    // Body text (Inter Regular / SemiBold)
    juce::Font font (float height, bool bold = false);
    // UI labels and buttons (Inter Medium)
    juce::Font medium (float height);
    // Large numbers and read-outs (Inter Display Light / SemiBold)
    juce::Font display (float height, bool bold = false);
    // Small upper-case captions with open tracking
    juce::Font caps (float height);

    //==========================================================================
    // Soft radial light - the one decorative gesture of the design
    inline void drawGlow (juce::Graphics& g, juce::Point<float> centre, float radius, float alpha,
                          juce::Colour c = colours::accent)
    {
        juce::ColourGradient grad (c.withAlpha (alpha), centre.x, centre.y,
                                   c.withAlpha (0.0f), centre.x + radius, centre.y, true);
        grad.addColour (0.4, c.withAlpha (alpha * 0.45f));
        g.setGradientFill (grad);
        g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));
    }

    // Glass surface: faint fill, hairline border whose top edge catches the light
    inline void drawGlass (juce::Graphics& g, juce::Rectangle<float> r, float corner, float fillAlpha = 0.026f)
    {
        g.setColour (colours::accent.withAlpha (fillAlpha));
        g.fillRoundedRectangle (r, corner);
        g.setGradientFill (juce::ColourGradient (colours::accent.withAlpha (0.11f), r.getX(), r.getY(),
                                                 colours::accent.withAlpha (0.04f), r.getX(), r.getY() + std::min (r.getHeight(), 140.0f), false));
        g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
    }

    // Upper-case caption with letter spacing
    inline void drawCaption (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r,
                             juce::Justification j = juce::Justification::centredLeft,
                             juce::Colour c = colours::textDim, float height = 10.0f)
    {
        g.setColour (c);
        g.setFont (caps (height));
        g.drawText (text.toUpperCase(), r, j, false);
    }

    // Base design size; the editor scales this canvas uniformly
    inline constexpr int baseWidth = 1280;
    inline constexpr int baseHeight = 800;
}
