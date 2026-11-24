#include "EmberEditor.h"
#include "UI/ValueText.h"
#include "Text/I18n.h"

namespace ember
{
    using namespace vocalypse;
    using i18n::tr;
    namespace c = ui::colours;

    namespace
    {
        // Jakob 2026-11-13 (next to FabFilter Pro-DS at the same window size): "die Woerter kann man nicht lesen".
        // EMBER's panel uses larger type than the VOCALYPSE design; set when the plugin binary loads, before
        // any component makes a font.
        constexpr float emberTextScale = 1.35f;
        const bool textScaleSet = (ui::setTextScale (emberTextScale), true);

        juce::String utf8 (const char* s) { return juce::String::fromUTF8 (s); }

        juce::Path triangle (bool right)
        {
            juce::Path p;
            if (right) p.addTriangle (0.0f, 0.0f, 10.0f, 6.0f, 0.0f, 12.0f);
            else       p.addTriangle (10.0f, 0.0f, 0.0f, 6.0f, 10.0f, 12.0f);
            return p;
        }

        juce::Path hamburger()
        {
            juce::Path p;
            for (int i = 0; i < 3; ++i)
                p.addRoundedRectangle (0.0f, (float) i * 6.0f, 18.0f, 2.2f, 1.1f);
            return p;
        }

        void styleShape (juce::ShapeButton& b, const juce::Path& path)
        {
            b.setShape (path, false, true, false);
            b.setColours (c::textMuted, c::textStrong, look::accent);
            b.setBorderSize (juce::BorderSize<int> (7));
        }

        void styleSegment (juce::TextButton& b, const juce::String& text, int group)
        {
            b.setButtonText (text);
            b.setClickingTogglesState (false);
            b.getProperties().set (ui::VocalypseLookAndFeel::segmentProperty, true);
            b.setColour (juce::TextButton::buttonOnColourId, look::accent);
            b.setRadioGroupId (group);
        }

        void styleLabel (juce::Label& l, const juce::String& text)
        {
            l.setText (i18n::upper (text), juce::dontSendNotification);
            l.setFont (ui::caps (11.0f));
            l.setColour (juce::Label::textColourId, c::textMuted);
            l.setJustificationType (juce::Justification::centredLeft);
            l.setInterceptsMouseClicks (false, false);
        }

        juce::String hzText (float hz)
        {
            return hz >= 1000.0f ? i18n::number (hz / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
        }
    }

    // ================================================================ look
    namespace
    {
        // the panels' glass (EmberPanel::paint) as solid colours for windows of their own
        const juce::Colour glassTop { 0xff170812 }, glassBottom { 0xff0b0409 }, field { 0xff0a0408 };
        const juce::Colour ink { 0xffeee6ea };        // text on the glass (warm white)
        const juce::Colour inkSoft { 0xffc4b5bd };    // button texts (warm grey, not the shared look's blue-grey)
        const juce::Colour inkOff { 0xff9b8c95 };     // segments that are not chosen
    }

    EmberLookAndFeel::EmberLookAndFeel()
    {
        using C = juce::Colour;
        setColour (ui::LicenceLock::accentColourId, look::accent);   // the lock card in pink
        setColour (juce::ResizableWindow::backgroundColourId, seal::wall);
        setColour (juce::TextButton::buttonOnColourId, look::accent);
        setColour (juce::TextButton::textColourOnId, ink);
        setColour (juce::TextButton::textColourOffId, inkSoft);
        setColour (juce::ToggleButton::tickColourId, look::accent);
        // menus (a background that is not opaque lets the menu window have round corners)
        setColour (juce::PopupMenu::backgroundColourId, glassTop.withAlpha (0.985f));
        setColour (juce::PopupMenu::textColourId, ink);
        setColour (juce::PopupMenu::headerTextColourId, look::accentSoft);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, look::accent.withAlpha (0.22f));
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        // dialogs, text fields, tooltips
        setColour (juce::AlertWindow::backgroundColourId, glassTop);
        setColour (juce::AlertWindow::textColourId, ink);
        setColour (juce::AlertWindow::outlineColourId, look::accent.withAlpha (0.55f));
        setColour (juce::TextEditor::backgroundColourId, field);
        setColour (juce::TextEditor::textColourId, ink);
        setColour (juce::TextEditor::highlightedTextColourId, juce::Colours::white);
        setColour (juce::TextEditor::outlineColourId, look::accent.withAlpha (0.3f));
        setColour (juce::TextEditor::focusedOutlineColourId, look::accent.withAlpha (0.85f));
        setColour (juce::TextEditor::highlightColourId, look::accent.withAlpha (0.35f));
        setColour (juce::TextEditor::shadowColourId, C (0x00000000));
        setColour (juce::CaretComponent::caretColourId, look::accentSoft);
        setColour (juce::Label::textWhenEditingColourId, ink);
        setColour (juce::Label::backgroundWhenEditingColourId, field);
        setColour (juce::Label::outlineWhenEditingColourId, look::accent.withAlpha (0.6f));
        setColour (juce::Slider::textBoxHighlightColourId, look::accent.withAlpha (0.35f));
        setColour (juce::ComboBox::backgroundColourId, field);
        setColour (juce::ComboBox::outlineColourId, look::accent.withAlpha (0.3f));
        setColour (juce::ComboBox::focusedOutlineColourId, look::accent.withAlpha (0.85f));
        setColour (juce::ComboBox::arrowColourId, look::accentSoft);
        setColour (juce::ComboBox::textColourId, ink);
        setColour (juce::TooltipWindow::backgroundColourId, glassTop);
        setColour (juce::TooltipWindow::textColourId, ink);
        setColour (juce::TooltipWindow::outlineColourId, look::accent.withAlpha (0.5f));
        setColour (juce::ScrollBar::thumbColourId, look::accent.withAlpha (0.5f));
        setColour (juce::ScrollBar::trackColourId, C (0x00000000));
        setColour (juce::ScrollBar::backgroundColourId, C (0x00000000));
        setColour (juce::ListBox::backgroundColourId, field);
        setColour (juce::ListBox::outlineColourId, look::accent.withAlpha (0.3f));
        setColour (juce::ListBox::textColourId, ink);
        setColour (juce::ProgressBar::foregroundColourId, look::accent);
        setColour (juce::ProgressBar::backgroundColourId, field);
        // the shared info card and spectrum in pink
        setColour (ui::InfoPopup::backgroundColourId, glassTop.withAlpha (0.97f));
        setColour (ui::InfoPopup::glowColourId, look::accent);
        setColour (ui::InfoPopup::valueColourId, look::accentSoft);
        setColour (ui::InfoPopup::barColourId, look::accent);
        setColour (ui::InfoPopup::outlineColourId, look::accent.withAlpha (0.35f));
        setColour (ui::SpectrumView::curveColourId, look::accent);
    }

