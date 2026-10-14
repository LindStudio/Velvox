#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../DSP/Routing.h"

// Visual tokens (v0.2 "club" identity).
// The neon accent runs cyan -> violet -> magenta along the signal path, so a
// module's colour tells you where it sits in the chain.
namespace velvox::ui
{
    namespace colours
    {
        inline const juce::Colour background { 0xff0b0d16 };
        inline const juce::Colour panel      { 0xff121522 };
        inline const juce::Colour raised     { 0xff1a1e2e };
        inline const juce::Colour line       { 0xff262b40 };
        inline const juce::Colour text       { 0xffe8ecf6 };
        inline const juce::Colour textDim    { 0xff8a90a8 };
        inline const juce::Colour warning    { 0xffff4d6a };
        inline const juce::Colour ok         { 0xff3ef0b0 };

        inline const juce::Colour cyan       { 0xff22e4ff };
        inline const juce::Colour violet     { 0xff8b5cff };
        inline const juce::Colour magenta    { 0xffff3ea5 };

        // Module accents: positions on the cyan -> violet -> magenta path
        inline const juce::Colour eq         { 0xff22e4ff };
        inline const juce::Colour comp       { 0xff3fb8ff };
        inline const juce::Colour deess      { 0xff5c8cff };
        inline const juce::Colour sat        { 0xff7a6bff };
        inline const juce::Colour doubler    { 0xff8b5cff };
        inline const juce::Colour pitch      { 0xffb04dff };
        inline const juce::Colour chop       { 0xffd445f0 };
        inline const juce::Colour delay      { 0xfff040c8 };
        inline const juce::Colour reverb     { 0xffff3ea5 };
        inline const juce::Colour io         { 0xffc9d3ea };
    }

    inline juce::Colour moduleColour (dsp::ModuleId id)
    {
        using M = dsp::ModuleId;
        switch (id)
        {
            case M::eq:      return colours::eq;
            case M::comp:    return colours::comp;
            case M::deess:   return colours::deess;
            case M::sat:     return colours::sat;
            case M::doubler: return colours::doubler;
            case M::pitch:   return colours::pitch;
            case M::chop:    return colours::chop;
            case M::delay:   return colours::delay;
            case M::reverb:  return colours::reverb;
        }
        return colours::io;
    }

    // Body text
    inline juce::Font font (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions ("Segoe UI", height, bold ? juce::Font::bold : juce::Font::plain));
    }

    // Titles and numbers: Bahnschrift (DIN-style Windows system font); falls
    // back to the default sans if it is not installed.
    inline juce::Font display (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions ("Bahnschrift", height, bold ? juce::Font::bold : juce::Font::plain));
    }

    // Base design size; the editor scales this canvas uniformly
    inline constexpr int baseWidth = 1280;
    inline constexpr int baseHeight = 800;
}
