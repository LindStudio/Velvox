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
        // Deep navy base: neutral enough for long sessions, dark enough that the
        // coloured light behind the glass reads as depth, not decoration
        inline const juce::Colour background { 0xff090c15 };
        inline const juce::Colour panel      { 0xff121726 };   // main panels (opaque equivalent)
        inline const juce::Colour raised     { 0xff1a2031 };   // controls, cards
        inline const juce::Colour raisedHi   { 0xff22293d };
        inline const juce::Colour line       { 0xff2c344a };   // hairlines
        inline const juce::Colour lineSoft   { 0xff21283a };
        inline const juce::Colour text       { 0xfff4f6fb };
        inline const juce::Colour textDim    { 0xffb0b7ca };   // secondary labels: dimmer, still clearly legible
        inline const juce::Colour textFaint  { 0xff7f879d };   // scales, units, tertiary hints
        inline const juce::Colour accent     { 0xffffffff };
        inline const juce::Colour warning    { 0xffff5c6c };
        inline const juce::Colour ok         { 0xff4be3a6 };
        inline const juce::Colour io         { 0xffc9cfdc };

        // Light of the glass: cyan -> blue -> violet ("aurora")
        inline const juce::Colour teal       { 0xff3ddcf5 };   // cyan end
        inline const juce::Colour blue       { 0xff5b7dff };
        inline const juce::Colour violet     { 0xff9a72ff };

        // Interaction accent: selection, knob pointers, active states
        inline const juce::Colour cyan       { 0xff4fd6ff };
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
        inline constexpr int hoverInfoDelayMs = 450;
    }

    namespace colours
    {
        // Status: on / active uses the aurora, problems use `warning`. The global
        // bypass is a deliberate, loud state and uses an amber of its own.
        inline const juce::Colour bypass { 0xffffb547 };
        inline const juce::Colour scrim  { 0xd8050608 };   // behind dialogs and overlays
    }

    // Position on the aurora gradient (0 = cyan, 0.5 = blue, 1 = violet)
    inline juce::Colour aurora (float t, float alpha = 1.0f)
    {
        t = juce::jlimit (0.0f, 1.0f, t);
        const auto c = t < 0.5f ? colours::teal.interpolatedWith (colours::blue, t * 2.0f)
                                : colours::blue.interpolatedWith (colours::violet, (t - 0.5f) * 2.0f);
        return c.withAlpha (alpha);
    }

    inline juce::ColourGradient auroraGradient (juce::Point<float> from, juce::Point<float> to, float alpha = 1.0f)
    {
        juce::ColourGradient g (aurora (0.0f, alpha), from, aurora (1.0f, alpha), to, false);
        g.addColour (0.5, aurora (0.5f, alpha));
        return g;
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

    // HEADLINER brand artwork (transparent PNGs, cached by juce::ImageCache)
    enum class Brand { icon, wordmark, logo };
    juce::Image brandImage (Brand which);
    // Draws the artwork fitted into `area` (keeps the aspect ratio)
    void drawBrand (juce::Graphics& g, Brand which, juce::Rectangle<float> area,
                    juce::RectanglePlacement placement = juce::RectanglePlacement::centred, float alpha = 1.0f);

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

    // Liquid glass surface: a faint frosted body that is brighter at the top, a
    // soft sheen over the upper part, and a rim whose top edge catches the light.
    // Drawn over an opaque panel colour, so text on it always keeps its contrast.
    inline void drawGlass (juce::Graphics& g, juce::Rectangle<float> r, float corner, float fillAlpha = 0.026f)
    {
        g.setGradientFill (juce::ColourGradient (colours::accent.withAlpha (fillAlpha * 1.7f), r.getX(), r.getY(),
                                                 colours::accent.withAlpha (fillAlpha * 0.5f), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, corner);

        {
            // Sheen: a soft highlight across the top third
            juce::Graphics::ScopedSaveState s (g);
            juce::Path clip;
            clip.addRoundedRectangle (r, corner);
            g.reduceClipRegion (clip);
            const float h = std::min (r.getHeight() * 0.42f, 120.0f);
            g.setGradientFill (juce::ColourGradient (colours::accent.withAlpha (0.045f), r.getX(), r.getY(),
                                                     colours::accent.withAlpha (0.0f), r.getX(), r.getY() + h, false));
            g.fillRect (r.withHeight (h));
        }

        // Rim: bright on top, fading down the sides
        g.setGradientFill (juce::ColourGradient (colours::accent.withAlpha (0.17f), r.getX(), r.getY(),
                                                 colours::accent.withAlpha (0.045f), r.getX(), r.getY() + std::min (r.getHeight(), 160.0f), false));
        g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);

        // Specular line along the top edge
        juce::ColourGradient spec (colours::accent.withAlpha (0.0f), r.getX() + corner, r.getY(),
                                   colours::accent.withAlpha (0.0f), r.getRight() - corner, r.getY(), false);
        spec.addColour (0.35, colours::accent.withAlpha (0.30f));
        spec.addColour (0.65, colours::accent.withAlpha (0.12f));
        g.setGradientFill (spec);
        g.fillRect (juce::Rectangle<float> (r.getX() + corner, r.getY() + 0.5f, r.getWidth() - 2.0f * corner, 1.0f));
    }

    // A main glass panel: soft shadow underneath, translucent tinted body, glass on top
    inline void drawGlassPanel (juce::Graphics& g, juce::Rectangle<float> r, float corner, juce::Colour tint = colours::teal, float tintAlpha = 0.05f)
    {
        g.setColour (juce::Colours::black.withAlpha (0.28f));
        g.fillRoundedRectangle (r.translated (0.0f, 4.0f).expanded (1.0f), corner + 1.0f);
        g.setColour (colours::panel.withAlpha (0.88f));
        g.fillRoundedRectangle (r, corner);
        g.setGradientFill (juce::ColourGradient (tint.withAlpha (tintAlpha), r.getX(), r.getY(),
                                                 tint.withAlpha (0.0f), r.getX() + r.getWidth() * 0.6f, r.getY() + r.getHeight() * 0.8f, false));
        g.fillRoundedRectangle (r, corner);
        drawGlass (g, r, corner, 0.022f);
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
