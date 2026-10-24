#include "Widgets.h"
#include "VelvoxLookAndFeel.h"
#include "Spectrum.h"
#include "../Parameters/ParameterLayout.h"
#include "ValueText.h"

namespace velvox::ui
{
    namespace
    {
        float dbToY (float db, juce::Rectangle<float> r, float minDb, float maxDb)
        {
            return juce::jmap (juce::jlimit (minDb, maxDb, db), minDb, maxDb, r.getBottom(), r.getY());
        }

        juce::Label& styleCaption (juce::Label& l, float size = typesize::label)
        {
            // Style guide: control names in tracked capitals
            l.setFont (caps (size - 1.5f));
            l.setJustificationType (juce::Justification::centred);
            l.setColour (juce::Label::textColourId, colours::textDim);
            l.setInterceptsMouseClicks (false, false);
            return l;
        }

        constexpr int knobLabelH = 14, knobValueH = 16, knobGap = 4;   // design: value 16, name 14, 4 px under the dial
        constexpr int popupWidth = 290;

        juce::TextLayout layoutText (const juce::String& text, float width)
        {
            juce::AttributedString s;
            s.setJustification (juce::Justification::topLeft);
            s.setLineSpacing (2.0f);
            s.append (text, font (13.5f), colours::text.withAlpha (0.8f));
            juce::TextLayout layout;
            layout.createLayout (s, width);
            return layout;
        }
    }

    //==========================================================================
    InfoPopup::InfoPopup()
    {
        setInterceptsMouseClicks (false, false);
        setVisible (false);
    }

    void InfoPopup::show (juce::Rectangle<int> anchor, const juce::String& t, const juce::String& v, const juce::String& body)
    {
        const bool sameCard = shown && anchor == anchorArea && t == title && body == text;
        title = t;
        value = v;
        text = body;
        anchorArea = anchor;
        if (sameCard)
        {
            repaint();   // live value update while the card is open
            return;
        }

        const auto layout = layoutText (text, (float) popupWidth - 32.0f);
        const int h = 16 + 20 + 10 + (int) std::ceil (layout.getHeight()) + 16;
        auto parent = getParentComponent() != nullptr ? getParentComponent()->getLocalBounds() : juce::Rectangle<int> (0, 0, baseWidth, baseHeight);

        auto b = juce::Rectangle<int> (popupWidth, h).withCentre ({ anchor.getCentreX(), 0 });
        b.setY (anchor.getY() - h - 6);
        if (b.getY() < parent.getY() + 8)
            b.setY (anchor.getBottom() + 6);
        finalBounds = b.constrainedWithin (parent.reduced (8));

        if (! shown)
        {
            fade.snap (0.0f);
            setAlpha (0.0f);
        }
        shown = true;
        fade.target = 1.0f;
        setBounds (finalBounds.translated (0, juce::roundToInt (8.0f * (1.0f - fade.current))));
        setVisible (true);
        toFront (false);
        repaint();
    }

    void InfoPopup::hide()
    {
        shown = false;
        fade.target = 0.0f;
    }

    void InfoPopup::tick()
    {
        if (! fade.tick (0.2f))
            return;
        // Ease: fade and lift into place
        setAlpha (fade.current);
        setBounds (finalBounds.translated (0, juce::roundToInt (8.0f * (1.0f - fade.current))));
        if (! shown && fade.current <= 0.001f)
            setVisible (false);
    }

    void InfoPopup::paint (juce::Graphics& g)
    {
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (colours::raised.withAlpha (0.97f));
        g.fillRoundedRectangle (r, 12.0f);
        drawGlow (g, { r.getX() + 30.0f, r.getY() }, 120.0f, 0.10f, colours::teal);
        drawGlass (g, r, 12.0f, 0.02f);

        auto inner = r.reduced (16.0f, 16.0f);
        auto head = inner.removeFromTop (20.0f);
        if (value.isNotEmpty())
        {
            const auto valueFont = display (15.0f, true);
            const float vw = (float) juce::GlyphArrangement::getStringWidthInt (valueFont, value) + 4.0f;
            g.setFont (valueFont);
            g.setColour (colours::cyan);
            g.drawText (value, head.removeFromRight (vw), juce::Justification::centredRight, false);
        }
        g.setColour (colours::text);
        g.setFont (font (14.5f, true));
        g.drawText (title, head, juce::Justification::centredLeft, true);
        inner.removeFromTop (4.0f);
        g.setGradientFill (auroraGradient (inner.getTopLeft(), inner.getTopLeft().translated (36.0f, 0.0f)));
        g.fillRoundedRectangle (inner.getX(), inner.getY(), 36.0f, 2.0f, 1.0f);
        inner.removeFromTop (8.0f);
        layoutText (text, inner.getWidth()).draw (g, inner);
    }

