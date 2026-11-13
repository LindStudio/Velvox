#include "Theme.h"
#include "VocalypseBinaryData.h"

namespace vocalypse::ui
{
    EmbeddedFonts::EmbeddedFonts()
    {
        using namespace VocalypseBinaryData;
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

        std::atomic<float> productTextScale { 1.0f };

        juce::Font make (juce::Typeface::Ptr EmbeddedFonts::* which, float height, float kerning = 0.0f, float scale = sizeScale)
        {
            juce::SharedResourcePointer<EmbeddedFonts> fonts;
            const auto& tf = (*fonts).*which;
            height *= scale * productTextScale.load (std::memory_order_relaxed);
            if (tf == nullptr)
                return juce::Font (juce::FontOptions (height)).withExtraKerningFactor (kerning);
            return juce::Font (juce::FontOptions (tf).withHeight (height).withKerningFactor (kerning));
        }
    }

    void setTextScale (float s) noexcept { productTextScale.store (juce::jlimit (0.5f, 3.0f, s), std::memory_order_relaxed); }
    float getTextScale() noexcept       { return productTextScale.load (std::memory_order_relaxed); }

    juce::Font font (float height, bool bold)  { return make (bold ? &EmbeddedFonts::semibold : &EmbeddedFonts::regular, height); }
    juce::Font medium (float height)           { return make (&EmbeddedFonts::medium, height); }
    juce::Font display (float height, bool bold) { return make (bold ? &EmbeddedFonts::bold : &EmbeddedFonts::semibold, height); }
    juce::Font caps (float height)             { return make (&EmbeddedFonts::semibold, height, 0.06f); }

    // Sizes of the VOCALYPSE VOCAL design system are exact pixels (no Montserrat correction)
    juce::Font monoFont (float height)                 { return make (&EmbeddedFonts::mono, height, 0.0f, 1.0f); }
    juce::Font semiBold (float height, float tracking) { return make (&EmbeddedFonts::semibold, height, tracking, 1.0f); }

    juce::String moduleIconSvg (dsp::ModuleId id)
    {
        using namespace VocalypseBinaryData;
        using M = dsp::ModuleId;
        const auto text = [] (const char* data, int size) { return juce::String::createStringFromData (data, size); };
        switch (id)
        {
            case M::ride:    return text (levelride_svg, levelride_svgSize);
            case M::smooth:  return text (smooth_svg, smooth_svgSize);
            case M::smash:   return text (vocalsmash_svg, vocalsmash_svgSize);
            case M::delay:   return text (delay_svg, delay_svgSize);
            case M::reverb:  return text (reverb_svg, reverb_svgSize);
            case M::eq:      return text (eq_svg, eq_svgSize);
            case M::comp:    return text (comp_svg, comp_svgSize);
            case M::deess:   return text (deess_svg, deess_svgSize);
            case M::sat:     return text (sat_svg, sat_svgSize);
            case M::doubler: return text (doubler_svg, doubler_svgSize);
            case M::check:   return text (check_svg, check_svgSize);
            case M::denoise: return text (denoise_svg, denoise_svgSize);
            case M::breath:  return text (breath_svg, breath_svgSize);
        }
        return {};
    }

    juce::Image brandImage (Brand which)
    {
        using namespace VocalypseBinaryData;
        switch (which)
        {
            case Brand::icon:     return juce::ImageCache::getFromMemory (VocalypseIcon_png, VocalypseIcon_pngSize);
            case Brand::wordmark: return juce::ImageCache::getFromMemory (VocalypseWordmark_png, VocalypseWordmark_pngSize);
            case Brand::logo:     break;   // VOCALYPSE: the wordmark is the logo (W3, Jakob 2026-11-03)
        }
        return juce::ImageCache::getFromMemory (VocalypseWordmark_png, VocalypseWordmark_pngSize);
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
