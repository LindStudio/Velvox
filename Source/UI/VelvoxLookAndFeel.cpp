#include "VelvoxLookAndFeel.h"

namespace velvox::ui
{
    VelvoxLookAndFeel::VelvoxLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, colours::background);
        setColour (juce::Label::textColourId, colours::text);
        setColour (juce::Slider::textBoxTextColourId, colours::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, colours::accent.withAlpha (0.2f));
        setColour (juce::TextEditor::backgroundColourId, colours::raised);
        setColour (juce::TextEditor::textColourId, colours::text);
        setColour (juce::TextEditor::outlineColourId, colours::line);
        setColour (juce::TextEditor::focusedOutlineColourId, colours::teal.withAlpha (0.6f));
        setColour (juce::TextEditor::highlightColourId, colours::teal.withAlpha (0.25f));
        setColour (juce::CaretComponent::caretColourId, colours::text);

        setColour (juce::ComboBox::backgroundColourId, colours::raised);
        setColour (juce::ComboBox::textColourId, colours::text);
        setColour (juce::ComboBox::outlineColourId, colours::line);
        setColour (juce::ComboBox::arrowColourId, colours::textDim);

        setColour (juce::PopupMenu::backgroundColourId, colours::raised);
        setColour (juce::PopupMenu::textColourId, colours::text);
        setColour (juce::PopupMenu::headerTextColourId, colours::textFaint);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::teal.withAlpha (0.12f));
        setColour (juce::PopupMenu::highlightedTextColourId, colours::accent);

        setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        setColour (juce::TextButton::buttonOnColourId, colours::accent.withAlpha (0.11f));
        setColour (juce::TextButton::textColourOffId, colours::textDim);
        setColour (juce::TextButton::textColourOnId, colours::text);

        setColour (juce::TooltipWindow::backgroundColourId, colours::raisedHi);
        setColour (juce::TooltipWindow::textColourId, colours::text);
        setColour (juce::TooltipWindow::outlineColourId, colours::line);

        setColour (juce::AlertWindow::backgroundColourId, colours::panel);
        setColour (juce::AlertWindow::textColourId, colours::text);
        setColour (juce::AlertWindow::outlineColourId, colours::line);
    }

    juce::Typeface::Ptr VelvoxLookAndFeel::getTypefaceForFont (const juce::Font& f)
    {
        // Anything left on the default sans (labels, editors) uses Inter too
        if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName() && fonts->regular != nullptr)
            return f.isBold() ? fonts->semibold : fonts->regular;
        return LookAndFeel_V4::getTypefaceForFont (f);
    }

    //==========================================================================
    // Knob geometry shared by the cached metal body and the live overlay
    namespace
    {
        struct DialGeometry
        {
            float radius, groove, grooveR, bodyR, capR;
        };

        DialGeometry dialGeometry (float radius)
        {
            DialGeometry d;
            d.radius = radius;
            d.groove = std::max (3.0f, radius * VelvoxLookAndFeel::grooveRatio);
            d.grooveR = radius - d.groove * 0.5f;
            d.bodyR = d.grooveR - d.groove * 0.5f - std::max (2.5f, radius * 0.075f);
            d.capR = d.bodyR * 0.80f;
            return d;
        }

        // Deterministic noise for the brushing pattern (same look on every redraw)
        float hashNoise (int i)
        {
            auto h = (juce::uint32) i * 2654435761u;
            h ^= h >> 15;
            h *= 2246822519u;
            h ^= h >> 13;
            return (float) (h & 0xffff) / 65535.0f;
        }

        // The machined metal body: drop shadow, skirt with fine knurling and a
        // chamfer, and a cap of dark brushed metal (concentric brushing plus the
        // two opposed sheen lobes that circular-brushed metal shows under a
        // single light). Independent of the value, so it is drawn once per size.
        void paintMetalBody (juce::Graphics& g, juce::Point<float> c, const DialGeometry& d)
        {
            using juce::Colour;
            const float bodyR = d.bodyR, capR = d.capR;
            const auto body = juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (c);
            const auto cap = juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (c);

            // Cast shadow (light from above) and tight contact shadow
            drawGlow (g, c.translated (0.0f, bodyR * 0.22f), bodyR * 1.28f, 0.75f, juce::Colours::black);
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillEllipse (body.expanded (1.2f).translated (0.0f, 1.2f));

            // Skirt: bevelled side wall, lit on top, falling into shadow below
            g.setGradientFill (juce::ColourGradient (Colour (0xff6a707b), c.x, body.getY(),
                                                     Colour (0xff0b0d10), c.x, body.getBottom(), false));
            g.fillEllipse (body);

            // Fine knurling on the skirt, brighter where it faces the light
            const int ridges = juce::jlimit (36, 90, (int) (bodyR * 1.6f));
            const float r0 = capR + (bodyR - capR) * 0.18f, r1 = bodyR - 0.8f;
            for (int i = 0; i < ridges; ++i)
            {
                const float ang = juce::MathConstants<float>::twoPi * (float) i / (float) ridges;
                const float light = 0.5f + 0.5f * std::cos (ang);
                g.setColour (juce::Colours::black.withAlpha (0.35f));
                g.drawLine (juce::Line<float> (c.getPointOnCircumference (r0, ang + 0.012f), c.getPointOnCircumference (r1, ang + 0.012f)), 0.9f);
                g.setColour (colours::accent.withAlpha (0.03f + 0.14f * light * light));
                g.drawLine (juce::Line<float> (c.getPointOnCircumference (r0, ang), c.getPointOnCircumference (r1, ang)), 0.7f);
            }
            g.setColour (juce::Colours::black.withAlpha (0.7f));
            g.drawEllipse (body.reduced (0.4f), 0.9f);

            // Chamfer between skirt and cap: a bright ring on top, dark below
            const auto chamfer = cap.expanded (std::max (1.2f, bodyR * 0.035f));
            g.setGradientFill (juce::ColourGradient (Colour (0xffa9b0bb), c.x, chamfer.getY(),
                                                     Colour (0xff15181c), c.x, chamfer.getBottom(), false));
            g.fillEllipse (chamfer);

            // Cap: dark metal base
            g.setGradientFill (juce::ColourGradient (Colour (0xff3a3f47), c.x, cap.getY(),
                                                     Colour (0xff1c1f24), c.x, cap.getBottom(), false));
            g.fillEllipse (cap);

            // Concentric brushing: many fine rings with slightly varying brightness
            const int rings = juce::jlimit (24, 140, (int) (capR * 1.4f));
            for (int i = 0; i < rings; ++i)
            {
                const float rr = capR * (float) (i + 1) / (float) rings;
                const float n = hashNoise (i) - 0.5f;
                g.setColour ((n > 0.0f ? juce::Colours::white : juce::Colours::black).withAlpha (std::abs (n) * 0.045f));
                g.drawEllipse (juce::Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (c), 0.6f);
            }

            // Anisotropic sheen: two opposed bright lobes (light from the upper left)
            {
                juce::Graphics::ScopedSaveState s (g);
                juce::Path clip;
                clip.addEllipse (cap);
                g.reduceClipRegion (clip);
                constexpr int wedges = 96;
                const float lightAngle = -0.65f;   // radians, clockwise from 12 o'clock
                for (int i = 0; i < wedges; ++i)
                {
                    const float a0 = juce::MathConstants<float>::twoPi * (float) i / (float) wedges;
                    const float a1 = a0 + juce::MathConstants<float>::twoPi / (float) wedges + 0.004f;
                    const float mid = (a0 + a1) * 0.5f;
                    const float lobe = std::pow (std::abs (std::cos (mid - lightAngle)), 7.0f);
                    const float primaryLobe = std::cos (mid - lightAngle) > 0.0f ? 1.0f : 0.55f;
                    const float alpha = 0.20f * lobe * primaryLobe;
                    if (alpha < 0.004f)
                        continue;
                    juce::Path wedge;
                    wedge.addPieSegment (cap, a0, a1, 0.0f);
                    g.setColour (juce::Colours::white.withAlpha (alpha));
                    g.fillPath (wedge);
                }
            }

            // Soft dome highlight and the cap's edge light / edge shadow
            {
                juce::ColourGradient spec (colours::accent.withAlpha (0.10f), c.x - capR * 0.3f, c.y - capR * 0.5f,
                                           colours::accent.withAlpha (0.0f), c.x - capR * 0.3f + capR * 1.1f, c.y - capR * 0.5f, true);
                g.setGradientFill (spec);
                g.fillEllipse (cap);
            }
            juce::Path topEdge, bottomEdge;
            topEdge.addCentredArc (c.x, c.y, capR - 0.5f, capR - 0.5f, 0.0f, -1.3f, 1.3f, true);
            bottomEdge.addCentredArc (c.x, c.y, capR - 0.5f, capR - 0.5f, 0.0f, juce::MathConstants<float>::pi - 1.1f,
                                      juce::MathConstants<float>::pi + 1.1f, true);
            g.setColour (colours::accent.withAlpha (0.28f));
            g.strokePath (topEdge, juce::PathStrokeType (1.0f));
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.strokePath (bottomEdge, juce::PathStrokeType (1.2f));
        }
    }

    const juce::Image& VelvoxLookAndFeel::metalBody (float radius, float physicalScale)
    {
        // Rendered at the physical pixel size so it stays sharp at any window size / DPI
        const int px = juce::jlimit (16, 1024, juce::roundToInt (radius * 2.0f * physicalScale));
        if (auto it = metalCache.find (px); it != metalCache.end())
            return it->second;

        if (metalCache.size() > 48)
            metalCache.clear();

        juce::Image img (juce::Image::ARGB, px, px, true);
        {
            juce::Graphics gi (img);
            const float s = (float) px / (radius * 2.0f);
            gi.addTransform (juce::AffineTransform::scale (s));
            paintMetalBody (gi, { radius, radius }, dialGeometry (radius));
        }
        return metalCache.emplace (px, std::move (img)).first->second;
    }

    //==========================================================================
    // 3D metal dial. Outside: a recessed groove carrying the cyan value arc.
    // Inside: the machined, brushed-metal knob (cached) with a cyan pointer
    // line set into the cap.
    void VelvoxLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                              float startAngle, float endAngle, juce::Slider& slider)
    {
        using juce::Colour;
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
        const float radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const bool enabled = slider.isEnabled();
        const auto& props = slider.getProperties();
        const float shown = (float) (double) props.getWithDefault (animPosProperty, (double) pos);
        const float hover = enabled ? (float) (double) props.getWithDefault (hoverProperty, 0.0) : 0.0f;
        const float a = enabled ? 1.0f : 0.35f;
        const auto d = dialGeometry (radius);

        const float span = endAngle - startAngle;
        const auto range = slider.getRange();
        const bool bipolar = range.getStart() < 0.0 && range.getEnd() > 0.0;
        const float valueAngle = startAngle + shown * span;
        float fromAngle = startAngle;
        if (bipolar)
            fromAngle = startAngle + (float) slider.valueToProportionOfLength (0.0) * span;

        // ---- Groove: dark channel cut into the panel, lit lower lip
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, d.grooveR, d.grooveR, 0.0f, startAngle, endAngle, true);
        g.setColour (Colour (0xff06070a));
        g.strokePath (track, juce::PathStrokeType (d.groove, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        juce::Path lip;
        lip.addCentredArc (centre.x, centre.y, d.radius - 0.5f, d.radius - 0.5f, 0.0f,
                           juce::MathConstants<float>::pi * 0.55f, juce::MathConstants<float>::pi * 1.45f, true);
        g.setColour (colours::accent.withAlpha (0.07f));
        g.strokePath (lip, juce::PathStrokeType (1.0f));

        // Centre detent mark for bipolar controls
        if (bipolar)
        {
            const auto p0 = centre.getPointOnCircumference (d.radius + 1.0f, fromAngle);
            const auto p1 = centre.getPointOnCircumference (d.radius - d.groove - 1.0f, fromAngle);
            g.setColour (colours::accent.withAlpha (0.25f * a));
            g.drawLine ({ p0, p1 }, 1.0f);
        }

        // ---- Value arc: cyan light inside the groove
        if (std::abs (valueAngle - fromAngle) > 0.002f)
        {
            const float lo = std::min (fromAngle, valueAngle), hi = std::max (fromAngle, valueAngle);
            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, d.grooveR, d.grooveR, 0.0f, lo, hi, true);
            // Light runs cyan -> blue -> violet along the travel of the dial
            const auto from = centre.getPointOnCircumference (d.grooveR, startAngle);
            const auto to = centre.getPointOnCircumference (d.grooveR, endAngle);
            g.setGradientFill (auroraGradient (from, to, (0.13f + 0.09f * hover) * a));
            g.strokePath (arc, juce::PathStrokeType (d.groove * 1.9f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setGradientFill (auroraGradient (from, to, 0.97f * a));
            g.strokePath (arc, juce::PathStrokeType (d.groove * 0.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // ---- Knob body (cached brushed metal)
        {
            const float physical = g.getInternalContext().getPhysicalPixelScaleFactor();
            const auto& img = metalBody (radius, physical);
            const juce::Graphics::ScopedSaveState s (g);
            g.setOpacity (enabled ? 1.0f : 0.55f);
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (img, juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));
        }

        // Hover: the cap catches a little more light
        if (hover > 0.01f)
        {
            g.setColour (colours::accent.withAlpha (0.05f * hover));
            g.fillEllipse (juce::Rectangle<float> (d.capR * 2.0f, d.capR * 2.0f).withCentre (centre));
        }

        // ---- Pointer: cyan line engraved into the cap, from near the centre to the edge
        const auto inner = centre.getPointOnCircumference (d.capR * 0.34f, valueAngle);
        const auto outer = centre.getPointOnCircumference (d.capR * 0.88f, valueAngle);
        const float lineW = std::max (2.0f, d.capR * 0.10f);
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawLine ({ inner.translated (0.0f, 1.0f), outer.translated (0.0f, 1.0f) }, lineW + 1.6f);
        if (enabled)
        {
            g.setColour (colours::cyan.withAlpha (0.22f + 0.12f * hover));
            g.drawLine ({ inner, outer }, lineW * 2.4f);
        }
        g.setColour (enabled ? colours::cyan.interpolatedWith (juce::Colours::white, 0.25f) : colours::textFaint);
        g.drawLine ({ inner, outer }, lineW);
    }

    juce::Label* VelvoxLookAndFeel::createSliderTextBox (juce::Slider& slider)
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (slider);
        const bool strong = (bool) slider.getProperties().getWithDefault (strongProperty, false);
        l->setHasFocusOutline (false);
        l->setFont (medium (strong ? typesize::value + 1.0f : typesize::value));
        l->setJustificationType (juce::Justification::centredBottom);
        l->setColour (juce::Label::textColourId, strong ? colours::text : colours::text.withAlpha (0.88f));
        l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
        l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        l->setColour (juce::Label::outlineWhenEditingColourId, colours::line);
        l->setColour (juce::Label::backgroundWhenEditingColourId, colours::raised);
        return l;
    }

    //==========================================================================
    // Pill toggle: status dot that lights up, text brightens
    void VelvoxLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
    {
        const bool on = b.getToggleState();
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const float corner = r.getHeight() * 0.5f;
        const float dot = std::max (5.0f, r.getHeight() * 0.22f);
        const auto dc = juce::Point<float> (r.getX() + corner * 1.05f, r.getCentreY());
        const auto textArea = r.withTrimmedLeft (corner * 1.7f).withTrimmedRight (corner * 0.7f);

        // The global bypass switch turns amber while engaged - a state you must not miss
        if (on && (bool) b.getProperties().getWithDefault ("danger", false))
        {
            g.setColour (colours::bypass.withAlpha (0.16f));
            g.fillRoundedRectangle (r, corner);
            g.setColour (colours::bypass.withAlpha (0.75f));
            g.drawRoundedRectangle (r, corner, 1.2f);
            drawGlow (g, dc, dot * 2.6f, 0.6f, colours::bypass);
            g.setColour (colours::bypass);
            g.fillEllipse (juce::Rectangle<float> (dot, dot).withCentre (dc));
            g.setColour (colours::text);
            g.setFont (medium (std::min (14.0f, r.getHeight() * 0.46f)));
            g.drawText (b.getButtonText(), textArea, juce::Justification::centred, false);
            return;
        }

        if (on)
        {
            g.setColour (colours::cyan.withAlpha (0.12f));
            g.fillRoundedRectangle (r, corner);
        }
        else
        {
            g.setColour (colours::accent.withAlpha (highlighted ? 0.05f : 0.025f));
            g.fillRoundedRectangle (r, corner);
        }
        g.setColour (on ? colours::cyan.withAlpha (0.6f) : (highlighted ? colours::line.brighter (0.5f) : colours::line.brighter (0.2f)));
        g.drawRoundedRectangle (r, corner, 1.0f);

        if (on)
            drawGlow (g, dc, dot * 2.2f, 0.35f, colours::cyan);
        g.setColour (on ? colours::cyan.interpolatedWith (juce::Colours::white, 0.3f) : colours::textFaint);
        g.fillEllipse (juce::Rectangle<float> (dot, dot).withCentre (dc));

        g.setColour (on ? colours::text : colours::textDim);
        g.setFont (medium (std::min (14.0f, r.getHeight() * 0.46f)));
        g.drawText (b.getButtonText(), textArea, juce::Justification::centred, false);
    }

    // Text buttons: ghost style. Connected edges form segmented controls.
    void VelvoxLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& background,
                                                  bool highlighted, bool down)
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const float corner = std::min (8.0f, r.getHeight() * 0.5f);
        const bool flatL = b.isConnectedOnLeft(), flatR = b.isConnectedOnRight();
        const bool on = b.getToggleState();

        juce::Path shape;
        shape.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), corner, corner,
                                   ! flatL, ! flatR, ! flatL, ! flatR);

        if ((bool) b.getProperties().getWithDefault ("primary", false))
        {
            // Primary action: a lit glass pill in the aurora colours
            const float a = b.isEnabled() ? 1.0f : 0.35f;
            g.setGradientFill (auroraGradient (r.getTopLeft(), r.getBottomRight(), (down ? 0.62f : (highlighted ? 0.55f : 0.46f)) * a));
            g.fillPath (shape);
            g.setGradientFill (juce::ColourGradient (colours::accent.withAlpha (0.22f * a), r.getX(), r.getY(),
                                                     colours::accent.withAlpha (0.0f), r.getX(), r.getCentreY(), false));
            g.fillPath (shape);
            g.setColour (colours::accent.withAlpha (0.35f * a));
            g.strokePath (shape, juce::PathStrokeType (1.0f));
            return;
        }

        if (on)
        {
            g.setColour (colours::cyan.withAlpha (down ? 0.24f : 0.16f));
            g.fillPath (shape);
            g.setColour (colours::cyan.withAlpha (0.55f));
        }
        else
        {
            auto fill = background.isTransparent() ? colours::accent.withAlpha (0.025f) : background;
            if (down)
                fill = colours::accent.withAlpha (0.09f);
            else if (highlighted)
                fill = colours::accent.withAlpha (0.055f);
            g.setColour (fill);
            g.fillPath (shape);
            g.setColour (highlighted ? colours::line.brighter (0.5f) : colours::line.brighter (0.2f));
        }
        g.strokePath (shape, juce::PathStrokeType (1.0f));
    }

    void VelvoxLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool highlighted, bool)
    {
        const bool on = b.getToggleState();
        auto c = b.findColour (on ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId);
        if (highlighted && ! on)
            c = colours::text.withAlpha (0.9f);
        if ((bool) b.getProperties().getWithDefault ("primary", false))
            c = colours::text;
        if (! b.isEnabled())
            c = colours::textFaint;
        g.setColour (c);
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (8, 0), juce::Justification::centred, 1, 0.9f);
    }

    juce::Font VelvoxLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
    {
        return medium (std::min (14.5f, (float) buttonHeight * 0.44f));
    }

    //==========================================================================
    void VelvoxLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
    {
        auto r = juce::Rectangle<int> (width, height).toFloat().reduced (0.5f);
        const float corner = std::min (8.0f, r.getHeight() * 0.5f);
        const bool hover = box.isMouseOver (true);
        g.setColour (hover ? colours::raisedHi : colours::raised);
        g.fillRoundedRectangle (r, corner);
        g.setColour (box.hasKeyboardFocus (true) || hover ? colours::line.brighter (0.4f) : colours::line);
        g.drawRoundedRectangle (r, corner, 1.0f);

        const float ax = r.getRight() - 15.0f, ay = r.getCentreY();
        juce::Path arrow;
        arrow.startNewSubPath (ax - 4.0f, ay - 2.0f);
        arrow.lineTo (ax, ay + 2.0f);
        arrow.lineTo (ax + 4.0f, ay - 2.0f);
        g.setColour (colours::textDim);
        g.strokePath (arrow, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    juce::Font VelvoxLookAndFeel::getComboBoxFont (juce::ComboBox& box)
    {
        return medium (std::min (14.0f, (float) box.getHeight() * 0.46f));
    }

    void VelvoxLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
    {
        label.setBounds (10, 1, box.getWidth() - 32, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }

    juce::Font VelvoxLookAndFeel::getPopupMenuFont()
    {
        return medium (15.0f);
    }

    void VelvoxLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
    {
        g.fillAll (colours::raised);
        g.setColour (colours::line);
        g.drawRect (0, 0, width, height, 1);
    }

    void VelvoxLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& name)
    {
        drawCaption (g, name, area.toFloat().reduced (14.0f, 0.0f).withTrimmedTop (4.0f),
                     juce::Justification::centredLeft, colours::textDim, 10.5f);
    }

    juce::Font VelvoxLookAndFeel::getLabelFont (juce::Label& label)
    {
        return label.getFont();
    }

    //==========================================================================
    void VelvoxLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
    {
        const auto r = juce::Rectangle<int> (width, height).toFloat();
        g.setColour (colours::raisedHi);
        g.fillRect (r);
        g.setColour (colours::line.brighter (0.2f));
        g.drawRect (r, 1.0f);

        juce::AttributedString s;
        s.setJustification (juce::Justification::centredLeft);
        s.append (text, font (13.5f), colours::text.withAlpha (0.92f));
        juce::TextLayout layout;
        layout.createLayoutWithBalancedLineLengths (s, (float) width - 20.0f);
        layout.draw (g, r.reduced (10.0f, 7.0f));
    }

    juce::Rectangle<int> VelvoxLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                                              juce::Rectangle<int> parentArea)
    {
        juce::AttributedString s;
        s.append (tipText, font (13.5f));
        juce::TextLayout layout;
        layout.createLayoutWithBalancedLineLengths (s, 340.0f);
        const int w = (int) (layout.getWidth() + 22.0f), h = (int) (layout.getHeight() + 16.0f);
        return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                     screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6, w, h)
            .constrainedWithin (parentArea);
    }

    void VelvoxLookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging)
    {
        g.setColour (colours::accent.withAlpha (isMouseOver || isMouseDragging ? 0.35f : 0.12f));
        for (float i = 0.0f; i < 2.0f; i += 1.0f)
        {
            const float o = 4.0f + i * 4.0f;
            g.drawLine ((float) w - o, (float) h - 2.0f, (float) w - 2.0f, (float) h - o, 1.0f);
        }
    }
}
