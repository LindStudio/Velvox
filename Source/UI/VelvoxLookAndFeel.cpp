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
    // Knob of the HEADLINER VOCAL design. Units of a 100 x 100 knob (u = px per unit):
    //   tick ring   57 ticks over 270 deg, r 40.5..47.5, width 1.3; lit up to the value
    //               in the accent colour with a glow, the rest #30343D
    //   shadow      circle r 37, 7 units down, soft
    //   body        r 35, #34373F -> #121418, fine light edge on top
    //   cap         r 26, #17191D -> #2A2D34 (recessed)
    //   pointer     glowing dot r 3.4 on r 27
    // Bipolar parameters light the ticks from the centre detent towards the value.
    void VelvoxLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                              float startAngle, float endAngle, juce::Slider& slider)
    {
        const auto area = juce::Rectangle<int> (x, y, width, height).toFloat();
        const float size = std::min (area.getWidth(), area.getHeight());
        const float u = size / 100.0f;
        const auto centre = area.getCentre();
        const bool enabled = slider.isEnabled();
        const auto& props = slider.getProperties();
        const float shown = (float) (double) props.getWithDefault (animPosProperty, (double) pos);
        const float hover = enabled ? (float) (double) props.getWithDefault (hoverProperty, 0.0) : 0.0f;
        const auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);
        const float span = endAngle - startAngle;
        const float valueAngle = startAngle + shown * span;

        const auto range = slider.getRange();
        const bool bipolar = range.getStart() < 0.0 && range.getEnd() > 0.0;
        const float origin = bipolar ? (float) slider.valueToProportionOfLength (0.0) : 0.0f;
        const float lo = std::min (origin, shown), hi = std::max (origin, shown);

        const auto polar = [&] (float angle, float radius) { return centre.getPointOnCircumference (radius * u, angle); };
        const auto circle = [&] (juce::Point<float> c, float radius) { return juce::Rectangle<float> (radius * 2.0f * u, radius * 2.0f * u).withCentre (c); };

        // 1) Tick ring
        constexpr int numTicks = 57;
        for (int i = 0; i < numTicks; ++i)
        {
            const float t = (float) i / (float) (numTicks - 1);
            const float a = startAngle + t * span;
            const bool lit = enabled && t >= lo - 0.0001f && t <= hi + 0.0001f && hi - lo > 0.0005f;
            const juce::Line<float> tick (polar (a, 40.5f), polar (a, 47.5f));
            if (lit)
            {
                g.setColour (accent.withAlpha (0.22f + 0.1f * hover));
                g.drawLine (tick, 3.4f * u);
            }
            g.setColour (lit ? accent : colours::tickOff);
            g.drawLine (tick, std::max (1.0f, 1.3f * u));
        }

        // 2) Shadow
        {
            const auto sc = centre.translated (0.0f, 7.0f * u);
            juce::ColourGradient shadow (juce::Colours::black.withAlpha (0.85f), sc.x, sc.y,
                                         juce::Colours::transparentBlack, sc.x + 37.0f * u, sc.y, true);
            shadow.addColour (0.5, juce::Colours::black.withAlpha (0.85f));
            g.setGradientFill (shadow);
            g.fillEllipse (circle (sc, 37.0f));
        }

        // 3) Body
        {
            const auto r = circle (centre, 35.0f);
            g.setGradientFill (juce::ColourGradient (colours::knobTop, r.getX(), r.getY(),
                                                     colours::knobBottom, r.getX() + r.getWidth() * 0.3f, r.getBottom(), false));
            g.fillEllipse (r);
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.14f), r.getCentreX(), r.getY(),
                                                     juce::Colours::transparentWhite, r.getCentreX(), r.getCentreY(), false));
            g.drawEllipse (circle (centre, 34.4f), 1.2f * u);
        }

        // 4) Cap (recessed)
        {
            const auto r = circle (centre, 26.0f);
            g.setGradientFill (juce::ColourGradient (colours::capTop, r.getX(), r.getY(),
                                                     colours::capBottom, r.getX() + r.getWidth() * 0.3f, r.getBottom(), false));
            g.fillEllipse (r);
            g.setGradientFill (juce::ColourGradient (juce::Colours::transparentWhite, r.getCentreX(), r.getCentreY(),
                                                     juce::Colours::white.withAlpha (0.14f), r.getCentreX(), r.getBottom(), false));
            g.drawEllipse (circle (centre, 25.4f), 0.8f * u);
        }

        if (hover > 0.01f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.03f * hover));
            g.fillEllipse (circle (centre, 35.0f));
        }

        // 5) Glowing dot
        {
            const auto p = polar (valueAngle, 27.0f);
            const auto dot = enabled ? accent : colours::tickOff;
            for (int i = 3; i >= 1; --i)
            {
                g.setColour (dot.withAlpha (0.10f));
                g.fillEllipse (circle (p, 3.4f + (float) i * 1.7f));
            }
            g.setColour (dot);
            g.fillEllipse (circle (p, 3.4f));
        }
    }

    juce::Label* VelvoxLookAndFeel::createSliderTextBox (juce::Slider& slider)
    {
        // Design: value in Roboto Mono 11.5 px, #E8EBF2 (editable on click)
        auto* l = LookAndFeel_V4::createSliderTextBox (slider);
        l->setHasFocusOutline (false);
        l->setFont (monoFont (11.5f));
        l->setJustificationType (juce::Justification::centredBottom);
        l->setColour (juce::Label::textColourId, colours::text);
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

        // Design switch: track 26 x 14 (accent when on, with a soft halo), white knob 10 px,
        // label to the right in Montserrat 11 px
        const auto accentId = juce::ToggleButton::tickColourId;
        const auto accent = b.isColourSpecified (accentId) ? b.findColour (accentId) : colours::teal;
        const auto bounds = b.getLocalBounds().toFloat();
        const auto track = juce::Rectangle<float> (26.0f, 14.0f).withPosition (bounds.getX() + 1.0f, bounds.getCentreY() - 7.0f);
        if (on)
        {
            g.setColour (accent.withAlpha (0.25f));
            g.fillRoundedRectangle (track.expanded (2.0f), 9.0f);
        }
        g.setColour (on ? accent : colours::switchOff);
        g.fillRoundedRectangle (track, 7.0f);
        const float knobX = on ? track.getRight() - 7.0f : track.getX() + 7.0f;
        g.setColour (b.isEnabled() ? colours::textStrong : colours::textMuted);
        g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ knobX, track.getCentreY() }));

        auto textCol = highlighted ? colours::textStrong : colours::textSoft;
        if (! b.isEnabled())
            textCol = textCol.withMultipliedAlpha (0.5f);
        g.setColour (textCol);
        g.setFont (medium (12.0f));
        g.drawFittedText (b.getButtonText(), bounds.withTrimmedLeft (track.getWidth() + 9.0f).toNearestInt(),
                          juce::Justification::centredLeft, 1, 0.9f);
        juce::ignoreUnused (dot, textArea, corner);
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

        // Design segment (on a #0D0F15 group): active = accent at 16 %, hover = white 3 / 6 %
        if ((bool) b.getProperties().getWithDefault (segmentProperty, false))
        {
            const auto seg = b.getLocalBounds().toFloat();
            if (on)
                g.setColour (b.findColour (juce::TextButton::buttonOnColourId).withAlpha (0.16f));
            else
                g.setColour (juce::Colours::white.withAlpha (down ? 0.06f : (highlighted ? 0.03f : 0.0f)));
            g.fillRoundedRectangle (seg, 5.0f);
            return;
        }

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
        if ((bool) b.getProperties().getWithDefault (segmentProperty, false))
        {
            // Design: active = accent mixed 55 % towards white, inactive #7C859B; SemiBold 11
            auto c = on ? b.findColour (juce::TextButton::buttonOnColourId).interpolatedWith (juce::Colours::white, 0.55f)
                        : (highlighted ? colours::textSoft : juce::Colour (0xff7c859b));
            if (! b.isEnabled())
                c = c.withMultipliedAlpha (0.5f);
            g.setColour (c);
            g.setFont (semiBold (11.0f));
            g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (2, 0), juce::Justification::centred, 1, 0.8f);
            return;
        }
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
