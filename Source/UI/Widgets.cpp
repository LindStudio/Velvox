#include "Widgets.h"
#include "VelvoxLookAndFeel.h"
#include "Spectrum.h"
#include "../Parameters/ParameterLayout.h"
#include "../DSP/SaturationModule.h"

namespace velvox::ui
{
    namespace
    {
        float dbToY (float db, juce::Rectangle<float> r, float minDb, float maxDb)
        {
            return juce::jmap (juce::jlimit (minDb, maxDb, db), minDb, maxDb, r.getBottom(), r.getY());
        }

        juce::Label& styleCaption (juce::Label& l, float size = 9.5f)
        {
            l.setFont (caps (size));
            l.setJustificationType (juce::Justification::centred);
            l.setColour (juce::Label::textColourId, colours::textDim);
            l.setInterceptsMouseClicks (false, false);
            return l;
        }

        constexpr int knobLabelH = 14, knobValueH = 16, knobGap = 4;
    }

    //==========================================================================
    Knob::Knob (APVTS& state, const char* paramId, const juce::String& label, juce::Colour)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, knobValueH);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        slider.setMouseDragSensitivity (220);
        slider.setRepaintsOnMouseActivity (true);
        slider.setVelocityModeParameters (0.9, 1, 0.08, true, juce::ModifierKeys::shiftModifier);
        addAndMakeVisible (slider);

        name.setText (label, juce::dontSendNotification);
        addAndMakeVisible (styleCaption (name));

        attachment = std::make_unique<APVTS::SliderAttachment> (state, paramId, slider);

        if (const auto* spec = findParameterSpec (paramId))
        {
            slider.setDoubleClickReturnValue (true, spec->defaultValue);
            slider.setTooltip (juce::String (spec->description) + "\nDouble-click to reset, click the value to type.");
        }
    }

    void Knob::setHero (bool isHero)
    {
        hero = isHero;
        slider.getProperties().set (VelvoxLookAndFeel::heroProperty, isHero);
        styleCaption (name, isHero ? 10.5f : 9.5f);
        name.setColour (juce::Label::textColourId, isHero ? colours::text.withAlpha (0.9f) : colours::textDim);
        resized();
        repaint();
    }

    void Knob::resized()
    {
        // Caption, dial and value stay together as one block, centred in the cell
        auto r = getLocalBounds();
        const int labelH = hero ? knobLabelH + 4 : knobLabelH;
        const int dial = juce::jlimit (24, maxDial, std::min (r.getWidth() - 8, r.getHeight() - labelH - knobValueH - 2 * knobGap));
        auto block = r.withSizeKeepingCentre (r.getWidth(), labelH + knobGap + dial + knobValueH);

        name.setBounds (block.removeFromTop (labelH));
        block.removeFromTop (knobGap);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, block.getWidth(), knobValueH);
        slider.setBounds (block);
    }

    juce::Rectangle<float> Knob::dialBounds() const
    {
        // Same area the look-and-feel draws the dial into
        auto b = slider.getBounds().toFloat().withTrimmedBottom ((float) knobValueH);
        const float size = std::min (b.getWidth(), b.getHeight());
        return b.withSizeKeepingCentre (size, size).reduced (4.0f);
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

        // Glowing dot on the ring where the macros move the processed value
        const auto params = slider.getRotaryParameters();
        const float prop = (float) slider.valueToProportionOfLength (eff);
        const float angle = params.startAngleRadians + prop * (params.endAngleRadians - params.startAngleRadians);
        const auto d = dialBounds();
        const float radius = d.getWidth() * 0.5f - std::max (2.0f, d.getWidth() * 0.5f * 0.075f) * 0.5f;
        const auto p = d.getCentre().getPointOnCircumference (radius, angle);

        drawGlow (g, p, 7.0f, 0.4f);
        g.setColour (colours::background);
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (p));
        g.setColour (colours::accent);
        g.drawEllipse (juce::Rectangle<float> (5.5f, 5.5f).withCentre (p), 1.4f);
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
            auto block = r.withSizeKeepingCentre (r.getWidth(), knobLabelH + 8 + 28);
            caption.setBounds (block.removeFromTop (knobLabelH));
            block.removeFromTop (8);
            box.setBounds (block.withSizeKeepingCentre (std::min (block.getWidth() - 8, 150), 28));
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
            drawGlow (g, r.getCentre(), size * 0.9f, 0.18f);
        g.setColour (on ? colours::accent.withAlpha (0.10f) : colours::raised);
        g.fillEllipse (r);
        g.setColour (on ? colours::accent.withAlpha (0.55f) : (highlighted ? colours::line.brighter (0.5f) : colours::line.brighter (0.15f)));
        g.drawEllipse (r.reduced (0.5f), 1.0f);

        const auto c = r.getCentre();
        const float rad = size * 0.22f;
        juce::Path p;
        p.addCentredArc (c.x, c.y, rad, rad, 0.0f, 0.7f, juce::MathConstants<float>::twoPi - 0.7f, true);
        p.startNewSubPath (c.x, c.y - rad * 1.3f);
        p.lineTo (c.x, c.y - rad * 0.25f);
        g.setColour (on ? colours::accent : colours::textDim);
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    //==========================================================================
    void LevelMeter::push (float left, float right, bool clipped)
    {
        constexpr float decayPerFrame = 0.92f; // ~ 20 dB/s at 30 fps
        const float in[2] { left, right };

        for (size_t i = 0; i < 2; ++i)
        {
            level[i] = std::max (in[i], level[i] * decayPerFrame);
            if (in[i] >= hold[i])
            {
                hold[i] = in[i];
                holdFrames[i] = 45;
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

        // LUFS read-out
        auto readout = r.removeFromBottom (40.0f);
        g.setColour (colours::text);
        g.setFont (display (22.0f));
        g.drawText (loudness <= -99.0f ? juce::String ("-") : juce::String (loudness, 1),
                    readout.removeFromTop (24.0f), juce::Justification::centred, false);
        drawCaption (g, "LUFS", readout, juce::Justification::centredTop, colours::textFaint, 9.0f);

        // Clip lamp
        auto lampRow = r.removeFromTop (10.0f);
        r.removeFromTop (8.0f);
        const auto lamp = juce::Rectangle<float> (6.0f, 6.0f).withCentre (lampRow.getCentre());
        if (clipLatched)
            drawGlow (g, lamp.getCentre(), 9.0f, 0.5f, colours::warning);
        g.setColour (clipLatched ? colours::warning : colours::accent.withAlpha (0.08f));
        g.fillEllipse (lamp);

        auto scale = scaleLeft ? r.removeFromLeft (20.0f) : r.removeFromRight (20.0f);
        auto bars = r.withSizeKeepingCentre (std::min (r.getWidth(), 18.0f), r.getHeight());
        if (scaleLeft)
            bars.setX (r.getX() + 4.0f);
        else
            bars.setX (r.getRight() - 4.0f - bars.getWidth());

        g.setFont (medium (9.0f));
        for (float db : { 0.0f, -6.0f, -12.0f, -24.0f, -36.0f, -48.0f })
        {
            const float y = dbToY (db, bars, minDb, maxDb);
            g.setColour (colours::textFaint);
            g.drawText (juce::String ((int) std::abs (db)), scale.withY (y - 6.0f).withHeight (12.0f),
                        scaleLeft ? juce::Justification::centredRight : juce::Justification::centredLeft, false);
        }

        const float barGap = 4.0f;
        const float barW = (bars.getWidth() - barGap) * 0.5f;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto bar = juce::Rectangle<float> (bars.getX() + (float) ch * (barW + barGap), bars.getY(), barW, bars.getHeight());
            g.setColour (colours::accent.withAlpha (0.05f));
            g.fillRoundedRectangle (bar, barW * 0.5f);

            const float db = juce::Decibels::gainToDecibels (level[(size_t) ch], -100.0f);
            const auto fill = bar.withTop (dbToY (db, bar, minDb, maxDb));
            if (fill.getHeight() > 0.5f)
            {
                juce::ColourGradient grad (colours::accent.withAlpha (0.28f), 0.0f, bar.getBottom(),
                                           colours::accent, 0.0f, bar.getY(), false);
                grad.addColour (juce::jmap (-1.0, (double) minDb, (double) maxDb, 0.0, 1.0), colours::accent);
                grad.addColour (juce::jmap (0.0, (double) minDb, (double) maxDb, 0.0, 1.0), colours::warning);
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
        const float next = reductionDb > shown ? reductionDb : shown * 0.85f + reductionDb * 0.15f;
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
            drawCaption (g, caption, r.removeFromTop (16.0f), juce::Justification::centred, colours::textDim, 9.0f);
        auto label = r.removeFromBottom (20.0f);
        r.removeFromTop (4.0f);

        const auto bar = r.withSizeKeepingCentre (6.0f, r.getHeight());
        g.setColour (colours::accent.withAlpha (0.05f));
        g.fillRoundedRectangle (bar, 3.0f);

        const float h = bar.getHeight() * juce::jlimit (0.0f, 1.0f, shown / maxDb);
        if (h > 0.5f)
        {
            const auto fill = bar.withHeight (h);
            drawGlow (g, { fill.getCentreX(), fill.getBottom() }, 12.0f, 0.25f);
            g.setGradientFill (juce::ColourGradient (colours::accent.withAlpha (0.35f), 0.0f, fill.getY(),
                                                     colours::accent, 0.0f, fill.getBottom(), false));
            g.fillRoundedRectangle (fill, 3.0f);
        }

        g.setColour (colours::text.withAlpha (0.8f));
        g.setFont (medium (11.5f));
        g.drawText (shown < 0.05f ? "0.0" : "-" + juce::String (shown, 1), label, juce::Justification::centred, false);
    }

    //==========================================================================
    void CorrelationView::push (float c)
    {
        const float next = value * 0.7f + c * 0.3f;
        if (std::abs (next - value) > 0.002f)
        {
            value = next;
            repaint();
        }
    }

    void CorrelationView::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        auto labels = r.removeFromBottom (12.0f);
        g.setFont (medium (9.0f));
        g.setColour (colours::textFaint);
        g.drawText ("-1", labels, juce::Justification::left, false);
        g.drawText ("0", labels, juce::Justification::centred, false);
        g.drawText ("+1", labels, juce::Justification::right, false);

        const auto track = r.withSizeKeepingCentre (r.getWidth(), 3.0f);
        g.setColour (colours::accent.withAlpha (0.07f));
        g.fillRoundedRectangle (track, 1.5f);
        g.setColour (colours::accent.withAlpha (0.18f));
        g.fillRect (juce::Rectangle<float> (track.getCentreX() - 0.5f, track.getY() - 3.0f, 1.0f, track.getHeight() + 6.0f));

        const float x = juce::jmap (juce::jlimit (-1.0f, 1.0f, value), -1.0f, 1.0f, track.getX(), track.getRight());
        const auto colour = value < 0.0f ? colours::warning : colours::accent;
        g.setColour (colour.withAlpha (0.45f));
        g.fillRoundedRectangle (juce::Rectangle<float> (std::min (x, track.getCentreX()), track.getY(),
                                                        std::abs (x - track.getCentreX()), track.getHeight()), 1.5f);
        drawGlow (g, { x, track.getCentreY() }, 10.0f, 0.35f, colour);
        g.setColour (colour);
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
        const auto r = getLocalBounds().toFloat().reduced (0.0f, 6.0f);

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
        const auto r = getLocalBounds().toFloat().reduced (0.0f, 6.0f);
        const float zeroY = dbToY (0.0f, r, -rangeDb, rangeDb);

        // dB guides of the EQ scale
        for (float db : { -12.0f, 12.0f })
        {
            g.setColour (colours::accent.withAlpha (0.03f));
            g.fillRect (juce::Rectangle<float> (r.getX(), dbToY (db, r, -rangeDb, rangeDb), r.getWidth(), 1.0f));
        }
        g.setColour (colours::accent.withAlpha (0.08f));
        g.fillRect (juce::Rectangle<float> (r.getX(), zeroY, r.getWidth(), 1.0f));
        g.setFont (medium (9.0f));
        g.setColour (colours::textFaint);
        for (float db : { -12.0f, 0.0f, 12.0f })
            g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (r.getRight() - 30.0f, dbToY (db, r, -rangeDb, rangeDb) - 13.0f, 26.0f, 12.0f),
                        juce::Justification::right, false);

        const float a = active ? 1.0f : 0.35f;
        juce::Path fill (curve);
        fill.lineTo (r.getRight(), zeroY);
        fill.lineTo (r.getX(), zeroY);
        fill.closeSubPath();
        g.setColour (colours::accent.withAlpha (0.035f * a));
        g.fillPath (fill);

        g.setColour (colours::accent.withAlpha (0.10f * a));
        g.strokePath (curve, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (colours::accent.withAlpha (0.95f * a));
        g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    //==========================================================================
    void SaturationCurve::refresh()
    {
        if (! provider)
            return;
        float d = 0.0f, m = 0.0f;
        int c = 0;
        bool a = false;
        provider (d, m, c, a);
        if (d != drive || m != mix || c != character || a != active)
        {
            drive = d;
            mix = m;
            character = c;
            active = a;
            repaint();
        }
    }

    void SaturationCurve::paint (juce::Graphics& g)
    {
        const auto bounds = getLocalBounds().toFloat();
        drawGlass (g, bounds, 12.0f, 0.018f);

        const auto r = bounds.reduced (14.0f);
        g.setColour (colours::accent.withAlpha (0.05f));
        for (int i = 1; i < 4; ++i)
        {
            g.fillRect (juce::Rectangle<float> (r.getX() + r.getWidth() * (float) i / 4.0f, r.getY(), 1.0f, r.getHeight()));
            g.fillRect (juce::Rectangle<float> (r.getX(), r.getY() + r.getHeight() * (float) i / 4.0f, r.getWidth(), 1.0f));
        }
        g.setColour (colours::accent.withAlpha (0.10f));
        g.drawLine (r.getX(), r.getBottom(), r.getRight(), r.getY(), 1.0f);

        const float driveLin = juce::Decibels::decibelsToGain (drive);
        const float comp = dsp::SaturationModule::compensationFor (driveLin, character);

        juce::Path p;
        const int n = 96;
        for (int i = 0; i <= n; ++i)
        {
            const float x = -1.0f + 2.0f * (float) i / n;
            const float wet = dsp::SaturationModule::shape (x, driveLin, character) * comp;
            const float y = juce::jlimit (-1.2f, 1.2f, x + (wet - x) * mix);
            const float px = juce::jmap (x, -1.0f, 1.0f, r.getX(), r.getRight());
            const float py = juce::jmap (y, -1.2f, 1.2f, r.getBottom(), r.getY());
            if (i == 0)
                p.startNewSubPath (px, py);
            else
                p.lineTo (px, py);
        }

        const float a = active ? 1.0f : 0.35f;
        g.setColour (colours::accent.withAlpha (0.10f * a));
        g.strokePath (p, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (colours::accent.withAlpha (0.95f * a));
        g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        drawCaption (g, "Transfer", bounds.reduced (14.0f, 10.0f).withHeight (14.0f), juce::Justification::centredLeft, colours::textFaint, 9.0f);
    }

    //==========================================================================
    void StepPatternView::set (const char* steps, int currentStep, bool activeNow, juce::Colour)
    {
        const juce::String s (steps);
        if (s != pattern || currentStep != current || activeNow != isActive)
        {
            pattern = s;
            current = currentStep;
            isActive = activeNow;
            repaint();
        }
    }

    void StepPatternView::paint (juce::Graphics& g)
    {
        const auto r = getLocalBounds().toFloat().withTrimmedBottom (6.0f);
        const int n = juce::jmax (1, pattern.length());
        const float gap = 5.0f;
        const float w = (r.getWidth() - gap * (float) (n - 1)) / (float) n;
        const float a = isActive ? 1.0f : 0.45f;

        for (int i = 0; i < n; ++i)
        {
            const auto cell = juce::Rectangle<float> (r.getX() + (float) i * (w + gap), r.getY(), w, r.getHeight());
            const auto type = pattern[i];
            const bool now = isActive && i == current;

            if (now)
                drawGlow (g, cell.getCentre(), std::max (w, cell.getHeight()) * 0.9f, 0.22f);

            if (type == 'x')
            {
                g.setGradientFill (juce::ColourGradient (colours::accent.withAlpha ((now ? 1.0f : 0.55f) * a), 0.0f, cell.getY(),
                                                         colours::accent.withAlpha ((now ? 0.8f : 0.30f) * a), 0.0f, cell.getBottom(), false));
                g.fillRoundedRectangle (cell, 4.0f);
            }
            else if (type == 'r')
            {
                g.setColour (colours::accent.withAlpha (0.08f * a));
                g.fillRoundedRectangle (cell, 4.0f);
                g.setColour (colours::accent.withAlpha ((now ? 1.0f : 0.55f) * a));
                g.drawRoundedRectangle (cell.reduced (0.5f), 4.0f, 1.2f);
                g.setFont (caps (10.0f));
                g.drawText ("R", cell, juce::Justification::centred, false);
            }
            else
            {
                g.setColour (colours::accent.withAlpha (now ? 0.16f : 0.045f));
                g.fillRoundedRectangle (cell, 4.0f);
            }

            if (i % 4 == 0)
            {
                g.setColour (colours::accent.withAlpha (0.18f));
                g.fillRoundedRectangle (cell.getX(), r.getBottom() + 4.0f, w, 1.5f, 0.75f);
            }
        }
    }
}
