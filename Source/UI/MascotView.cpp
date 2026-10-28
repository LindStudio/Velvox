#include "MascotView.h"
#include "VelvoxBinaryData.h"
#include "../Text/I18n.h"

namespace velvox::ui
{
    namespace
    {
        // Binary data names of the stickers, in Mood order (Resources/MascotSticker, tools/mascot/cut_comic.py)
        constexpr const char* resourceNames[mascot::numMoods] {
            "listening_png", "talking_png", "wink_png", "celebrate_png", "sad_png",
            "shocked_png", "thinking_png", "proud_png", "annoyed_png"
        };
    }

    juce::Image MascotView::sticker (mascot::Mood mood)
    {
        static const auto images = []
        {
            std::array<juce::Image, mascot::numMoods> out;
            for (int i = 0; i < mascot::numMoods; ++i)
            {
                int size = 0;
                if (const char* data = VelvoxBinaryData::getNamedResource (resourceNames[i], size))
                    out[(size_t) i] = juce::ImageCache::getFromMemory (data, size);
            }
            return out;
        }();
        return images[(size_t) mood];
    }

    MascotView::MascotView()
        : brain (juce::Time::getMillisecondCounter())
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip (i18n::tr ("Klick mich: Vocal Check", "Click me: Vocal Check"));
    }

    void MascotView::tick (const mascot::Inputs& in, double dt)
    {
        brain.update (in, dt);
        repaint();   // he always moves a little (breathing)
    }

    juce::Rectangle<float> MascotView::getStandArea() const
    {
        return standArea.isEmpty() ? getLocalBounds().toFloat() : standArea;
    }

    juce::Rectangle<float> MascotView::stickerArea() const
    {
        // Square, standing on the bottom of the stand area; a little wider than it (the disc has air around it)
        const auto stand = getStandArea();
        const float size = std::min (stand.getWidth() * 1.15f, stand.getHeight());
        return { stand.getCentreX() - size * 0.5f, stand.getBottom() - size, size, size };
    }

    juce::AffineTransform MascotView::motion() const
    {
        const auto area = stickerArea();
        const float s = brain.scale();
        const auto feet = juce::Point<float> (area.getCentreX(), area.getBottom());
        return juce::AffineTransform::scale (s, s, feet.x, feet.y).translated (0.0f, -brain.lift() * area.getHeight());
    }

    bool MascotView::hitTest (int x, int y)
    {
        const auto area = stickerArea();
        const auto img = sticker (brain.mood());
        if (area.isEmpty() || ! img.isValid())
            return false;
        auto p = juce::Point<float> ((float) x, (float) y).transformedBy (motion().inverted());
        if (! area.contains (p))
            return false;
        const int px = (int) ((p.x - area.getX()) / area.getWidth() * img.getWidth());
        const int py = (int) ((p.y - area.getY()) / area.getHeight() * img.getHeight());
        return img.getPixelAt (juce::jlimit (0, img.getWidth() - 1, px), juce::jlimit (0, img.getHeight() - 1, py)).getAlpha() > 80;
    }

    void MascotView::paint (juce::Graphics& g)
    {
        const auto area = stickerArea();
        if (area.isEmpty())
            return;
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        const auto t = motion();
        const auto draw = [&] (mascot::Mood m, float alpha)
        {
            const auto img = sticker (m);
            if (! img.isValid() || alpha <= 0.0f)
                return;
            g.setOpacity (alpha);
            g.drawImageTransformed (img, juce::AffineTransform::scale (area.getWidth() / (float) img.getWidth(), area.getHeight() / (float) img.getHeight())
                                             .translated (area.getX(), area.getY())
                                             .followedBy (t));
        };
        // The new sticker over the old one while it fades in
        const float fade = brain.fadeAmount();
        if (fade > 0.0f)
            draw (brain.fadeFrom(), 1.0f);
        draw (brain.mood(), 1.0f - fade);
        g.setOpacity (1.0f);
    }

    void MascotView::mouseUp (const juce::MouseEvent& e)
    {
        if (e.mouseWasClicked() && onClick)
            onClick();
    }
}
