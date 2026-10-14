#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Visual tokens. Each module's accent is its position on the signal path,
// running teal (input side) -> gold (space effects), so colour encodes order.
namespace velvox::ui
{
    namespace colours
    {
        inline const juce::Colour background { 0xff12151c };
        inline const juce::Colour panel      { 0xff1a1f29 };
        inline const juce::Colour raised     { 0xff232a36 };
        inline const juce::Colour line       { 0xff2e3644 };
        inline const juce::Colour text       { 0xffe4e8f0 };
        inline const juce::Colour textDim    { 0xff8791a3 };
        inline const juce::Colour warning    { 0xffff5d5d };

        inline const juce::Colour eq         { 0xff5cc8c0 };
        inline const juce::Colour comp       { 0xff6fa8f5 };
        inline const juce::Colour deess      { 0xff9c8cf0 };
        inline const juce::Colour sat        { 0xffe07bb5 };
        inline const juce::Colour delay      { 0xfff09a6b };
        inline const juce::Colour reverb     { 0xffe8c46a };
        inline const juce::Colour io         { 0xffc9d1de };
    }

    inline juce::Font font (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
    }

    // Base design size; the editor scales this canvas uniformly
    inline constexpr int baseWidth = 1240;
    inline constexpr int baseHeight = 780;
}
