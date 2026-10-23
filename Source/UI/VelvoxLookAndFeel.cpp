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
            d.capR = d.bodyR * 0.88f;
            return d;
        }

        // The matte knob body: cast shadow, a dark rim with a thin bevel that
        // catches the light on top, and a smooth cap that is a shade lighter at
        // the upper left. Independent of the value, so it is drawn once per size.
        void paintDialBody (juce::Graphics& g, juce::Point<float> c, const DialGeometry& d)
        {
            using juce::Colour;
            const float bodyR = d.bodyR, capR = d.capR;
            const auto body = juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (c);
            const auto cap = juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (c);

            // Cast shadow (light from above) and tight contact shadow
            drawGlow (g, c.translated (0.0f, bodyR * 0.25f), bodyR * 1.3f, 0.8f, juce::Colours::black);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.fillEllipse (body.expanded (1.0f).translated (0.0f, 1.5f));

            // Rim: near-black side wall, a little lighter on top
            g.setGradientFill (juce::ColourGradient (Colour (0xff2a3346), c.x, body.getY(),
                                                     Colour (0xff05070c), c.x, body.getBottom(), false));
            g.fillEllipse (body);
            g.setColour (colours::accent.withAlpha (0.10f));
            g.drawEllipse (body.reduced (0.5f), 1.0f);

            // Cap: smooth dark face with a soft light from the upper left
            g.setGradientFill (juce::ColourGradient (Colour (0xff1e2638), c.x - capR * 0.5f, cap.getY(),
                                                     Colour (0xff0a0e17), c.x + capR * 0.4f, cap.getBottom(), false));
            g.fillEllipse (cap);
            juce::ColourGradient sheen (colours::accent.withAlpha (0.07f), c.x - capR * 0.35f, c.y - capR * 0.55f,
                                        colours::accent.withAlpha (0.0f), c.x - capR * 0.35f + capR * 1.2f, c.y - capR * 0.55f, true);
            g.setGradientFill (sheen);
            g.fillEllipse (cap);

            // Edge light on top of the cap, edge shadow below
            juce::Path topEdge, bottomEdge;
            topEdge.addCentredArc (c.x, c.y, capR - 0.5f, capR - 0.5f, 0.0f, -1.4f, 1.4f, true);
            bottomEdge.addCentredArc (c.x, c.y, capR - 0.5f, capR - 0.5f, 0.0f, juce::MathConstants<float>::pi - 1.1f,
                                      juce::MathConstants<float>::pi + 1.1f, true);
            g.setColour (colours::accent.withAlpha (0.16f));
            g.strokePath (topEdge, juce::PathStrokeType (1.0f));
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.strokePath (bottomEdge, juce::PathStrokeType (1.2f));
        }
    }

    const juce::Image& VelvoxLookAndFeel::dialBody (float radius, float physicalScale)
    {
        // Rendered at the physical pixel size so it stays sharp at any window size / DPI
        const int px = juce::jlimit (16, 1024, juce::roundToInt (radius * 2.0f * physicalScale));
        if (auto it = bodyCache.find (px); it != bodyCache.end())
            return it->second;

        if (bodyCache.size() > 48)
            bodyCache.clear();

        juce::Image img (juce::Image::ARGB, px, px, true);
        {
            juce::Graphics gi (img);
            const float s = (float) px / (radius * 2.0f);
            gi.addTransform (juce::AffineTransform::scale (s));
            paintDialBody (gi, { radius, radius }, dialGeometry (radius));
        }
        return bodyCache.emplace (px, std::move (img)).first->second;
    }

    //==========================================================================
    // Neon dial (style guide: "Regler, modern, glow"). Outside: a dark track
    // ring carrying the value arc, lit cyan -> blue -> violet along the travel
    // and glowing into the panel. Inside: the matte knob (cached) with a white
    // pointer line.
    void VelvoxLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                              float startAngle, float endAngle, juce::Slider& slider)
    {
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

        // ---- Track: a thin dark ring
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, d.grooveR, d.grooveR, 0.0f, startAngle, endAngle, true);
        g.setColour (juce::Colour (0xff070a12));
        g.strokePath (track, juce::PathStrokeType (d.groove, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (colours::line.withAlpha (0.55f));
        g.strokePath (track, juce::PathStrokeType (d.groove * 0.35f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Centre detent mark for bipolar controls
        if (bipolar)
        {
            const auto p0 = centre.getPointOnCircumference (d.radius + 1.0f, fromAngle);
            const auto p1 = centre.getPointOnCircumference (d.radius - d.groove - 1.0f, fromAngle);
            g.setColour (colours::accent.withAlpha (0.3f * a));
            g.drawLine ({ p0, p1 }, 1.0f);
        }

        // ---- Value arc: neon light with a wide soft glow underneath
        if (std::abs (valueAngle - fromAngle) > 0.002f)
        {
            const float lo = std::min (fromAngle, valueAngle), hi = std::max (fromAngle, valueAngle);
            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, d.grooveR, d.grooveR, 0.0f, lo, hi, true);
            const auto from = centre.getPointOnCircumference (d.grooveR, startAngle);
            const auto to = centre.getPointOnCircumference (d.grooveR, endAngle);
            const float glow = enabled ? 1.0f + 0.5f * hover : 0.0f;
            if (glow > 0.0f)
            {
                g.setGradientFill (auroraGradient (from, to, 0.07f * glow));
                g.strokePath (arc, juce::PathStrokeType (d.groove * 4.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setGradientFill (auroraGradient (from, to, 0.16f * glow));
                g.strokePath (arc, juce::PathStrokeType (d.groove * 2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            g.setGradientFill (auroraGradient (from, to, a));
            g.strokePath (arc, juce::PathStrokeType (d.groove * 0.9f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            // Hot core of the light
            g.setColour (colours::accent.withAlpha (0.35f * a));
            g.strokePath (arc, juce::PathStrokeType (std::max (0.8f, d.groove * 0.22f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // ---- Knob body (cached)
        {
            const float physical = g.getInternalContext().getPhysicalPixelScaleFactor();
            const auto& img = dialBody (radius, physical);
            const juce::Graphics::ScopedSaveState s (g);
            g.setOpacity (enabled ? 1.0f : 0.55f);
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (img, juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));
        }

        // Hover: a faint coloured light on the cap
        if (hover > 0.01f)
        {
            g.setColour (aurora (shown, 0.07f * hover));
            g.fillEllipse (juce::Rectangle<float> (d.capR * 2.0f, d.capR * 2.0f).withCentre (centre));
        }

        // ---- Pointer: a white line from inside the cap to its edge
        const auto inner = centre.getPointOnCircumference (d.capR * 0.42f, valueAngle);
        const auto outer = centre.getPointOnCircumference (d.capR * 0.92f, valueAngle);
        const float lineW = std::max (1.8f, d.capR * 0.085f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawLine ({ inner.translated (0.0f, 1.0f), outer.translated (0.0f, 1.0f) }, lineW + 1.2f);
        if (enabled)
        {
            g.setColour (colours::accent.withAlpha (0.12f + 0.1f * hover));
            g.drawLine ({ inner, outer }, lineW * 2.6f);
        }
        g.setColour (enabled ? colours::accent.withAlpha (0.95f) : colours::textFaint);
        g.drawLine ({ inner, outer }, lineW);
    }

    juce::Label* VelvoxLookAndFeel::createSliderTextBox (juce::Slider& slider)
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (slider);
        const bool strong = (bool) slider.getProperties().getWithDefault (strongProperty, false);
        l->setHasFocusOutline (false);
        l->setFont (display (strong ? typesize::value + 3.0f : typesize::value + 1.0f));
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
            juce::Path shape;
            shape.addRoundedRectangle (r, corner);
            drawNeonFill (g, shape, r, 1.0f);
        }
        else
        {
            g.setColour (colours::accent.withAlpha (highlighted ? 0.05f : 0.02f));
            g.fillRoundedRectangle (r, corner);
            g.setColour (highlighted ? colours::line.brighter (0.4f) : colours::line);
            g.drawRoundedRectangle (r, corner, 1.0f);
        }

        if (on)
            drawGlow (g, dc, dot * 2.2f, 0.4f, colours::teal);
        g.setColour (on ? colours::teal.interpolatedWith (juce::Colours::white, 0.35f) : colours::textFaint);
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
            // Primary action ("Get Headliner"): a solid blue -> violet pill with a glow
            const float a = b.isEnabled() ? 1.0f : 0.35f;
            if (b.isEnabled())
            {
                g.setColour (colours::violet.withAlpha (highlighted ? 0.22f : 0.14f));
                g.fillRoundedRectangle (r.expanded (2.5f), corner + 2.5f);
            }
            juce::ColourGradient fill (colours::blue.withAlpha (a), r.getX(), r.getY(),
                                       colours::violet.withAlpha (a), r.getRight(), r.getY(), false);
            g.setGradientFill (fill);
            g.fillPath (shape);
            g.setGradientFill (juce::ColourGradient (colours::accent.withAlpha ((down ? 0.0f : (highlighted ? 0.2f : 0.12f)) * a), r.getX(), r.getY(),
                                                     colours::accent.withAlpha (0.0f), r.getX(), r.getCentreY(), false));
            g.fillPath (shape);
            g.setColour (colours::accent.withAlpha (0.25f * a));
            g.strokePath (shape, juce::PathStrokeType (1.0f));
            return;
        }

        if (on)
        {
            drawNeonFill (g, shape, r, down ? 1.3f : 1.0f);
            return;
        }

        auto fill = background.isTransparent() ? colours::accent.withAlpha (0.02f) : background;
        if (down)
            fill = colours::accent.withAlpha (0.08f);
        else if (highlighted)
            fill = colours::accent.withAlpha (0.05f);
        g.setColour (fill);
        g.fillPath (shape);
        g.setColour (highlighted ? colours::line.brighter (0.4f) : colours::line);
        g.strokePath (shape, juce::PathStrokeType (1.0f));
    }

    // Active state of the style guide ("PUNCH"): indigo body, a blue -> violet
    // border and a faint glow around it
    void VelvoxLookAndFeel::drawNeonFill (juce::Graphics& g, const juce::Path& shape, juce::Rectangle<float> r, float strength)
    {
        g.setColour (colours::blue.withAlpha (0.10f * strength));
        g.strokePath (shape, juce::PathStrokeType (5.0f));
        g.setColour (colours::blue.withAlpha (0.22f * strength));
        g.fillPath (shape);
        g.setGradientFill (juce::ColourGradient (colours::accent.withAlpha (0.06f), r.getX(), r.getY(),
                                                 colours::accent.withAlpha (0.0f), r.getX(), r.getBottom(), false));
        g.fillPath (shape);
        juce::ColourGradient rim (colours::blue.brighter (0.25f), r.getX(), r.getY(),
                                  colours::violet.brighter (0.15f), r.getRight(), r.getBottom(), false);
        g.setGradientFill (rim);
        g.strokePath (shape, juce::PathStrokeType (1.3f));
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