    //==========================================================================
    HoverInfo::HoverInfo (juce::Component& c, std::function<juce::String()> t, std::function<juce::String()> v, std::function<juce::String()> d)
        : anchor (c), title (std::move (t)), value (std::move (v)), description (std::move (d))
    {
        anchor.addMouseListener (this, true);
    }

    HoverInfo::~HoverInfo()
    {
        // The info card notices on its own when its anchor goes away
        anchor.removeMouseListener (this);
    }

    void HoverInfo::mouseEnter (const juce::MouseEvent& e)
    {
        if (! e.mods.isAnyMouseButtonDown())
            startTimer (motion::hoverInfoDelayMs);
    }

    void HoverInfo::mouseExit (const juce::MouseEvent&)
    {
        if (anchor.isMouseOver (true))
            return;
        stopTimer();
        hide();
    }

    void HoverInfo::mouseDown (const juce::MouseEvent&)
    {
        stopTimer();
        hide();
    }

    void HoverInfo::hide()
    {
        if (! shown)
            return;
        shown = false;
        if (auto* host = anchor.findParentComponentOfClass<InfoPopupHost>())
            host->hideInfo (anchor);
    }

    void HoverInfo::timerCallback()
    {
        stopTimer();
        if (juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown() || ! anchor.isMouseOver (true))
            return;
        if (auto* host = anchor.findParentComponentOfClass<InfoPopupHost>())
        {
            shownValue = value ? value() : juce::String();
            shown = true;
            host->showInfo (anchor, title ? title() : juce::String(), shownValue, description ? description() : juce::String());
        }
    }

    void HoverInfo::refresh()
    {
        // Keep the read-out of an open card current (wheel, keys, automation)
        if (! shown || ! value)
            return;
        const auto v = value();
        if (v != shownValue)
            if (auto* host = anchor.findParentComponentOfClass<InfoPopupHost>())
            {
                shownValue = v;
                host->showInfo (anchor, title ? title() : juce::String(), v, description ? description() : juce::String());
            }
    }

    //==========================================================================
    void FineSlider::bindParameter (juce::RangedAudioParameter* p)
    {
        parameter = p;
        if (p == nullptr)
            return;
        // Default (double-click and Alt+click, see juce::Slider) and typed values with units
        setDoubleClickReturnValue (true, (double) p->convertFrom0to1 (p->getDefaultValue()), juce::ModifierKeys::altModifier);
        valueFromTextFunction = [this] (const juce::String& text)
        {
            if (parameter != nullptr)
                if (const auto v = parseValueText (text, *parameter))
                    return (double) *v;
            return getValue();   // not a value: keep it
        };
        setPopupMenuEnabled (false);   // our own menu (below)
    }

    void FineSlider::mouseDown (const juce::MouseEvent& e)
    {
        ignoreDrag = false;
        if (e.mods.isPopupMenu())
        {
            ignoreDrag = true;
            if (parameter != nullptr && isEnabled())
            {
                juce::Component::SafePointer<FineSlider> safe (this);
                gestures::showMenu (*this, *parameter, [safe] { if (safe != nullptr) safe->showTextBox(); });
            }
            return;
        }
        if (e.mods.isAltDown())
            ignoreDrag = true;   // Alt+click = default (juce::Slider), never a drag after it

        juce::Slider::mouseDown (e);   // gesture start, Alt-click reset, text box handling
        lastPos = e.position;
        dragProportion = valueToProportionOfLength (getValue());
    }

    void FineSlider::mouseUp (const juce::MouseEvent& e)
    {
        if (e.mods.isPopupMenu())
            return;
        juce::Slider::mouseUp (e);
        ignoreDrag = false;
    }

    void FineSlider::mouseDrag (const juce::MouseEvent& e)
    {
        if (ignoreDrag || ! isEnabled() || e.mods.isPopupMenu() || getRange().getLength() <= 0.0)
            return;

        // Up / right raises the value. Measured from the last event (not the
        // drag start), so pressing or releasing the fine modifier never jumps.
        const float diff = (e.position.x - lastPos.x) + (lastPos.y - e.position.y);
        lastPos = e.position;

        const bool fine = e.mods.isShiftDown() || e.mods.isCtrlDown() || e.mods.isCommandDown();
        const double pixels = (double) normalPixels * (fine ? fineFactor : 1);
        dragProportion = juce::jlimit (0.0, 1.0, dragProportion + (double) diff / pixels);
        setValue (proportionOfLengthToValue (dragProportion), juce::sendNotificationSync);
    }

