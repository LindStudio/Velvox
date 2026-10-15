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
    // 3D dial. Outside: a recessed groove that carries the aurora value arc.
    // Inside: a raised, machined knob lit from above - drop shadow, bevelled
    // rim with knurling that turns with the value, satin face with a specular
    // highlight, and a glowing LED line as the pointer.
    void VelvoxLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                              float startAngle, float endAngle, juce::Slider& slider)
    {
        using juce::Colour;
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
        const float radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const bool enabled = slider.isEnabled();
        const auto& props = slider.getProperties();
        const bool hero = (bool) props.getWithDefault (heroProperty, false);
        const float shown = (float) (double) props.getWithDefault (animPosProperty, (double) pos);
        const float hover = enabled ? (float) (double) props.getWithDefault (hoverProperty, 0.0) : 0.0f;
        const float a = enabled ? 1.0f : 0.35f;

        const float span = endAngle - startAngle;
        const auto range = slider.getRange();
        const bool bipolar = range.getStart() < 0.0 && range.getEnd() > 0.0;
        const float valueAngle = startAngle + shown * span;
        float fromAngle = startAngle;
        if (bipolar)
            fromAngle = startAngle + (float) slider.valueToProportionOfLength (0.0) * span;
        const float amount = bipolar ? std::abs (valueAngle - fromAngle) / span * 2.0f : shown;

        const float groove = std::max (3.0f, radius * grooveRatio);
        const float grooveR = radius - groove * 0.5f;

        // Ambient aurora light under the dial, brighter the further it is turned
        if (enabled && (hero || hover > 0.01f))
            drawGlow (g, centre, radius * (hero ? 1.6f : 1.3f),
                      (hero ? 0.03f + 0.11f * amount : 0.0f) + 0.05f * hover, aurora (amount * 0.8f));

        // ---- Groove: dark channel, lit lower lip (it is cut into the panel)
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, grooveR, grooveR, 0.0f, startAngle, endAngle, true);
        g.setColour (Colour (0xff040506));
        g.strokePath (track, juce::PathStrokeType (groove, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        juce::Path lip;
        lip.addCentredArc (centre.x, centre.y, radius - 0.5f, radius - 0.5f, 0.0f,
                           juce::MathConstants<float>::pi * 0.55f, juce::MathConstants<float>::pi * 1.45f, true);
        g.setColour (colours::accent.withAlpha (0.06f));
        g.strokePath (lip, juce::PathStrokeType (1.0f));

        // ---- Value arc: aurora light inside the groove
        if (std::abs (valueAngle - fromAngle) > 0.002f)
        {
            const float lo = std::min (fromAngle, valueAngle), hi = std::max (fromAngle, valueAngle);
            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, grooveR, grooveR, 0.0f, lo, hi, true);

            g.setColour (aurora (0.4f, (0.10f + 0.08f * hover) * a));
            g.strokePath (arc, juce::PathStrokeType (groove * 2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            // Colour runs teal -> blue along the scale
            const int segments = std::max (2, (int) ((hi - lo) / 0.06f));
            for (int i = 0; i < segments; ++i)
            {
                const float s0 = lo + (hi - lo) * (float) i / (float) segments;
                const float s1 = lo + (hi - lo) * (float) (i + 1) / (float) segments + 0.004f;
                juce::Path seg;
                seg.addCentredArc (centre.x, centre.y, grooveR, grooveR, 0.0f, s0, std::min (s1, hi), true);
                g.setColour (aurora ((s0 - startAngle) / span, 0.95f * a));
                g.strokePath (seg, juce::PathStrokeType (groove * 0.55f, juce::PathStrokeType::curved,
                                                         i == 0 || i == segments - 1 ? juce::PathStrokeType::rounded : juce::PathStrokeType::butt));
            }

            // Bright head at the value end
            const auto head = centre.getPointOnCircumference (grooveR, valueAngle);
            drawGlow (g, head, groove * 1.6f, 0.55f * a, aurora (shown));
            g.setColour (aurora (shown).interpolatedWith (juce::Colours::white, 0.65f).withAlpha (a));
            g.fillEllipse (juce::Rectangle<float> (groove * 0.5f, groove * 0.5f).withCentre (head));
        }

        // ---- Knob body
        const float bodyR = grooveR - groove * 0.5f - std::max (2.0f, radius * 0.07f);
        const float faceR = bodyR * 0.84f;
        const auto body = juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (centre);
        const auto face = juce::Rectangle<float> (faceR * 2.0f, faceR * 2.0f).withCentre (centre);

        // Contact shadow and cast shadow (light from above)
        drawGlow (g, centre.translated (0.0f, bodyR * 0.28f), bodyR * 1.45f, 0.85f, juce::Colours::black);

        // Rim: bevel, bright on top, dark underneath
        g.setGradientFill (juce::ColourGradient (Colour (0xff4a4e58).brighter (0.15f * hover), centre.x, body.getY(),
                                                 Colour (0xff0a0b0e), centre.x, body.getBottom(), false));
        g.fillEllipse (body);

        // Knurling on the rim, turning with the value
        {
            const int ridges = hero ? 44 : 32;
            const float r0 = faceR + (bodyR - faceR) * 0.2f, r1 = bodyR - (bodyR - faceR) * 0.15f;
            for (int i = 0; i < ridges; ++i)
            {
                const float ang = valueAngle + juce::MathConstants<float>::twoPi * (float) i / (float) ridges;
                const float light = 0.5f + 0.5f * std::cos (ang);   // brighter where the rim faces the light
                g.setColour (colours::accent.withAlpha ((0.025f + 0.11f * light) * a));
                g.drawLine (juce::Line<float> (centre.getPointOnCircumference (r0, ang), centre.getPointOnCircumference (r1, ang)), 1.0f);
            }
        }
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawEllipse (body.reduced (0.3f), 0.8f);

        // Face: satin top, slightly concave (lighter at the bottom edge)
        g.setGradientFill (juce::ColourGradient (Colour (0xff2c2f36).brighter (0.12f * hover), centre.x, face.getY(),
                                                 Colour (0xff15161a), centre.x, face.getBottom(), false));
        g.fillEllipse (face);
        {
            juce::ColourGradient spec (colours::accent.withAlpha (0.13f + 0.04f * hover), centre.x - faceR * 0.25f, centre.y - faceR * 0.55f,
                                       colours::accent.withAlpha (0.0f), centre.x - faceR * 0.25f + faceR * 0.95f, centre.y - faceR * 0.55f, true);
            g.setGradientFill (spec);
            g.fillEllipse (face);
        }
        // Rim light on the face edge (top) and its shadow side (bottom)
        juce::Path topEdge, bottomEdge;
        topEdge.addCentredArc (centre.x, centre.y, faceR - 0.5f, faceR - 0.5f, 0.0f, -1.2f, 1.2f, true);
        bottomEdge.addCentredArc (centre.x, centre.y, faceR - 0.5f, faceR - 0.5f, 0.0f, juce::MathConstants<float>::pi - 1.0f,
                                  juce::MathConstants<float>::pi + 1.0f, true);
        g.setColour (colours::accent.withAlpha (0.22f));
        g.strokePath (topEdge, juce::PathStrokeType (1.0f));
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.strokePath (bottomEdge, juce::PathStrokeType (1.0f));

        // ---- Pointer: a glowing LED line set into the face
        const auto inner = centre.getPointOnCircumference (faceR * 0.36f, valueAngle);
        const auto outer = centre.getPointOnCircumference (faceR * 0.80f, valueAngle);
        const float lineW = std::max (2.0f, faceR * 0.085f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawLine ({ inner.translated (0.0f, 0.8f), outer.translated (0.0f, 0.8f) }, lineW + 1.5f);
        if (enabled)
        {
            g.setColour (aurora (shown, 0.30f + 0.15f * hover));
            g.drawLine ({ inner, outer }, lineW * 3.2f);
        }
        g.setColour (enabled ? aurora (shown).interpolatedWith (juce::Colours::white, 0.55f) : colours::textFaint);
        g.drawLine ({ inner, outer }, lineW);
    }

    juce::Label* VelvoxLookAndFeel::createSliderTextBox (juce::Slider& slider)
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (slider);
        l->setHasFocusOutline (false);
        l->setFont (medium (12.5f));
        l->setColour (juce::Label::textColourId, colours::text.withAlpha (0.78f));
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

        if (on)
        {
            g.setGradientFill (auroraGradient (r.getTopLeft(), r.getTopRight(), 0.10f));
            g.fillRoundedRectangle (r, corner);
        }
        else if (highlighted)
        {
            g.setColour (colours::accent.withAlpha (0.03f));
            g.fillRoundedRectangle (r, corner);
        }
        g.setColour (on ? aurora (0.3f, 0.45f) : (highlighted ? colours::line.brighter (0.4f) : colours::line));
        g.drawRoundedRectangle (r, corner, 1.0f);

        const float dot = std::max (5.0f, r.getHeight() * 0.22f);
        const auto dc = juce::Point<float> (r.getX() + corner * 1.05f, r.getCentreY());
        if (on)
            drawGlow (g, dc, dot * 2.6f, 0.55f, colours::teal);
        g.setColour (on ? colours::teal.interpolatedWith (juce::Colours::white, 0.35f) : colours::textFaint);
        g.fillEllipse (juce::Rectangle<float> (dot, dot).withCentre (dc));

        g.setColour (on ? colours::text : colours::textDim);
        g.setFont (medium (std::min (12.5f, r.getHeight() * 0.48f)));
        g.drawText (b.getButtonText(), r.withTrimmedLeft (corner * 1.7f).withTrimmedRight (corner * 0.7f),
                    juce::Justification::centred, false);
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

        if (on)
        {
            g.setGradientFill (auroraGradient (r.getTopLeft(), r.getBottomRight(), down ? 0.24f : 0.15f));
            g.fillPath (shape);
            g.setColour (aurora (0.4f, 0.45f));
        }
        else
        {
            auto fill = background;
            if (down)
                fill = colours::accent.withAlpha (0.08f);
            else if (highlighted)
                fill = colours::accent.withAlpha (0.045f);
            g.setColour (fill);
            g.fillPath (shape);
            g.setColour (colours::line);
        }
        g.strokePath (shape, juce::PathStrokeType (1.0f));
    }

    void VelvoxLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool highlighted, bool)
    {
        const bool on = b.getToggleState();
        auto c = b.findColour (on ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId);
        if (highlighted && ! on)
            c = colours::text.withAlpha (0.9f);
        if (! b.isEnabled())
            c = colours::textFaint;
        g.setColour (c);
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.drawText (b.getButtonText(), b.getLocalBounds().reduced (6, 0), juce::Justification::centred, false);
    }

    juce::Font VelvoxLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
    {
        return medium (std::min (13.0f, (float) buttonHeight * 0.45f));
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

        const float ax = r.getRight() - 13.0f, ay = r.getCentreY();
        juce::Path arrow;
        arrow.startNewSubPath (ax - 3.5f, ay - 1.75f);
        arrow.lineTo (ax, ay + 1.75f);
        arrow.lineTo (ax + 3.5f, ay - 1.75f);
        g.setColour (colours::textDim);
        g.strokePath (arrow, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    juce::Font VelvoxLookAndFeel::getComboBoxFont (juce::ComboBox& box)
    {
        return medium (std::min (12.5f, (float) box.getHeight() * 0.5f));
    }

    void VelvoxLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
    {
        label.setBounds (8, 1, box.getWidth() - 26, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }

    juce::Font VelvoxLookAndFeel::getPopupMenuFont()
    {
        return medium (13.5f);
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
                     juce::Justification::centredLeft, colours::textFaint, 9.5f);
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
        s.append (text, font (12.5f), colours::text.withAlpha (0.9f));
        juce::TextLayout layout;
        layout.createLayoutWithBalancedLineLengths (s, (float) width - 20.0f);
        layout.draw (g, r.reduced (10.0f, 7.0f));
    }

    juce::Rectangle<int> VelvoxLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                                              juce::Rectangle<int> parentArea)
    {
        juce::AttributedString s;
        s.append (tipText, font (12.5f));
        juce::TextLayout layout;
        layout.createLayoutWithBalancedLineLengths (s, 300.0f);
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
