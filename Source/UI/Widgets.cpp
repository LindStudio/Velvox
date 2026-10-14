#include "Widgets.h"
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

        juce::Label& styleCaption (juce::Label& l, float size = 13.0f)
        {
            l.setFont (font (size));
            l.setJustificationType (juce::Justification::centred);
            l.setColour (juce::Label::textColourId, colours::text.withAlpha (0.82f));
            l.setInterceptsMouseClicks (false, false);
            return l;
        }
    }

    //==========================================================================
    Knob::Knob (APVTS& state, const char* paramId, const juce::String& label, juce::Colour accent)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 16);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
        slider.setMouseDragSensitivity (220);
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

    void Knob::resized()
    {
        // Name, dial and value stay together as one block, centred in the cell
        constexpr int labelH = 16, valueH = 16;
        auto r = getLocalBounds();
        const int dial = juce::jlimit (24, 84, std::min (r.getWidth() - 8, r.getHeight() - labelH - valueH - 4));
        auto block = r.withSizeKeepingCentre (r.getWidth(), labelH + dial + valueH + 4);

        name.setBounds (block.removeFromTop (labelH));
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, block.getWidth(), valueH);
        slider.setBounds (block);
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

        // Dot on the dial edge where the macros move the processed value
        const auto params = slider.getRotaryParameters();
        const float prop = (float) slider.valueToProportionOfLength (eff);
        const float angle = params.startAngleRadians + prop * (params.endAngleRadians - params.startAngleRadians);
        const auto dialArea = slider.getBounds().toFloat().withTrimmedBottom (16.0f).reduced (3.0f);
        const float radius = std::min (dialArea.getWidth(), dialArea.getHeight()) * 0.5f;
        const auto p = dialArea.getCentre().getPointOnCircumference (radius + 1.0f, angle);

        g.setColour (colours::text);
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (p));
        g.setColour (colours::background);
        g.drawEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (p), 1.0f);
    }

    //==========================================================================
    Toggle::Toggle (APVTS& state, const char* paramId, const juce::String& label, juce::Colour accent)
    {
        setButtonText (label);
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
            auto block = r.withSizeKeepingCentre (r.getWidth(), 16 + 4 + 26);
            caption.setBounds (block.removeFromTop (16));
            block.removeFromTop (4);
            box.setBounds (block.withSizeKeepingCentre (std::min (block.getWidth() - 4, 150), 26));
        }
        else
        {
            box.setBounds (r);
        }
    }

    //==========================================================================
    PowerButton::PowerButton (APVTS& state, const char* paramId, juce::Colour c) : accent (c)
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
        {
            g.setColour (accent.withAlpha (0.25f));
            g.fillEllipse (r.expanded (1.5f));
        }
        g.setColour (on ? accent.withAlpha (0.18f) : colours::raised);
        g.fillEllipse (r);
        g.setColour (on ? accent : (highlighted ? colours::textDim : colours::line.brighter (0.2f)));
        g.drawEllipse (r, 1.0f);

        const auto c = r.getCentre();
        const float rad = size * 0.24f;
        juce::Path p;
        p.addCentredArc (c.x, c.y, rad, rad, 0.0f, 0.75f, juce::MathConstants<float>::twoPi - 0.75f, true);
        p.startNewSubPath (c.x, c.y - rad * 1.25f);
        p.lineTo (c.x, c.y - rad * 0.2f);
        g.setColour (on ? accent : colours::textDim);
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
        auto readout = r.removeFromBottom (32.0f);
        g.setColour (colours::text);
        g.setFont (display (16.0f));
        g.drawText (loudness <= -99.0f ? juce::String ("--") : juce::String (loudness, 1),
                    readout.removeFromTop (18.0f), juce::Justification::centred, false);
        g.setColour (colours::textDim);
        g.setFont (font (10.5f));
        g.drawText ("LUFS", readout, juce::Justification::centredTop, false);

        auto lamp = r.removeFromTop (8.0f);
        r.removeFromTop (4.0f);
        g.setColour (clipLatched ? colours::warning : colours::raised);
        g.fillRoundedRectangle (lamp.reduced (2.0f, 0.0f), 2.0f);

        auto scale = r.removeFromLeft (22.0f);
        g.setFont (display (10.5f));
        for (float db : { 0.0f, -6.0f, -12.0f, -24.0f, -36.0f, -48.0f })
        {
            const float y = dbToY (db, r, minDb, maxDb);
            g.setColour (colours::textDim.withAlpha (0.8f));
            g.drawText (juce::String ((int) db), scale.withY (y - 6.0f).withHeight (12.0f), juce::Justification::centredRight, false);
            g.setColour (colours::line.withAlpha (0.6f));
            g.drawHorizontalLine ((int) y, r.getX() + 3.0f, r.getRight());
        }
        r.removeFromLeft (4.0f);

        const float barW = (r.getWidth() - 3.0f) * 0.5f;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto bar = juce::Rectangle<float> (r.getX() + (float) ch * (barW + 3.0f), r.getY(), barW, r.getHeight());
            g.setColour (colours::raised);
            g.fillRoundedRectangle (bar, 2.0f);

            const float db = juce::Decibels::gainToDecibels (level[(size_t) ch], -100.0f);
            const auto fill = bar.withTop (dbToY (db, bar, minDb, maxDb));

            juce::ColourGradient grad (colours::cyan, bar.getX(), bar.getBottom(), colours::warning, bar.getX(), bar.getY(), false);
            grad.addColour (juce::jmap (-18.0, (double) minDb, (double) maxDb, 0.0, 1.0), colours::cyan);
            grad.addColour (juce::jmap (-6.0, (double) minDb, (double) maxDb, 0.0, 1.0), colours::violet);
            grad.addColour (juce::jmap (-1.0, (double) minDb, (double) maxDb, 0.0, 1.0), colours::magenta);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (fill, 2.0f);

            const float holdDb = juce::Decibels::gainToDecibels (hold[(size_t) ch], -100.0f);
            if (holdDb > minDb)
            {
                g.setColour (holdDb > 0.0f ? colours::warning : colours::text);
                g.fillRect (bar.getX(), dbToY (holdDb, bar, minDb, maxDb) - 1.0f, bar.getWidth(), 2.0f);
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
        {
            g.setColour (colours::textDim);
            g.setFont (font (10.5f));
            g.drawText (caption, r.removeFromTop (14.0f), juce::Justification::centred, false);
        }
        auto label = r.removeFromBottom (16.0f);

        g.setColour (colours::raised);
        g.fillRoundedRectangle (r, 3.0f);

        const float h = r.getHeight() * juce::jlimit (0.0f, 1.0f, shown / maxDb);
        if (h > 0.5f)
        {
            g.setColour (accent.withAlpha (0.25f));
            g.fillRoundedRectangle (r.withHeight (h).expanded (1.5f, 0.0f), 3.0f);
            g.setColour (accent);
            g.fillRoundedRectangle (r.withHeight (h), 3.0f);
        }

        g.setColour (colours::textDim);
        g.setFont (display (11.5f));
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
        auto labels = r.removeFromBottom (13.0f);
        g.setFont (display (10.5f));
        g.setColour (colours::textDim);
        g.drawText ("-1", labels, juce::Justification::left, false);
        g.drawText ("0", labels, juce::Justification::centred, false);
        g.drawText ("+1", labels, juce::Justification::right, false);

        const auto bar = r.reduced (0.0f, 2.0f);
        g.setColour (colours::raised);
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (colours::line);
        g.drawVerticalLine ((int) bar.getCentreX(), bar.getY(), bar.getBottom());

        const float x = juce::jmap (juce::jlimit (-1.0f, 1.0f, value), -1.0f, 1.0f, bar.getX(), bar.getRight());
        const auto colour = value < 0.0f ? colours::warning : (value < 0.3f ? colours::magenta : colours::ok);
        g.setColour (colour.withAlpha (0.35f));
        g.fillRoundedRectangle (juce::Rectangle<float> (std::min (x, bar.getCentreX()), bar.getY(),
                                                        std::abs (x - bar.getCentreX()), bar.getHeight()), 3.0f);
        g.setColour (colour);
        g.fillRoundedRectangle (juce::Rectangle<float> (4.0f, bar.getHeight() + 4.0f).withCentre ({ x, bar.getCentreY() }), 2.0f);
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
        constexpr float rangeDb = 18.0f;
        const auto r = getLocalBounds().toFloat().reduced (1.0f, 4.0f);

        curve.clear();
        const int points = std::max (64, getWidth() / 2);
        for (int i = 0; i <= points; ++i)
        {
            const double t = (double) i / points;
            const double f = 20.0 * std::pow (1000.0, t);
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
        const auto bounds = getLocalBounds().toFloat();
        const auto r = bounds.reduced (1.0f, 4.0f);

        g.setColour (colours::background.withAlpha (0.7f));
        g.fillRoundedRectangle (bounds, 6.0f);

        g.setFont (display (10.5f));
        for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        {
            const float x = r.getX() + (float) (std::log (f / 20.0) / std::log (1000.0)) * r.getWidth();
            g.setColour (colours::line.withAlpha (0.5f));
            g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
            g.setColour (colours::textDim.withAlpha (0.7f));
            g.drawText (f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f),
                        juce::Rectangle<float> (x + 2.0f, r.getBottom() - 13.0f, 30.0f, 12.0f), juce::Justification::left, false);
        }
        for (float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
        {
            const float y = dbToY (db, r, -18.0f, 18.0f);
            g.setColour (db == 0.0f ? colours::line : colours::line.withAlpha (0.45f));
            g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
        }

        const auto accent = active ? colours::eq : colours::textDim;
        juce::Path fill (curve);
        fill.lineTo (r.getRight(), dbToY (0.0f, r, -18.0f, 18.0f));
        fill.lineTo (r.getX(), dbToY (0.0f, r, -18.0f, 18.0f));
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.22f), r.getX(), r.getY(), colours::violet.withAlpha (0.08f), r.getRight(), r.getBottom(), false));
        g.fillPath (fill);

        g.setColour (accent.withAlpha (0.25f));
        g.strokePath (curve, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (accent);
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
        g.setColour (colours::background.withAlpha (0.7f));
        g.fillRoundedRectangle (bounds, 6.0f);

        const auto r = bounds.reduced (8.0f);
        g.setColour (colours::line.withAlpha (0.6f));
        g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());
        g.drawVerticalLine ((int) r.getCentreX(), r.getY(), r.getBottom());
        g.setColour (colours::line);
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

        const auto c = active ? colours::sat : colours::textDim;
        g.setColour (c.withAlpha (0.25f));
        g.strokePath (p, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    //==========================================================================
    void StepPatternView::set (const char* steps, int currentStep, bool activeNow, juce::Colour accent)
    {
        const juce::String s (steps);
        if (s != pattern || currentStep != current || activeNow != isActive || accent != colour)
        {
            pattern = s;
            current = currentStep;
            isActive = activeNow;
            colour = accent;
            repaint();
        }
    }

    void StepPatternView::paint (juce::Graphics& g)
    {
        const auto r = getLocalBounds().toFloat();
        const int n = juce::jmax (1, pattern.length());
        const float gap = 4.0f;
        const float w = (r.getWidth() - gap * (n - 1)) / n;

        for (int i = 0; i < n; ++i)
        {
            const auto cell = juce::Rectangle<float> (r.getX() + i * (w + gap), r.getY(), w, r.getHeight());
            const auto type = pattern[i];
            const bool now = isActive && i == current;

            juce::Colour fill = colours::raised;
            if (type == 'x') fill = colour.withAlpha (isActive ? 0.55f : 0.25f);
            if (type == 'r') fill = colours::magenta.withAlpha (isActive ? 0.45f : 0.2f);
            if (now)
            {
                g.setColour (colours::text.withAlpha (0.3f));
                g.fillRoundedRectangle (cell.expanded (2.0f), 4.0f);
            }
            g.setColour (fill);
            g.fillRoundedRectangle (cell, 3.0f);
            if (type == 'r')
            {
                g.setColour (colours::text.withAlpha (0.8f));
                g.setFont (display (11.0f));
                g.drawText ("R", cell, juce::Justification::centred, false);
            }
            if (i % 4 == 0)
            {
                g.setColour (colours::line.brighter (0.3f));
                g.fillRect (cell.getX(), r.getBottom() + 3.0f, w, 1.5f);
            }
        }
    }
}