    //==========================================================================
    Knob::Knob (APVTS& state, const char* paramId, const juce::String& label, juce::Colour accent)
    {
        // White (the old default) means "no module colour given": the design cyan
        slider.setColour (juce::Slider::rotarySliderFillColourId, accent == colours::accent ? colours::teal : accent);
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, knobValueH);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        slider.setMouseDragSensitivity (FineSlider::normalPixels);   // also used by the mouse wheel
        slider.setVelocityModeParameters (0.9, 1, 0.08, false);     // fine mode is handled by FineSlider
        addAndMakeVisible (slider);

        name.setText (label.toUpperCase(), juce::dontSendNotification);
        addAndMakeVisible (name);
        applyLabelStyle();

        attachment = std::make_unique<APVTS::SliderAttachment> (state, paramId, slider);
        this->paramId = paramId;
        slider.bindParameter (state.getParameter (paramId));

        // Hover card: name, short description, value with unit (the gestures are in the help)
        if (const auto* spec = findParameterSpec (paramId))
            description = spec->description;
        info = std::make_unique<HoverInfo> (*this, [this] { return name.getText(); },
                                            [this] { return slider.getTextFromValue (slider.getValue()); },
                                            [this] { return description; });

        shownPos.snap ((float) slider.valueToProportionOfLength (slider.getValue()));
        slider.getProperties().set (VelvoxLookAndFeel::animPosProperty, shownPos.current);
    }

    Knob::~Knob() = default;

    void Knob::setHero (bool isHero)
    {
        hero = isHero;
        slider.getProperties().set (VelvoxLookAndFeel::heroProperty, isHero);
        if (isHero)
            maxDial = dialsize::macro;
        applyLabelStyle();
        resized();
        repaint();
    }

    void Knob::setProminence (Prominence p)
    {
        prominence = p;
        maxDial = p == Prominence::primary ? dialsize::primary : (p == Prominence::detail ? dialsize::detail : dialsize::secondary);
        applyLabelStyle();
        resized();
        repaint();
    }

    void Knob::applyLabelStyle()
    {
        // Design: knob name Montserrat SemiBold 9 px, #6B7489, capitals, under the value
        name.setFont (semiBold (9.0f, 0.06f));
        name.setJustificationType (juce::Justification::centred);
        name.setColour (juce::Label::textColourId, colours::knobName);
        name.setInterceptsMouseClicks (false, false);
        slider.getProperties().set (VelvoxLookAndFeel::strongProperty, hero || prominence == Prominence::primary);
        slider.sendLookAndFeelChange();
    }

    void Knob::resized()
    {
        // Design: dial on top, value (Roboto Mono) and name below, one block centred in the cell
        auto r = getLocalBounds();
        const int dial = juce::jlimit (16, maxDial, std::min (r.getWidth() - 8, r.getHeight() - knobValueH - knobLabelH - knobGap));
        auto block = r.withSizeKeepingCentre (r.getWidth(), dial + knobGap + knobValueH + knobLabelH);

        valueBoxH = knobGap + knobValueH;
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, block.getWidth(), valueBoxH);
        slider.setBounds (block.removeFromTop (dial + valueBoxH));
        name.setBounds (block.removeFromTop (knobLabelH));

        dialArea = slider.getBounds().withHeight (dial).withSizeKeepingCentre (dial, dial);
        valueArea = slider.getBounds().withTrimmedTop (dial + knobGap);
    }

    juce::Rectangle<float> Knob::dialBounds() const
    {
        // Same area the look-and-feel draws the dial into
        return dialArea.toFloat();
    }

    void Knob::tick()
    {
        const float target = (float) slider.valueToProportionOfLength (slider.getValue());
        shownPos.target = target;
        bool moved;
        if (slider.isMouseButtonDown())
        {
            moved = shownPos.current != target;
            shownPos.snap (target);
        }
        else
        {
            moved = shownPos.tick (0.2f);
        }

        hover.target = slider.isMouseOverOrDragging() && slider.isEnabled() ? 1.0f : 0.0f;
        moved = hover.tick (0.16f) || moved;

        if (moved)
        {
            slider.getProperties().set (VelvoxLookAndFeel::animPosProperty, shownPos.current);
            slider.getProperties().set (VelvoxLookAndFeel::hoverProperty, hover.current);
            slider.repaint();
        }
        if (info != nullptr)
            info->refresh();
    }

    void Knob::refreshEffective()
    {
        if (! effectiveValue)
            return;
        const float v = effectiveValue();
        if (std::isnan (lastEffective) || std::abs (v - lastEffective) > 1.0e-4f)
        {
            lastEffective = v;
            repaint();
        }
    }

    void Knob::paintOverChildren (juce::Graphics& g)
    {
        if (! effectiveValue || std::isnan (lastEffective))
            return;

        const double base = slider.getValue();
        const auto range = slider.getRange();
        const double eff = juce::jlimit (range.getStart(), range.getEnd(), (double) lastEffective);
        if (std::abs (eff - base) < 1.0e-3 * (range.getLength() + 1.0e-9))
            return;

        // Glowing dot on the tick ring (r 44 of 100 units) where the macros move the processed value
        const auto params = slider.getRotaryParameters();
        const float prop = (float) slider.valueToProportionOfLength (eff);
        const float angle = params.startAngleRadians + prop * (params.endAngleRadians - params.startAngleRadians);
        const auto d = dialBounds();
        const auto p = d.getCentre().getPointOnCircumference (d.getWidth() * 0.44f, angle);

        drawGlow (g, p, 8.0f, 0.5f, colours::teal);
        g.setColour (colours::text);
        g.fillEllipse (juce::Rectangle<float> (4.5f, 4.5f).withCentre (p));
    }

    //==========================================================================
    HSlider::HSlider (APVTS& state, const char* id, const juce::String& text, juce::Colour accentColour)
        : paramId (id), label (text), accent (accentColour)
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setSliderSnapsToMousePosition (false);   // a click never jumps: drag moves from where it is
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
        slider.setColour (juce::Slider::trackColourId, accent);
        slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
        slider.setMouseDragSensitivity (FineSlider::normalPixels);
        addAndMakeVisible (slider);

        attachment = std::make_unique<APVTS::SliderAttachment> (state, id, slider);
        slider.bindParameter (state.getParameter (id));
        if (const auto* spec = findParameterSpec (id))
            description = spec->description;
        info = std::make_unique<HoverInfo> (*this, [this] { return label; },
                                            [this] { return slider.getTextFromValue (slider.getValue()); },
                                            [this] { return description; });
    }

    HSlider::~HSlider() = default;

    void HSlider::resized()
    {
        auto r = getLocalBounds();
        nameArea = r.removeFromLeft (std::min (104, r.getWidth() / 3));
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, std::min (22, r.getHeight()));
        slider.setBounds (r);
    }

    void HSlider::paint (juce::Graphics& g)
    {
        // Macro colour dot + name in capitals (design label style)
        const auto dot = juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ (float) nameArea.getX() + 4.0f, (float) nameArea.getCentreY() });
        g.setColour (accent);
        g.fillEllipse (dot);
        g.setColour (colours::textSoft);
        g.setFont (semiBold (10.0f, 0.12f));
        g.drawText (label.toUpperCase(), nameArea.withTrimmedLeft (14), juce::Justification::centredLeft, true);
    }

    //==========================================================================
    Toggle::Toggle (APVTS& state, const char* paramId, const juce::String& label, juce::Colour accent)
    {
        setButtonText (label);
        this->paramId = paramId;
        if (accent != colours::accent)
            setColour (juce::ToggleButton::tickColourId, accent);
        attachment = std::make_unique<APVTS::ButtonAttachment> (state, paramId, *this);
        if (const auto* spec = findParameterSpec (paramId))
            setTooltip (spec->description);
    }

    //==========================================================================
    Choice::Choice (APVTS& state, const char* paramId, const juce::String& captionText)
    {
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId)))
            box.addItemList (p->choices, 1);
        attachment = std::make_unique<APVTS::ComboBoxAttachment> (state, paramId, box);
        if (const auto* spec = findParameterSpec (paramId))
            box.setTooltip (spec->description);
        box.setRepaintsOnMouseActivity (true);
        addAndMakeVisible (box);

        caption.setText (captionText.toUpperCase(), juce::dontSendNotification);
        addAndMakeVisible (styleCaption (caption));
        caption.setVisible (captionText.isNotEmpty());
    }

    void Choice::resized()
    {
        auto r = getLocalBounds();
        if (caption.isVisible())
        {
            auto block = r.withSizeKeepingCentre (r.getWidth(), knobLabelH + 8 + 34);
            caption.setBounds (block.removeFromTop (knobLabelH));
            block.removeFromTop (8);
            box.setBounds (block.withSizeKeepingCentre (std::min (block.getWidth() - 8, 180), 34));
        }
        else
        {
            box.setBounds (r);
        }
    }

    //==========================================================================
    PowerButton::PowerButton (APVTS& state, const char* paramId, juce::Colour)
    {
        attachment = std::make_unique<APVTS::ButtonAttachment> (state, paramId, *this);
        setTooltip ("Switch this module on or off");
    }

    void PowerButton::paintButton (juce::Graphics& g, bool highlighted, bool)
    {
        const bool on = getToggleState();
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const float size = std::min (r.getWidth(), r.getHeight());
        r = r.withSizeKeepingCentre (size, size);

        if (on)
            drawGlow (g, r.getCentre(), size * 1.1f, 0.4f, hue);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a2d34), r.getCentreX(), r.getY(),
                                                 juce::Colour (0xff121317), r.getCentreX(), r.getBottom(), false));
        g.fillEllipse (r);
        g.setColour (on ? hue.withAlpha (0.8f) : (highlighted ? colours::line.brighter (0.6f) : colours::line.brighter (0.25f)));
        g.drawEllipse (r.reduced (0.5f), 1.0f);

        const auto c = r.getCentre();
        const float rad = size * 0.22f;
        juce::Path p;
        p.addCentredArc (c.x, c.y, rad, rad, 0.0f, 0.7f, juce::MathConstants<float>::twoPi - 0.7f, true);
        p.startNewSubPath (c.x, c.y - rad * 1.3f);
        p.lineTo (c.x, c.y - rad * 0.25f);
        g.setColour (on ? hue : colours::textDim);
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    //==========================================================================
    void LevelMeter::push (float left, float right, bool clipped)
    {
        constexpr float decayPerFrame = 0.96f; // ~ 20 dB/s at 60 fps
        const float in[2] { left, right };

        for (size_t i = 0; i < 2; ++i)
        {
            level[i] = std::max (in[i], level[i] * decayPerFrame);
            if (in[i] >= hold[i])
            {
                hold[i] = in[i];
                holdFrames[i] = 90;
            }
            else if (--holdFrames[i] <= 0)
            {
                hold[i] *= decayPerFrame;
            }
        }

        clipLatched = clipLatched || clipped;
        repaint();
    }

    float LevelMeter::getPeakDb() const noexcept
    {
        return juce::Decibels::gainToDecibels (std::max (hold[0], hold[1]), -100.0f);
    }

    void LevelMeter::paint (juce::Graphics& g)
    {
        constexpr float minDb = -60.0f, maxDb = 6.0f;
        auto r = getLocalBounds().toFloat();

        // Design: caption on top, LUFS in Roboto Mono below the bars
        g.setColour (colours::textFaint);
        g.setFont (semiBold (10.0f, 0.15f));
        g.drawText (scaleLeft ? "INPUT" : "OUTPUT", r.removeFromTop (16.0f), juce::Justification::centred, false);
        r.removeFromTop (6.0f);
        auto readout = r.removeFromBottom (40.0f);
        g.setColour (colours::text);
        g.setFont (monoFont (17.0f));
        g.drawText (loudness <= -99.0f ? juce::String ("-") : juce::String (loudness, 1),
                    readout.removeFromTop (22.0f), juce::Justification::centred, false);
        g.setColour (colours::knobName);
        g.setFont (semiBold (9.0f, 0.1f));
        g.drawText ("LUFS", readout.removeFromTop (14.0f), juce::Justification::centred, false);
        r.removeFromBottom (8.0f);

        // Clip lamp
        auto lampRow = r.removeFromTop (12.0f);
        r.removeFromTop (10.0f);
        const auto lamp = juce::Rectangle<float> (8.0f, 8.0f).withCentre (lampRow.getCentre());
        if (clipLatched)
            drawGlow (g, lamp.getCentre(), 11.0f, 0.5f, colours::warning);
        g.setColour (clipLatched ? colours::warning : colours::accent.withAlpha (0.12f));
        g.fillEllipse (lamp);

        // Scale on one side, two 5 px bars next to it, both centred as one block
        constexpr float scaleW = 26.0f, barsW = 14.0f, scaleGap = 6.0f;
        auto block = r.withSizeKeepingCentre (std::min (r.getWidth(), scaleW + scaleGap + barsW), r.getHeight());
        auto scale = scaleLeft ? block.removeFromLeft (scaleW) : block.removeFromRight (scaleW);
        auto bars = scaleLeft ? block.removeFromRight (barsW) : block.removeFromLeft (barsW);

        g.setFont (monoFont (9.5f));
        for (float db : { 6.0f, 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f })
        {
            const float y = dbToY (db, bars, minDb, maxDb);
            g.setColour (db == 0.0f ? colours::textMuted : colours::textFaint);
            g.drawText (db > 0.0f ? "+" + juce::String ((int) db) : juce::String ((int) std::abs (db)), scale.withY (y - 7.0f).withHeight (14.0f),
                        scaleLeft ? juce::Justification::centredRight : juce::Justification::centredLeft, false);
            g.setColour (colours::accent.withAlpha (db == 0.0f ? 0.22f : 0.08f));
            g.fillRect (scaleLeft ? juce::Rectangle<float> (bars.getX() - 5.0f, y - 0.5f, 3.0f, 1.0f)
                                  : juce::Rectangle<float> (bars.getRight() + 2.0f, y - 0.5f, 3.0f, 1.0f));
        }

        // Design: 2 x 5 px bars, gradient cyan -> violet -> pink (red only above 0 dBFS)
        const float barGap = 4.0f;
        const float barW = 5.0f;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto bar = juce::Rectangle<float> (bars.getX() + (float) ch * (barW + barGap), bars.getY(), barW, bars.getHeight());
            g.setColour (colours::control);
            g.fillRoundedRectangle (bar, barW * 0.5f);

            const float db = juce::Decibels::gainToDecibels (level[(size_t) ch], -100.0f);
            const auto fill = bar.withTop (dbToY (db, bar, minDb, maxDb));
            if (fill.getHeight() > 0.5f)
            {
                juce::ColourGradient grad (juce::Colour (0xff00d1ff), 0.0f, bar.getBottom(), colours::warning, 0.0f, bar.getY(), false);
                grad.addColour (juce::jmap (-24.0, (double) minDb, (double) maxDb, 0.0, 1.0), juce::Colour (0xff8b5cf6));
                grad.addColour (juce::jmap (-1.0, (double) minDb, (double) maxDb, 0.0, 1.0), juce::Colour (0xfff472b6));
                grad.addColour (juce::jmap (0.0, (double) minDb, (double) maxDb, 0.0, 1.0), juce::Colour (0xfff472b6));
                g.setGradientFill (grad);
                g.fillRoundedRectangle (fill, barW * 0.5f);
            }

            const float holdDb = juce::Decibels::gainToDecibels (hold[(size_t) ch], -100.0f);
            if (holdDb > minDb)
            {
                g.setColour (holdDb > 0.0f ? colours::warning : colours::accent.withAlpha (0.9f));
                g.fillRoundedRectangle (bar.getX(), dbToY (holdDb, bar, minDb, maxDb) - 1.0f, bar.getWidth(), 2.0f, 1.0f);
            }
        }
    }

    //==========================================================================
    void GainReductionMeter::push (float reductionDb)
    {
        const float next = reductionDb > shown ? reductionDb : shown * 0.92f + reductionDb * 0.08f;
        if (std::abs (next - shown) > 0.01f)
        {
            shown = next;
            repaint();
        }
    }

    void GainReductionMeter::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        if (caption.isNotEmpty())
            drawCaption (g, caption, r.removeFromTop (20.0f), juce::Justification::centred, colours::textDim, 10.0f);
        auto label = r.removeFromBottom (24.0f);
        r.removeFromTop (6.0f);

        const auto bar = r.withSizeKeepingCentre (10.0f, r.getHeight());
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (bar, 5.0f);

        // Scale ticks every 6 dB
        for (float db = 6.0f; db < maxDb; db += 6.0f)
        {
            g.setColour (colours::accent.withAlpha (0.10f));
            g.fillRect (juce::Rectangle<float> (bar.getRight() + 3.0f, bar.getY() + bar.getHeight() * db / maxDb - 0.5f, 4.0f, 1.0f));
        }

        const float h = bar.getHeight() * juce::jlimit (0.0f, 1.0f, shown / maxDb);
        if (h > 0.5f)
        {
            const auto fill = bar.withHeight (h);
            g.setGradientFill (auroraGradient (fill.getTopLeft(), fill.getBottomLeft()));
            g.fillRoundedRectangle (fill, 5.0f);
        }

        g.setColour (colours::text);
        g.setFont (medium (13.5f));
        g.drawText (shown < 0.05f ? "0.0" : "-" + juce::String (shown, 1), label, juce::Justification::centred, false);
    }

    //==========================================================================
    void CorrelationView::push (float c)
    {
        const float next = value * 0.85f + c * 0.15f;
        if (std::abs (next - value) > 0.002f)
        {
            value = next;
            repaint();
        }
    }

    void CorrelationView::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        auto labels = r.removeFromBottom (15.0f);
        g.setFont (medium (11.5f));
        g.setColour (colours::textDim);
        g.drawText ("-1", labels, juce::Justification::left, false);
        g.drawText ("0", labels, juce::Justification::centred, false);
        g.drawText ("+1", labels, juce::Justification::right, false);

        const auto track = r.withSizeKeepingCentre (r.getWidth(), 3.0f);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (track, 1.5f);
        g.setColour (colours::accent.withAlpha (0.18f));
        g.fillRect (juce::Rectangle<float> (track.getCentreX() - 0.5f, track.getY() - 3.0f, 1.0f, track.getHeight() + 6.0f));

        const float x = juce::jmap (juce::jlimit (-1.0f, 1.0f, value), -1.0f, 1.0f, track.getX(), track.getRight());
        const auto colour = value < 0.0f ? colours::warning : colours::teal;
        g.setColour (colour.withAlpha (0.5f));
        g.fillRoundedRectangle (juce::Rectangle<float> (std::min (x, track.getCentreX()), track.getY(),
                                                        std::abs (x - track.getCentreX()), track.getHeight()), 1.5f);
        drawGlow (g, { x, track.getCentreY() }, 11.0f, 0.45f, colour);
        g.setColour (colour.interpolatedWith (juce::Colours::white, 0.4f));
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ x, track.getCentreY() }));
    }

    //==========================================================================
    void EqCurve::refresh()
    {
        if (! provider || getWidth() <= 0)
            return;

        dsp::EqSettings s;
        bool on = true;
        float mix = 1.0f;
        provider (s, on, mix);

        const juce::Array<float> key { s.hpfOn ? 1.0f : 0.0f, s.hpfFreq, (float) s.hpfSlope, (float) s.lowType, s.lowFreq, s.lowGain, s.lowQ,
                                       s.midFreq, s.midGain, s.midQ, (float) s.highType, s.highFreq, s.highGain, s.highQ,
                                       s.lpfOn ? 1.0f : 0.0f, s.lpfFreq, (float) s.lpfSlope, on ? 1.0f : 0.0f, mix };
        if (key == lastKey)
            return;
        lastKey = key;
        active = on;

        constexpr double displayRate = 48000.0;
        const auto r = plotArea (getLocalBounds().toFloat());

        curve.clear();
        const int points = std::max (64, getWidth() / 2);
        for (int i = 0; i <= points; ++i)
        {
            const double t = (double) i / points;
            const double f = SpectrumSource::minHz * std::pow ((double) SpectrumSource::maxHz / SpectrumSource::minHz, t);
            const double eqLin = std::pow (10.0, dsp::EqualizerModule::responseDb (s, f, displayRate) / 20.0);
            const double lin = 1.0 + (eqLin - 1.0) * mix; // magnitude approximation of the dry/wet blend
            const float db = (float) (20.0 * std::log10 (std::max (lin, 1.0e-6)));
            const float x = r.getX() + (float) t * r.getWidth();
            const float y = dbToY (db, r, -rangeDb, rangeDb);
            if (i == 0)
                curve.startNewSubPath (x, y);
            else
                curve.lineTo (x, y);
        }

        repaint();
    }

    void EqCurve::paint (juce::Graphics& g)
    {
        const auto r = plotArea (getLocalBounds().toFloat());
        const float zeroY = dbToY (0.0f, r, -rangeDb, rangeDb);

        // dB guides of the EQ scale
        for (float db : { -12.0f, 12.0f })
        {
            g.setColour (colours::accent.withAlpha (0.03f));
            g.fillRect (juce::Rectangle<float> (r.getX(), dbToY (db, r, -rangeDb, rangeDb), r.getWidth(), 1.0f));
        }
        g.setColour (colours::accent.withAlpha (0.08f));
        g.fillRect (juce::Rectangle<float> (r.getX(), zeroY, r.getWidth(), 1.0f));
        g.setFont (medium (11.0f));
        g.setColour (colours::textFaint);
        for (float db : { -12.0f, 0.0f, 12.0f })
            g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (r.getRight() - 38.0f, dbToY (db, r, -rangeDb, rangeDb) - 15.0f, 30.0f, 14.0f),
                        juce::Justification::right, false);

        const float a = active ? 1.0f : 0.35f;
        juce::Path fill (curve);
        fill.lineTo (r.getRight(), zeroY);
        fill.lineTo (r.getX(), zeroY);
        fill.closeSubPath();
        g.setGradientFill (auroraGradient (r.getTopLeft(), r.getTopRight(), 0.07f * a));
        g.fillPath (fill);

        g.setGradientFill (auroraGradient (r.getTopLeft(), r.getTopRight(), 0.16f * a));
        g.strokePath (curve, juce::PathStrokeType (8.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        auto core = auroraGradient (r.getTopLeft(), r.getTopRight(), a);
        core.addColour (0.5, colours::teal.interpolatedWith (juce::Colours::white, 0.45f).withAlpha (a));
        g.setGradientFill (core);
        g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    //==========================================================================
    juce::String autoMixRole (const juce::String& id)
    {
        static const std::map<juce::String, const char*> roles {
            { pid::hpfFreq,        "Auto-Mix: knapp unter den tiefsten gesungenen Ton gesetzt (gemessen)." },
            { pid::lowGain,        "Auto-Mix: nur bei dröhnender oder sehr dünner Stimme verändert." },
            { pid::midGain,        "Auto-Mix: schneidet eine gemessene Resonanz oder Mulmigkeit unter 1,2 kHz." },
            { pid::midFreq,        "Auto-Mix: Frequenz der gemessenen Resonanz bzw. der Tiefmitten." },
            { pid::highGain,       "Auto-Mix: hebt nur dunkle Aufnahmen an, nie scharfe." },
            { pid::rideTarget,     "Auto-Mix: auf den mittleren Pegel der analysierten Stimme gesetzt." },
            { pid::rideAmount,     "Auto-Mix: richtet sich nach dem gemessenen Lautheitsumfang." },
            { pid::rideMaxBoost,   "Auto-Mix: bei Rauschen begrenzt, damit Pausen nicht lauter werden." },
            { pid::compThreshold,  "Auto-Mix: aus dem Stimmpegel berechnet und am Ergebnis nachgeprüft (Ziel: wenige dB Reduktion)." },
            { pid::compRatio,      "Auto-Mix: 3:1 als Standard, sanfter bei bereits komprimierten Aufnahmen." },
            { pid::compMakeup,     "Auto-Mix: gleicht die gemessene Reduktion aus (Gain-Staging)." },
            { pid::deessThreshold, "Auto-Mix: zwischen Vokal- und S-Pegel gesetzt, damit nur S-Laute reduziert werden." },
            { pid::deessFreq,      "Auto-Mix: aus dem gemessenen Schwerpunkt der S-Laute." },
            { pid::deessRange,     "Auto-Mix: richtet sich nach der Stärke der S-Laute." },
            { pid::smoothAmount,   "Auto-Mix: nur bei gemessener Schärfe oder Resonanz eingeschaltet." },
            { pid::satDrive,       "Auto-Mix: dezent; aus bei bereits verzerrten Aufnahmen." },
            { pid::satMix,         "Auto-Mix: dezent; aus bei bereits verzerrten Aufnahmen." },
            { pid::smashBlend,     "Auto-Mix: kleiner Anteil für Dichte; aus bei bereits komprimierten Aufnahmen." },
            { pid::doublerMix,     "Auto-Mix: dezent und mono-sicher; aus bei bereits breiten Aufnahmen." },
            { pid::reverbMix,      "Auto-Mix: weniger oder kein Hall, wenn die Aufnahme schon Raum hat." },
            { pid::delayMix,       "Auto-Mix: tempo-synchron und geduckt; aus bei verhallten Aufnahmen." },
            { pid::outputGain,     "Auto-Mix: gleicht die Lautheit an das Original an (fairer Vergleich) und hält 1 dB Reserve." },
            { pid::macroPresence,  "Feinschliff nach dem Auto-Mix: wirkt auf EQ-Höhen und De-Esser." },
            { pid::macroWarmth,    "Feinschliff nach dem Auto-Mix: wirkt auf Tiefen, Höhen und Sättigung." },
            { pid::macroAggression,"Feinschliff nach dem Auto-Mix: wirkt auf Kompressor und Sättigung." },
            { pid::macroWidth,     "Feinschliff nach dem Auto-Mix: wirkt auf Doubler und Stereobreite." },
            { pid::macroSpace,     "Feinschliff nach dem Auto-Mix: wirkt auf Hall, Delay und deren Ducking." },
            { pid::macroDensity,   "Feinschliff nach dem Auto-Mix: wirkt auf Kompressor und Sättigung." },
        };
        auto it = roles.find (id);
        return it != roles.end() ? juce::String::fromUTF8 (it->second) : juce::String();
    }

    float eqWheelQ (float currentQ, float wheelDeltaY, bool fine, float minQ, float maxQ) noexcept
    {
        if (! std::isfinite (currentQ) || ! std::isfinite (wheelDeltaY))
            return juce::jlimit (minQ, maxQ, std::isfinite (currentQ) ? currentQ : minQ);
        // One wheel notch is a delta of about 0.11: x 1.25 per notch (about 1/3 octave of Q),
        // x 1.045 with `fine`
        const float exponent = juce::jlimit (-2.0f, 2.0f, wheelDeltaY) * (fine ? 0.58f : 2.9f);
        return juce::jlimit (minQ, maxQ, currentQ * std::pow (2.0f, exponent));
    }
}
