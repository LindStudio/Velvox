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

    juce::Rectangle<float> MascotView::frameArea() const
    {
        // The lower 64 % of his square, a little narrower: his body sits in it, his head is above it; raised
        // by its offset shadow so that shows below it too
        const auto a = stickerArea();
        const float w = a.getWidth() * 0.86f, shadow = 5.0f * w / 128.0f;
        return { a.getX() + a.getWidth() * 0.07f - shadow * 0.5f, a.getBottom() - shadow - a.getHeight() * 0.64f, w, a.getHeight() * 0.64f };
    }

    void MascotView::paintFrame (juce::Graphics& g, juce::Rectangle<float> f) const
    {
        // "Plugin-Violett" (Jakob 2026-10-28): violet panel, pink speed lines from behind him, ink rim,
        // hard cyan offset shadow - the plugin's accent colours in comic form
        const float u = f.getWidth() / 128.0f;   // the design was made at 128 px frame width
        const float rim = std::max (2.0f, 3.5f * u);
        g.setColour (juce::Colour (0xff00d1ff));
        g.fillRect (f.translated (5.0f * u, 5.0f * u));

        const auto centre = juce::Point<float> (f.getCentreX(), f.getY() + f.getHeight() * 0.55f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffa78bfa), centre, juce::Colour (0xff5b2fd6),
                                                 { f.getRight(), f.getBottom() }, true));
        g.fillRect (f);

        // Speed lines: thin wedges every 9 degrees, clipped to the panel
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (f.toNearestInt());
            juce::Path lines;
            const float reach = f.getWidth() + f.getHeight();
            for (int i = 0; i < 40; ++i)
            {
                const float a0 = juce::degreesToRadians (9.0f * (float) i), a1 = a0 + juce::degreesToRadians (1.4f);
                lines.startNewSubPath (centre);
                lines.lineTo (centre + juce::Point<float> (std::sin (a0), -std::cos (a0)) * reach);
                lines.lineTo (centre + juce::Point<float> (std::sin (a1), -std::cos (a1)) * reach);
                lines.closeSubPath();
            }
            g.setColour (juce::Colour (0xd9f472b6));
            g.fillPath (lines);
            // A calm middle, so the speed lines start around him
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffa78bfa), centre, juce::Colour (0x00a78bfa),
                                                     centre.translated (f.getWidth() * 0.28f, 0.0f), true));
            g.fillRect (f);
        }
        g.setColour (juce::Colour (0xff121018));
        g.drawRect (f, rim);
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
        if (frameArea().contains ((float) x, (float) y))
            return true;
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
        const auto frame = frameArea();
        paintFrame (g, frame);

        // The figure: cut by the frame at its sides and bottom (inside the rim), free above its top edge
        const float rim = std::max (2.0f, 3.5f * frame.getWidth() / 128.0f);
        juce::Graphics::ScopedSaveState clip (g);
        juce::RectangleList<int> visible;
        visible.add (juce::Rectangle<float>::leftTopRightBottom (frame.getX() + rim, frame.getY(), frame.getRight() - rim, frame.getBottom() - rim)
                         .getSmallestIntegerContainer());
        visible.add (getLocalBounds().withBottom (juce::roundToInt (frame.getY() + rim)));
        g.reduceClipRegion (visible);
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
