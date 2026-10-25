#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../DSP/Routing.h"

// Visual tokens (v0.5 "headliner" identity, see docs/STYLEGUIDE.md).
// Deep navy canvas, matte cards with a thin slate border, and one light source:
// the neon spectrum cyan -> blue -> violet -> pink. Every module owns a hue on
// that spectrum (its icon, power light and dial ring glow in it). Red is
// reserved for real problems (clipping, phase cancellation).
namespace velvox::ui
{
    namespace colours
    {
        // HEADLINER VOCAL design system (docs/design/HEADLINER_DESIGN_HANDOFF.md)
        inline const juce::Colour windowBg   { 0xff05060a };   // plugin background
        inline const juce::Colour panelInset { 0xff040509 };   // visualizer boxes (border borderInset, radius 8)
        inline const juce::Colour borderInset { 0xff13161e };
        inline const juce::Colour control    { 0xff0d0f15 };   // segment groups (radius 7)
        inline const juce::Colour border     { 0xff161925 };   // panel frames (radius 12)
        inline const juce::Colour borderSoft { 0xff13151e };   // dividers
        inline const juce::Colour tickOff    { 0xff30343d };   // unlit ticks of the knob ring
        inline const juce::Colour knobTop    { 0xff34373f };
        inline const juce::Colour knobBottom { 0xff121418 };
        inline const juce::Colour capTop     { 0xff17191d };
        inline const juce::Colour capBottom  { 0xff2a2d34 };
        inline const juce::Colour switchOff  { 0xff2a3040 };
        inline const juce::Colour textStrong { 0xfff1f5f9 };
        inline const juce::Colour textSoft   { 0xffc9ced9 };
        inline const juce::Colour textMuted  { 0xff8a93a6 };
        inline const juce::Colour knobName   { 0xff6b7489 };   // name under a knob

        // Existing roles mapped onto the design system
        inline const juce::Colour background = windowBg;
        inline const juce::Colour panel      { 0xff07080d };   // columns, module panels
        inline const juce::Colour raised     = control;        // controls, recessed fields
        inline const juce::Colour raisedHi   { 0xff11141c };   // cards, popups
        inline const juce::Colour line       = border;
        inline const juce::Colour lineSoft   = borderSoft;
        inline const juce::Colour text       { 0xffe8ebf2 };
        inline const juce::Colour textDim    = textMuted;      // secondary labels
        inline const juce::Colour textFaint  { 0xff5b6478 };   // section labels, scales, units
        inline const juce::Colour accent     { 0xffffffff };
        inline const juce::Colour warning    { 0xffff5c6c };
        inline const juce::Colour ok         { 0xff4be3a6 };
        inline const juce::Colour io         { 0xffc9cfdc };

        // The neon spectrum: Cyan Primary, Blue Primary, Violet Primary, Pink Accent
        inline const juce::Colour teal       { 0xff00d1ff };   // cyan end
        inline const juce::Colour blue       { 0xff6366ff };
        inline const juce::Colour violet     { 0xffa855f7 };
        inline const juce::Colour pink       { 0xfff472b6 };

        // Interaction accent: selection, knob pointers, active states
        inline const juce::Colour cyan       { 0xff22d3ff };
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

    // Dial diameters of the design: module knobs 50-56, Vocal Control 54, SMASH 150
    namespace dialsize
    {
        inline constexpr int primary = 56, secondary = 50, detail = 50, rail = 54, macro = 54, smash = 150;
    }

    namespace motion
    {
        inline constexpr int revealMs = 260;     // panels sliding in
        inline constexpr float fadeRate = 0.2f;  // per-frame approach for Smoothed fades
        inline constexpr int hoverInfoDelayMs = 600;   // spec 8: tooltip delay 600 ms
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

    // Position on the full neon spectrum (0 = cyan, 1/3 = blue, 2/3 = violet, 1 = pink)
    inline juce::Colour neon (float t, float alpha = 1.0f)
    {
        t = juce::jlimit (0.0f, 1.0f, t) * 3.0f;
        const juce::Colour stops[] { colours::teal, colours::blue, colours::violet, colours::pink };
        const int i = std::min (2, (int) t);
        return stops[i].interpolatedWith (stops[i + 1], t - (float) i).withAlpha (alpha);
    }

    // Each module glows in its own hue (Voxity-style columns): cleaning tools in
    // cyan, dynamics in blue, space and voices in violet, colour and drive in pink
    inline float moduleHue (dsp::ModuleId id)
    {
        using M = dsp::ModuleId;
        switch (id)
        {
            case M::eq:      return 0.00f;
            case M::check:   return 0.06f;
            case M::deess:   return 0.12f;
            case M::smooth:  return 0.20f;
            case M::comp:    return 0.32f;
            case M::ride:    return 0.40f;
            case M::smash:   return 0.47f;
            case M::doubler: return 0.56f;
            case M::choir:   return 0.62f;
            case M::pitch:   return 0.68f;
            case M::delay:   return 0.74f;
            case M::reverb:  return 0.80f;
            case M::chop:    return 0.88f;
            case M::pulse:   return 0.94f;
            case M::sat:     return 1.00f;
        }
        return 0.0f;
    }

    // Module colours of the design (8 design modules verbatim; the other modules sit on the
    // same cyan -> blue -> violet -> pink run)
    inline juce::Colour moduleColour (dsp::ModuleId id, float alpha = 1.0f)
    {
        using M = dsp::ModuleId;
        juce::uint32 argb = 0xff00d1ff;
        switch (id)
        {
            case M::ride:    argb = 0xff00d1ff; break;
            case M::smooth:  argb = 0xff22b8ff; break;
            case M::smash:   argb = 0xff3b82f6; break;
            case M::choir:   argb = 0xff6366ff; break;
            case M::pulse:   argb = 0xff8b5cf6; break;
            case M::chop:    argb = 0xffa855f7; break;
            case M::delay:   argb = 0xffd946ef; break;
            case M::reverb:  argb = 0xfff472b6; break;
            case M::eq:      argb = 0xff2dd4bf; break;
            case M::check:   argb = 0xff38bdf8; break;
            case M::deess:   argb = 0xff0ea5e9; break;
            case M::comp:    argb = 0xff2563eb; break;
            case M::doubler: argb = 0xff818cf8; break;
            case M::pitch:   argb = 0xffc084fc; break;
            case M::sat:     argb = 0xffec4899; break;
        }
        return juce::Colour (argb).withAlpha (alpha);
    }

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
    // Embedded typefaces (Montserrat, SIL OFL 1.1). Held alive by a
    // SharedResourcePointer in the editor so they are created once per session.
    struct EmbeddedFonts
    {
        EmbeddedFonts();
        juce::Typeface::Ptr regular, medium, semibold, light, bold, mono;
    };

    // Design system type (exact pixel sizes): values in Roboto Mono, labels in Montserrat SemiBold
    juce::Font monoFont (float height);
    juce::Font semiBold (float height, float tracking = 0.0f);

    // White line-art module icon (SVG text, 48 x 48); NeonIcon recolours it
    juce::String moduleIconSvg (dsp::ModuleId id);

    // Body text (Montserrat Regular / SemiBold)
    juce::Font font (float height, bool bold = false);
    // UI labels and buttons (Montserrat Medium)
    juce::Font medium (float height);
    // Large numbers and read-outs (Montserrat Medium / Bold)
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
    inline constexpr int baseWidth = 1000;   // spec 2.1 / 8: base 1000 x 600 (like Pro-Q 4)
    inline constexpr int baseHeight = 600;
}
