#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../DSP/Routing.h"

// Visual tokens (v0.4 "anthracite" identity).
// Dark anthracite canvas, raised panels that separate clearly from it, glass
// edges made of faint white and a restrained cyan accent for selection and
// pointers. Red is reserved for real problems (clipping, phase cancellation).
namespace velvox::ui
{
    namespace colours
    {
        inline const juce::Colour background { 0xff101216 };
        inline const juce::Colour panel      { 0xff171a1f };   // main panels (opaque equivalent)
        inline const juce::Colour raised     { 0xff1f2329 };   // controls, cards
        inline const juce::Colour raisedHi   { 0xff272c33 };
        inline const juce::Colour line       { 0xff2e333b };   // hairlines
        inline const juce::Colour lineSoft   { 0xff23272e };
        inline const juce::Colour text       { 0xfff4f6f8 };
        inline const juce::Colour textDim    { 0xffaab0ba };   // secondary labels: dimmer, still clearly legible
        inline const juce::Colour textFaint  { 0xff7a818c };   // scales, units, tertiary hints
        inline const juce::Colour accent     { 0xffffffff };
        inline const juce::Colour warning    { 0xffff5c63 };
        inline const juce::Colour io         { 0xffc9ccd3 };

        // Atmospheric accent: "aurora", teal fading into blue
        inline const juce::Colour teal       { 0xff2ee6c5 };
        inline const juce::Colour blue       { 0xff3b82f6 };

        // Interaction accent: selection, knob pointers, active states
        inline const juce::Colour cyan       { 0xff35d4ee };
    }

    // Layout tokens (design units on the 1440 x 900 canvas). New components use
    // these instead of literal numbers so spacing, corners and type stay uniform.
    namespace space
    {
        inline constexpr int xs = 4, s = 8, m = 12, l = 16, xl = 24, xxl = 32;
    }

    namespace radius
    {
        inline constexpr float control = 8.0f;   // buttons, fields
        inline constexpr float tile = 12.0f;     // module cards
        inline constexpr float card = 12.0f;     // popups, recessed stages
        inline constexpr float panel = 16.0f;    // main panels, dialogs
    }

    namespace typesize
    {
        inline constexpr float caption = 11.5f;  // small captions (scales, units, badges)
        inline constexpr float small = 12.5f;
        inline constexpr float body = 14.0f;
        inline constexpr float lead = 15.0f;     // help text, intro lines
        inline constexpr float label = 13.5f;    // control names
        inline constexpr float value = 14.5f;    // control read-outs
        inline constexpr float heading = 14.5f;  // group headings inside panels
        inline constexpr float title = 23.0f;
        inline constexpr float hero = 27.0f;
    }

    // Dial diameters: primary controls, secondary controls, rarely used details
    namespace dialsize
    {
        inline constexpr int primary = 104, secondary = 74, detail = 62, rail = 70;
    }

    namespace motion
    {
        inline constexpr int revealMs = 260;     // panels sliding in
        inline constexpr float fadeRate = 0.2f;  // per-frame approach for Smoothed fades
        inline constexpr int hoverInfoDelayMs = 1000;
    }

    namespace colours
    {
        // Status: on / active uses the aurora, problems use `warning`. The global
        // bypass is a deliberate, loud state and uses an amber of its own.
        inline const juce::Colour bypass { 0xffffb547 };
        inline const juce::Colour scrim  { 0xd8050608 };   // behind dialogs and overlays
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
    // Small captions (semibold, light tracking)
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

    // Small caption (sentence case, lightly tracked). Sizes below the readable
    // minimum are lifted, so older call sites with tiny sizes stay legible.
    inline float captionSize (float requested) { return std::max (typesize::caption, requested + 2.0f); }

    inline void drawCaption (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r,
                             juce::Justification j = juce::Justification::centredLeft,
                             juce::Colour c = colours::textDim, float height = 10.0f)
    {
        g.setColour (c);
        g.setFont (caps (captionSize (height)));
        g.drawText (text, r, j, false);
    }

    // Base design size; the editor scales this canvas uniformly
    inline constexpr int baseWidth = 1440;
    inline constexpr int baseHeight = 900;
}
