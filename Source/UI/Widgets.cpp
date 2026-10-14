#include "Widgets.h"
#include "../Parameters/ParameterLayout.h"
#include "../DSP/SaturationModule.h"

namespace velvox::ui
{
    namespace
    {
        float rawValue (APVTS& s, const char* id)
        {
            return s.getRawParameterValue (id)->load();
        }

        float dbToY (float db, juce::Rectangle<float> r, float minDb, float maxDb)
        {
            return juce::jmap (juce::jlimit (minDb, maxDb, db), minDb, maxDb, r.getBottom(), r.getY());
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
        name.setJustificationType (juce::Justification::centred);
        name.setFont (font (13.0f));
        name.setColour (juce::Label::textColourId, colours::text.withAlpha (0.85f));
        name.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (name);

        attachment = std::make_unique<APVTS::SliderAttachment> (state, paramId, slider);

        if (const auto* spec = findParameterSpec (paramId))
        {
            slider.setDoubleClickReturnValue (true, spec->defaultValue);
            slider.setTooltip (juce::String (spec->description) + "\nDouble-click to reset, click the value to type.");
        }
    }

    void Knob::resized()
    {
        // Keep name, dial and value together as one block, centred in the cell
        constexpr int labelH = 16, valueH = 16;
        auto r = getLocalBounds();
        const int dial = juce::jlimit (24, 76, std::min (r.getWidth() - 8, r.getHeight() - labelH - valueH - 4));
        auto block = r.withSizeKeepingCentre (r.getWidth(), labelH + dial + valueH + 4);

        name.setBounds (block.removeFromTop (labelH));
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, block.getWidth(), valueH);
        slider.setBounds (block);
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
    Choice::Choice (APVTS& state, const char* paramId)
    {
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId)))
            addItemList (p->choices, 1);
        attachment = std::make_unique<APVTS::ComboBoxAttachment> (state, paramId, *this);
        if (const auto* spec = findParameterSpec (paramId))
            setTooltip (spec->description);
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

    void LevelMeter::paint (juce::Graphics& g)
    {
        constexpr float minDb = -60.0f, maxDb = 6.0f;
        auto r = getLocalBounds().toFloat();
        auto lamp = r.removeFromTop (10.0f);
        r.removeFromTop (4.0f);

        g.setColour (clipLatched ? colours::warning : colours::raised);
        g.fillRoundedRectangle (lamp.reduced (2.0f, 1.0f), 2.0f);

        auto scale = r.removeFromLeft (22.0f);
        g.setFont (font (10.0f));
        for (float db : { 0.0f, -6.0f, -12.0f, -24.0f, -36.0f, -48.0f })
        {
            const float y = dbToY (db, r, minDb, maxDb);
            g.setColour (colours::textDim.withAlpha (0.8f));
            g.drawText (juce::String ((int) db), scale.withY (y - 6.0f).withHeight (12.0f), juce::Justification::centredRight, false);
            g.setColour (colours::line.withAlpha (0.6f));
            g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
        }
        r.removeFromLeft (4.0f);

        const float barW = (r.getWidth() - 3.0f) * 0.5f;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto bar = juce::Rectangle<float> (r.getX() + (float) ch * (barW + 3.0f), r.getY(), barW, r.getHeight());
            g.setColour (colours::raised);
            g.fillRoundedRectangle (bar, 2.0f);

            const float db = juce::Decibels::gainToDecibels (level[(size_t) ch], -100.0f);
            const float top = dbToY (db, bar, minDb, maxDb);
            const auto fill = bar.withTop (top);

            juce::ColourGradient grad (colours::eq, bar.getX(), bar.getBottom(), colours::warning, bar.getX(), bar.getY(), false);
            grad.addColour (juce::jmap (-12.0, (double) minDb, (double) maxDb, 0.0, 1.0), colours::eq);
            grad.addColour (juce::jmap (-3.0, (double) minDb, (double) maxDb, 0.0, 1.0), colours::reverb);
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
        shown = reductionDb > shown ? reductionDb : shown * 0.85f + reductionDb * 0.15f;
        repaint();
    }

    void GainReductionMeter::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        auto label = r.removeFromBottom (16.0f);

        g.setColour (colours::raised);
        g.fillRoundedRectangle (r, 3.0f);

        const float h = r.getHeight() * juce::jlimit (0.0f, 1.0f, shown / maxDb);
        g.setColour (accent);
        g.fillRoundedRectangle (r.withHeight (h), 3.0f);

        g.setColour (colours::textDim);
        g.setFont (font (11.0f));
        g.drawText (shown < 0.05f ? "0.0" : "-" + juce::String (shown, 1), label, juce::Justification::centred, false);
    }

    //==========================================================================
    EqCurve::EqCurve (APVTS& state) : apvts (state)
    {
        lastKey.fill (-1.0e9f);
        setInterceptsMouseClicks (false, false);
    }

    void EqCurve::refresh()
    {
        const std::array<float, 12> key {
            rawValue (apvts, pid::hpfOn), rawValue (apvts, pid::hpfFreq), rawValue (apvts, pid::hpfSlope),
            rawValue (apvts, pid::lowFreq), rawValue (apvts, pid::lowGain), rawValue (apvts, pid::midFreq),
            rawValue (apvts, pid::midGain), rawValue (apvts, pid::midQ), rawValue (apvts, pid::highFreq),
            rawValue (apvts, pid::highGain), rawValue (apvts, pid::eqOn), rawValue (apvts, pid::eqMix)
        };

        if (key == lastKey || getWidth() <= 0)
            return;
        lastKey = key;

        dsp::EqSettings s;
        s.hpfOn = key[0] > 0.5f;
        s.hpfFreq = key[1];
        s.hpfSlope = (int) std::lround (key[2]);
        s.lowFreq = key[3];
        s.lowGain = key[4];
        s.midFreq = key[5];
        s.midGain = key[6];
        s.midQ = key[7];
        s.highFreq = key[8];
        s.highGain = key[9];
        active = key[10] > 0.5f;
        const double mix = key[11] * 0.01;

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

        g.setColour (colours::background.withAlpha (0.55f));
        g.fillRoundedRectangle (bounds, 4.0f);

        g.setFont (font (10.0f));
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
        g.setColour (accent.withAlpha (0.12f));
        g.fillPath (fill);

        g.setColour (accent);
        g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    //==========================================================================
    void SaturationCurve::refresh()
    {
        const float d = rawValue (apvts, pid::satDrive);
        const float m = rawValue (apvts, pid::satMix);
        const bool a = rawValue (apvts, pid::satOn) > 0.5f;
        if (d != drive || m != mix || a != active)
        {
            drive = d;
            mix = m;
            active = a;
            repaint();
        }
    }

    void SaturationCurve::paint (juce::Graphics& g)
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour (colours::background.withAlpha (0.55f));
        g.fillRoundedRectangle (bounds, 4.0f);

        const auto r = bounds.reduced (6.0f);
        g.setColour (colours::line.withAlpha (0.6f));
        g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());
        g.drawVerticalLine ((int) r.getCentreX(), r.getY(), r.getBottom());

        // Unity reference
        g.setColour (colours::line);
        g.drawLine (r.getX(), r.getBottom(), r.getRight(), r.getY(), 1.0f);

        const float driveLin = juce::Decibels::decibelsToGain (drive);
        const float comp = dsp::SaturationModule::compensationFor (driveLin);
        const float m = mix * 0.01f;

        juce::Path p;
        const int n = 96;
        for (int i = 0; i <= n; ++i)
        {
            const float x = -1.0f + 2.0f * (float) i / n;
            const float wet = dsp::SaturationModule::shape (x, driveLin) * comp;
            const float y = juce::jlimit (-1.2f, 1.2f, x + (wet - x) * m);
            const float px = juce::jmap (x, -1.0f, 1.0f, r.getX(), r.getRight());
            const float py = juce::jmap (y, -1.2f, 1.2f, r.getBottom(), r.getY());
            if (i == 0)
                p.startNewSubPath (px, py);
            else
                p.lineTo (px, py);
        }

        g.setColour (active ? colours::sat : colours::textDim);
        g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}
