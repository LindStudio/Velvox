#include "Widgets.h"
#include "VelvoxLookAndFeel.h"
#include "Spectrum.h"
#include "../Parameters/ParameterLayout.h"

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
            l.setFont (medium (size));
            l.setJustificationType (juce::Justification::centred);
            l.setColour (juce::Label::textColourId, colours::textDim);
            l.setInterceptsMouseClicks (false, false);
            return l;
        }

        constexpr int knobLabelH = 18, knobValueH = 20, knobGap = 5;
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

    void InfoPopup::show (juce::Rectangle<int> anchor, const juce::String& t, const juce::String& body)
    {
        title = t;
        text = body;
        anchorArea = anchor;

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
        g.setColour (colours::text);
        g.setFont (font (14.5f, true));
        g.drawText (title, inner.removeFromTop (20.0f), juce::Justification::centredLeft, false);
        inner.removeFromTop (4.0f);
        g.setGradientFill (auroraGradient (inner.getTopLeft(), inner.getTopLeft().translated (36.0f, 0.0f)));
        g.fillRoundedRectangle (inner.getX(), inner.getY(), 36.0f, 2.0f, 1.0f);
        inner.removeFromTop (8.0f);
        layoutText (text, inner.getWidth()).draw (g, inner);
    }

    //==========================================================================
    void FineSlider::mouseDown (const juce::MouseEvent& e)
    {
        juce::Slider::mouseDown (e);   // gesture start, Alt-click reset, text box handling
        lastPos = e.position;
        dragProportion = valueToProportionOfLength (getValue());
    }

    void FineSlider::mouseDrag (const juce::MouseEvent& e)
    {
        if (! isEnabled() || e.mods.isPopupMenu() || getRange().getLength() <= 0.0)
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
    Knob::Knob (APVTS& state, const char* paramId, const juce::String& label, juce::Colour)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, knobValueH);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        slider.setMouseDragSensitivity (FineSlider::normalPixels);   // also used by the mouse wheel
        slider.setVelocityModeParameters (0.9, 1, 0.08, false);     // fine mode is handled by FineSlider
        slider.addMouseListener (this, true);
        addAndMakeVisible (slider);

        name.setText (label, juce::dontSendNotification);
        addAndMakeVisible (styleCaption (name));

        attachment = std::make_unique<APVTS::SliderAttachment> (state, paramId, slider);

        if (const auto* spec = findParameterSpec (paramId))
        {
            slider.setDoubleClickReturnValue (true, spec->defaultValue);
            description = spec->description;
        }

        shownPos.snap ((float) slider.valueToProportionOfLength (slider.getValue()));
        slider.getProperties().set (VelvoxLookAndFeel::animPosProperty, shownPos.current);
    }

    Knob::~Knob()
    {
        // The info card notices on its own when its anchor goes away
        slider.removeMouseListener (this);
    }

    void Knob::setHero (bool isHero)
    {
        hero = isHero;
        slider.getProperties().set (VelvoxLookAndFeel::heroProperty, isHero);
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
        // Primary controls read first: larger, brighter name and read-out
        const bool strong = hero || prominence == Prominence::primary;
        styleCaption (name, hero ? 15.0f : (strong ? 14.5f : typesize::label));
        name.setColour (juce::Label::textColourId, strong ? colours::text : colours::textDim);
        slider.getProperties().set (VelvoxLookAndFeel::strongProperty, strong);
        slider.sendLookAndFeelChange();
    }

    void Knob::resized()
    {
        // Caption, dial and value stay together as one block, centred in the cell
        auto r = getLocalBounds();
        const bool strong = hero || prominence == Prominence::primary;
        const int labelH = strong ? knobLabelH + 2 : knobLabelH;
        const int valueH = strong ? knobValueH + 2 : knobValueH;
        const int dial = juce::jlimit (24, maxDial, std::min (r.getWidth() - 8, r.getHeight() - labelH - valueH - 2 * knobGap));
        auto block = r.withSizeKeepingCentre (r.getWidth(), labelH + knobGap + dial + knobGap + valueH);

        name.setBounds (block.removeFromTop (labelH));
        block.removeFromTop (knobGap);
        valueBoxH = valueH + knobGap;
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, block.getWidth(), valueBoxH);
        slider.setBounds (block);
    }

    juce::Rectangle<float> Knob::dialBounds() const
    {
        // Same area the look-and-feel draws the dial into
        auto b = slider.getBounds().toFloat().withTrimmedBottom ((float) valueBoxH);
        const float size = std::min (b.getWidth(), b.getHeight());
        return b.withSizeKeepingCentre (size, size).reduced (2.0f);
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
    }

    void Knob::mouseEnter (const juce::MouseEvent&)
    {
        if (description.isNotEmpty() && ! slider.isMouseButtonDown())
            startTimer (1000);
    }

    void Knob::mouseExit (const juce::MouseEvent&)
    {
        if (isMouseOver (true))
            return;
        stopTimer();
        if (auto* host = findParentComponentOfClass<InfoPopupHost>())
            host->hideInfo (*this);
    }

    void Knob::mouseDown (const juce::MouseEvent&)
    {
        stopTimer();
        if (auto* host = findParentComponentOfClass<InfoPopupHost>())
            host->hideInfo (*this);
    }

    void Knob::timerCallback()
    {
        stopTimer();
        if (slider.isMouseButtonDown() || ! isMouseOver (true))
            return;
        if (auto* host = findParentComponentOfClass<InfoPopupHost>())
            host->showInfo (*this, name.getText(), description);
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

        // Glowing dot on the groove where the macros move the processed value
        const auto params = slider.getRotaryParameters();
        const float prop = (float) slider.valueToProportionOfLength (eff);
        const float angle = params.startAngleRadians + prop * (params.endAngleRadians - params.startAngleRadians);
        const auto d = dialBounds();
        const float radius = d.getWidth() * 0.5f;
        const float tw = std::max (3.0f, radius * VelvoxLookAndFeel::grooveRatio);
        const auto p = d.getCentre().getPointOnCircumference (radius - tw * 0.5f, angle);

        drawGlow (g, p, 8.0f, 0.5f, colours::teal);
        g.setColour (colours::text);
        g.fillEllipse (juce::Rectangle<float> (4.5f, 4.5f).withCentre (p));
    }

    //==========================================================================
    Toggle::Toggle (APVTS& state, const char* paramId, const juce::String& label, juce::Colour)
    {
        setButtonText (label);
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

        caption.setText (captionText, juce::dontSendNotification);
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
            drawGlow (g, r.getCentre(), size * 1.0f, 0.30f, colours::teal);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a2d34), r.getCentreX(), r.getY(),
                                                 juce::Colour (0xff121317), r.getCentreX(), r.getBottom(), false));
        g.fillEllipse (r);
        g.setColour (on ? aurora (0.2f, 0.7f) : (highlighted ? colours::line.brighter (0.6f) : colours::line.brighter (0.25f)));
        g.drawEllipse (r.reduced (0.5f), 1.0f);

        const auto c = r.getCentre();
        const float rad = size * 0.22f;
        juce::Path p;
        p.addCentredArc (c.x, c.y, rad, rad, 0.0f, 0.7f, juce::MathConstants<float>::twoPi - 0.7f, true);
        p.startNewSubPath (c.x, c.y - rad * 1.3f);
        p.lineTo (c.x, c.y - rad * 0.25f);
        g.setColour (on ? colours::teal : colours::textDim);
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

        // LUFS read-out on a recessed plate
        auto readout = r.removeFromBottom (58.0f);
        {
            const auto plate = readout.reduced (2.0f, 2.0f);
            g.setColour (juce::Colours::black.withAlpha (0.28f));
            g.fillRoundedRectangle (plate, radius::control);
            g.setColour (colours::lineSoft);
            g.drawRoundedRectangle (plate.reduced (0.5f), radius::control, 1.0f);
        }
        readout.removeFromTop (6.0f);
        g.setColour (colours::text);
        g.setFont (display (30.0f));
        g.drawText (loudness <= -99.0f ? juce::String ("-") : juce::String (loudness, 1),
                    readout.removeFromTop (32.0f), juce::Justification::centred, false);
        drawCaption (g, "LUFS", readout, juce::Justification::centredTop, colours::textDim, 9.5f);
        r.removeFromBottom (12.0f);

        // Clip lamp
        auto lampRow = r.removeFromTop (12.0f);
        r.removeFromTop (10.0f);
        const auto lamp = juce::Rectangle<float> (8.0f, 8.0f).withCentre (lampRow.getCentre());
        if (clipLatched)
            drawGlow (g, lamp.getCentre(), 11.0f, 0.5f, colours::warning);
        g.setColour (clipLatched ? colours::warning : colours::accent.withAlpha (0.12f));
        g.fillEllipse (lamp);

        // Scale on one side, two bars next to it, both centred as one block
        constexpr float scaleW = 30.0f, barsW = 26.0f, scaleGap = 6.0f;
        auto block = r.withSizeKeepingCentre (std::min (r.getWidth(), scaleW + scaleGap + barsW), r.getHeight());
        auto scale = scaleLeft ? block.removeFromLeft (scaleW) : block.removeFromRight (scaleW);
        auto bars = scaleLeft ? block.removeFromRight (barsW) : block.removeFromLeft (barsW);

        g.setFont (medium (11.5f));
        for (float db : { 6.0f, 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f })
        {
            const float y = dbToY (db, bars, minDb, maxDb);
            g.setColour (db == 0.0f ? colours::textDim : colours::textFaint);
            g.drawText (db > 0.0f ? "+" + juce::String ((int) db) : juce::String ((int) std::abs (db)), scale.withY (y - 7.0f).withHeight (14.0f),
                        scaleLeft ? juce::Justification::centredRight : juce::Justification::centredLeft, false);
            g.setColour (colours::accent.withAlpha (db == 0.0f ? 0.22f : 0.08f));
            g.fillRect (scaleLeft ? juce::Rectangle<float> (bars.getX() - 5.0f, y - 0.5f, 3.0f, 1.0f)
                                  : juce::Rectangle<float> (bars.getRight() + 2.0f, y - 0.5f, 3.0f, 1.0f));
        }

        const float barGap = 4.0f;
        const float barW = (bars.getWidth() - barGap) * 0.5f;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto bar = juce::Rectangle<float> (bars.getX() + (float) ch * (barW + barGap), bars.getY(), barW, bars.getHeight());
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillRoundedRectangle (bar, barW * 0.5f);

            const float db = juce::Decibels::gainToDecibels (level[(size_t) ch], -100.0f);
            const auto fill = bar.withTop (dbToY (db, bar, minDb, maxDb));
            if (fill.getHeight() > 0.5f)
            {
                // Blue at the bottom, teal through the body, white near full scale, red above
                juce::ColourGradient grad (colours::blue.withAlpha (0.55f), 0.0f, bar.getBottom(), colours::warning, 0.0f, bar.getY(), false);
                grad.addColour (juce::jmap (-30.0, (double) minDb, (double) maxDb, 0.0, 1.0), colours::teal);
                grad.addColour (juce::jmap (-3.0, (double) minDb, (double) maxDb, 0.0, 1.0), colours::teal.interpolatedWith (juce::Colours::white, 0.6f));
                grad.addColour (juce::jmap (0.0, (double) minDb, (double) maxDb, 0.0, 1.0), juce::Colours::white);
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
}
