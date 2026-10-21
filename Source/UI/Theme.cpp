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
    juce::Font caps (float height)             { return make (&EmbeddedFonts::semibold, height, 0.03f); }

    juce::Image brandImage (Brand which)
    {
        using namespace VelvoxBinaryData;
        switch (which)
        {
            case Brand::icon:     return juce::ImageCache::getFromMemory (HeadlinerIcon_png, HeadlinerIcon_pngSize);
            case Brand::wordmark: return juce::ImageCache::getFromMemory (HeadlinerWordmark_png, HeadlinerWordmark_pngSize);
            case Brand::logo:     break;
        }
        return juce::ImageCache::getFromMemory (HeadlinerLogo_png, HeadlinerLogo_pngSize);
    }

    void drawBrand (juce::Graphics& g, Brand which, juce::Rectangle<float> area, juce::RectanglePlacement placement, float alpha)
    {
        const auto img = brandImage (which);
        if (! img.isValid())
            return;
        g.saveState();
        g.setOpacity (alpha);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (img, area, placement);
        g.restoreState();
    }
}
