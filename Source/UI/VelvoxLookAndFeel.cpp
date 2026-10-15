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
        setColour (juce::TextEditor::focusedOutlineColourId, colours::accent.withAlpha (0.5f));
        setColour (juce::TextEditor::highlightColourId, colours::accent.withAlpha (0.2f));
        setColour (juce::CaretComponent::caretColourId, colours::text);

        setColour (juce::ComboBox::backgroundColourId, colours::raised);
        setColour (juce::ComboBox::textColourId, colours::text);
        setColour (juce::ComboBox::outlineColourId, colours::line);
        setColour (juce::ComboBox::arrowColourId, colours::textDim);

        setColour (juce::PopupMenu::backgroundColourId, colours::raised);
        setColour (juce::PopupMenu::textColourId, colours::text);
        setColour (juce::PopupMenu::headerTextColourId, colours::textFaint);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::accent.withAlpha (0.08f));
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
    // Dial: hairline track, white value arc with a soft glow, and a dark cap lit
    // from above with a small indicator - no colour, light does the work.
    void VelvoxLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                              float startAngle, float endAngle, juce::Slider& slider)
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
        const float radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const bool enabled = slider.isEnabled();
        const bool hero = (bool) slider.getProperties().getWithDefault (heroProperty, false);
        const bool hover = slider.isMouseOverOrDragging() && enabled;

        const float trackWidth = std::max (2.0f, radius * (hero ? 0.055f : 0.075f));
        const float arcRadius = radius - trackWidth * 0.5f;

        const auto range = slider.getRange();
        const bool bipolar = range.getStart() < 0.0 && range.getEnd() > 0.0;
        const float valueAngle = startAngle + pos * (endAngle - startAngle);
        float fromAngle = startAngle;
        if (bipolar)
            fromAngle = startAngle + (float) slider.valueToProportionOfLength (0.0) * (endAngle - startAngle);
        const float amount = bipolar ? std::abs (valueAngle - fromAngle) / (endAngle - startAngle) * 2.0f : pos;

        // Ambient light under the dial, stronger the further it is turned
        if (hero && enabled)
            drawGlow (g, centre, radius * 1.55f, 0.025f + 0.075f * amount);

        // Track
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
        g.setColour (colours::accent.withAlpha (0.07f));
        g.strokePath (track, juce::PathStrokeType (trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Value arc with glow
        if (std::abs (valueAngle - fromAngle) > 0.001f)
        {
            juce::Path value;
            value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                 std::min (fromAngle, valueAngle), std::max (fromAngle, valueAngle), true);
            const float a = enabled ? 1.0f : 0.35f;
            g.setColour (colours::accent.withAlpha (0.05f * a));
            g.strokePath (value, juce::PathStrokeType (trackWidth * 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (colours::accent.withAlpha (0.12f * a));
            g.strokePath (value, juce::PathStrokeType (trackWidth * 2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (colours::accent.withAlpha (0.95f * a));
            g.strokePath (value, juce::PathStrokeType (trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Cap: drop shadow, body lit from the top, rim highlight
        const float capRadius = arcRadius - trackWidth * (hero ? 3.2f : 2.6f);
        const auto cap = juce::Rectangle<float> (capRadius * 2.0f, capRadius * 2.0f).withCentre (centre);

        drawGlow (g, centre.translated (0.0f, capRadius * 0.18f), capRadius * 1.25f, 0.55f, juce::Colours::black);

        g.setGradientFill (juce::ColourGradient (hover ? juce::Colour (0xff2a2c32) : juce::Colour (0xff232429), centre.x, cap.getY(),
                                                 juce::Colour (0xff0c0d10), centre.x, cap.getBottom(), false));
        g.fillEllipse (cap);
        g.setGradientFill (juce::ColourGradient (colours::accent.withAlpha (0.20f), centre.x, cap.getY(),
                                                 colours::accent.withAlpha (0.02f), centre.x, cap.getCentreY() + capRadius * 0.4f, false));
        g.drawEllipse (cap.reduced (0.5f), 1.0f);

        // Indicator
        const auto inner = centre.getPointOnCircumference (capRadius * 0.48f, valueAngle);
        const auto outer = centre.getPointOnCircumference (capRadius * 0.80f, valueAngle);
        const float lineW = std::max (1.8f, capRadius * 0.075f);
        g.setColour (colours::accent.withAlpha (enabled ? 0.22f : 0.08f));
        g.drawLine ({ inner, outer }, lineW * 3.0f);
        g.setColour (enabled ? colours::text : colours::textFaint);
        g.drawLine ({ inner, outer }, lineW);
    }

    juce::Label* VelvoxLookAndFeel::createSliderTextBox (juce::Slider& slider)
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (slider);
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

        g.setColour (on ? colours::accent.withAlpha (0.09f) : (highlighted ? colours::accent.withAlpha (0.03f) : juce::Colours::transparentBlack));
        g.fillRoundedRectangle (r, corner);
        g.setColour (on ? colours::accent.withAlpha (0.30f) : (highlighted ? colours::line.brighter (0.4f) : colours::line));
        g.drawRoundedRectangle (r, corner, 1.0f);

        const float dot = std::max (5.0f, r.getHeight() * 0.22f);
        const auto dc = juce::Point<float> (r.getX() + corner * 1.05f, r.getCentreY());
        if (on)
            drawGlow (g, dc, dot * 2.2f, 0.35f);
        g.setColour (on ? colours::accent : colours::textFaint);
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

        auto fill = on ? b.findColour (juce::TextButton::buttonOnColourId) : background;
        if (down)
            fill = colours::accent.withAlpha (on ? 0.16f : 0.08f);
        else if (highlighted)
            fill = on ? fill : colours::accent.withAlpha (0.045f);
        g.setColour (fill);
        g.fillPath (shape);

        g.setColour (on ? colours::accent.withAlpha (0.22f) : colours::line);
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
