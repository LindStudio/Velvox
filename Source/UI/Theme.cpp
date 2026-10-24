#include "Theme.h"
#include "VelvoxBinaryData.h"

namespace velvox::ui
{
    EmbeddedFonts::EmbeddedFonts()
    {
        using namespace VelvoxBinaryData;
        light    = juce::Typeface::createSystemTypefaceFor (MontserratLight_ttf, (size_t) MontserratLight_ttfSize);
        regular  = juce::Typeface::createSystemTypefaceFor (MontserratRegular_ttf, (size_t) MontserratRegular_ttfSize);
        medium   = juce::Typeface::createSystemTypefaceFor (MontserratMedium_ttf, (size_t) MontserratMedium_ttfSize);
        semibold = juce::Typeface::createSystemTypefaceFor (MontserratSemiBold_ttf, (size_t) MontserratSemiBold_ttfSize);
        bold     = juce::Typeface::createSystemTypefaceFor (MontserratBold_ttf, (size_t) MontserratBold_ttfSize);
        mono     = juce::Typeface::createSystemTypefaceFor (RobotoMonoRegular_ttf, (size_t) RobotoMonoRegular_ttfSize);
    }

    namespace
    {
        // Montserrat runs wider than the type sizes were tuned for; this keeps
        // every existing text box at its old width
        constexpr float sizeScale = 0.93f;

        juce::Font make (juce::Typeface::Ptr EmbeddedFonts::* which, float height, float kerning = 0.0f, float scale = sizeScale)
        {
            juce::SharedResourcePointer<EmbeddedFonts> fonts;
            const auto& tf = (*fonts).*which;
            height *= scale;
            if (tf == nullptr)
                return juce::Font (juce::FontOptions (height)).withExtraKerningFactor (kerning);
            return juce::Font (juce::FontOptions (tf).withHeight (height).withKerningFactor (kerning));
        }
    }

    juce::Font font (float height, bool bold)  { return make (bold ? &EmbeddedFonts::semibold : &EmbeddedFonts::regular, height); }
    juce::Font medium (float height)           { return make (&EmbeddedFonts::medium, height); }
    juce::Font display (float height, bool bold) { return make (bold ? &EmbeddedFonts::bold : &EmbeddedFonts::semibold, height); }
    juce::Font caps (float height)             { return make (&EmbeddedFonts::semibold, height, 0.06f); }

    // Sizes of the HEADLINER VOCAL design system are exact pixels (no Montserrat correction)
    juce::Font monoFont (float height)                 { return make (&EmbeddedFonts::mono, height, 0.0f, 1.0f); }
    juce::Font semiBold (float height, float tracking) { return make (&EmbeddedFonts::semibold, height, tracking, 1.0f); }

    juce::String moduleIconSvg (dsp::ModuleId id)
    {
        using namespace VelvoxBinaryData;
        using M = dsp::ModuleId;
        const auto text = [] (const char* data, int size) { return juce::String::createStringFromData (data, size); };
        switch (id)
        {
            case M::ride:    return text (levelride_svg, levelride_svgSize);
            case M::smooth:  return text (smooth_svg, smooth_svgSize);
            case M::smash:   return text (vocalsmash_svg, vocalsmash_svgSize);
            case M::choir:   return text (choir_svg, choir_svgSize);
            case M::pulse:   return text (pulse_svg, pulse_svgSize);
            case M::chop:    return text (stutter_svg, stutter_svgSize);
            case M::delay:   return text (delay_svg, delay_svgSize);
            case M::reverb:  return text (reverb_svg, reverb_svgSize);
            case M::eq:      return text (eq_svg, eq_svgSize);
            case M::comp:    return text (comp_svg, comp_svgSize);
            case M::deess:   return text (deess_svg, deess_svgSize);
            case M::sat:     return text (sat_svg, sat_svgSize);
            case M::doubler: return text (doubler_svg, doubler_svgSize);
            case M::pitch:   return text (pitch_svg, pitch_svgSize);
            case M::check:   return text (check_svg, check_svgSize);
        }
        return {};
    }

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
