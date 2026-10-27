#include "MascotView.h"
#include "../Text/I18n.h"

namespace velvox::ui
{
    MascotView::MascotView()
        : library (mascot::ClipLibrary::get()), brain (library.infos())
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip (i18n::tr ("Klick mich: Vocal Check", "Click me: Vocal Check"));
    }

    void MascotView::tick (const mascot::Inputs& in, double dt)
    {
        brain.update (in, dt);
        // Frames change at the clip's 30 fps; the cross-fade needs every editor frame
        if (brain.frame() != lastFrame || brain.clip() != lastClip || brain.mouth() != lastMouth || brain.fadeFrom().has_value())
        {
            lastFrame = brain.frame();
            lastClip = brain.clip();
            lastMouth = brain.mouth();
            repaint();
        }
    }

    const juce::Image& MascotView::frameImage (Cached& slot, mascot::Clip clip, int frame)
    {
        if (slot.clip != clip || slot.frame != frame)
        {
            slot = { clip, frame, library.clip (clip).decodeBase (frame) };
        }
        return slot.image;
    }

    juce::Rectangle<float> MascotView::getStandArea() const
    {
        return standArea.isEmpty() ? getLocalBounds().toFloat() : standArea;
    }

    juce::Rectangle<float> MascotView::frameArea() const
    {
        const auto& clip = library.clip (brain.clip());
        const auto size = clip.size();
        const auto standing = clip.standingBox().toFloat();
        if (size.x <= 0 || size.y <= 0 || standing.isEmpty())
            return {};
        const auto stand = getStandArea();
        const float scale = std::min (stand.getWidth() / standing.getWidth(), stand.getHeight() / standing.getHeight());
        return { stand.getCentreX() - standing.getCentreX() * scale, stand.getBottom() - standing.getBottom() * scale,
                 size.x * scale, size.y * scale };
    }

    bool MascotView::hitTest (int x, int y)
    {
        const auto area = frameArea();
        const auto& img = frameImage (current, brain.clip(), brain.frame());
        if (area.isEmpty() || ! img.isValid() || ! area.contains ((float) x, (float) y))
            return false;
        const int px = (int) ((x - area.getX()) / area.getWidth() * img.getWidth());
        const int py = (int) ((y - area.getY()) / area.getHeight() * img.getHeight());
        return img.getPixelAt (juce::jlimit (0, img.getWidth() - 1, px), juce::jlimit (0, img.getHeight() - 1, py)).getAlpha() > 80;
    }

    void MascotView::paint (juce::Graphics& g)
    {
        const auto area = frameArea();
        if (area.isEmpty())
            return;
        const auto size = library.clip (brain.clip()).size();
        const float scale = area.getWidth() / (float) size.x;
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

        const auto& img = frameImage (current, brain.clip(), brain.frame());
        if (img.isValid())
            g.drawImage (img, area);

        // The mouth patch of this frame (decoded once per frame and mouth)
        if (brain.mouth() != mascot::Mouth::closed)
        {
            if (mouthClip != brain.clip() || mouthFrame != brain.frame() || mouthKind != brain.mouth())
            {
                mouthPatch = library.clip (brain.clip()).patch (brain.frame(), brain.mouth());
                mouthClip = brain.clip();
                mouthFrame = brain.frame();
                mouthKind = brain.mouth();
            }
            if (mouthPatch.image.isValid())
                g.drawImage (mouthPatch.image,
                             juce::Rectangle<float> (area.getX() + mouthPatch.position.x * scale, area.getY() + mouthPatch.position.y * scale,
                                                     mouthPatch.image.getWidth() * scale, mouthPatch.image.getHeight() * scale));
        }

        // The old clip fades out on top (its last frame, mouth closed)
        if (const auto from = brain.fadeFrom())
        {
            const auto& old = frameImage (fading, from->clip, from->frame);
            if (old.isValid())
            {
                g.setOpacity (brain.fadeAmount());
                g.drawImage (old, area);
                g.setOpacity (1.0f);
            }
        }
    }

    void MascotView::mouseUp (const juce::MouseEvent& e)
    {
        if (e.mouseWasClicked() && onClick)
            onClick();
    }
}
