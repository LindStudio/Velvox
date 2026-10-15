#include "Theme.h"
#include "VelvoxBinaryData.h"

namespace velvox::ui
{
    EmbeddedFonts::EmbeddedFonts()
    {
        using namespace VelvoxBinaryData;
        regular  = juce::Typeface::createSystemTypefaceFor (InterRegular_ttf, (size_t) InterRegular_ttfSize);
        medium   = juce::Typeface::createSystemTypefaceFor (InterMedium_ttf, (size_t) InterMedium_ttfSize);
        semibold = juce::Typeface::createSystemTypefaceFor (InterSemiBold_ttf, (size_t) InterSemiBold_ttfSize);
        light    = juce::Typeface::createSystemTypefaceFor (InterDisplayLight_ttf, (size_t) InterDisplayLight_ttfSize);
    }

    namespace
    {
        juce::Font make (juce::Typeface::Ptr EmbeddedFonts::* which, float height, float kerning = 0.0f)
        {
            juce::SharedResourcePointer<EmbeddedFonts> fonts;
            const auto& tf = (*fonts).*which;
            if (tf == nullptr)
                return juce::Font (juce::FontOptions (height)).withExtraKerningFactor (kerning);
            return juce::Font (juce::FontOptions (tf).withHeight (height).withKerningFactor (kerning));
        }
    }

    juce::Font font (float height, bool bold)  { return make (bold ? &EmbeddedFonts::semibold : &EmbeddedFonts::regular, height); }
    juce::Font medium (float height)           { return make (&EmbeddedFonts::medium, height); }
    juce::Font display (float height, bool bold) { return make (bold ? &EmbeddedFonts::semibold : &EmbeddedFonts::light, height); }
    juce::Font caps (float height)             { return make (&EmbeddedFonts::semibold, height, 0.12f); }
}
