#include "MascotView.h"
#include "VocalypseBinaryData.h"
#include "../Text/I18n.h"

namespace vocalypse::ui
{
    namespace
    {
        // Binary data names of the stickers, in Mood order (Resources/MascotSticker, tools/mascot/cut_comic.py)
        constexpr const char* resourceNames[mascot::numMoods] {
            "listening_png", "talking_png", "wink_png", "celebrate_png", "sad_png",
            "shocked_png", "thinking_png", "proud_png", "annoyed_png"
        };

        // His open eyes in each picture (centre x, y and radii in the 384 px picture; measured 2026-10-28 and
        // checked against the extent of the eye whites),
        // for the blink. Celebrating his eyes are closed; winking only one is open.
        struct Eye { float x, y, rx, ry; };
        struct Eyes { int count; Eye eye[2]; };
        constexpr Eyes eyesOf[mascot::numMoods] {
            { 2, { { 195, 150, 30, 27 }, { 280, 151, 19, 23 } } },   // listening
            { 2, { { 136, 156, 21, 24 }, { 218, 156, 27, 26 } } },   // talking
            { 1, { { 149, 160, 26, 25 }, {} } },                    // wink
            { 0, {} },                                               // celebrate
            { 2, { { 164, 184, 28, 16 }, { 243, 188, 24, 16 } } },   // sad
            { 2, { { 151, 150, 39, 36 }, { 225, 147, 38, 41 } } },   // shocked
            { 2, { { 159, 144, 27, 27 }, { 243, 130, 22, 25 } } },   // thinking
            { 2, { { 134, 147, 29, 25 }, { 224, 140, 24, 23 } } },   // proud
            { 2, { { 142, 150, 28, 14 }, { 235, 149, 25, 16 } } },   // annoyed
        };
    }

    // The lids close over his open eyes from the top: skin from just above the eye, a dark lid line
    void MascotView::paintBlink (juce::Graphics& g, const juce::Image& img, mascot::Mood mood, float amount,
                                 const juce::AffineTransform& toView) const
    {
        const auto& eyes = eyesOf[(size_t) mood];
        if (amount <= 0.01f || eyes.count == 0)
            return;
        const float px = 384.0f / (float) img.getWidth();
        for (int i = 0; i < eyes.count; ++i)
        {
            const auto e = eyes.eye[i];
            // The skin next to the eye: of a few points around it the one closest to his skin red (brows,
            // the black outline and the ears are near some of them)
            juce::Colour skin (0xffe8463f);
            float best = 1.0e9f;
            for (const auto off : { juce::Point<float> (0.0f, e.ry + 8.0f), { -e.rx - 8.0f, 0.0f }, { e.rx + 8.0f, 0.0f },
                                    { 0.0f, -e.ry - 6.0f }, { -e.rx * 0.8f, e.ry + 6.0f }, { e.rx * 0.8f, e.ry + 6.0f } })
            {
                const auto c = img.getPixelAt (juce::roundToInt ((e.x + off.x) / px), juce::roundToInt ((e.y + off.y) / px));
                const float d = std::abs (c.getRed() - 232.0f) + std::abs (c.getGreen() - 70.0f) + std::abs (c.getBlue() - 64.0f)
                              + (c.getAlpha() < 250 ? 1000.0f : 0.0f);
                if (d < best)
                {
                    best = d;
                    skin = c;
                }
            }
            // Slightly larger than the eye so its own outline closes with it
            const auto oval = juce::Rectangle<float> (e.rx * 2.3f, e.ry * 2.3f).withCentre ({ e.x, e.y });
            const float lidY = oval.getY() + oval.getHeight() * amount;
            juce::Path lid;
            lid.addEllipse (oval);
            juce::Path cover;
            cover.addRectangle (oval.withBottom (lidY));
            juce::Graphics::ScopedSaveState s (g);
            g.addTransform (toView);
            g.reduceClipRegion (lid, {});
            g.setColour (skin.withAlpha (1.0f));
            g.fillPath (cover);
            // The lid's edge: a line that sags a little, like a closed eye at the end of the blink
            juce::Path edge;
            const float sag = e.ry * 0.25f * amount;
            edge.startNewSubPath (oval.getX(), lidY - sag * 0.6f);
            edge.quadraticTo (e.x, lidY + sag, oval.getRight(), lidY - sag * 0.6f);
            g.setColour (juce::Colour (0xff1a1014));
            g.strokePath (edge, juce::PathStrokeType (3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    MascotView::Stickers::Stickers()
    {
        for (int i = 0; i < mascot::numMoods; ++i)
        {
            int size = 0;
            if (const char* data = VocalypseBinaryData::getNamedResource (resourceNames[i], size))
                images[(size_t) i] = juce::ImageFileFormat::loadFrom (data, (size_t) size);
        }
    }

    juce::Image MascotView::sticker (mascot::Mood mood)
    {
        // Cheap while a view holds the set; otherwise decodes once for this call and lets it go again
        const juce::SharedResourcePointer<Stickers> set;
        return set->images[(size_t) mood];
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
        // Fire (Jakob 2026-11-03, was "Plugin-Violett" 2026-10-28): orange panel, gold speed lines from behind
        // him, ink rim, hard pink offset shadow - the colours of the logo in comic form
        const float u = f.getWidth() / 128.0f;   // the design was made at 128 px frame width
        const float rim = std::max (2.0f, 3.5f * u);
        g.setColour (juce::Colour (0xfff0265a));
        g.fillRect (f.translated (5.0f * u, 5.0f * u));

        const auto centre = juce::Point<float> (f.getCentreX(), f.getY() + f.getHeight() * 0.55f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffff9a2e), centre, juce::Colour (0xffc0163e),
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
            g.setColour (juce::Colour (0xd9ffd23a));
            g.fillPath (lines);
            // A calm middle, so the speed lines start around him
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffff9a2e), centre, juce::Colour (0x00ff9a2e),
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

        // He blinks (not in the middle of a cross-fade)
        if (fade <= 0.0f)
            paintBlink (g, sticker (brain.mood()), brain.mood(), brain.blink(),
                        juce::AffineTransform::scale (area.getWidth() / 384.0f, area.getHeight() / 384.0f).translated (area.getX(), area.getY()).followedBy (t));
    }

    void MascotView::mouseUp (const juce::MouseEvent& e)
    {
        if (e.mouseWasClicked() && onClick)
            onClick();
    }
}
