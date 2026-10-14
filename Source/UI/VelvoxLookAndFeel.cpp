#include "VelvoxLookAndFeel.h"

namespace velvox::ui
{
    VelvoxLookAndFeel::VelvoxLookAndFeel()
    {
        setDefaultSansSerifTypefaceName ("Segoe UI");

        setColour (juce::ResizableWindow::backgroundColourId, colours::background);
        setColour (juce::Label::textColourId, colours::text);
        setColour (juce::Slider::textBoxTextColourId, colours::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, colours::line);
        setColour (juce::TextEditor::backgroundColourId, colours::raised);
        setColour (juce::TextEditor::textColourId, colours::text);
        setColour (juce::TextEditor::outlineColourId, colours::line);
        setColour (juce::TextEditor::focusedOutlineColourId, colours::eq);
        setColour (juce::CaretComponent::caretColourId, colours::text);

        setColour (juce::ComboBox::backgroundColourId, colours::raised);
        setColour (juce::ComboBox::textColourId, colours::text);
        setColour (juce::ComboBox::outlineColourId, colours::line);
        setColour (juce::ComboBox::arrowColourId, colours::textDim);

        setColour (juce::PopupMenu::backgroundColourId, colours::raised);
        setColour (juce::PopupMenu::textColourId, colours::text);
        setColour (juce::PopupMenu::headerTextColourId, colours::textDim);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::line);
        setColour (juce::PopupMenu::highlightedTextColourId, colours::text);

        setColour (juce::TextButton::buttonColourId, colours::raised);
        setColour (juce::TextButton::buttonOnColourId, colours::line);
        setColour (juce::TextButton::textColourOffId, colours::text);
        setColour (juce::TextButton::textColourOnId, colours::text);

        setColour (juce::AlertWindow::backgroundColourId, colours::panel);
        setColour (juce::AlertWindow::textColourId, colours::text);
        setColour (juce::AlertWindow::outlineColourId, colours::line);
    }

    void VelvoxLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                              float startAngle, float endAngle, juce::Slider& slider)
    {
        const auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
        const float radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float trackWidth = std::max (2.5f, radius * 0.11f);
        const float arcRadius = radius - trackWidth * 0.5f;
        const bool enabled = slider.isEnabled();

        // Track
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
        g.setColour (colours::line);
        g.strokePath (track, juce::PathStrokeType (trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Value arc: bipolar parameters grow from the centre (0 dB), others from the start
        const auto range = slider.getRange();
        const bool bipolar = range.getStart() < 0.0 && range.getEnd() > 0.0;
        const float valueAngle = startAngle + pos * (endAngle - startAngle);
        float fromAngle = startAngle;
        if (bipolar)
            fromAngle = startAngle + (float) slider.valueToProportionOfLength (0.0) * (endAngle - startAngle);

        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                             std::min (fromAngle, valueAngle), std::max (fromAngle, valueAngle), true);
        g.setColour (enabled ? accent : accent.withSaturation (0.1f).withAlpha (0.5f));
        g.strokePath (value, juce::PathStrokeType (trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Cap
        const float capRadius = arcRadius - trackWidth * 1.6f;
        g.setGradientFill (juce::ColourGradient (colours::raised.brighter (0.15f), centre.x, centre.y - capRadius,
                                                 colours::raised.darker (0.35f), centre.x, centre.y + capRadius, false));
        g.fillEllipse (juce::Rectangle<float> (capRadius * 2.0f, capRadius * 2.0f).withCentre (centre));
        g.setColour (colours::line.brighter (0.1f));
        g.drawEllipse (juce::Rectangle<float> (capRadius * 2.0f, capRadius * 2.0f).withCentre (centre), 1.0f);

        // Pointer
        const auto inner = centre.getPointOnCircumference (capRadius * 0.35f, valueAngle);
        const auto outer = centre.getPointOnCircumference (capRadius * 0.9f, valueAngle);
        g.setColour (enabled ? colours::text : colours::textDim);
        g.drawLine ({ inner, outer }, std::max (1.6f, radius * 0.06f));
    }

    juce::Label* VelvoxLookAndFeel::createSliderTextBox (juce::Slider& slider)
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (slider);
        l->setFont (font (12.0f));
        l->setColour (juce::Label::textColourId, colours::textDim);
        l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
        l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        l->setColour (juce::Label::outlineWhenEditingColourId, colours::line);
        l->setColour (juce::Label::backgroundWhenEditingColourId, colours::raised);
        return l;
    }

    // Pill-shaped toggle with a status dot
    void VelvoxLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
    {
        const auto accent = b.findColour (juce::ToggleButton::tickColourId);
        const bool on = b.getToggleState();
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const float corner = r.getHeight() * 0.5f;

        g.setColour (on ? accent.withAlpha (0.16f) : colours::raised);
        g.fillRoundedRectangle (r, corner);
        g.setColour (on ? accent.withAlpha (0.7f) : (highlighted ? colours::line.brighter (0.2f) : colours::line));
        g.drawRoundedRectangle (r, corner, 1.0f);

        const float dot = r.getHeight() * 0.32f;
        auto dotArea = juce::Rectangle<float> (dot, dot).withCentre ({ r.getX() + corner, r.getCentreY() });
        g.setColour (on ? accent : colours::textDim.withAlpha (0.6f));
        g.fillEllipse (dotArea);

        g.setColour (on ? colours::text : colours::textDim);
        g.setFont (font (std::min (13.0f, r.getHeight() * 0.55f)));
        g.drawText (b.getButtonText(), r.withTrimmedLeft (corner * 1.6f).withTrimmedRight (corner * 0.6f),
                    juce::Justification::centred, false);
    }

    void VelvoxLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& background,
                                                  bool highlighted, bool down)
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto c = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : background;
        if (down)
            c = c.brighter (0.15f);
        else if (highlighted)
            c = c.brighter (0.07f);

        g.setColour (c);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (b.getToggleState() ? colours::text.withAlpha (0.35f) : colours::line);
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
    }

    juce::Font VelvoxLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
    {
        return font (std::min (14.0f, (float) buttonHeight * 0.5f));
    }

    void VelvoxLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
    {
        auto r = juce::Rectangle<int> (width, height).toFloat().reduced (0.5f);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (box.hasKeyboardFocus (true) ? colours::textDim : box.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (r, 5.0f, 1.0f);

        const float arrowW = 8.0f;
        const float ax = r.getRight() - 14.0f, ay = r.getCentreY();
        juce::Path arrow;
        arrow.startNewSubPath (ax - arrowW * 0.5f, ay - 2.0f);
        arrow.lineTo (ax, ay + 2.5f);
        arrow.lineTo (ax + arrowW * 0.5f, ay - 2.0f);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId));
        g.strokePath (arrow, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    juce::Font VelvoxLookAndFeel::getComboBoxFont (juce::ComboBox& box)
    {
        return font (std::min (14.0f, (float) box.getHeight() * 0.55f));
    }

    void VelvoxLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
    {
        label.setBounds (8, 1, box.getWidth() - 28, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }

    juce::Font VelvoxLookAndFeel::getPopupMenuFont()
    {
        return font (14.0f);
    }

    void VelvoxLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
    {
        g.fillAll (colours::raised);
        g.setColour (colours::line);
        g.drawRect (0, 0, width, height, 1);
    }

    juce::Font VelvoxLookAndFeel::getLabelFont (juce::Label& label)
    {
        return label.getFont();
    }
}
