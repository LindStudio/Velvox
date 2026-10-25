#pragma once

#include "Theme.h"
#include "Widgets.h"

// The headphone of the 0.6.1 band solos (EQ band, Saturation band). Like Pro-Q: a click latches the
// solo, a second click ends it; pressing longer than holdMs plays it only while held. A monitoring
// state: the button never touches a parameter or the undo history - its owner sends the request to
// the DSP (an atomic) and shows the DSP's state with setActive().
namespace velvox::ui
{
    // Headphones stroked into `r` (band + two cups); filled cups while active
    inline void drawHeadphones (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool active, float thickness = 1.5f)
    {
        const float w = r.getWidth(), h = r.getHeight();
        juce::Path band;
        band.addCentredArc (r.getCentreX(), r.getY() + h * 0.58f, w * 0.40f, h * 0.48f, 0.0f,
                            -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
        g.setColour (c);
        g.strokePath (band, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const float cupW = w * 0.24f, cupH = h * 0.40f, y = r.getY() + h * 0.56f;
        const juce::Rectangle<float> cups[2] { { r.getX() + w * 0.06f, y, cupW, cupH }, { r.getRight() - w * 0.06f - cupW, y, cupW, cupH } };
        for (const auto& cup : cups)
        {
            if (active)
                g.fillRoundedRectangle (cup, cupW * 0.4f);
            else
                g.drawRoundedRectangle (cup, cupW * 0.4f, thickness);
        }
    }

    // Press / release logic shared by the button and the EQ graph's hot spot
    struct AuditionPress
    {
        static constexpr juce::uint32 holdMs = 350;

        // Returns the state right after the press (an inactive solo turns on at once)
        bool press (bool wasActive, juce::uint32 now) noexcept
        {
            pressedAt = now;
            turnedOn = ! wasActive;
            return true;
        }
        // Returns the state after the release: a click keeps a fresh solo latched, a long press ends it,
        // a click on an active solo ends it
        bool release (juce::uint32 now) const noexcept
        {
            return turnedOn && now - pressedAt < holdMs;
        }

        juce::uint32 pressedAt = 0;
        bool turnedOn = false;
    };

    class AuditionButton final : public juce::Component
    {
    public:
        AuditionButton()
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            setWantsKeyboardFocus (false);
            info = std::make_unique<HoverInfo> (*this, [] { return juce::String ("SOLO"); },
                                                [this] { return active ? juce::String::fromUTF8 ("an") : juce::String::fromUTF8 ("aus"); },
                                                [this] { return description; });
        }

        // The user switched the solo (owner: send it to the DSP)
        std::function<void (bool on)> onChange;
        // Time source (tests replace it)
        std::function<juce::uint32()> clock = [] { return juce::Time::getMillisecondCounter(); };

        void setActive (bool on)
        {
            if (on != active)
            {
                active = on;
                repaint();
                info->refresh();
            }
        }
        bool isActive() const noexcept { return active; }
        void setAccent (juce::Colour c) { accent = c; repaint(); }
        void setDescription (juce::String d) { description = std::move (d); }

        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat().reduced (1.0f);
            const float d = std::min (r.getWidth(), r.getHeight());
            const auto circle = juce::Rectangle<float> (d, d).withCentre (r.getCentre());
            const bool hot = isMouseOverOrDragging();
            if (active)
                drawGlow (g, circle.getCentre(), d * 0.75f, 0.35f, accent);
            g.setColour (active ? accent.withAlpha (0.22f) : colours::raisedHi.withAlpha (hot ? 1.0f : 0.85f));
            g.fillEllipse (circle);
            g.setColour (active ? accent.withAlpha (0.9f) : juce::Colours::white.withAlpha (hot ? 0.35f : 0.16f));
            g.drawEllipse (circle.reduced (0.5f), 1.0f);
            const auto icon = circle.reduced (d * 0.24f);
            drawHeadphones (g, icon, active ? accent.interpolatedWith (juce::Colours::white, 0.35f)
                                            : (hot ? colours::textSoft : colours::textMuted), active, std::max (1.2f, d * 0.055f));
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu())
                return;
            pressing = true;
            gesture.press (active, clock());
            if (! active)
                change (true);
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (! pressing || e.mods.isPopupMenu())
                return;
            pressing = false;
            const bool on = gesture.release (clock());
            if (on != active)
                change (on);
        }

        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }

    private:
        void change (bool on)
        {
            setActive (on);
            if (onChange)
                onChange (on);
        }

        std::unique_ptr<HoverInfo> info;
        AuditionPress gesture;
        juce::Colour accent { colours::teal };
        juce::String description;
        bool active = false, pressing = false;
    };
}