    void EmberLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& background, bool highlighted, bool down)
    {
        const auto& props = b.getProperties();
        const bool fire = (bool) props.getWithDefault ("fire", false) || (bool) props.getWithDefault ("primary", false);
        if (! fire)
        {
            if ((bool) props.getWithDefault (segmentProperty, false) || (bool) props.getWithDefault ("link", false))
            {
                VocalypseLookAndFeel::drawButtonBackground (g, b, background, highlighted, down);
                return;
            }
            // plain buttons (here and in the dialogs): a ghost with a faint pink edge; switched on (DELTA, BYPASS) in
            // their own colour, never the shared look's blue
            auto r = b.getLocalBounds().toFloat().reduced (0.5f);
            const float corner = std::min (8.0f, r.getHeight() * 0.5f);
            const bool flatL = b.isConnectedOnLeft(), flatR = b.isConnectedOnRight();
            juce::Path shape;
            shape.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), corner, corner, ! flatL, ! flatR, ! flatL, ! flatR);
            if (b.getToggleState())
            {
                const auto on = b.findColour (juce::TextButton::buttonOnColourId);
                g.setColour (on.withAlpha (0.1f));
                g.strokePath (shape, juce::PathStrokeType (5.0f));
                g.setColour (on.withAlpha (down ? 0.3f : 0.2f));
                g.fillPath (shape);
                g.setColour (on.withAlpha (0.8f));
                g.strokePath (shape, juce::PathStrokeType (1.2f));
                return;
            }
            g.setColour (juce::Colours::white.withAlpha (down ? 0.08f : (highlighted ? 0.05f : 0.02f)));
            g.fillPath (shape);
            g.setColour (look::accent.withAlpha (! b.isEnabled() ? 0.1f : (highlighted ? 0.45f : 0.22f)));
            g.strokePath (shape, juce::PathStrokeType (1.0f));
            return;
        }
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const float corner = juce::jmin (8.0f, r.getHeight() * 0.3f), a = b.isEnabled() ? 1.0f : 0.35f;
        if (b.isEnabled())
        {
            g.setColour (seal::pink.withAlpha (highlighted ? 0.34f : 0.2f));
            g.fillRoundedRectangle (r.expanded (2.5f), corner + 2.5f);
        }
        juce::ColourGradient fill (seal::pinkLight.withAlpha (a), r.getX(), r.getY(), juce::Colour (0xffd8124e).withAlpha (a), r.getRight(), r.getBottom(), false);
        fill.addColour (0.45, seal::pink.withAlpha (a));
        g.setGradientFill (fill);
        g.fillRoundedRectangle (r, corner);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha ((down ? 0.0f : (highlighted ? 0.28f : 0.14f)) * a), r.getX(), r.getY(),
                                                 juce::Colours::white.withAlpha (0.0f), r.getX(), r.getCentreY(), false));
        g.fillRoundedRectangle (r, corner);
    }

    void EmberLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool highlighted, bool down)
    {
        if ((bool) b.getProperties().getWithDefault ("link", false))
        {
            // a link in pink (the shared look draws it cyan)
            const auto f = ui::font (12.5f);
            const auto text = b.getButtonText();
            const float w = std::min ((float) b.getWidth(), (float) juce::GlyphArrangement::getStringWidthInt (f, text));
            g.setColour (! b.isEnabled() ? c::textFaint : highlighted ? ink : look::accentSoft);
            g.setFont (f);
            g.drawText (text, b.getLocalBounds(), juce::Justification::centredLeft, true);
            g.fillRect (juce::Rectangle<float> (0.0f, (float) b.getHeight() * 0.5f + f.getHeight() * 0.5f, w, 1.0f));
            return;
        }
        if ((bool) b.getProperties().getWithDefault (segmentProperty, false) && ! b.getToggleState())
        {
            // a segment that is not chosen: warm grey (the shared look's is blue-grey); chosen ones as shared
            auto col = highlighted ? ink : inkOff;
            if (! b.isEnabled())
                col = col.withMultipliedAlpha (0.5f);
            g.setColour (col);
            g.setFont (ui::semiBold (11.0f));
            g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (2, 0), juce::Justification::centred, 1, 0.8f);
            return;
        }
        if ((bool) b.getProperties().getWithDefault ("primary", false))
        {
            g.setColour (b.isEnabled() ? c::fireInk : c::textFaint);
            g.setFont (getTextButtonFont (b, b.getHeight()));
            g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (8, 0), juce::Justification::centred, 1, 0.9f);
            return;
        }
        VocalypseLookAndFeel::drawButtonText (g, b, highlighted, down);
    }

    juce::Font EmberLookAndFeel::getPopupMenuFont()
    {
        return ui::medium (11.0f);   // x 1.35 (EMBER's text scale): the size of the panel's own texts
    }

    void EmberLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
    {
        const auto r = juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f);
        g.setGradientFill (juce::ColourGradient (glassTop, 0.0f, 0.0f, glassBottom, 0.0f, (float) height, false));
        g.fillRoundedRectangle (r, 9.0f);
        g.setColour (look::accent.withAlpha (0.45f));
        g.drawRoundedRectangle (r, 9.0f, 1.0f);
    }

    void EmberLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& name)
    {
        const auto r = area.toFloat().reduced (14.0f, 0.0f).withTrimmedTop (6.0f);
        g.setFont (ui::caps (8.5f));
        g.setColour (look::accentSoft.withAlpha (0.9f));
        g.drawText (i18n::upper (name), r, juce::Justification::centredLeft, true);
    }

    void EmberLookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                                      int& idealWidth, int& idealHeight)
    {
        if (isSeparator)
        {
            idealWidth = 50;
            idealHeight = 11;
            return;
        }
        const auto f = getPopupMenuFont();
        idealHeight = standardMenuItemHeight > 0 ? standardMenuItemHeight : juce::roundToInt (f.getHeight() * 1.75f);
        idealWidth = juce::GlyphArrangement::getStringWidthInt (f, text) + idealHeight * 2 + 16;
    }

    void EmberLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                              bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                                              const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour)
    {
        if (isSeparator)
        {
            const auto r = area.toFloat().reduced (12.0f, 0.0f);
            g.setColour (look::accent.withAlpha (0.22f));
            g.fillRect (r.withHeight (1.0f).withCentre (r.getCentre()));
            return;
        }
        const auto r = area.toFloat().reduced (5.0f, 1.0f);
        auto colour = textColour != nullptr ? *textColour : findColour (juce::PopupMenu::textColourId);
        if (isHighlighted && isActive)
        {
            g.setColour (findColour (juce::PopupMenu::highlightedBackgroundColourId));
            g.fillRoundedRectangle (r, 6.0f);
            colour = findColour (juce::PopupMenu::highlightedTextColourId);
        }
        else if (! isActive)
            colour = colour.withMultipliedAlpha (0.45f);
        auto row = r.reduced (6.0f, 0.0f);
        const auto mark = row.removeFromLeft (16.0f);
        if (icon != nullptr)
            icon->drawWithin (g, mark, juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize, 1.0f);
        else if (isTicked)
        {
            // the chosen entry: a small pink diamond (the seal's colour), not a grey tick
            juce::Path d;
            const auto cpt = mark.getCentre();
            d.addQuadrilateral (cpt.x, cpt.y - 4.5f, cpt.x + 4.5f, cpt.y, cpt.x, cpt.y + 4.5f, cpt.x - 4.5f, cpt.y);
            g.setColour (look::accent.withMultipliedAlpha (isActive ? 1.0f : 0.5f));
            g.fillPath (d);
        }
        row.removeFromLeft (6.0f);
        if (hasSubMenu)
        {
            const auto arrow = row.removeFromRight (10.0f);
            juce::Path p;
            p.startNewSubPath (arrow.getX() + 2.0f, arrow.getCentreY() - 4.0f);
            p.lineTo (arrow.getX() + 6.0f, arrow.getCentreY());
            p.lineTo (arrow.getX() + 2.0f, arrow.getCentreY() + 4.0f);
            g.setColour (isHighlighted ? colour : look::accentSoft.withMultipliedAlpha (isActive ? 0.9f : 0.4f));
            g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (colour);
        g.setFont (getPopupMenuFont());
        g.drawFittedText (text, row.toNearestInt(), juce::Justification::centredLeft, 1, 0.9f);
        if (shortcutKeyText.isNotEmpty())
        {
            g.setFont (ui::font (9.5f));
            g.setColour (colour.withMultipliedAlpha (0.6f));
            g.drawText (shortcutKeyText, row, juce::Justification::centredRight, true);
        }
    }

    juce::Font EmberLookAndFeel::getAlertWindowTitleFont() { return ui::semiBold (12.5f); }
    juce::Font EmberLookAndFeel::getAlertWindowMessageFont() { return ui::font (11.0f); }
    juce::Font EmberLookAndFeel::getAlertWindowFont() { return ui::font (10.0f); }

    void EmberLookAndFeel::drawAlertBox (juce::Graphics& g, juce::AlertWindow& alert, const juce::Rectangle<int>&, juce::TextLayout& textLayout)
    {
        // the panels' dark glass with a thin pink edge and a pink line on top; the icon as a small pink sign
        const auto r = alert.getLocalBounds().toFloat();
        g.setGradientFill (juce::ColourGradient (glassTop, 0.0f, 0.0f, glassBottom, 0.0f, r.getBottom(), false));
        g.fillRect (r);
        {
            juce::ColourGradient top (look::accent.withAlpha (0.0f), r.getX(), 0.0f, look::accent.withAlpha (0.0f), r.getRight(), 0.0f, false);
            top.addColour (0.5, look::accent.withAlpha (0.9f));
            g.setGradientFill (top);
            g.fillRect (r.withHeight (2.0f));
        }
        g.setColour (alert.findColour (juce::AlertWindow::outlineColourId));
        g.drawRect (r, 1.0f);

        int iconSpace = 0;
        if (alert.getAlertType() != juce::MessageBoxIconType::NoIcon)
        {
            iconSpace = 80;   // what AlertWindow keeps free for it
            const auto badge = juce::Rectangle<float> (30.0f, 30.0f).withCentre ({ 40.0f, 46.0f });
            g.setColour (look::accent.withAlpha (0.16f));
            g.fillEllipse (badge);
            g.setColour (look::accent.withAlpha (0.8f));
            g.drawEllipse (badge, 1.4f);
            const auto type = alert.getAlertType();
            g.setColour (look::accentSoft);
            g.setFont (ui::semiBold (12.0f));
            g.drawText (type == juce::MessageBoxIconType::WarningIcon ? "!" : (type == juce::MessageBoxIconType::QuestionIcon ? "?" : "i"),
                        badge, juce::Justification::centred, false);
        }
        g.setColour (alert.findColour (juce::AlertWindow::textColourId));
        textLayout.draw (g, juce::Rectangle<int> (iconSpace, 30, alert.getWidth() - iconSpace,
                                                  alert.getHeight() - getAlertWindowButtonHeight() - 20).toFloat().reduced (iconSpace > 0 ? 0.0f : 10.0f, 0.0f));
    }

    void EmberLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
    {
        const auto r = juce::Rectangle<int> (width, height).toFloat();
        g.setColour (glassTop);
        g.fillRect (r);
        g.setColour (look::accent.withAlpha (0.5f));
        g.drawRect (r, 1.0f);
        juce::AttributedString s;
        s.setJustification (juce::Justification::centredLeft);
        s.append (text, ui::font (13.5f), ink.withAlpha (0.92f));
        juce::TextLayout layout;
        layout.createLayoutWithBalancedLineLengths (s, (float) width - 20.0f);
        layout.draw (g, r.reduced (10.0f, 7.0f));
    }

    void EmberLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& te)
    {
        g.setColour (te.findColour (juce::TextEditor::backgroundColourId));
        g.fillRoundedRectangle (juce::Rectangle<float> ((float) width, (float) height), 5.0f);
    }

    void EmberLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& te)
    {
        if (! te.isEnabled())
            return;
        const bool focused = te.hasKeyboardFocus (true) && ! te.isReadOnly();
        g.setColour (te.findColour (focused ? juce::TextEditor::focusedOutlineColourId : juce::TextEditor::outlineColourId));
        g.drawRoundedRectangle (juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f), 5.0f, focused ? 1.5f : 1.0f);
    }

    void EmberLookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging)
    {
        g.setColour (look::accent.withAlpha (isMouseOver || isMouseDragging ? 0.6f : 0.25f));
        for (float i = 0.0f; i < 2.0f; i += 1.0f)
        {
            const float o = 4.0f + i * 4.0f;
            g.drawLine ((float) w - o, (float) h - 2.0f, (float) w - 2.0f, (float) h - o, 1.0f);
        }
    }

    // ================================================================ seal overlay
    void SealOverlay::repaintEdges (juce::Rectangle<float> r, float w)
    {
        const auto o = r.expanded (w).getSmallestIntegerContainer();
        const int k = (int) std::ceil (w * 2.0f);
        repaint (o.withHeight (k));
        repaint (o.withTop (o.getBottom() - k));
        repaint (o.withWidth (k));
        repaint (o.withLeft (o.getRight() - k));
    }

    void SealOverlay::update (const seal::Scene& s, bool simpleView, juce::Rectangle<float> knobArea, std::vector<juce::Rectangle<float>> plateAreas)
    {
        const bool surge = scene.sinceS < 1.2f || s.sinceS < 1.2f;
        const bool viewChanged = simple != simpleView || knob != knobArea || scene.calm != s.calm;
        scene = s;
        simple = simpleView;
        knob = knobArea;
        plates = std::move (plateAreas);
        // calm (animation reduced / off): the border and the knob's seal stand still, only their light follows the work
        const float lit = s.on ? juce::jmin (1.0f, s.heat) : -1.0f;
        const bool due = s.calm ? (viewChanged || std::abs (lit - paintedLit) > 0.02f) : true;
        lastUpdate = juce::Time::getMillisecondCounterHiRes() * 0.001;
        // moving border: repainted from the screen's frame clock (onFrame); calm: only when its light changes
        if ((s.calm || viewChanged) && due)
        {
            paintedLit = lit;
            repaintEdges (getLocalBounds().toFloat().reduced (6.0f), 7.0f);
        }
        // the knob's seal stands still (2026-11-24): drawn again only when the knob turns or its light changes
        // (the knob repaints itself while it turns; this catches a change of the light alone)
        const float knobLit = s.on ? juce::jmin (1.0f, s.heat) : -1.0f;
        const float angle = knobAngle ? knobAngle() : 0.0f;
        // (on a change of view also in ADVANCED: the seal drawn there in SIMPLE must go)
        if (! knob.isEmpty() && (viewChanged || (simple && (std::abs (knobLit - paintedKnobLit) > 0.03f || std::abs (angle - paintedAngle) > 0.002f))))
        {
            paintedKnobLit = knobLit;
            paintedAngle = angle;
            repaint (knob.expanded (4.0f).getSmallestIntegerContainer());
        }
        if (surge)
            for (auto& r : plates)
                repaintEdges (r, 8.0f);
    }

    void SealOverlay::onFrame (double frameTime)
    {
        // The running runes (2026-11-24, Jakob: "ruckeln"): moved by the screen's own frame clock, about 30 times a
        // second at even steps, by the real time since the last step (the panel's 30 Hz timer came at uneven times
        // and only every other one moved the border: 15 uneven steps a second)
        const double wall = juce::Time::getMillisecondCounterHiRes() * 0.001;
        const bool live = ! scene.calm && isShowing() && wall - lastUpdate < 0.5;   // the panel updates us (not covered)
        if (! live)
        {
            lastFrame = 0.0;
            return;
        }
        // the time the frame will be shown at (the screen's clock: even steps), at most ~40 steps a second
        if (lastFrame > 0.0 && frameTime - lastFrame < 1.0 / 40.0)
            return;   // a faster screen (60 / 120 / 144 Hz): every second / third ... frame
        borderTime += lastFrame > 0.0 ? juce::jlimit (0.0, 0.1, frameTime - lastFrame) : 0.0;
        lastFrame = frameTime;
        if (stepLogForTest != nullptr)
            stepLogForTest->push_back (borderTime);
        repaintEdges (getLocalBounds().toFloat().reduced (6.0f), 7.0f);
    }

    void SealOverlay::paint (juce::Graphics& g)
    {
        const float scale = juce::jlimit (1.0f, 3.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        borderRunes.setScale (scale);
        sealRunes.setScale (scale);
        auto border = scene;
        border.t = borderTime;   // the border's own clock (onFrame); every strip of one frame shows the same moment
        seal::drawRuneBorder (g, borderRunes, getLocalBounds().toFloat(), border);
        if (simple && ! knob.isEmpty() && g.clipRegionIntersects (knob.expanded (4.0f).getSmallestIntegerContainer()))
            seal::drawMiniSeal (g, sealRunes, knob.getCentre(), knob.getWidth() * 0.5f, knobAngle ? knobAngle() : 0.0f, scene);
        // the wave of light through the panels, one after the other
        if (scene.on)
            for (size_t i = 0; i < plates.size(); ++i)
            {
                const float u = (scene.sinceS - 0.07f * (float) i) / 0.8f;
                if (u < 0.0f || u >= 1.0f)
                    continue;
                const float a = 1.0f - u;
                const auto r = plates[i];
                g.setColour (seal::pink.withAlpha (0.18f * a));
                g.drawRoundedRectangle (r.expanded (3.0f), 14.0f, 6.0f);
                g.setColour (seal::pinkLight.withAlpha (0.85f * a));
                g.drawRoundedRectangle (r, 12.0f, 1.4f);
            }
    }

    // ================================================================ meters
    void ReductionMeter::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        auto readout = r.removeFromTop (26.0f);
        r.removeFromBottom (4.0f);
        auto label = r.removeFromLeft (14.0f);   // written upwards next to the scale
        auto bar = r.removeFromRight (14.0f);
        g.setColour (c::panelInset);
        g.fillRoundedRectangle (bar, 4.0f);
        g.setColour (c::borderInset);
        g.drawRoundedRectangle (bar, 4.0f, 1.0f);

        const float frac = juce::jlimit (0.0f, 1.0f, value / maxDb);
        auto lit = bar.reduced (2.0f).withHeight ((bar.getHeight() - 4.0f) * frac);
        g.setGradientFill (juce::ColourGradient (look::glow, lit.getX(), bar.getY(), look::accent, lit.getX(), bar.getBottom(), false));
        g.fillRoundedRectangle (lit, 3.0f);
        const float hy = bar.getY() + 2.0f + (bar.getHeight() - 4.0f) * juce::jlimit (0.0f, 1.0f, peakHold / maxDb);
        g.setColour (c::textStrong.withAlpha (0.8f));
        g.fillRect (bar.getX() + 1.0f, hy - 1.0f, bar.getWidth() - 2.0f, 2.0f);

        // the same sign everywhere: the scale and the read-out both count down from 0
        g.setFont (ui::monoFont (9.5f));
        g.setColour (c::textMuted);
        for (float db : { 0.0f, 3.0f, 6.0f, 9.0f, 12.0f })
        {
            const float y = bar.getY() + 2.0f + (bar.getHeight() - 4.0f) * db / maxDb;
            g.drawText ((db > 0.0f ? utf8 ("−") : juce::String()) + juce::String ((int) db),
                        juce::Rectangle<float> (bar.getX() - 30.0f, y - 8.0f, 27.0f, 16.0f), juce::Justification::centredRight);
        }
        g.setFont (ui::monoFont (11.0f));
        g.setColour (peakHold >= 0.05f ? look::glow : c::textMuted);
        g.drawFittedText ((peakHold >= 0.05f ? utf8 ("−") : juce::String()) + i18n::number (peakHold, 1) + " dB",
                          readout.withTrimmedBottom (6.0f).toNearestInt(), juce::Justification::centredRight, 1, 0.75f);
        g.setFont (ui::caps (11.0f));
        g.setColour (c::textMuted);
        {
            juce::Graphics::ScopedSaveState keep (g);
            g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, label.getCentreX(), label.getCentreY()));
            g.drawText (tr ("ABSENKUNG", "REDUCTION"), juce::Rectangle<float> (label.getHeight(), label.getWidth() + 4.0f).withCentre (label.getCentre()),
                        juce::Justification::centred, false);
        }
    }

    void ReductionMeter::setValue (float db)
    {
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        const float dt = lastUpdate > 0.0 ? (float) juce::jlimit (0.0, 0.25, now - lastUpdate) : 0.0f;
        lastUpdate = now;
        // repainted only while something shows a change (the window's runes repaint with it; silence = no repaints)
        const float oldValue = value, oldPeak = peakHold;
        value = db;
        if (value >= peakHold)
        {
            peakHold = value;
            holdLeft = 1.0f;
        }
        else if ((holdLeft -= dt) <= 0.0f)
            peakHold = std::max (value, peakHold - 4.5f * dt);
        if (std::abs (value - oldValue) > 0.005f || std::abs (peakHold - oldPeak) > 0.005f)
            repaint();
    }

    DetectorMeter::DetectorMeter (EmberAudioProcessor& p) : processor (p) {}

    bool DetectorMeter::nearHandle (juce::Point<float> p) const
    {
        const auto b = barArea();
        return std::abs (p.y - yFor (processor.state.getRawParameterValue (pid::threshold)->load())) <= 8.0f && p.x >= b.getX() - 24.0f;
    }

    void DetectorMeter::mouseMove (const juce::MouseEvent& e)
    {
        const bool h = nearHandle (e.position);
        setMouseCursor (h ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
        if (h != hover) { hover = h; repaint(); }
    }

    void DetectorMeter::mouseExit (const juce::MouseEvent&)
    {
        if (hover) { hover = false; repaint(); }
    }

    void DetectorMeter::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
    {
        // like the knobs: a step per notch, Shift / Ctrl = fine
        if (auto* p = processor.state.getParameter (pid::threshold))
        {
            const float step = (e.mods.isShiftDown() || e.mods.isCtrlDown() ? 1.6f : 8.0f) * (w.isReversed ? -w.deltaY : w.deltaY);
            ui::gestures::setReal (*p, juce::jlimit (minDb, maxDb, p->convertFrom0to1 (p->getValue()) + step));
            repaint();
        }
    }

    juce::Rectangle<float> DetectorMeter::barArea() const
    {
        auto r = getLocalBounds().toFloat();
        r.removeFromTop (8.0f);
        r.removeFromBottom (22.0f);
        return r.withSizeKeepingCentre (16.0f, r.getHeight()).withX (r.getRight() - 22.0f);
    }

    float DetectorMeter::yFor (float db) const
    {
        const auto b = barArea();
        return b.getBottom() - b.getHeight() * juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb));
    }

    float DetectorMeter::dbFor (float y) const
    {
        const auto b = barArea();
        return minDb + (maxDb - minDb) * juce::jlimit (0.0f, 1.0f, (b.getBottom() - y) / b.getHeight());
    }

    void DetectorMeter::setLevel (float db, float smartWeight)
    {
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        const float dt = lastUpdate > 0.0 ? (float) juce::jlimit (0.0, 0.25, now - lastUpdate) : 0.0f;
        lastUpdate = now;
        level = db;
        weight = smartWeight;
        hold = db > hold ? db : hold - 18.0f * dt;   // falls 18 dB/s, whatever the repaint rate
        repaint();
    }

    void DetectorMeter::paint (juce::Graphics& g)
    {
        const auto b = barArea();
        g.setColour (c::panelInset);
        g.fillRoundedRectangle (b, 4.0f);
        g.setColour (c::borderInset);
        g.drawRoundedRectangle (b, 4.0f, 1.0f);

        const float thr = processor.state.getRawParameterValue (pid::threshold)->load();
        const float ty = yFor (thr);
        if (hold > minDb)
        {
            // above the threshold the bar burns (if smart detection says it is an S), below it stays calm
            auto bar = b.reduced (2.0f).withTop (yFor (hold));
            const auto over = bar.withBottom (std::max (bar.getY(), std::min (ty, bar.getBottom())));
            const auto under = bar.withTop (std::max (bar.getY(), ty));
            g.setColour (c::textMuted.withAlpha (0.55f));
            g.fillRoundedRectangle (under, 3.0f);
            g.setColour (weight > 0.5f ? look::accent : c::textMuted);
            g.fillRoundedRectangle (over, 3.0f);
            if (weight <= 0.5f && over.getHeight() > 1.0f)
            {
                // Smart hears no S: striped, so it does not rest on the colour alone
                juce::Graphics::ScopedSaveState keep (g);
                g.reduceClipRegion (over.getSmallestIntegerContainer());
                g.setColour (c::panelInset.withAlpha (0.75f));
                const float w = over.getWidth();
                for (float y = over.getY() - w; y < over.getBottom(); y += 5.0f)
                    g.drawLine (over.getX(), y + w, over.getRight(), y, 1.6f);
            }
        }

        // Threshold handle
        g.setColour (dragging ? look::glow : look::accent);
        g.fillRect (b.getX() - 18.0f, ty - 1.0f, b.getWidth() + 22.0f, 2.0f);
        juce::Path tri;
        tri.addTriangle (b.getX() - 20.0f, ty - 6.0f, b.getX() - 20.0f, ty + 6.0f, b.getX() - 11.0f, ty);
        g.fillPath (tri);
        if (dragging || hover)
        {
            // the value at the line while it is held
            g.setFont (ui::monoFont (10.0f));
            g.setColour (c::textStrong);
            g.drawText (i18n::number (thr, 1) + " dB", juce::Rectangle<float> (0.0f, ty - 22.0f, b.getX() - 10.0f, 14.0f), juce::Justification::centredRight);
        }

        g.setFont (ui::monoFont (9.5f));
        g.setColour (c::textMuted);
        for (float db : { -10.0f, -20.0f, -30.0f, -40.0f, -50.0f })
            g.drawText (db < 0.0f ? utf8 ("−") + juce::String ((int) -db) : juce::String ("0"),
                        juce::Rectangle<float> (b.getX() - 46.0f, yFor (db) - 6.0f, 24.0f, 12.0f), juce::Justification::centredRight);
        g.drawText ("dB", juce::Rectangle<float> (b.getX() - 19.0f, yFor (-10.0f) - 6.0f, 17.0f, 12.0f), juce::Justification::centredLeft);   // the scale's unit, after the top tick
        g.setFont (ui::caps (10.5f));
        g.setColour (c::textMuted);
        g.drawText (tr ("S-PEGEL", "S LEVEL"), getLocalBounds().toFloat().removeFromBottom (18.0f), juce::Justification::centredRight);
    }

    void DetectorMeter::mouseDown (const juce::MouseEvent& e)
    {
        // only the line is grabbed, and it moves with the mouse from where it is (no jump to the click)
        if (auto* p = processor.state.getParameter (pid::threshold); p != nullptr && nearHandle (e.position))
        {
            dragging = true;
            lastDragY = e.position.y;
            dragDb = p->convertFrom0to1 (p->getValue());
            p->beginChangeGesture();
            repaint();
        }
    }

    void DetectorMeter::mouseDrag (const juce::MouseEvent& e)
    {
        if (auto* p = processor.state.getParameter (pid::threshold); p != nullptr && dragging)
        {
            const float fine = e.mods.isShiftDown() || e.mods.isCtrlDown() ? 0.2f : 1.0f;
            const float dbPerPx = (maxDb - minDb) / std::max (1.0f, barArea().getHeight());
            dragDb = juce::jlimit (minDb, maxDb, dragDb - (e.position.y - lastDragY) * dbPerPx * fine);
            lastDragY = e.position.y;
            p->setValueNotifyingHost (p->convertTo0to1 (dragDb));
            repaint();
        }
    }

    void DetectorMeter::mouseUp (const juce::MouseEvent&)
    {
        if (auto* p = processor.state.getParameter (pid::threshold); p != nullptr && dragging)
            p->endChangeGesture();
        dragging = false;
    }

    void DetectorMeter::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (auto* p = processor.state.getParameter (pid::threshold); p != nullptr && nearHandle (e.position))
            ui::gestures::resetToDefault (*p);
    }

    // ================================================================ display
    BandDisplay::BandDisplay (EmberAudioProcessor& p, ui::SpectrumSource& source) : processor (p), spectrum (&source)
    {
        spectrum.setRange (-90.0f, 0.0f);
        spectrum.setShowGrid (false);   // its grid is for 20 Hz - 20 kHz; this display draws its own (zoomed)
        addAndMakeVisible (spectrum);
        history.fill (0.0f);
    }

    // The axis shows minHz..maxHz only (the frequency is set between 2 and 16 kHz; below 1 kHz there is nothing to
    // set): the shared spectrum view (20 Hz - 20 kHz) is made wider than the display and shifted, the rest is clipped
    void BandDisplay::resized()
    {
        const float w = (float) getWidth();
        const float visible = std::log (maxHz / minHz);
        const float all = std::log (ui::SpectrumSource::maxHz / ui::SpectrumSource::minHz);
        const float left = w * std::log (minHz / ui::SpectrumSource::minHz) / visible;
        spectrum.setBounds (juce::Rectangle<float> (-left, 0.0f, w * all / visible, (float) getHeight()).toNearestInt());
    }

    float BandDisplay::xForHz (float hz) const
    {
        const auto r = getLocalBounds().toFloat();
        return r.getX() + r.getWidth() * std::log (juce::jlimit (minHz, maxHz, hz) / minHz) / std::log (maxHz / minHz);
    }

    float BandDisplay::hzForX (float x) const
    {
        const auto r = getLocalBounds().toFloat();
        const float t = juce::jlimit (0.0f, 1.0f, (x - r.getX()) / r.getWidth());
        return minHz * std::pow (maxHz / minHz, t);
    }

    void BandDisplay::setHasSignal (bool s)
    {
        if (s == hasSignal)
            return;
        hasSignal = s;
        spectrum.setVisible (s);   // no flat lines at the floor while nothing plays
        repaint();
    }

    void BandDisplay::pushReduction (float db)
    {
        history[(size_t) head] = db;
        head = (head + 1) % (int) history.size();
    }

    void BandDisplay::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (c::panelInset);
        g.fillRoundedRectangle (r, ui::radius::card);

        // faint grid: frequencies, and the level every 20 dB
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        for (float f : { 2000.0f, 3000.0f, 5000.0f, 7000.0f, 10000.0f, 15000.0f })
            g.fillRect (juce::Rectangle<float> (xForHz (f), r.getY(), 1.0f, r.getHeight()));
        for (float db : { -20.0f, -40.0f, -60.0f, -80.0f })
            g.fillRect (juce::Rectangle<float> (r.getX(), r.getY() + r.getHeight() * -db / 90.0f, r.getWidth(), 1.0f));

        const float hz = processor.state.getRawParameterValue (pid::freq)->load();
        const float x = xForHz (hz);
        const float gr = processor.engine.currentReductionDb.load();
        const float strong = 0.10f + 0.03f * std::min (gr, 12.0f);
        if (std::lround (processor.state.getRawParameterValue (pid::mode)->load()) == 0)
        {
            // Split: the band around the S between its half-reduction points (Width), strongest at its centre
            const auto shape = splitBand (hz, processor.state.getRawParameterValue (pid::width)->load());
            const float xl = std::max (r.getX(), xForHz ((float) shape.lowHz)), xc = xForHz ((float) shape.centreHz),
                        xr = std::min (r.getRight(), xForHz ((float) shape.highHz));
            auto band = r.withLeft (xl).withRight (std::max (xl + 1.0f, xr));
            juce::ColourGradient grad (look::accent.withAlpha (0.4f * strong), band.getX(), 0, look::accent.withAlpha (0.0f), band.getRight(), 0, false);
            grad.addColour (juce::jlimit (0.0, 1.0, (double) ((xc - band.getX()) / band.getWidth())), look::accent.withAlpha (strong));
            g.setGradientFill (grad);
            g.fillRect (band);
        }
        else
        {
            auto band = r.withLeft (x);
            g.setGradientFill (juce::ColourGradient (look::accent.withAlpha (strong), band.getX(), 0,
                                                     look::accent.withAlpha (0.02f), band.getRight(), 0, false));
            g.fillRect (band);
        }
        // the band's edges (Split): thin lines with a grip in the middle, brighter while hovered or held
        if (std::lround (processor.state.getRawParameterValue (pid::mode)->load()) == 0)
        {
            const auto shape = splitBand (hz, processor.state.getRawParameterValue (pid::width)->load());
            for (auto [edgeHz, h] : { std::pair { shape.lowHz, Handle::lowEdge }, std::pair { shape.highHz, Handle::highEdge } })
            {
                if (edgeHz <= minHz || edgeHz >= maxHz)
                    continue;
                const float ex = xForHz ((float) edgeHz);
                const bool lit = dragging == h || hover == h;
                g.setColour (look::accentSoft.withAlpha (lit ? 0.9f : 0.35f));
                g.fillRect (ex - 0.5f, r.getY(), 1.0f, r.getHeight());
                const auto grip = juce::Rectangle<float> (6.0f, 22.0f).withCentre ({ ex, r.getCentreY() });
                g.setColour (lit ? look::accentSoft : look::accent.withAlpha (0.7f));
                g.fillRoundedRectangle (grip, 3.0f);
            }
        }
        const bool markerLit = dragging == Handle::frequency || hover == Handle::frequency;
        g.setColour (look::accent.withAlpha (markerLit ? 1.0f : 0.85f));
        g.fillRect (x - 1.0f, r.getY(), 2.0f, r.getHeight());
        g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ x, r.getY() + 14.0f }));
        if (markerLit)
        {
            g.setColour (look::glow.withAlpha (0.35f));
            g.drawEllipse (juce::Rectangle<float> (18.0f, 18.0f).withCentre ({ x, r.getY() + 14.0f }), 1.2f);
        }

        // Reduction history (top, hanging down: 0 dB at the top edge, 12 dB at 45 % height); only once there is some
        const int n = (int) history.size();
        if (*std::max_element (history.begin(), history.end()) >= 0.05f)
        {
            juce::Path h;
            const float hTop = r.getY() + 1.0f, hSpan = r.getHeight() * 0.45f;
            for (int i = 0; i < n; ++i)
            {
                const float v = history[(size_t) ((head + i) % n)];
                const float px = r.getX() + r.getWidth() * (float) i / (float) (n - 1);
                const float py = hTop + hSpan * juce::jlimit (0.0f, 1.0f, v / ReductionMeter::maxDb);
                if (i == 0) h.startNewSubPath (px, hTop);
                h.lineTo (px, py);
            }
            h.lineTo (r.getRight(), hTop);
            h.closeSubPath();
            g.setColour (look::glow.withAlpha (0.16f));
            g.fillPath (h);
            g.setColour (look::glow.withAlpha (0.65f));
            g.strokePath (h, juce::PathStrokeType (1.2f));
        }

        // frequency and width at the marker, on the side with room
        g.setFont (ui::monoFont (11.0f));
        const auto label = markerText();
        const float lw = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), label) + 12.0f;
        auto box = juce::Rectangle<float> (x + 11.0f, r.getY() + 6.0f, lw, 17.0f);
        if (box.getRight() > r.getRight() - 4.0f)
            box.setX (x - 11.0f - lw);
        g.setColour (c::panelInset.withAlpha (0.75f));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (c::textStrong);
        g.drawText (label, box.reduced (6.0f, 0.0f), juce::Justification::centredLeft, false);
    }

    void BandDisplay::paintOverChildren (juce::Graphics& g)
    {
        const auto r = getLocalBounds().toFloat();
        // labels above the spectrum fill, each on a small dark backing
        g.setFont (ui::font (11.0f));
        for (float f : { 2000.0f, 3000.0f, 5000.0f, 7000.0f, 10000.0f, 15000.0f })
        {
            const auto text = juce::String ((int) (f / 1000.0f)) + "k";
            const float w = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), text);
            const juce::Rectangle<float> box (xForHz (f) + 2.0f, r.getBottom() - 18.0f, w + 6.0f, 14.0f);
            g.setColour (c::background.withAlpha (0.72f));
            g.fillRoundedRectangle (box, 3.0f);
            g.setColour (c::textMuted);
            g.drawText (text, box.withTrimmedLeft (3.0f), juce::Justification::centredLeft, false);
        }
        g.setFont (ui::monoFont (9.5f));
        g.setColour (c::textMuted);
        for (float db : { -20.0f, -40.0f, -60.0f })
            g.drawText (utf8 ("−") + juce::String ((int) -db) + " dB",
                        juce::Rectangle<float> (r.getX() + 6.0f, r.getY() + r.getHeight() * -db / 90.0f - 13.0f, 60.0f, 12.0f),
                        juce::Justification::centredLeft, false);

        if (! hasSignal)
        {
            // on a dark backing, so the band's edge behind it does not cut through the words
            g.setFont (ui::font (13.0f));
            const auto hint = tr ("Spiel Audio ab – hier siehst du die Höhen deiner Stimme und den S-Bereich.",
                                  "Play audio – this shows the highs of your voice and the S range.");
            const float tw = std::min (r.getWidth() - 80.0f, (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), hint) + 28.0f);
            const auto box = r.withSizeKeepingCentre (tw, 34.0f);
            g.setColour (c::panelInset.withAlpha (0.95f));
            g.fillRoundedRectangle (box, 8.0f);
            g.setColour (c::textMuted);
            g.drawFittedText (hint, box.reduced (12.0f, 0.0f).toNearestInt(), juce::Justification::centred, 2);
        }
    }

    bool BandDisplay::isSplit() const
    {
        return std::lround (processor.state.getRawParameterValue (pid::mode)->load()) == 0;
    }

    float BandDisplay::widthForLowEdge (float freq, float hz)
    {
        // lo = 1.25 f x 2^(-n/2) (above its floor 0.45 f; below it the low edge stays where it is)
        const float fc = splitCenterRatio * freq;
        return juce::jlimit (minWidthOct, maxWidthOct, 2.0f * std::log2 (fc / std::max (1.0f, hz)));
    }

    float BandDisplay::widthForHighEdge (float freq, float hz)
    {
        // hi = lo x 2^n: with lo free, hi = 1.25 f x 2^(n/2); with lo at its floor, hi = 0.45 f x 2^n
        const float fc = splitCenterRatio * freq;
        const float n = 2.0f * std::log2 (std::max (1.0f, hz) / fc);
        if (fc * std::exp2 (-0.5f * n) < splitLowestRatio * freq)
            return juce::jlimit (minWidthOct, maxWidthOct, std::log2 (std::max (1.0f, hz) / (splitLowestRatio * freq)));
        return juce::jlimit (minWidthOct, maxWidthOct, n);
    }

    BandDisplay::Handle BandDisplay::handleAt (juce::Point<float> p) const
    {
        const float hz = processor.state.getRawParameterValue (pid::freq)->load();
        Handle best = Handle::none;
        float bestD = 9.0f;   // grab distance, px
        const auto consider = [&] (Handle h, float x)
        {
            if (x < 1.0f || x > (float) getWidth() - 1.0f)
                return;   // an edge outside the display (e.g. the high edge above 20 kHz) cannot be grabbed here
            if (const float d = std::abs (p.x - x); d < bestD)
            {
                bestD = d;
                best = h;
            }
        };
        consider (Handle::frequency, xForHz (hz));
        if (isSplit())
        {
            const auto shape = splitBand (hz, processor.state.getRawParameterValue (pid::width)->load());
            if (shape.lowHz > minHz) consider (Handle::lowEdge, xForHz ((float) shape.lowHz));
            if (shape.highHz < maxHz) consider (Handle::highEdge, xForHz ((float) shape.highHz));
        }
        return best;
    }

    juce::String BandDisplay::markerText() const
    {
        const auto hz = hzText (processor.state.getRawParameterValue (pid::freq)->load());
        if (! isSplit())
            return hz;
        auto* w = processor.state.getParameter (pid::width);
        return hz + utf8 (" · ") + w->getCurrentValueAsText();
    }

    void BandDisplay::mouseMove (const juce::MouseEvent& e)
    {
        const auto h = handleAt (e.position);
        setMouseCursor (h != Handle::none ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
        if (h != hover)
        {
            hover = h;
            repaint();
        }
    }

    void BandDisplay::mouseExit (const juce::MouseEvent&)
    {
        if (hover != Handle::none)
        {
            hover = Handle::none;
            repaint();
        }
    }

    void BandDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
    {
        // Split: the wheel sets the width (like Q in an EQ), a quarter octave per notch; Wideband has no width: the
        // frequency, about 1/8 octave per notch. Shift / Ctrl = fine.
        const float fine = e.mods.isShiftDown() || e.mods.isCtrlDown() ? 0.2f : 1.0f;
        const float notches = (w.isReversed ? -w.deltaY : w.deltaY) * fine;
        if (isSplit())
        {
            if (auto* p = processor.state.getParameter (pid::width))
                ui::gestures::setReal (*p, juce::jlimit (minWidthOct, maxWidthOct, p->convertFrom0to1 (p->getValue()) + 2.0f * notches));
        }
        else if (auto* p = processor.state.getParameter (pid::freq))
            ui::gestures::setReal (*p, juce::jlimit (2000.0f, 16000.0f, p->convertFrom0to1 (p->getValue()) * std::pow (2.0f, notches)));
        repaint();
    }

    void BandDisplay::mouseDown (const juce::MouseEvent& e)
    {
        // only the marker and the band's edges are grabbed, and they move with the mouse from where they are (no jump)
        const auto h = handleAt (e.position);
        auto* p = processor.state.getParameter (h == Handle::frequency ? pid::freq : pid::width);
        if (h == Handle::none || p == nullptr)
            return;
        dragging = h;
        lastDragX = e.position.x;
        dragHz = processor.state.getRawParameterValue (pid::freq)->load();
        if (h != Handle::frequency)
        {
            const auto shape = splitBand (dragHz, processor.state.getRawParameterValue (pid::width)->load());
            dragEdgeX = xForHz ((float) (h == Handle::lowEdge ? shape.lowHz : shape.highHz));
        }
        p->beginChangeGesture();
        repaint();
    }

    void BandDisplay::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragging == Handle::none)
            return;
        const float fine = e.mods.isShiftDown() || e.mods.isCtrlDown() ? 0.2f : 1.0f;
        const float dx = (e.position.x - lastDragX) * fine;
        lastDragX = e.position.x;
        if (dragging == Handle::frequency)
        {
            if (auto* p = processor.state.getParameter (pid::freq))
            {
                dragHz = juce::jlimit (2000.0f, 16000.0f, hzForX (xForHz (dragHz) + dx));
                p->setValueNotifyingHost (p->convertTo0to1 (dragHz));
            }
        }
        else if (auto* p = processor.state.getParameter (pid::width))
        {
            // the edge follows the mouse (unrounded while held); the width is what puts the edge there
            dragEdgeX += dx;
            const float hz = minHz * std::pow (maxHz / minHz, (dragEdgeX - getLocalBounds().toFloat().getX()) / (float) getWidth());
            const float n = dragging == Handle::lowEdge ? widthForLowEdge (dragHz, hz) : widthForHighEdge (dragHz, hz);
            p->setValueNotifyingHost (p->convertTo0to1 (n));
        }
        repaint();
    }

    void BandDisplay::mouseUp (const juce::MouseEvent&)
    {
        if (dragging != Handle::none)
            if (auto* p = processor.state.getParameter (dragging == Handle::frequency ? pid::freq : pid::width))
                p->endChangeGesture();
        dragging = Handle::none;
        repaint();
    }

    void BandDisplay::mouseDoubleClick (const juce::MouseEvent& e)
    {
        const auto h = handleAt (e.position);
        if (h == Handle::none)
            return;
        if (auto* p = processor.state.getParameter (h == Handle::frequency ? pid::freq : pid::width))
            ui::gestures::resetToDefault (*p);
        repaint();
    }

    // ================================================================ compact slider
    CompactSlider::CompactSlider (juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& n) : name (n)
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setSliderSnapsToMousePosition (false);   // a click never jumps: drag moves from where it is
        slider.setColour (juce::Slider::trackColourId, look::accent);
        slider.setColour (juce::Slider::rotarySliderFillColourId, look::accent);
        slider.setMouseDragSensitivity (ui::FineSlider::normalPixels);
        addAndMakeVisible (slider);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
        slider.bindParameter (state.getParameter (paramId));
        slider.onValueChange = [this] { repaint(); };
    }

    void CompactSlider::resized()
    {
        slider.setBounds (getLocalBounds().withTrimmedTop (16).reduced (0, 0));
    }

    void CompactSlider::paint (juce::Graphics& g)
    {
        auto top = getLocalBounds().removeFromTop (16).toFloat();
        g.setFont (ui::caps (10.0f));
        g.setColour (c::knobName);
        g.drawText (i18n::upper (name), top, juce::Justification::centredLeft, true);
        g.setFont (ui::monoFont (10.5f));
        g.setColour (c::textStrong);
        g.drawText (valueText(), top, juce::Justification::centredRight, false);
    }

    // ================================================================ about
    class EmberPanel::AboutOverlay final : public juce::Component
    {
    public:
        void paint (juce::Graphics& g) override
        {
            g.fillAll (c::scrim);
            auto card = getLocalBounds().toFloat().withSizeKeepingCentre (460.0f, 300.0f);
            g.setColour (c::raisedHi);
            g.fillRoundedRectangle (card, ui::radius::panel);
            g.setColour (look::accent.withAlpha (0.6f));
            g.drawRoundedRectangle (card, ui::radius::panel, 1.0f);
            auto r = card.reduced (28.0f, 22.0f);
            g.setFont (ui::display (30.0f / emberTextScale, true).withExtraKerningFactor (0.06f));   // the wordmark keeps its size
            g.setGradientFill (juce::ColourGradient (look::glow, r.getX(), r.getY(), look::accent, r.getX(), r.getY() + 34.0f, false));
            g.drawText ("EMBER", r.removeFromTop (36.0f), juce::Justification::centredLeft);
            g.setColour (c::textStrong);
            g.setFont (ui::semiBold (15.0f));
            g.drawText ("De-Esser  " + juce::String (EMBER_VERSION_STRING), r.removeFromTop (22.0f), juce::Justification::centredLeft);
            g.setColour (c::textMuted);
            g.setFont (ui::font (13.0f));
            g.drawText (utf8 ("© 2026 Lind Audio · lind-audio.com"), r.removeFromTop (20.0f), juce::Justification::centredLeft);
            r.removeFromTop (14.0f);
            g.setColour (c::textSoft);
            g.setFont (ui::font (12.5f));
            g.drawFittedText (tr ("Mit JUCE gebaut. Schriften: Montserrat (SIL Open Font License 1.1), Roboto Mono (Apache License 2.0).\n"
                                  "Danke an alle Sängerinnen und Sänger, deren Aufnahmen beim Abstimmen geholfen haben.",
                                  "Built with JUCE. Fonts: Montserrat (SIL Open Font License 1.1), Roboto Mono (Apache License 2.0).\n"
                                  "Thanks to all singers whose recordings helped tune it."),
                              r.removeFromTop (90.0f).toNearestInt(), juce::Justification::topLeft, 6);
            g.setColour (c::textMuted);
            g.setFont (ui::font (12.0f));
            g.drawText (tr ("Klicken oder Esc zum Schließen", "Click or press Esc to close"), r, juce::Justification::bottomLeft);
        }
        void mouseUp (const juce::MouseEvent&) override { setVisible (false); }
        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key != juce::KeyPress::escapeKey)
                return false;
            setVisible (false);
            return true;
        }
    };


    // ================================================================ option texts
    juce::String EmberPanel::groupName (const juce::String& id)
    {
        if (id == pid::mode) return tr ("Modus", "Mode");
        if (id == pid::detection) return tr ("Erkennung", "Detection");
        if (id == pid::listen) return tr ("Abhören", "Listen");
        return "Stereo";
    }

    std::vector<EmberPanel::OptionText> EmberPanel::optionTexts (const juce::String& id)
    {
        if (id == pid::mode)
            return {
                { "Split", tr ("Nur der S-Bereich wird leiser. Die Stimme darunter und die Luft darüber bleiben unberührt – das S klingt danach "
                               "wie ein leiseres S. Für Gesang die beste Wahl.",
                               "Only the S range gets quieter. The voice below and the air above stay untouched – the S then sounds like a "
                               "softer S. The best choice for singing.") },
                { tr ("Breitband", "Wideband"), tr ("Die ganze Stimme wird bei jedem S kurz leiser. Klingt bei Sprache und Podcasts oft "
                                                    "natürlicher. Eine Breite gibt es in diesem Modus nicht.",
                                                    "The whole voice dips briefly on each S. Often sounds more natural for speech and podcasts. "
                                                    "There is no width in this mode.") } };
        if (id == pid::detection)
            return {
                { "Smart", tr ("Empfohlen für eine einzelne Stimme. Greift nur ein, wenn wirklich ein S-Laut klingt – helle Vokale, Luft und "
                               "Atem bleiben unberührt.",
                               "Recommended for a single voice. Steps in only when a real S sound is there – bright vowels, air and "
                               "breaths stay untouched.") },
                { tr ("Relativ", "Relative"), tr ("Misst das S im Verhältnis zur Lautstärke der Stimme: Leise und laute Stellen werden gleich "
                                                  "behandelt. Prüft aber nicht, ob es wirklich ein S-Laut ist.",
                                                  "Measures the S against the loudness of the voice: quiet and loud parts are treated alike. "
                                                  "It does not check whether it really is an S sound.") },
                { tr ("Absolut", "Absolute"), tr ("Fester Pegel wie bei einem klassischen De-Esser: Alles im S-Bereich über dem Threshold wird "
                                                  "abgesenkt, egal wie laut die Stimme gerade ist.",
                                                  "A fixed level like a classic de-esser: everything in the S range above the threshold is "
                                                  "lowered, however loud the voice is at the moment.") },
                { "Allround", tr ("Für Mixe, Busse und Instrumente, wo unter der Stimme noch anderes klingt. Reagiert auf scharfe Höhen, "
                                  "ohne eine einzelne Stimme zum Vergleich zu brauchen.",
                                  "For mixes, buses and instruments, where more is playing under the voice. Reacts to harsh highs "
                                  "without needing a single voice to compare with.") } };
        if (id == pid::listen)
            return {
                { tr ("Aus", "Off"), tr ("Du hörst das bearbeitete Signal – so, wie es aus EMBER herauskommt.",
                                         "You hear the processed signal, just as it leaves EMBER.") },
                { tr ("S-Bereich", "S range"), tr ("Du hörst nur den Bereich, der abgesenkt wird (Split: der pinke S-Bereich, Breitband: alles "
                                                   "ab der Frequenz). Hilft beim Finden von Frequenz und Breite. Wird nie gespeichert.",
                                                   "You hear only the range that is lowered (Split: the pink S range, Wideband: everything from "
                                                   "the frequency up). Helps to find frequency and width. Never saved.") },
                { "Delta", tr ("Du hörst nur, was entfernt wird. Das sollten nur S-Laute sein – hörst du Vokale, ist es zu viel. "
                               "Wird nie gespeichert.",
                               "You hear only what is removed. It should be nothing but S sounds – if you hear vowels, it is too much. "
                               "Never saved.") } };
        return {
            { "L/R", tr ("Links und rechts werden getrennt gemessen; Stereo-Link legt fest, wie stark beide gemeinsam absenken. "
                         "Der Standard für Stereospuren.",
                         "Left and right are measured separately; Stereo Link sets how much the two lower together. "
                         "The default for stereo tracks.") },
            { "M/S", tr ("Mitte und Seiten werden getrennt bearbeitet (mit Stereo-Link). Gut, wenn die S-Laute in der Mitte und an den "
                         "Seiten verschieden stark sind.",
                         "Mid and sides are processed separately (with Stereo Link). Useful when the S sounds differ between the "
                         "centre and the sides.") },
            { "Mid", tr ("Nur die Mitte wird bearbeitet, z. B. die Leadstimme in einem Mix. Die Seiten bleiben unberührt.",
                         "Only the centre is processed, e.g. the lead vocal in a mix. The sides stay untouched.") },
            { "Side", tr ("Nur die Seiten werden bearbeitet, z. B. gedoppelte Chöre oder breiter Hall. Die Mitte bleibt unberührt.",
                          "Only the sides are processed, e.g. doubled backing vocals or wide reverb. The centre stays untouched.") } };
    }

    juce::String EmberPanel::spectrumHelp (bool split)
    {
        return split ? tr ("Spektrum – Linie ziehen: Frequenz (ab hier sucht EMBER nach S-Lauten). Kanten ziehen oder Mausrad: Breite. "
                           "Shift = fein, Doppelklick = Standard. Oben: die Absenkung der letzten Sekunden.",
                           "Spectrum – drag the line: frequency (EMBER looks for S sounds from here up). Drag the edges or use the wheel: "
                           "width. Shift = fine, double-click = default. Top: the reduction of the last seconds.")
                     : tr ("Spektrum – Linie ziehen oder Mausrad: Frequenz (ab hier sucht EMBER nach S-Lauten). Breitband hat keine Breite. "
                           "Shift = fein, Doppelklick = Standard. Oben: die Absenkung der letzten Sekunden.",
                           "Spectrum – drag the line or use the wheel: frequency (EMBER looks for S sounds from here up). Wideband has no "
                           "width. Shift = fine, double-click = default. Top: the reduction of the last seconds.");
    }

    ui::Segments* EmberPanel::segmentsFor (juce::Component& anchor)
    {
        const std::pair<ui::Segments*, juce::Label*> groups[] { { &modeSeg, &modeLbl }, { &detectSeg, &detectLbl },
                                                                { &listenSeg, &listenLbl }, { &stereoSeg, &stereoLbl } };
        for (auto [seg, lbl] : groups)
            if (&anchor == seg || &anchor == lbl)
                return seg;
        return nullptr;
    }

    int EmberPanel::hoveredOption (ui::Segments& seg) const
    {
        int i = 0;
        for (auto* ch : seg.getChildren())
            if (dynamic_cast<juce::Button*> (ch) != nullptr)
            {
                if (ch->isMouseOver (false))
                    return i;
                ++i;
            }
        return -1;
    }

    void EmberPanel::showOptionCard (ui::Segments& seg, juce::Component& anchor, int option)
    {
        const auto texts = optionTexts (seg.getParamId());
        const int chosen = seg.getSelectedSegment();
        if (! juce::isPositiveAndBelow (option, (int) texts.size()))
            option = chosen;
        if (! juce::isPositiveAndBelow (option, (int) texts.size()))
            return;
        auto text = texts[(size_t) option].text;
        if (option != chosen && juce::isPositiveAndBelow (chosen, (int) texts.size()))
            text << "\n" << i18n::format (tr ("Gewählt ist gerade: {0}.", "Currently chosen: {0}."), { texts[(size_t) chosen].name });
        cardGroup = &seg;
        cardOption = option;
        infoPopup.toFront (false);
        infoPopup.show (getLocalArea (&anchor, anchor.getLocalBounds()), i18n::upper (groupName (seg.getParamId())) + utf8 (" · ") + texts[(size_t) option].name,
                        {}, text);
    }

    // ================================================================ panel
    EmberPanel::EmberPanel (EmberAudioProcessor& p, UserSettings& s, std::function<void (juce::String)> lang)
        : processor (p), settings (s), onLanguage (std::move (lang)),
          spectrumSource (p.engine.inputTap, p.engine.outputTap),
          display (p, spectrumSource),
          detector (p),
          amountBig (p.state, pid::amount, "Amount", look::accent),
          threshK (p.state, pid::threshold, "Threshold", look::accent),
          rangeK (p.state, pid::range, "Range", look::accent),
          amountK (p.state, pid::amount, "Amount", look::accent),
          attackK (p.state, pid::attack, "Attack", look::accent),
          releaseK (p.state, pid::release, "Release", look::accent),
          mixK (p.state, pid::mix, "Mix", look::accent),
          outK (p.state, pid::output, "Output", look::accent),
          lookS (p.state, pid::lookahead, "Lookahead"),
          linkS (p.state, pid::link, tr ("Stereo-Link", "Stereo Link")),
          modeSeg (p.state, pid::mode, { "Split", tr ("Breitband", "Wideband") }, look::accent),
          detectSeg (p.state, pid::detection, { "Smart", tr ("Relativ", "Relative"), tr ("Absolut", "Absolute"), "Allround" }, look::accent),
          listenSeg (p.state, pid::listen, { tr ("Aus", "Off"), tr ("S-Bereich", "S range"), "Delta" }, look::accent),
          stereoSeg (p.state, pid::stereo, { "L/R", "M/S", "Mid", "Side" }, look::accent)
    {

        // ---- top bar
        styleSegment (simpleBtn, "SIMPLE", 1);
        styleSegment (advancedBtn, "ADVANCED", 1);
        simpleBtn.onClick = [this] { setAdvanced (false); };
        advancedBtn.onClick = [this] { setAdvanced (true); };
        autoBtn.getProperties().set ("fire", true);
        autoBtn.setColour (juce::TextButton::textColourOffId, c::fireInk);
        autoBtn.onClick = [this] { autoClicked(); };
        bypassBtn.setButtonText ("BYPASS");
        bypassBtn.setClickingTogglesState (true);
        bypassBtn.setColour (juce::TextButton::buttonOnColourId, c::bypass);
        if (auto* bp = p.state.getParameter (pid::bypass))
            bypassAttachment = std::make_unique<juce::ButtonParameterAttachment> (*bp, bypassBtn);
        styleShape (menuBtn, hamburger());
        menuBtn.onClick = [this] { showMainMenu(); };

        // ---- preset bar
        styleShape (prevBtn, triangle (false));
        styleShape (nextBtn, triangle (true));
        prevBtn.onClick = [this] { processor.stepPreset (-1); showPresetInfo(); };
        nextBtn.onClick = [this] { processor.stepPreset (1); showPresetInfo(); };
        presetBtn.onClick = [this] { showPresetMenu(); };
        presetBtn.setColour (juce::TextButton::textColourOffId, c::textStrong);
        saveBtn.setButtonText (tr ("SPEICHERN", "SAVE"));
        saveBtn.onClick = [this] { savePresetAs(); };
        styleSegment (slotA, "A", 3);
        styleSegment (slotB, "B", 3);
        slotA.onClick = [this] { processor.switchSlot (0); refreshButtons(); };
        slotB.onClick = [this] { processor.switchSlot (1); refreshButtons(); };
        slotCopy.setButtonText (utf8 ("A → B"));
        slotCopy.onClick = [this] { processor.copyActiveToOther(); };

        for (juce::Component* comp : { (juce::Component*) &simpleBtn, (juce::Component*) &advancedBtn, (juce::Component*) &autoBtn,
                                       (juce::Component*) &bypassBtn, (juce::Component*) &menuBtn, (juce::Component*) &prevBtn,
                                       (juce::Component*) &nextBtn, (juce::Component*) &presetBtn, (juce::Component*) &saveBtn,
                                       (juce::Component*) &slotA, (juce::Component*) &slotB, (juce::Component*) &slotCopy })
            addAndMakeVisible (*comp);

        // ---- SIMPLE
        mascot.onClick = [this]
        {
            // while he listens or thinks a click on him does nothing (the AUTO button cancels): no accidental cancel
            if (const auto ph = processor.getAutoPhase(); ph != EmberAudioProcessor::AutoPhase::listening && ph != EmberAudioProcessor::AutoPhase::analysing)
                autoClicked();
        };
        mascot.setSealStage (true);
        addChildComponent (mascot);
        addChildComponent (meter);
        addChildComponent (advancedMeter);
        amountBig.setHero (true);
        amountBig.setDialSize (150);
        addChildComponent (amountBig);
        styleLabel (voiceLbl, tr ("Stimmlage", "Voice"));
        addChildComponent (voiceLbl);
        styleSegment (voiceLow, tr ("TIEF", "LOW"), 2);
        styleSegment (voiceMid, tr ("MITTEL", "MID"), 2);
        styleSegment (voiceHigh, tr ("HOCH", "HIGH"), 2);
        voiceLow.onClick = [this] { setVoice (0); };
        voiceMid.onClick = [this] { setVoice (1); };
        voiceHigh.onClick = [this] { setVoice (2); };
        listenBtn.setButtonText ("DELTA");
        listenBtn.onClick = [this]
        {
            if (auto* lp = processor.state.getParameter (pid::listen))
                ui::gestures::setReal (*lp, lp->convertFrom0to1 (lp->getValue()) > 1.5f ? 0.0f : 2.0f);
        };
        for (auto* b : { &voiceLow, &voiceMid, &voiceHigh, &listenBtn })
            addChildComponent (*b);

        status.setFont (ui::font (13.5f));
        status.setColour (juce::Label::textColourId, c::textSoft);
        status.setJustificationType (juce::Justification::centredLeft);
        status.setMinimumHorizontalScale (0.8f);
        status.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (status);

        // ---- ADVANCED
        addChildComponent (display);
        addChildComponent (detector);
        for (auto* k : { &threshK, &rangeK, &amountK, &attackK, &releaseK, &mixK, &outK })
        {
            k->setDialSize (ui::dialsize::primary);
            addChildComponent (*k);
        }
        addChildComponent (lookS);
        addChildComponent (linkS);
        styleLabel (modeLbl, tr ("Modus", "Mode"));
        styleLabel (detectLbl, tr ("Erkennung", "Detection"));
        styleLabel (listenLbl, tr ("Abhören", "Listen"));
        styleLabel (stereoLbl, "Stereo");
        for (auto* l : { &modeLbl, &detectLbl, &listenLbl, &stereoLbl })
        {
            l->setInterceptsMouseClicks (true, false);   // hover = the chosen option's card
            infos.push_back (std::make_unique<ui::HoverInfo> (*l, [l] { return l->getText(); }, nullptr, [] { return juce::String ("-"); }));
        }
        for (juce::Component* comp : { (juce::Component*) &modeSeg, (juce::Component*) &detectSeg, (juce::Component*) &listenSeg,
                                       (juce::Component*) &modeLbl, (juce::Component*) &detectLbl, (juce::Component*) &listenLbl,
                                       (juce::Component*) &stereoSeg, (juce::Component*) &stereoLbl })
            addChildComponent (*comp);

        // ---- explanations (info card on hover)
        const auto amountText = tr ("Wie stark die S-Laute reduziert werden. 50 % = genau das, was Threshold und Range (oder AUTO) vorgeben, "
                                    "100 % = anderthalbmal so viel, 0 % = aus.",
                                    "How hard the S sounds are reduced. 50 % = exactly what Threshold and Range (or AUTO) set, "
                                    "100 % = one and a half times as much, 0 % = off.");
        amountBig.setDescription (amountText);
        amountK.setDescription (amountText);
        threshK.setDescription (tr ("Ab diesem S-Pegel greift der De-Esser ein. Tiefer = mehr S-Laute werden erfasst. "
                                    "Auch an der S-Pegel-Anzeige rechts ziehbar.",
                                    "The S level above which the de-esser steps in. Lower = more S sounds are caught. "
                                    "Also draggable on the S level meter on the right."));
        rangeK.setDescription (tr ("So viel nimmt der De-Esser höchstens weg. 4–8 dB klingen natürlich, deutlich mehr kann lispeln lassen.",
                                   "The most the de-esser ever removes. 4–8 dB sound natural, much more can make the voice lisp."));
        attackK.setDescription (tr ("Wie schnell der De-Esser zupackt, wenn ein S beginnt.", "How fast the de-esser grabs when an S starts."));
        releaseK.setDescription (tr ("Wie schnell er nach dem S wieder loslässt. Kurz (30–40 ms) lässt den nächsten Vokal unberührt.",
                                     "How fast it lets go after the S. Short (30–40 ms) leaves the next vowel untouched."));
        mixK.setDescription (tr ("Anteil des bearbeiteten Signals. Unter 100 % bleibt etwas vom Original-S erhalten.",
                                 "Share of the processed signal. Below 100 % some of the original S remains."));
        outK.setDescription (tr ("Ausgangspegel nach dem De-Esser.", "Output level after the de-esser."));
        stereoSeg.setInfo ("Stereo", tr ("L/R: links und rechts (mit Stereo-Link). M/S: Mitte und Seiten getrennt (mit Stereo-Link). "
                                         "Mid: nur die Mitte (die Leadstimme), die Seiten bleiben unberührt. Side: nur die Seiten (z. B. gedoppelte Chöre).",
                                         "L/R: left and right (with Stereo Link). M/S: mid and sides separately (with Stereo Link). "
                                         "Mid: only the centre (the lead vocal), the sides stay untouched. Side: only the sides (e.g. doubled backing vocals)."));
        modeSeg.setInfo (tr ("Modus", "Mode"),
                         tr ("Split: nur der S-Bereich wird leiser, die Stimme darunter und die Luft darüber bleiben. "
                             "Breitband: die ganze Stimme taucht beim S kurz ab (natürlicher bei Sprache).",
                             "Split: only the S range gets quieter, the voice below and the air above stay. "
                             "Wideband: the whole voice dips briefly on each S (more natural for speech)."));
        detectSeg.setInfo (tr ("Erkennung", "Detection"),
                           tr ("Smart (empfohlen für eine Stimme): reagiert nur, wenn wirklich ein S-Laut klingt. Helle Vokale, Luft und Atem "
                               "bleiben unberührt. Relativ: misst das S im Verhältnis zur Lautstärke der Stimme, leise und laute "
                               "Stellen gleich. Absolut: fester Pegel wie ein klassischer De-Esser. Allround: für Mixe, Busse und "
                               "Instrumente, wo unter der Stimme noch anderes klingt.",
                               "Smart (recommended for a single voice): reacts only when a real S sound is there. Bright vowels, air and breaths "
                               "stay untouched. Relative: measures the S against the loudness of the voice, quiet and loud "
                               "parts alike. Absolute: fixed level like a classic de-esser. Allround: for mixes, buses and "
                               "instruments, where more is playing under the voice."));
        listenSeg.setInfo (tr ("Abhören", "Listen"),
                           tr ("S-Bereich: nur den Bereich hören, der abgesenkt wird (Split: der pinke S-Bereich im Spektrum, Breitband: alles ab der Frequenz). "
                               "Delta: nur das hören, was entfernt wird. Es sollten nur S-Laute zu hören sein. "
                               "Abhören wird nie gespeichert.",
                               "S range: hear only the range that is lowered (Split: the pink S range in the spectrum, Wideband: everything from the frequency up). "
                               "Delta: hear only what is removed. You should hear nothing but S sounds. "
                               "Listening is never saved."));

        addInfo (simpleBtn, "SIMPLE", tr ("Die einfache Ansicht: Ember, Amount, Stimmlage und Delta. Reicht für die meisten Stimmen.",
                                          "The simple view: Ember, Amount, voice and Delta. Enough for most vocals."));
        addInfo (advancedBtn, "ADVANCED", tr ("Alle Einstellungen: das Spektrum mit Frequenz und Breite des S-Bereichs, die S-Pegel-Anzeige mit dem Threshold und alle Regler.",
                                              "Every setting: the spectrum with the frequency and width of the S range, the S level meter with the threshold, and every control."));
        addInfo (autoBtn, "AUTO", tr ("Spiel deinen Gesang ab und klick auf AUTO. Ember hört 12 Sekunden Gesang zu (Pausen zählen nicht) und ist dann von selbst fertig, "
                                      "auch wenn du nach mindestens 3 Sekunden die Wiedergabe stoppst. Er stellt nur Frequenz, Threshold, Range und Release ein (und Amount auf 50 %, wenn es unter 25 % stand), alles andere bleibt. "
                                      "Deine Einstellung von vorher bleibt auf dem Slot, auf dem du warst (meist A), das Ergebnis kommt auf den anderen. Liegt dort schon eine "
                                      "andere Einstellung von dir, bleibt sie, und Strg+Z holt die vorherige zurück. Während er zuhört, bricht ein Klick ab.",
                                      "Play your vocal and click AUTO. Ember listens to 12 seconds of vocal (pauses don't count) and then finishes on his own, "
                                      "also when you stop playback after at least 3 seconds. He sets only frequency, threshold, range and release (and Amount to 50 % if it was below 25 %), everything else stays. "
                                      "Your previous setting stays on the slot you were on (usually A), the result goes to the other one. If that slot already holds "
                                      "a different setting of yours, it stays, and Ctrl+Z brings back the previous one. While he listens, a click cancels."));
        addInfo (bypassBtn, "BYPASS", tr ("Schaltet den De-Esser aus, um mit dem Original zu vergleichen. Die Latenz bleibt gleich.",
                                          "Switches the de-esser off to compare with the original. The latency stays the same."));
        addInfo (menuBtn, tr ("Menü", "Menu"), tr ("Rückgängig / Wiederholen, Fenstergröße, Sprache, Animation, Handbuch, Ember-Tipp, Lizenz und Infos zu EMBER.",
                                                   "Undo / redo, window size, language, animation, manual, Ember's tip, licence and about EMBER."));
        addInfo (prevBtn, tr ("Voriges Preset", "Previous preset"), tr ("Lädt das vorige Preset.", "Loads the previous preset."));
        addInfo (nextBtn, tr ("Nächstes Preset", "Next preset"), tr ("Lädt das nächste Preset.", "Loads the next preset."));
        addInfo (presetBtn, "Preset", tr ("Werks-Presets und deine eigenen Presets (dort auch umbenennen und löschen). Ein * zeigt, dass du seit dem Laden etwas geändert hast.",
                                          "Factory presets and your own presets (rename and delete them there too). A * shows you changed something since loading."));
        addInfo (saveBtn, tr ("Preset speichern", "Save preset"), tr ("Speichert die aktuelle Einstellung als eigenes Preset.",
                                                                      "Saves the current setting as your own preset."));
        addInfo (slotA, tr ("Slot A", "Slot A"), tr ("Zwei Einstellungen vergleichen: Die Slots A und B merken sich je eine Einstellung. "
                                                     "Umschalten, hören, die bessere behalten.",
                                                     "Compare two settings: slots A and B each keep one setting. Switch, listen, keep the better one."));
        addInfo (slotB, tr ("Slot B", "Slot B"), tr ("Beim ersten Klick startet B als Kopie von A.", "On the first click B starts as a copy of A."));
        addInfo (slotCopy, tr ("Kopieren", "Copy"), tr ("Kopiert die aktive Einstellung in den anderen Slot: A → B, wenn A aktiv ist, sonst B → A. "
                                                        "A und B werden mit dem Projekt gespeichert.",
                                                        "Copies the active setting into the other slot: A → B while A is active, otherwise B → A. "
                                                        "A and B are saved with the project."));
        addInfo (mascot, "Ember", tr ("Ember zeigt, was der De-Esser tut: Er tanzt zur Stimme, und jedes S, das abgesenkt wird, fliegt ihm zu und er frisst es. "
                                      "Was er sagt, steht auf der Tafel über ihm. Klick auf ihn startet AUTO.",
                                      "Ember shows what the de-esser does: he dances to the vocal, and every S that is reduced flies to him and he eats it. "
                                      "What he says appears on the plate above him. Click him to start AUTO."));
        for (auto* m : { &meter, &advancedMeter })
            addInfo (*m, tr ("Absenkung", "Reduction"), tr ("So viel leiser werden die S-Laute gerade, in dB (Gain Reduction).",
                                                            "How much quieter the S sounds are right now, in dB (gain reduction)."));
        addInfo (voiceLow, tr ("Tiefe Stimme", "Low voice"), tr ("Frequenz 5 kHz, greift etwas früher: für tiefe und männliche Stimmen.", "Frequency 5 kHz, steps in a little earlier: for low and male voices."));
        addInfo (voiceMid, tr ("Mittlere Stimme", "Mid voice"), tr ("Frequenz 6 kHz, passt für die meisten Stimmen.", "Frequency 6 kHz, fits most voices."));
        addInfo (voiceHigh, tr ("Hohe Stimme", "High voice"), tr ("Frequenz 7,4 kHz, für hohe und weibliche Stimmen.", "Frequency 7.4 kHz, for high and female voices."));
        addInfo (listenBtn, "Delta", tr ("Nur das hören, was entfernt wird. Hörst du Vokale, ist es zu viel.",
                                         "Hear only what is removed. If you hear vowels, it is too much."));
        addInfo (display, tr ("Spektrum", "Spectrum"),
                 tr ("Pink: der S-Bereich, der bei S-Lauten leiser wird. Oben: wie viel in den letzten Sekunden abgesenkt wurde.\n"
                     "Pinke Linie ziehen = Frequenz: ab hier sucht der De-Esser nach S-Lauten (tiefe Stimmen um 5 kHz, hohe um 7–7,5 kHz). "
                     "Kanten des S-Bereichs ziehen oder Mausrad = Breite in Oktaven (nur Split): schmal = nur das S, breit = auch die Luft darüber. "
                     "Im Breitband-Modus stellt das Mausrad die Frequenz. Shift = fein, Doppelklick = Standard.",
                     "Pink: the S range that gets quieter on S sounds. Top: how much was reduced over the last seconds.\n"
                     "Drag the pink line = frequency: from here up the de-esser looks for S sounds (low voices around 5 kHz, high ones around 7–7.5 kHz). "
                     "Drag the edges of the S range or use the wheel = width in octaves (Split only): narrow = only the S, wide = the air above it too. "
                     "In Wideband mode the wheel sets the frequency. Shift = fine, double-click = default."),
                 [this] { return display.markerText(); });
        addInfo (lookS, "Lookahead", tr ("So viel früher sieht der De-Esser das S kommen und erfasst auch dessen Anfang. "
                                         "Die Latenz bleibt immer bei 10 ms.",
                                         "The de-esser sees the S coming this much earlier and also catches its start. "
                                         "The latency always stays at 10 ms."),
                 [this] { return lookS.valueText(); });
        addInfo (linkS, tr ("Stereo-Link", "Stereo Link"),
                 tr ("Wie stark die beiden Kanäle gemeinsam arbeiten (bei L/R und M/S). 100 %: Ein S links senkt beide Seiten gleich ab, "
                     "das Stereobild bleibt. 0 %: Jeder Kanal arbeitet für sich.",
                     "How much the two channels work together (with L/R and M/S). 100 %: an S on the left lowers both sides alike, "
                     "the stereo image stays. 0 %: each channel works on its own."),
                 [this] { return linkS.valueText(); });
        addInfo (detector, tr ("S-Pegel", "S level"), tr ("Wie laut die S-Laute gerade sind, in dB. Über der pinken Linie (Threshold) wird reduziert. "
                                                          "Grau gestreift = Smart erkennt keinen S-Laut. Linie ziehen = Threshold, Mausrad = Schritt, Shift = fein.",
                                                          "How loud the S sounds are right now, in dB. Above the pink line (threshold) they are reduced. "
                                                          "Grey stripes = Smart hears no S sound. Drag the line = threshold, wheel = step, Shift = fine."));

        addAndMakeVisible (sealOverlay);
        sealOverlay.knobAngle = [this]
        {
            // where the knob shows its value dot (its animated position, the same as the dot)
            auto& sl = amountBig.slider;
            const auto rp = sl.getRotaryParameters();
            const double pos = (double) sl.getProperties().getWithDefault (ui::VocalypseLookAndFeel::animPosProperty,
                                                                            sl.valueToProportionOfLength (sl.getValue()));
            return rp.startAngleRadians + (float) pos * (rp.endAngleRadians - rp.startAngleRadians);
        };
        addChildComponent (infoPopup);
        about = std::make_unique<AboutOverlay>();
        about->setWantsKeyboardFocus (true);
        addChildComponent (*about);
        addChildComponent (licenceLock);
        licenceLock.setLicence (&processor.getLicence());

        juce::Desktop::getInstance().addGlobalMouseListener (&dragWatch);
        setWantsKeyboardFocus (true);   // Ctrl+Z / Ctrl+Shift+Z
        setSize (look::baseW, look::baseH);
        advanced = (bool) processor.state.state.getProperty ("advanced", false);
        setAdvanced (advanced);
        LiveMascotView::setAnimationLevel (animationLevelFromString (settings.file().getValue ("animation", "full")));
        startTimerHz (30);
    }

    EmberPanel::~EmberPanel()
    {
        stopTimer();
        juce::Desktop::getInstance().removeGlobalMouseListener (&dragWatch);
    }

    void EmberPanel::addInfo (juce::Component& comp, const juce::String& title, const juce::String& text, std::function<juce::String()> value)
    {
        infos.push_back (std::make_unique<ui::HoverInfo> (comp, [title] { return title; }, std::move (value), [text] { return text; }));
    }

    void EmberPanel::showInfo (juce::Component& anchor, const juce::String& title, const juce::String& value, const juce::String& text)
    {
        // the spectrum is never covered: its explanation is in the info line while the mouse is over it (2026-11-24)
        if (&anchor == &display || display.isParentOf (&anchor))
            return;
        // no card while a mouse button is held or right after it is let go (a drag ends, the mouse rests)
        const bool sameCard = infoPopup.isShowingFor (getLocalArea (&anchor, anchor.getLocalBounds()));
        if (! sameCard && (dragWatch.quiet() || juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown()))
            return;
        if (auto* seg = segmentsFor (anchor))
        {
            showOptionCard (*seg, anchor, &anchor == seg ? hoveredOption (*seg) : -1);
            return;
        }
        cardGroup = nullptr;
        infoPopup.toFront (false);
        infoPopup.show (getLocalArea (&anchor, anchor.getLocalBounds()), title, value, text);
    }

    void EmberPanel::hideInfo (juce::Component&)
    {
        cardGroup = nullptr;
        infoPopup.hide();
    }

    std::vector<juce::Rectangle<float>> EmberPanel::plates() const
    {
        using F = juce::Rectangle<float>;
        if (advanced)
            return { F (0, 0, 900, 60), F (24, 68, 852, 32), F (12, 108, 876, 484) };
        return { F (0, 0, 900, 60), F (24, 68, 852, 32), F (548, 112, 336, 410), F (16, 528, 868, 58) };
    }

    void EmberPanel::paint (juce::Graphics& g)
    {
        // the chamber: the back wall of the seal (Jakob 2026-11-19), cached per size and screen density
        const float scale = juce::jlimit (1.0f, 3.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        const int cw = juce::roundToInt ((float) getWidth() * scale), ch = juce::roundToInt ((float) getHeight() * scale);
        // while the window is being resized the last wall is stretched; it is drawn anew once the size stays
        bool rebuild = chamber.getWidth() != cw || chamber.getHeight() != ch;
        if (rebuild && chamber.isValid())
        {
            const double nowMs = juce::Time::getMillisecondCounterHiRes();
            if (chamberWanted != juce::Point<int> (cw, ch))
            {
                chamberWanted = { cw, ch };
                chamberWantedSince = nowMs;
                juce::Timer::callAfterDelay (300, [safe = juce::Component::SafePointer<EmberPanel> (this)] { if (safe != nullptr) safe->repaint(); });
            }
            rebuild = nowMs - chamberWantedSince > 250.0;
        }
        if (rebuild)
        {
            chamber = juce::Image (juce::Image::RGB, juce::jmax (1, cw), juce::jmax (1, ch), false);
            {
                juce::Graphics cg (chamber);   // finished before its pixels are read (Direct2D images)
                cg.addTransform (juce::AffineTransform::scale (scale));
                seal::drawChamber (cg, getLocalBounds().toFloat(), juce::Rectangle<float> (28.0f, 116.0f, 520.0f, 402.0f));
            }
            // Ember's stage gets its piece of the wall and paints it itself (opaque: nothing underneath is redrawn)
            const auto mb = mascot.getBounds().toFloat();
            const auto px = juce::Rectangle<float> (mb.getX() * scale, mb.getY() * scale, mb.getWidth() * scale, mb.getHeight() * scale)
                                .getSmallestIntegerContainer().getIntersection (chamber.getBounds());
            mascot.setBackdrop (chamber.getClippedImage (px), scale);
        }
        g.drawImage (chamber, getLocalBounds().toFloat());
        auto r = getLocalBounds().toFloat();

        // glass plates the controls sit on (the wall shows through)
        for (auto p : plates())
        {
            const bool header = p.getY() < 1.0f;
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xe0140610), 0, p.getY(), juce::Colour (0xeb080308), 0, p.getBottom(), false));
            if (header) g.fillRect (p); else g.fillRoundedRectangle (p, 12.0f);
            g.setColour (look::accent.withAlpha (0.28f));
            if (header) g.fillRect (p.getX(), p.getBottom() - 1.0f, p.getWidth(), 1.0f); else g.drawRoundedRectangle (p, 12.0f, 1.0f);
        }

        auto top = r.removeFromTop (60.0f);

        auto brand = top.reduced (24.0f, 10.0f).removeFromLeft (360.0f);
        g.setFont (ui::display (30.0f / emberTextScale, true).withExtraKerningFactor (0.06f));   // the wordmark keeps its size
        g.setGradientFill (juce::ColourGradient (look::glow, brand.getX(), brand.getY(), look::accent, brand.getX(), brand.getBottom(), false));
        g.drawText ("EMBER", brand.removeFromLeft (128.0f), juce::Justification::centredLeft);
        g.setFont (ui::semiBold (15.0f, 0.04f));
        g.setColour (c::textStrong);
        auto sub = brand.removeFromLeft (220.0f);
        g.drawText ("De-Esser", sub.removeFromTop (sub.getHeight() * 0.55f), juce::Justification::bottomLeft);
        g.setFont (ui::caps (10.5f));
        g.setColour (c::textMuted);
        g.drawText (utf8 ("LIND AUDIO"), sub, juce::Justification::topLeft);

        if (advanced)
        {
            // the knob row in three groups (level | time | output), then the two small faders
            g.setColour (look::accent.withAlpha (0.14f));
            for (float x : { 322.0f, 526.0f, 733.0f })
                g.fillRect (x, 364.0f, 1.0f, 82.0f);
        }

    }

    void EmberPanel::resized() { layout(); }

    void EmberPanel::setAdvanced (bool a)
    {
        advanced = a;
        processor.state.state.setProperty ("advanced", a, nullptr);
        simpleBtn.setToggleState (! a, juce::dontSendNotification);
        advancedBtn.setToggleState (a, juce::dontSendNotification);

        // SIMPLE has no S range listening (its DELTA button only knows Delta): switch it off rather than hide it
        if (! a && std::lround (processor.state.getRawParameterValue (pid::listen)->load()) == 1)
            if (auto* lp = processor.state.getParameter (pid::listen))
                ui::gestures::setReal (*lp, 0.0f);

        // Ember lives in SIMPLE only (Jakob 2026-11-10)
        for (juce::Component* comp : { (juce::Component*) &mascot, (juce::Component*) &meter, (juce::Component*) &amountBig,
                                       (juce::Component*) &voiceLbl, (juce::Component*) &voiceLow, (juce::Component*) &voiceMid,
                                       (juce::Component*) &voiceHigh, (juce::Component*) &listenBtn })
            comp->setVisible (! a);
        for (juce::Component* comp : { (juce::Component*) &display, (juce::Component*) &detector, (juce::Component*) &advancedMeter,
                                       (juce::Component*) &threshK, (juce::Component*) &rangeK, (juce::Component*) &amountK,
                                       (juce::Component*) &attackK, (juce::Component*) &releaseK, (juce::Component*) &lookS,
                                       (juce::Component*) &mixK, (juce::Component*) &outK, (juce::Component*) &modeSeg,
                                       (juce::Component*) &detectSeg, (juce::Component*) &listenSeg, (juce::Component*) &modeLbl,
                                       (juce::Component*) &detectLbl, (juce::Component*) &listenLbl,
                                       (juce::Component*) &linkS, (juce::Component*) &stereoSeg, (juce::Component*) &stereoLbl })
            comp->setVisible (a);
        infoPopup.hide();
        // the window's seal knows the view at once: a repaint before its next update would otherwise draw the Amount
        // knob's small seal into ADVANCED, where it stayed as a circle next to the meters (Jakob 2026-11-24)
        sealOverlay.setSimpleView (! a);
        layout();
        refreshButtons();
        repaint();   // the glass plates differ between the views (only the controls' areas were redrawn: old plate edges stayed)
    }

    void EmberPanel::layout()
    {
        using R = juce::Rectangle<int>;
        simpleBtn.setBounds (R (420, 16, 92, 28));
        advancedBtn.setBounds (R (512, 16, 100, 28));
        autoBtn.setBounds (R (630, 16, 100, 28));
        bypassBtn.setBounds (R (740, 16, 92, 28));
        menuBtn.setBounds (R (842, 14, 34, 32));

        prevBtn.setBounds (R (30, 70, 28, 28));
        presetBtn.setBounds (R (60, 71, 300, 26));
        nextBtn.setBounds (R (362, 70, 28, 28));
        saveBtn.setBounds (R (400, 71, 110, 26));
        slotA.setBounds (R (700, 71, 40, 26));
        slotB.setBounds (R (742, 71, 40, 26));
        slotCopy.setBounds (R (790, 71, 80, 26));

        if (! advanced)
        {
            mascot.setBounds (R (28, 116, 520, 402));
            meter.setBounds (R (552, 126, 64, 386));
            amountBig.setBounds (R (624, 118, 248, 230));
            voiceLbl.setBounds (R (624, 362, 248, 18));
            voiceLow.setBounds (R (624, 382, 82, 32));
            voiceMid.setBounds (R (707, 382, 82, 32));
            voiceHigh.setBounds (R (790, 382, 82, 32));
            listenBtn.setBounds (R (624, 430, 248, 32));
            status.setBounds (R (40, 530, 820, 50));
        }
        else
        {
            display.setBounds (R (28, 118, 696, 222));
            advancedMeter.setBounds (R (730, 118, 62, 222));
            detector.setBounds (R (796, 114, 76, 230));
            // three groups of knobs (level | time | output) and, on the right above STEREO, the two small faders
            int x = 28;
            int i = 0;
            for (auto* k : { &threshK, &rangeK, &amountK, &attackK, &releaseK, &mixK, &outK })
            {
                k->setBounds (R (x, 350, 96, 108));
                x += 96 + (i == 2 || i == 4 ? 12 : 0);
                ++i;
            }
            lookS.setBounds (R (742, 366, 128, 34));
            linkS.setBounds (R (742, 414, 128, 34));
            modeLbl.setBounds (R (40, 466, 120, 18));
            modeSeg.setBounds (R (40, 484, 140, 30));
            detectLbl.setBounds (R (194, 466, 120, 18));
            detectSeg.setBounds (R (194, 484, 272, 30));
            listenLbl.setBounds (R (480, 466, 120, 18));
            listenSeg.setBounds (R (480, 484, 214, 30));
            stereoLbl.setBounds (R (708, 466, 120, 18));
            stereoSeg.setBounds (R (708, 484, 162, 30));
            status.setBounds (R (40, 528, 820, 50));
        }
        infoPopup.setBounds (getLocalBounds());
        sealOverlay.setBounds (getLocalBounds());
        if (about != nullptr)
            about->setBounds (getLocalBounds());
        licenceLock.setBounds (getLocalBounds().withTrimmedTop (60));   // the top bar (≡ menu) stays usable
    }

    void EmberPanel::setVoice (int voice)
    {
        using Result = EmberAudioProcessor::VoiceResult;
        const auto result = processor.setVoice (voice);
        if (result == Result::keptAutoFrequency)
            showNotice (i18n::format (tr ("AUTO hat in diesem Bereich {0} gefunden – die Frequenz bleibt.",
                                          "AUTO found {0} in this range – the frequency stays."),
                                      { hzText (processor.state.getRawParameterValue (pid::freq)->load()) }));
        else if (result == Result::setKeptThreshold)
            showNotice (i18n::format (tr ("Stimmlage gesetzt. Der Threshold bleibt bei {0} dB (von Hand, Preset oder AUTO gesetzt).",
                                          "Voice set. The threshold stays at {0} dB (set by hand, a preset or AUTO)."),
                                      { i18n::number (processor.state.getRawParameterValue (pid::threshold)->load(), 1) }));
        refreshButtons();
    }

    void EmberPanel::showNotice (const juce::String& text)
    {
        notice = text;
        noticeUntil = juce::Time::getMillisecondCounterHiRes() * 0.001 + 5.0;
    }

    void EmberPanel::warn (const juce::String& title, const juce::String& message)
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::WarningIcon)
                                          .withTitle (title)
                                          .withMessage (message)
                                          .withButton ("OK")
                                          .withAssociatedComponent (this),
                                      nullptr);
    }

    void EmberPanel::autoClicked()
    {
        using Phase = EmberAudioProcessor::AutoPhase;
        if (processor.getLicence().isLocked())
            return;
        // while listening or analysing the button cancels; finishing happens by itself (12 s of voice, or playback stopped)
        if (processor.getAutoPhase() == Phase::listening || processor.getAutoPhase() == Phase::analysing)
            processor.cancelAuto();
        else
        {
            processor.startAuto();
        }
        refreshButtons();
    }

    void EmberPanel::showPresetInfo()
    {
        // a factory preset says in the status line what it is for (user presets have no text)
        const auto& factory = factoryPresets();
        const int i = processor.getFactoryIndex();
        if (juce::isPositiveAndBelow (i, (int) factory.size()))
            showNotice (presetName (factory[(size_t) i]) + ": " + presetInfo (factory[(size_t) i]));
    }

    juce::PopupMenu EmberPanel::buildPresetMenu()
    {
        juce::PopupMenu menu;
        menu.setLookAndFeel (&getLookAndFeel());   // the plugin's look, also when the menu is its own desktop window
        menu.addSectionHeader (tr ("Werks-Presets", "Factory presets"));
        const auto& factory = factoryPresets();
        // "Default" on top, then one submenu per category (ticked while one of its presets is loaded)
        for (int i = 0; i < (int) factory.size(); ++i)
            if (factory[(size_t) i].category == PresetCategory::none)
                menu.addItem (1 + i, presetName (factory[(size_t) i]), true, processor.getFactoryIndex() == i);
        for (const auto category : presetCategories)
        {
            juce::PopupMenu sub;
            bool current = false;
            for (int i = 0; i < (int) factory.size(); ++i)
                if (factory[(size_t) i].category == category)
                {
                    sub.addItem (1 + i, presetName (factory[(size_t) i]), true, processor.getFactoryIndex() == i);
                    current = current || processor.getFactoryIndex() == i;
                }
            menu.addSubMenu (categoryName (category), sub, true, nullptr, current);
        }
        const auto users = userPresets();
        menu.addSectionHeader (tr ("Eigene Presets", "Your presets"));
        if (users.isEmpty())
            menu.addItem (-1, tr ("(noch keine – mit SPEICHERN anlegen)", "(none yet – create one with SAVE)"), false);
        juce::PopupMenu rename, remove;
        for (int i = 0; i < users.size(); ++i)
        {
            const auto name = users[i].getFileNameWithoutExtension();
            menu.addItem (100 + i, name, true, processor.getFactoryIndex() < 0 && processor.getPresetName() == name);
            rename.addItem (2000 + i, name);
            remove.addItem (3000 + i, name);
        }
        menu.addSeparator();
        menu.addItem (1000, tr ("Preset speichern …", "Save preset …"));
        menu.addSubMenu (tr ("Preset umbenennen", "Rename preset"), rename, ! users.isEmpty());
        menu.addSubMenu (tr ("Preset löschen", "Delete preset"), remove, ! users.isEmpty());
        menu.addItem (1001, tr ("Preset-Ordner öffnen", "Open preset folder"));
        return menu;
    }

    void EmberPanel::showPresetMenu()
    {
        const auto users = userPresets();   // the same list (and order) the menu was built from
        buildPresetMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetBtn).withMinimumWidth (presetBtn.getWidth()),
                            [safe = juce::Component::SafePointer<EmberPanel> (this), users] (int id)
                            {
                                if (safe == nullptr || id == 0)
                                    return;
                                auto& self = *safe;
                                if (id >= 1 && id < 100)
                                {
                                    self.processor.loadFactoryPreset (id - 1);
                                    self.showPresetInfo();
                                }
                                else if (id >= 100 && id < 1000 && id - 100 < users.size())
                                {
                                    if (! self.processor.loadUserPreset (users[id - 100]))
                                        self.warn (tr ("Preset laden", "Load preset"),
                                                   i18n::format (tr ("„{0}“ ließ sich nicht laden. Die Datei fehlt oder ist kein EMBER-Preset.",
                                                                     "“{0}” could not be loaded. The file is missing or is not an EMBER preset."),
                                                                 { users[id - 100].getFileNameWithoutExtension() }));
                                }
                                else if (id >= 2000 && id < 3000 && id - 2000 < users.size())
                                    self.renamePreset (users[id - 2000]);
                                else if (id >= 3000 && id < 4000 && id - 3000 < users.size())
                                    self.deletePreset (users[id - 3000]);
                                else if (id == 1000)
                                    self.savePresetAs();
                                else if (id == 1001)
                                {
                                    userPresetFolder().createDirectory();
                                    userPresetFolder().startAsProcess();
                                }
                                self.refreshButtons();
                            });
    }

    void EmberPanel::renamePreset (const juce::File& file)
    {
        dialog = std::make_unique<juce::AlertWindow> (tr ("Preset umbenennen", "Rename preset"),
                                                      i18n::format (tr ("Neuer Name für „{0}“:", "New name for “{0}”:"), { file.getFileNameWithoutExtension() }),
                                                      juce::MessageBoxIconType::NoIcon, this);
        dialog->setLookAndFeel (&getLookAndFeel());   // the panel (and its look) outlives the window it owns
        dialog->addTextEditor ("name", file.getFileNameWithoutExtension());
        dialog->addButton (tr ("Umbenennen", "Rename"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        dialog->addButton (tr ("Abbrechen", "Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        dialog->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<EmberPanel> (this), file] (int result)
        {
            if (safe == nullptr || safe->dialog == nullptr)
                return;
            const auto name = safe->dialog->getTextEditorContents ("name");
            // the window goes after its callback (it is still on the modal stack right now)
            juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->dialog.reset(); });
            if (result != 1)
                return;
            if (const auto error = safe->processor.renameUserPreset (file, name); error.isNotEmpty())
                safe->warn (tr ("Preset umbenennen", "Rename preset"), error);
            safe->refreshButtons();
        }), false);
    }

    void EmberPanel::deletePreset (const juce::File& file)
    {
        const auto name = file.getFileNameWithoutExtension();
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                          .withTitle (tr ("Preset löschen?", "Delete preset?"))
                                          .withMessage (i18n::format (tr ("„{0}“ kommt in den Papierkorb. Projekte, die es nutzen, behalten ihre Einstellung.",
                                                                          "“{0}” goes to the recycle bin. Projects that use it keep their setting."), { name }))
                                          .withButton (tr ("Löschen", "Delete"))
                                          .withButton (tr ("Abbrechen", "Cancel"))
                                          .withAssociatedComponent (this),
                                      [safe = juce::Component::SafePointer<EmberPanel> (this), file] (int result)
                                      {
                                          if (safe == nullptr || result != 1)
                                              return;
                                          if (const auto error = safe->processor.deleteUserPreset (file); error.isNotEmpty())
                                              safe->warn (tr ("Preset löschen", "Delete preset"), error);
                                          safe->refreshButtons();
                                      });
    }

    void EmberPanel::savePresetAs()
    {
        userPresetFolder().createDirectory();
        chooser = std::make_unique<juce::FileChooser> (tr ("Preset speichern", "Save preset"),
                                                       userPresetFolder().getChildFile (tr ("Mein Preset", "My preset") + presetExtension),
                                                       juce::String ("*") + presetExtension);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe = juce::Component::SafePointer<EmberPanel> (this)] (const juce::FileChooser& fc)
                              {
                                  const auto file = fc.getResult();
                                  if (safe == nullptr || file == juce::File())
                                      return;
                                  if (! safe->processor.saveUserPreset (file))
                                      safe->warn (tr ("Preset speichern", "Save preset"),
                                                  tr ("Das Preset ließ sich nicht speichern. Ist der Ordner schreibgeschützt oder voll?",
                                                      "The preset could not be saved. Is the folder read-only or full?"));
                                  safe->refreshButtons();
                              });
    }

    juce::PopupMenu EmberPanel::buildMainMenu()
    {
        juce::PopupMenu lang;
        const auto current = settings.getLanguage();
        lang.addItem (11, tr ("Systemsprache", "System language"), true, current == "system");
        lang.addItem (12, "Deutsch", true, current == "de");
        lang.addItem (13, "English", true, current == "en");
        juce::PopupMenu menu;
        menu.addItem (50, tr ("Rückgängig (Strg+Z)", "Undo (Ctrl+Z)"), processor.canUndo());
        menu.addItem (51, tr ("Wiederholen (Strg+Umschalt+Z / Strg+Y)", "Redo (Ctrl+Shift+Z / Ctrl+Y)"), processor.canRedo());
        menu.addSeparator();
        juce::PopupMenu size;
        const auto* editor = findParentComponentOfClass<EmberEditor>();
        const int percentNow = editor != nullptr ? editor->getScalePercent() : 100;
        for (int pct : { 75, 100, 125, 150, 200 })
            size.addItem (60 + pct, juce::String (pct) + " %", editor != nullptr, percentNow == pct);
        menu.addSubMenu (tr ("Fenstergröße", "Window size"), size);
        menu.addSubMenu (tr ("Sprache", "Language"), lang);
        {
            // how much moves, for every EMBER window (2026-11-23: less work for the computer)
            juce::PopupMenu anim;
            const auto level = LiveMascotView::getAnimationLevel();
            anim.addItem (61, tr ("Voll", "Full"), true, level == AnimationLevel::full);
            anim.addItem (62, tr ("Reduziert", "Reduced"), true, level == AnimationLevel::reduced);
            anim.addItem (63, tr ("Aus", "Off"), true, level == AnimationLevel::off);
            menu.addSubMenu (tr ("Animation", "Animation"), anim);
        }
        menu.addItem (20, tr ("Handbuch öffnen", "Open manual"));
        menu.addItem (21, tr ("Ember-Tipp wieder zeigen", "Show Ember's tip again"));
        auto& licence = processor.getLicence();
        if (licence.isOn())
        {
            // Licence > status line, enter key, release this computer, buy
            const bool licensed = licence.status().access == vocalypse::licensing::Access::licensed;
            juce::PopupMenu lic;
            lic.addItem (-1, licence.describe(), false);
            lic.addSeparator();
            lic.addItem (40, tr ("Lizenzschlüssel eingeben …", "Enter licence key …"), ! licensed);
            lic.addItem (41, tr ("Diesen Computer freigeben", "Release this computer"), licensed);
            lic.addItem (42, licence.freeKey() ? tr ("Gratis-Schlüssel holen", "Get free key") : tr ("EMBER kaufen", "Buy EMBER"),
                         ! licensed && licence.buyUrl().isNotEmpty());
            menu.addSubMenu (tr ("Lizenz", "Licence"), lic);
        }
        menu.addSeparator();
        menu.addItem (30, tr ("Über EMBER …", "About EMBER …"));
        menu.setLookAndFeel (&getLookAndFeel());
        return menu;
    }

    void EmberPanel::showMainMenu()
    {
        buildMainMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (menuBtn),
                            [this, safe = juce::Component::SafePointer<EmberPanel> (this)] (int id)
                            {
                                if (safe == nullptr || id == 0)
                                    return;
                                if (id >= 61 && id <= 63)
                                {
                                    const auto level = id == 61 ? AnimationLevel::full : (id == 62 ? AnimationLevel::reduced : AnimationLevel::off);
                                    settings.file().setValue ("animation", toString (level));
                                    settings.file().saveIfNeeded();
                                    LiveMascotView::setAnimationLevel (level);
                                    return;
                                }
                                auto& lic = processor.getLicence();
                                if (id == 40)
                                    vocalypse::ui::showActivationDialog (lic, getTopLevelComponent());
                                else if (id == 41)
                                {
                                    // one click would lock EMBER in every open project: ask first
                                    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                                                      .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                                                      .withTitle (tr ("Diesen Computer freigeben?", "Release this computer?"))
                                                                      .withMessage (tr ("EMBER lässt danach alles unbearbeitet durch, bis du den Schlüssel wieder eingibst. "
                                                                                        "So kannst du ihn auf einem anderen Computer aktivieren.",
                                                                                        "EMBER then passes everything through unprocessed until you enter the key again. "
                                                                                        "This lets you activate it on another computer."))
                                                                      .withButton (tr ("Freigeben", "Release"))
                                                                      .withButton (tr ("Abbrechen", "Cancel"))
                                                                      .withAssociatedComponent (this),
                                                                  [&lic, life = lic.lifetime()] (int result)
                                    {
                                        if (result != 1 || life.expired())
                                            return;
                                        lic.deactivate ([] (bool ok, const juce::String& message)
                                        {
                                            if (message.isNotEmpty())
                                                juce::AlertWindow::showMessageBoxAsync (ok ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
                                                                                        tr ("Lizenz", "Licence"), message);
                                        });
                                    });
                                }
                                else if (id == 42)
                                    juce::URL (lic.buyUrl()).launchInDefaultBrowser();
                                else if (id == 50)
                                    processor.undo();
                                else if (id == 51)
                                    processor.redo();
                                else if (id > 60 && id <= 260)
                                {
                                    if (auto* ed = findParentComponentOfClass<EmberEditor>())
                                        ed->setScalePercent (id - 60);
                                    return;   // the panel may be rebuilt / rescaled: nothing else to do here
                                }
                                if (id >= 11 && id <= 13)
                                {
                                    const juce::String pref = id == 11 ? "system" : (id == 12 ? "de" : "en");
                                    if (onLanguage)
                                        juce::MessageManager::callAsync ([cb = onLanguage, pref] { cb (pref); });
                                }
                                else if (id == 20)
                                {
                                    // the installer puts the manual into ProgramData; fall back to the website
                                    const auto local = juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory)
                                                           .getChildFile ("Lind Audio").getChildFile ("EMBER")
                                                           .getChildFile (i18n::isGerman() ? "EMBER_Handbuch.html" : "EMBER_Manual.html");
                                    if (local.existsAsFile())
                                        local.startAsProcess();
                                    else
                                        juce::URL ("https://lind-audio.com/ember").launchInDefaultBrowser();
                                }
                                else if (id == 21)
                                {
                                    settings.file().setValue ("autoUsed", false);
                                    settings.file().saveIfNeeded();
                                }
                                else if (id == 30)
                                {
                                    about->setVisible (true);
                                    about->toFront (true);   // with the focus: Esc closes it
                                }
                            });
    }

    bool EmberPanel::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::escapeKey && about != nullptr && about->isVisible())
        {
            about->setVisible (false);
            return true;
        }
        const auto mods = key.getModifiers();
        if (! mods.isCommandDown())
            return false;
        const int k = key.getKeyCode();
        if ((k == 'Z' || k == 'z') && ! mods.isShiftDown())
            return processor.undo(), true;
        if (((k == 'Z' || k == 'z') && mods.isShiftDown()) || k == 'Y' || k == 'y')
            return processor.redo(), true;
        return false;
    }

    void EmberPanel::AutoButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
    {
        juce::TextButton::paintButton (g, highlighted, down);
        if (progress < 0.0f)
            return;
        // how much Ember has heard, along the bottom edge
        const auto r = getLocalBounds().toFloat().reduced (8.0f, 0.0f).removeFromBottom (5.0f).withTrimmedBottom (2.0f);
        g.setColour (c::fireInk.withAlpha (0.22f));
        g.fillRoundedRectangle (r, 1.5f);
        g.setColour (c::fireInk.withAlpha (0.85f));
        g.fillRoundedRectangle (r.withWidth (r.getWidth() * juce::jlimit (0.0f, 1.0f, progress)), 1.5f);
    }

    void EmberPanel::refreshButtons()
    {
        using Phase = EmberAudioProcessor::AutoPhase;
        const auto phase = processor.getAutoPhase();
        // locked: the audio passes through unprocessed, so AUTO and BYPASS have nothing to do
        const bool locked = processor.getLicence().isLocked();
        autoBtn.setEnabled (! locked);
        bypassBtn.setEnabled (! locked);
        const float progress = phase == Phase::listening ? (float) processor.getAutoProgress() : -1.0f;
        if (std::abs (progress - autoBtn.progress) > 0.001f)
        {
            autoBtn.progress = progress;
            autoBtn.repaint();
        }
        if (phase == Phase::listening || phase == Phase::analysing)
            autoBtn.setButtonText (tr ("ABBRECHEN", "CANCEL"));
        else
            autoBtn.setButtonText ("AUTO");

        const float hz = processor.state.getRawParameterValue (pid::freq)->load();
        // the range the frequency lies in lights up - also after AUTO or a drag picked e.g. 7.3 kHz
        const int voice = EmberAudioProcessor::voiceRangeOf (hz);
        voiceLow.setToggleState (voice == 0, juce::dontSendNotification);
        voiceMid.setToggleState (voice == 1, juce::dontSendNotification);
        voiceHigh.setToggleState (voice == 2, juce::dontSendNotification);
        const bool hearing = std::lround (processor.state.getRawParameterValue (pid::listen)->load()) == 2;   // Delta
        listenBtn.setToggleState (hearing, juce::dontSendNotification);
        listenBtn.setColour (juce::TextButton::buttonOnColourId, look::accent);
        // Stereo-Link only works with two channels (L/R, M/S): dimmed for Mid / Side
        const bool linkUsed = std::lround (processor.state.getRawParameterValue (pid::stereo)->load()) < 2;
        linkS.setAlpha (linkUsed ? 1.0f : 0.4f);

        const auto name = processor.getPresetName() + (processor.isPresetModified() ? " *" : "");
        if (presetBtn.getButtonText() != name)
            presetBtn.setButtonText (name);
        slotA.setToggleState (processor.getActiveSlot() == 0, juce::dontSendNotification);
        slotB.setToggleState (processor.getActiveSlot() == 1, juce::dontSendNotification);
        slotCopy.setButtonText (processor.getActiveSlot() == 0 ? utf8 ("A → B") : utf8 ("B → A"));
    }

    void EmberPanel::timerCallback()
    {
        using Phase = EmberAudioProcessor::AutoPhase;
        const float gr = processor.engine.gainReductionDb.pull();
        const float in = processor.engine.inputPeak.pull();
        processor.engine.outputPeak.pull();
        shownGr = gr > shownGr ? gr : shownGr + (gr - shownGr) * 0.25f;

        const auto phase = processor.getAutoPhase();
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        const bool bypassed = processor.state.getRawParameterValue (pid::bypass)->load() > 0.5f;
        MascotInput mi;
        mi.bypass = bypassed;
        mi.hasSignal = in > 0.00316f;
        mi.autoListening = phase == Phase::listening || phase == Phase::analysing;
        mi.reductionDb = shownGr;
        mi.hostStopped = processor.isHostStopped();
        mi.bpm = processor.getHostBpm();
        mascot.setInput (mi);

        if (phase != lastPhase && (phase == Phase::done || phase == Phase::failed))
            messageUntil = now + 8.0;
        if (phase != lastPhase && phase == Phase::done)
            settings.setAutoUsed();   // the tip goes once AUTO really did its job (not on a cancel)
        lastPhase = phase;

        if (in > 0.00316f)
            lastSignal = now;

        juce::String text;
        const int listen = (int) std::lround (processor.state.getRawParameterValue (pid::listen)->load());
        const auto bypassNote = bypassed ? tr ("  BYPASS ist an: Das Ergebnis hörst du erst, wenn du BYPASS ausschaltest.",
                                               "  BYPASS is on: you hear the result once you switch BYPASS off.")
                                         : juce::String();
        if (phase == Phase::listening && processor.isAutoWaitingForAudio())
        {
            text = processor.getAutoMessage();
            mascot.setBubble (tr ("Ich hör nichts – drück Play in deiner DAW!", "I can't hear anything – press play in your DAW!"));
        }
        else if (phase == Phase::listening)
        {
            text = processor.getAutoMessage() + "  " + juce::String (juce::roundToInt (processor.getAutoProgress() * 100.0)) + " %" + bypassNote;
            mascot.setBubble (tr ("Ich hör zu …", "Listening …"));
        }
        else if (phase == Phase::analysing)
        {
            text = processor.getAutoMessage();
            mascot.setBubble (tr ("Moment …", "One moment …"));
        }
        else if (now < noticeUntil)
        {
            text = notice;
            mascot.setBubble ({});
        }
        else if (now < messageUntil && (phase == Phase::failed || processor.isAutoResultLoaded()))
        {
            text = processor.getAutoMessage() + (phase == Phase::done ? bypassNote : juce::String());
            mascot.setBubble (phase == Phase::done ? tr ("Passt!", "Got it!") : tr ("Hmm …", "Hmm …"));
        }
        else if (! advanced && listen == 0 && processor.isShowingAutoResult())
        {
            text = processor.getAutoMessage() + bypassNote;   // SIMPLE: what AUTO set stays readable until something changes
            mascot.setBubble ({});
        }
        else if (processor.getLicence().isLocked())
        {
            mascot.setBubble ({});
            text = tr ("Ohne Lizenzschlüssel läuft dein Gesang unbearbeitet durch.", "Without a licence key your vocal passes through unprocessed.");
        }
        else
        {
            mascot.setBubble (settings.wasAutoUsed() || bypassed ? juce::String() : tr ("Klick mich für AUTO!", "Click me for AUTO!"));
            if (advanced && display.isMouseOver (true))
                text = spectrumHelp (std::lround (processor.state.getRawParameterValue (pid::mode)->load()) == 0);
            else if (bypassed)
                text = tr ("Bypass: Der De-Esser ist aus, das Signal läuft unbearbeitet durch.", "Bypass: the de-esser is off, the signal passes through unprocessed.");
            else if (listen == 2)
                text = advanced ? tr ("Delta: Du hörst nur, was entfernt wird. Stille heißt: gerade wird nichts entfernt. Zum Ausschalten Abhören auf Aus stellen.",
                                      "Delta: you hear only what is removed. Silence means nothing is being removed. Set Listen to Off to switch it off.")
                                : tr ("Delta: Du hörst nur, was entfernt wird. Stille heißt: gerade wird nichts entfernt. Zum Ausschalten nochmal auf DELTA klicken.",
                                      "Delta: you hear only what is removed. Silence means nothing is being removed. Click DELTA again to switch it off.");
            else if (listen == 1)
                text = tr ("S-Bereich: Du hörst nur den Bereich, der bei S-Lauten abgesenkt wird. Zum Ausschalten Abhören auf Aus stellen.",
                           "S range: you hear only the range that is lowered on S sounds. Set Listen to Off to switch it off.");
            else if (advanced)
                text = tr ("Fahr mit der Maus über einen Regler, um zu sehen, was er macht. Im Spektrum: Linie ziehen = Frequenz, "
                           "Kanten oder Mausrad = Breite. Doppelklick = Standard.",
                           "Hover over a control to see what it does. In the spectrum: drag the line = frequency, "
                           "edges or wheel = width. Double-click = default.");
            else
                text = tr ("Spiel deinen Gesang ab und klick auf Ember oder AUTO: Er stellt den De-Esser auf deine Stimme ein.",
                           "Play your vocal and click Ember or AUTO: he tunes the de-esser to your voice.");
        }
        if (status.getText() != text)
            status.setText (text, juce::dontSendNotification);

        meter.setValue (shownGr);
        {
            // animation (≡ menu): off = a still seal, reduced = calm; under the licence card nothing moves
            const auto level = LiveMascotView::getAnimationLevel();
            const bool still = level == AnimationLevel::off, covered = licenceLock.isVisible();
            mascot.setCovered (covered);
            const double nowS = juce::Time::getMillisecondCounterHiRes() * 0.001;
            if (! still)
                sealClock += lastTickS > 0.0 ? juce::jmin (0.1, nowS - lastTickS) : 0.0;
            lastTickS = nowS;
            seal::Scene sc;
            sc.t = sealClock;
            sc.sinceS = advanced || still ? 99.0f : mascot.secondsSinceSwallow();
            sc.heat = still ? 0.0f : shownGr / 9.0f;
            sc.on = ! bypassed;
            sc.calm = level != AnimationLevel::full;
            // the small seal sits on the Amount knob's cap
            const auto kb = amountBig.getBounds().toFloat();
            const auto knobArea = juce::Rectangle<float> (92.0f, 92.0f).withCentre ({ kb.getCentreX(), kb.getY() + 90.0f });
            if (! covered)
                sealOverlay.update (sc, ! advanced, knobArea, plates());
        }
        if (advanced)
        {
            if (cardGroup != nullptr && infoPopup.isVisible())
                if (const int h = hoveredOption (*cardGroup); h >= 0 && h != cardOption)
                    showOptionCard (*cardGroup, *cardGroup, h);
            int gi = 0;
            for (auto* seg : { &modeSeg, &detectSeg, &listenSeg, &stereoSeg })
            {
                const int chosen = seg->getSelectedSegment();
                if (chosen != lastChoice[(size_t) gi] && lastChoice[(size_t) gi] != -2 && seg->isMouseOver (true))
                {
                    const auto texts = optionTexts (seg->getParamId());
                    if (juce::isPositiveAndBelow (chosen, (int) texts.size()))
                        showNotice (groupName (seg->getParamId()) + utf8 (" · ") + texts[(size_t) chosen].name + ": " + texts[(size_t) chosen].text);
                    if (cardGroup == seg && infoPopup.isVisible())
                        showOptionCard (*seg, *seg, hoveredOption (*seg));   // the "chosen" note goes
                }
                lastChoice[(size_t) gi++] = chosen;
            }
            advancedMeter.setValue (shownGr);
            display.setHasSignal (now - lastSignal < 2.0);
            spectrumSource.update (processor.getCurrentSampleRate());
            display.pushReduction (gr);
            display.refresh();
            detector.setLevel (bypassed ? -100.0f : processor.engine.detectorDb.load(), processor.engine.detectorWeight.load());
        }
        refreshButtons();
    }

    // ================================================================ editor
    void EmberEditor::applyLanguage (const juce::String& preference)
    {
        i18n::setCurrent (i18n::resolve (i18n::preferenceFromString (preference)));
    }

    EmberEditor::EmberEditor (EmberAudioProcessor& p) : AudioProcessorEditor (p), processor (p)
    {
        applyLanguage (settings->getLanguage());
        setLookAndFeel (&lnf);
        ui::setActivationDialogLook ([] { return std::make_shared<EmberLookAndFeel>(); });   // the key dialog in EMBER's look
        setWantsKeyboardFocus (true);   // Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y where the host passes the keys on
        rebuildPanel();
        setResizable (true, true);
        setResizeLimits (look::baseW * 3 / 4, look::baseH * 3 / 4, look::baseW * 2, look::baseH * 2);
        if (auto* cons = getConstrainer())
            cons->setFixedAspectRatio ((double) look::baseW / look::baseH);
        const int w = juce::jlimit (look::baseW * 3 / 4, look::baseW * 2, settings->getEditorWidth (look::baseW));
        setSize (w, w * look::baseH / look::baseW);
    }

    EmberEditor::~EmberEditor()
    {
        settings->file().saveIfNeeded();   // the window size (not written on every resize step)
        panel.reset();
        setLookAndFeel (nullptr);
    }

    void EmberEditor::rebuildPanel()
    {
        panel.reset();
        juce::Component::SafePointer<EmberEditor> safe (this);
        panel = std::make_unique<EmberPanel> (processor, *settings, [safe] (juce::String pref) { if (safe != nullptr) safe->setLanguage (pref); });
        addAndMakeVisible (*panel);
        resized();
    }

    void EmberEditor::setLanguage (const juce::String& preference)
    {
        settings->setLanguage (preference);
        applyLanguage (preference);
        rebuildPanel();
    }

    void EmberEditor::paint (juce::Graphics& g) { g.fillAll (seal::wall); }

    void EmberEditor::resized()
    {
        if (panel == nullptr)
            return;
        const float scale = (float) getWidth() / (float) look::baseW;
        panel->setBounds (0, 0, look::baseW, look::baseH);
        panel->setTransform (juce::AffineTransform::scale (scale));
        if (getWidth() > 0)
            settings->setEditorWidth (getWidth());
    }

    void EmberEditor::setScalePercent (int percent)
    {
        const int w = juce::jlimit (look::baseW * 3 / 4, look::baseW * 2, look::baseW * percent / 100);
        setSize (w, w * look::baseH / look::baseW);
        settings->file().saveIfNeeded();
    }

    int EmberEditor::getScalePercent() const
    {
        return juce::roundToInt (100.0 * getWidth() / look::baseW);
    }

    bool EmberEditor::keyPressed (const juce::KeyPress& key)
    {
        return panel != nullptr && panel->keyPressed (key);
    }

    bool EmberEditor::isAdvanced() const { return panel != nullptr && panel->isAdvanced(); }
    void EmberEditor::setAdvanced (bool a) { if (panel != nullptr) panel->setAdvanced (a); }
    void EmberEditor::tickForTest() { if (panel != nullptr) panel->tick(); }
}
