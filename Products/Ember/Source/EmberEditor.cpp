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
    void EmberLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& background, bool highlighted, bool down)
    {
        if (! (bool) b.getProperties().getWithDefault ("fire", false))
        {
            VocalypseLookAndFeel::drawButtonBackground (g, b, background, highlighted, down);
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
        scene = s;
        simple = simpleView;
        knob = knobArea;
        plates = std::move (plateAreas);
        if (++ticks % 2 == 0)   // the border and the knob's seal move slowly: 15 frames a second are enough
        {
            repaintEdges (getLocalBounds().toFloat().reduced (6.0f), 7.0f);
            if (simple && ! knob.isEmpty())
                repaint (knob.expanded (4.0f).getSmallestIntegerContainer());
        }
        if (surge)
            for (auto& r : plates)
                repaintEdges (r, 8.0f);
    }

    void SealOverlay::paint (juce::Graphics& g)
    {
        const float scale = juce::jlimit (1.0f, 3.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        borderRunes.setScale (scale);
        sealRunes.setScale (scale);
        seal::drawRuneBorder (g, borderRunes, getLocalBounds().toFloat(), scene);
        if (simple && ! knob.isEmpty())
            seal::drawMiniSeal (g, sealRunes, knob.getCentre(), knob.getWidth() * 0.5f, scene);
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
        value = db;
        if (value >= peakHold)
        {
            peakHold = value;
            holdLeft = 1.0f;
        }
        else if ((holdLeft -= dt) <= 0.0f)
            peakHold = std::max (value, peakHold - 4.5f * dt);
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
        for (float db : { 0.0f, -20.0f, -40.0f, -60.0f })
            g.drawText (db < 0.0f ? utf8 ("−") + juce::String ((int) -db) : juce::String ("0"),
                        juce::Rectangle<float> (b.getX() - 46.0f, yFor (db) - 6.0f, 24.0f, 12.0f), juce::Justification::centredRight);
        g.drawText ("dB", juce::Rectangle<float> (b.getX() - 46.0f, yFor (0.0f) + 7.0f, 24.0f, 12.0f), juce::Justification::centredRight);   // the scale's unit
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

    bool BandDisplay::nearHandle (juce::Point<float> p) const
    {
        return std::abs (p.x - xForHz (processor.state.getRawParameterValue (pid::freq)->load())) <= 10.0f;
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
            // Split: the bell around the S (strongest at its centre, the air above fades back out)
            const float xc = xForHz (hz * splitCenterRatio), xr = std::min (r.getRight(), xForHz (hz * splitCenterRatio * 2.2f));
            auto band = r.withLeft (x).withRight (std::max (x + 1.0f, xr));
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
        g.setColour (look::accent.withAlpha (dragging ? 1.0f : 0.85f));
        g.fillRect (x - 1.0f, r.getY(), 2.0f, r.getHeight());
        g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ x, r.getY() + 14.0f }));

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

        g.setFont (ui::monoFont (11.0f));
        g.setColour (c::textStrong);
        g.drawText (hzText (hz), juce::Rectangle<float> (x + 10.0f, r.getY() + 6.0f, 90.0f, 16.0f), juce::Justification::centredLeft);
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
            g.setFont (ui::font (13.0f));
            g.setColour (c::textMuted);
            g.drawFittedText (tr ("Spiel Audio ab – hier siehst du die Höhen deiner Stimme und den S-Bereich.",
                                  "Play audio – this shows the highs of your voice and the S range."),
                              r.reduced (40.0f, 0.0f).withSizeKeepingCentre (r.getWidth() - 80.0f, 40.0f).toNearestInt(),
                              juce::Justification::centred, 2);
        }
    }

    void BandDisplay::mouseMove (const juce::MouseEvent& e)
    {
        setMouseCursor (nearHandle (e.position) ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    }

    void BandDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
    {
        // like the knobs: a step per notch (about 1/8 octave), Shift / Ctrl = fine
        if (auto* p = processor.state.getParameter (pid::freq))
        {
            const float octaves = (e.mods.isShiftDown() || e.mods.isCtrlDown() ? 0.2f : 1.0f) * (w.isReversed ? -w.deltaY : w.deltaY);
            ui::gestures::setReal (*p, juce::jlimit (2000.0f, 16000.0f, p->convertFrom0to1 (p->getValue()) * std::pow (2.0f, octaves)));
            repaint();
        }
    }

    void BandDisplay::mouseDown (const juce::MouseEvent& e)
    {
        // only the pink edge is grabbed, and it moves with the mouse from where it is (no jump to the click)
        if (auto* p = processor.state.getParameter (pid::freq); p != nullptr && nearHandle (e.position))
        {
            dragging = true;
            lastDragX = e.position.x;
            dragHz = p->convertFrom0to1 (p->getValue());
            p->beginChangeGesture();
            repaint();
        }
    }

    void BandDisplay::mouseDrag (const juce::MouseEvent& e)
    {
        if (auto* p = processor.state.getParameter (pid::freq); p != nullptr && dragging)
        {
            const float fine = e.mods.isShiftDown() || e.mods.isCtrlDown() ? 0.2f : 1.0f;
            dragHz = juce::jlimit (2000.0f, 16000.0f, hzForX (xForHz (dragHz) + (e.position.x - lastDragX) * fine));
            lastDragX = e.position.x;
            p->setValueNotifyingHost (p->convertTo0to1 (dragHz));
            repaint();
        }
    }

    void BandDisplay::mouseUp (const juce::MouseEvent&)
    {
        if (auto* p = processor.state.getParameter (pid::freq); p != nullptr && dragging)
            p->endChangeGesture();
        dragging = false;
    }

    void BandDisplay::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (auto* p = processor.state.getParameter (pid::freq); p != nullptr && nearHandle (e.position))
            ui::gestures::resetToDefault (*p);
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

    // ================================================================ panel
    EmberPanel::EmberPanel (EmberAudioProcessor& p, UserSettings& s, std::function<void (juce::String)> lang)
        : processor (p), settings (s), onLanguage (std::move (lang)),
          spectrumSource (p.engine.inputTap, p.engine.outputTap),
          display (p, spectrumSource),
          detector (p),
          amountBig (p.state, pid::amount, "Amount", look::accent),
          freqK (p.state, pid::freq, tr ("Frequenz", "Frequency"), look::accent),
          threshK (p.state, pid::threshold, "Threshold", look::accent),
          rangeK (p.state, pid::range, "Range", look::accent),
          amountK (p.state, pid::amount, "Amount", look::accent),
          attackK (p.state, pid::attack, "Attack", look::accent),
          releaseK (p.state, pid::release, "Release", look::accent),
          lookK (p.state, pid::lookahead, "Lookahead", look::accent),
          mixK (p.state, pid::mix, "Mix", look::accent),
          outK (p.state, pid::output, "Output", look::accent),
          modeSeg (p.state, pid::mode, { "Split", tr ("Breitband", "Wideband") }, look::accent),
          detectSeg (p.state, pid::detection, { "Smart", tr ("Relativ", "Relative"), tr ("Absolut", "Absolute") }, look::accent),
          listenSeg (p.state, pid::listen, { tr ("Aus", "Off"), tr ("S-Bereich", "S range"), "Delta" }, look::accent)
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
        prevBtn.onClick = [this] { processor.stepPreset (-1); };
        nextBtn.onClick = [this] { processor.stepPreset (1); };
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
        for (auto* k : { &freqK, &threshK, &rangeK, &amountK, &attackK, &releaseK, &lookK, &mixK, &outK })
        {
            k->setDialSize (ui::dialsize::secondary);
            addChildComponent (*k);
        }
        styleLabel (modeLbl, tr ("Modus", "Mode"));
        styleLabel (detectLbl, tr ("Erkennung", "Detection"));
        styleLabel (listenLbl, tr ("Abhören", "Listen"));
        for (juce::Component* comp : { (juce::Component*) &modeSeg, (juce::Component*) &detectSeg, (juce::Component*) &listenSeg,
                                       (juce::Component*) &modeLbl, (juce::Component*) &detectLbl, (juce::Component*) &listenLbl })
            addChildComponent (*comp);

        // ---- explanations (info card on hover)
        const auto amountText = tr ("Wie stark die S-Laute reduziert werden. 50 % = genau das, was Threshold und Range (oder AUTO) vorgeben, "
                                    "100 % = anderthalbmal so viel, 0 % = aus.",
                                    "How hard the S sounds are reduced. 50 % = exactly what Threshold and Range (or AUTO) set, "
                                    "100 % = one and a half times as much, 0 % = off.");
        amountBig.setDescription (amountText);
        amountK.setDescription (amountText);
        freqK.setDescription (tr ("Ab dieser Frequenz sucht der De-Esser nach S-Lauten und senkt den S-Bereich knapp darüber ab. "
                                  "Tiefe Stimmen um 5 kHz, hohe um 7–7,5 kHz. Auch im Spektrum mit der Maus ziehbar.",
                                  "From this frequency up the de-esser looks for S sounds and lowers the S range just above it. "
                                  "Low voices around 5 kHz, high voices around 7–7.5 kHz. Also draggable in the spectrum."));
        threshK.setDescription (tr ("Ab diesem S-Pegel greift der De-Esser ein. Tiefer = mehr S-Laute werden erfasst. "
                                    "Auch an der S-Pegel-Anzeige rechts ziehbar.",
                                    "The S level above which the de-esser steps in. Lower = more S sounds are caught. "
                                    "Also draggable on the S level meter on the right."));
        rangeK.setDescription (tr ("So viel nimmt der De-Esser höchstens weg. 4–8 dB klingen natürlich, deutlich mehr kann lispeln lassen.",
                                   "The most the de-esser ever removes. 4–8 dB sound natural, much more can make the voice lisp."));
        attackK.setDescription (tr ("Wie schnell der De-Esser zupackt, wenn ein S beginnt.", "How fast the de-esser grabs when an S starts."));
        releaseK.setDescription (tr ("Wie schnell er nach dem S wieder loslässt. Kurz (30–40 ms) lässt den nächsten Vokal unberührt.",
                                     "How fast it lets go after the S. Short (30–40 ms) leaves the next vowel untouched."));
        lookK.setDescription (tr ("So viel früher sieht der De-Esser das S kommen und erfasst auch dessen Anfang. "
                                  "Die Latenz bleibt immer bei 5 ms.",
                                  "The de-esser sees the S coming this much earlier and also catches its start. "
                                  "The latency always stays at 5 ms."));
        mixK.setDescription (tr ("Anteil des bearbeiteten Signals. Unter 100 % bleibt etwas vom Original-S erhalten.",
                                 "Share of the processed signal. Below 100 % some of the original S remains."));
        outK.setDescription (tr ("Ausgangspegel nach dem De-Esser.", "Output level after the de-esser."));
        modeSeg.setInfo (tr ("Modus", "Mode"),
                         tr ("Split: nur der S-Bereich wird leiser, die Stimme darunter und die Luft darüber bleiben. "
                             "Breitband: die ganze Stimme taucht beim S kurz ab (natürlicher bei Sprache).",
                             "Split: only the S range gets quieter, the voice below and the air above stay. "
                             "Wideband: the whole voice dips briefly on each S (more natural for speech)."));
        detectSeg.setInfo (tr ("Erkennung", "Detection"),
                           tr ("Smart (empfohlen): reagiert nur, wenn wirklich ein S-Laut klingt. Helle Vokale, Luft und Atem "
                               "bleiben unberührt. Relativ: misst das S im Verhältnis zur Lautstärke der Stimme, leise und laute "
                               "Stellen gleich. Absolut: fester Pegel wie ein klassischer De-Esser.",
                               "Smart (recommended): reacts only when a real S sound is there. Bright vowels, air and breaths "
                               "stay untouched. Relative: measures the S against the loudness of the voice, quiet and loud "
                               "parts alike. Absolute: fixed level like a classic de-esser."));
        listenSeg.setInfo (tr ("Abhören", "Listen"),
                           tr ("S-Bereich: nur den Bereich hören, in dem nach S-Lauten gesucht wird (zum Einstellen der Frequenz). "
                               "Delta: nur das hören, was entfernt wird. Es sollten nur S-Laute zu hören sein. "
                               "Abhören wird nie gespeichert.",
                               "S range: hear only the range searched for S sounds (to set the frequency). "
                               "Delta: hear only what is removed. You should hear nothing but S sounds. "
                               "Listening is never saved."));

        addInfo (simpleBtn, "SIMPLE", tr ("Die einfache Ansicht: Ember, Amount, Stimmlage und Delta. Reicht für die meisten Stimmen.",
                                          "The simple view: Ember, Amount, voice and Delta. Enough for most vocals."));
        addInfo (advancedBtn, "ADVANCED", tr ("Alle Regler, das Spektrum mit dem S-Bereich und die S-Pegel-Anzeige mit dem Threshold.",
                                              "Every control, the spectrum with the S range and the S level meter with the threshold."));
        addInfo (autoBtn, "AUTO", tr ("Spiel deinen Gesang ab und klick auf AUTO. Ember hört 12 Sekunden Gesang zu (Pausen zählen nicht) und ist dann von selbst fertig, "
                                      "auch wenn du nach mindestens 3 Sekunden die Wiedergabe stoppst. Er stellt nur Frequenz, Threshold, Range und Release ein, alles andere bleibt. "
                                      "Deine Einstellung von vorher bleibt auf dem Slot, auf dem du warst (meist A), das Ergebnis kommt auf den anderen. Liegt dort schon eine "
                                      "andere Einstellung von dir, bleibt sie, und Strg+Z holt die vorherige zurück. Während er zuhört, bricht ein Klick ab.",
                                      "Play your vocal and click AUTO. Ember listens to 12 seconds of vocal (pauses don't count) and then finishes on his own, "
                                      "also when you stop playback after at least 3 seconds. He sets only frequency, threshold, range and release, everything else stays. "
                                      "Your previous setting stays on the slot you were on (usually A), the result goes to the other one. If that slot already holds "
                                      "a different setting of yours, it stays, and Ctrl+Z brings back the previous one. While he listens, a click cancels."));
        addInfo (bypassBtn, "BYPASS", tr ("Schaltet den De-Esser aus, um mit dem Original zu vergleichen. Die Latenz bleibt gleich.",
                                          "Switches the de-esser off to compare with the original. The latency stays the same."));
        addInfo (menuBtn, tr ("Menü", "Menu"), tr ("Rückgängig / Wiederholen, Fenstergröße, Sprache, Handbuch, Lizenz und Infos zu EMBER.",
                                                   "Undo / redo, window size, language, manual, licence and about EMBER."));
        addInfo (prevBtn, tr ("Voriges Preset", "Previous preset"), tr ("Lädt das vorige Preset.", "Loads the previous preset."));
        addInfo (nextBtn, tr ("Nächstes Preset", "Next preset"), tr ("Lädt das nächste Preset.", "Loads the next preset."));
        addInfo (presetBtn, "Preset", tr ("Werks-Presets und deine eigenen Presets (dort auch umbenennen und löschen). Ein * zeigt, dass du seit dem Laden etwas geändert hast.",
                                          "Factory presets and your own presets (rename and delete them there too). A * shows you changed something since loading."));
        addInfo (saveBtn, tr ("Preset speichern", "Save preset"), tr ("Speichert die aktuelle Einstellung als eigenes Preset.",
                                                                      "Saves the current setting as your own preset."));
        addInfo (slotA, tr ("Vergleich A", "Compare A"), tr ("Zwei Einstellungen vergleichen: A und B merken sich je eine Einstellung. "
                                                             "Umschalten, hören, die bessere behalten.",
                                                             "Compare two settings: A and B each keep one setting. Switch, listen, keep the better one."));
        addInfo (slotB, tr ("Vergleich B", "Compare B"), tr ("Beim ersten Klick startet B als Kopie von A.", "On the first click B starts as a copy of A."));
        addInfo (slotCopy, tr ("Kopieren", "Copy"), tr ("Kopiert die aktive Einstellung in den anderen Speicher: A → B, wenn A aktiv ist, sonst B → A. "
                                                        "A und B werden mit dem Projekt gespeichert.",
                                                        "Copies the active setting into the other slot: A → B while A is active, otherwise B → A. "
                                                        "A and B are saved with the project."));
        addInfo (mascot, "Ember", tr ("Ember zeigt, was der De-Esser tut: Er tanzt zur Stimme, und jedes S, das abgesenkt wird, fliegt ihm zu und er frisst es. Klick auf ihn startet AUTO.",
                                      "Ember shows what the de-esser does: he dances to the vocal, and every S that is reduced flies to him and he eats it. Click him to start AUTO."));
        for (auto* m : { &meter, &advancedMeter })
            addInfo (*m, tr ("Absenkung", "Reduction"), tr ("So viel leiser werden die S-Laute gerade, in dB (Gain Reduction).",
                                                            "How much quieter the S sounds are right now, in dB (gain reduction)."));
        addInfo (voiceLow, tr ("Tiefe Stimme", "Low voice"), tr ("S-Bereich ab 5 kHz, greift etwas früher: für tiefe und männliche Stimmen.", "S range from 5 kHz, steps in a little earlier: for low and male voices."));
        addInfo (voiceMid, tr ("Mittlere Stimme", "Mid voice"), tr ("S-Bereich ab 6,2 kHz, passt für die meisten Stimmen.", "S range from 6.2 kHz, fits most voices."));
        addInfo (voiceHigh, tr ("Hohe Stimme", "High voice"), tr ("S-Bereich ab 7,4 kHz, für hohe und weibliche Stimmen.", "S range from 7.4 kHz, for high and female voices."));
        addInfo (listenBtn, "Delta", tr ("Nur das hören, was entfernt wird. Hörst du Vokale, ist es zu viel.",
                                         "Hear only what is removed. If you hear vowels, it is too much."));
        addInfo (display, tr ("Spektrum", "Spectrum"), tr ("Pink markiert: der S-Bereich, der bei S-Lauten leiser wird. Die pinke Kante ziehen = Frequenz "
                                                           "(Shift = fein, Mausrad = Schritt). Oben: wie viel in den letzten Sekunden weggenommen wurde.",
                                                           "Pink: the S range that gets quieter on S sounds. Drag the pink edge = frequency "
                                                           "(Shift = fine, wheel = step). Top: how much was removed over the last seconds."));
        addInfo (detector, tr ("S-Pegel", "S level"), tr ("Wie laut die S-Laute gerade sind, in dB. Über der pinken Linie (Threshold) wird reduziert. "
                                                          "Grau gestreift = Smart erkennt keinen S-Laut. Linie ziehen = Threshold, Mausrad = Schritt, Shift = fein.",
                                                          "How loud the S sounds are right now, in dB. Above the pink line (threshold) they are reduced. "
                                                          "Grey stripes = Smart hears no S sound. Drag the line = threshold, wheel = step, Shift = fine."));

        addAndMakeVisible (sealOverlay);
        addChildComponent (infoPopup);
        about = std::make_unique<AboutOverlay>();
        about->setWantsKeyboardFocus (true);
        addChildComponent (*about);
        addChildComponent (licenceLock);
        licenceLock.setLicence (&processor.getLicence());

        setWantsKeyboardFocus (true);   // Ctrl+Z / Ctrl+Shift+Z
        setSize (look::baseW, look::baseH);
        advanced = (bool) processor.state.state.getProperty ("advanced", false);
        setAdvanced (advanced);
        startTimerHz (30);
    }

    EmberPanel::~EmberPanel()
    {
        stopTimer();
    }

    void EmberPanel::addInfo (juce::Component& comp, const juce::String& title, const juce::String& text)
    {
        infos.push_back (std::make_unique<ui::HoverInfo> (comp, [title] { return title; }, nullptr, [text] { return text; }));
    }

    void EmberPanel::showInfo (juce::Component& anchor, const juce::String& title, const juce::String& value, const juce::String& text)
    {
        infoPopup.toFront (false);
        infoPopup.show (getLocalArea (&anchor, anchor.getLocalBounds()), title, value, text);
    }

    void EmberPanel::hideInfo (juce::Component&)
    {
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
        if (chamber.getWidth() != cw || chamber.getHeight() != ch)
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
        for (juce::Component* comp : { (juce::Component*) &display, (juce::Component*) &detector, (juce::Component*) &advancedMeter, (juce::Component*) &freqK,
                                       (juce::Component*) &threshK, (juce::Component*) &rangeK, (juce::Component*) &amountK,
                                       (juce::Component*) &attackK, (juce::Component*) &releaseK, (juce::Component*) &lookK,
                                       (juce::Component*) &mixK, (juce::Component*) &outK, (juce::Component*) &modeSeg,
                                       (juce::Component*) &detectSeg, (juce::Component*) &listenSeg, (juce::Component*) &modeLbl,
                                       (juce::Component*) &detectLbl, (juce::Component*) &listenLbl })
            comp->setVisible (a);
        infoPopup.hide();
        layout();
        refreshButtons();
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
            int x = 28;
            for (auto* k : { &freqK, &threshK, &rangeK, &amountK, &attackK, &releaseK, &lookK, &mixK, &outK })
            {
                k->setBounds (R (x, 352, 94, 108));
                x += 94;
            }
            modeLbl.setBounds (R (40, 466, 120, 18));
            modeSeg.setBounds (R (40, 484, 200, 30));
            detectLbl.setBounds (R (284, 466, 120, 18));
            detectSeg.setBounds (R (284, 484, 270, 30));
            listenLbl.setBounds (R (598, 466, 120, 18));
            listenSeg.setBounds (R (598, 484, 262, 30));
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

    void EmberPanel::showPresetMenu()
    {
        juce::PopupMenu menu;
        menu.addSectionHeader (tr ("Werks-Presets", "Factory presets"));
        const auto& factory = factoryPresets();
        for (int i = 0; i < (int) factory.size(); ++i)
            menu.addItem (1 + i, presetName (factory[(size_t) i]), true, processor.getFactoryIndex() == i);
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
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetBtn),
                            [safe = juce::Component::SafePointer<EmberPanel> (this), users] (int id)
                            {
                                if (safe == nullptr || id == 0)
                                    return;
                                auto& self = *safe;
                                if (id >= 1 && id < 100)
                                    self.processor.loadFactoryPreset (id - 1);
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

    void EmberPanel::showMainMenu()
    {
        juce::PopupMenu lang;
        const auto current = settings.getLanguage();
        lang.addItem (11, tr ("Wie das System", "Same as the system"), true, current == "system");
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
        menu.addItem (20, tr ("Handbuch öffnen", "Open the manual"));
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
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (menuBtn),
                            [this, safe = juce::Component::SafePointer<EmberPanel> (this)] (int id)
                            {
                                if (safe == nullptr || id == 0)
                                    return;
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
            if (bypassed)
                text = tr ("Bypass: Der De-Esser ist aus, das Signal läuft unbearbeitet durch.", "Bypass: the de-esser is off, the signal passes through unprocessed.");
            else if (listen == 2)
                text = advanced ? tr ("Delta: Du hörst nur, was entfernt wird. Stille heißt: gerade wird nichts entfernt. Zum Ausschalten Abhören auf Aus stellen.",
                                      "Delta: you hear only what is removed. Silence means nothing is being removed. Set Listen to Off to switch it off.")
                                : tr ("Delta: Du hörst nur, was entfernt wird. Stille heißt: gerade wird nichts entfernt. Zum Ausschalten nochmal auf DELTA klicken.",
                                      "Delta: you hear only what is removed. Silence means nothing is being removed. Click DELTA again to switch it off.");
            else if (listen == 1)
                text = tr ("S-Bereich: Du hörst nur den Bereich, in dem nach S-Lauten gesucht wird. Zum Ausschalten Abhören auf Aus stellen.",
                           "S range: you hear only the range searched for S sounds. Set Listen to Off to switch it off.");
            else if (advanced)
                text = tr ("Fahr mit der Maus über einen Regler, um zu sehen, was er macht. Doppelklick setzt ihn zurück.",
                           "Hover over a control to see what it does. Double-click resets it.");
            else
                text = tr ("Spiel deinen Gesang ab und klick auf Ember oder AUTO: Er stellt den De-Esser auf deine Stimme ein.",
                           "Play your vocal and click Ember or AUTO: he tunes the de-esser to your voice.");
        }
        if (status.getText() != text)
            status.setText (text, juce::dontSendNotification);

        meter.setValue (shownGr);
        {
            const double nowS = juce::Time::getMillisecondCounterHiRes() * 0.001;
            sealClock += lastTickS > 0.0 ? juce::jmin (0.1, nowS - lastTickS) : 0.0;
            lastTickS = nowS;
            seal::Scene sc;
            sc.t = sealClock;
            sc.sinceS = advanced ? 99.0f : mascot.secondsSinceSwallow();
            sc.heat = shownGr / 9.0f;
            sc.on = ! bypassed;
            // the small seal sits on the Amount knob's cap
            const auto kb = amountBig.getBounds().toFloat();
            const auto knobArea = juce::Rectangle<float> (92.0f, 92.0f).withCentre ({ kb.getCentreX(), kb.getY() + 90.0f });
            sealOverlay.update (sc, ! advanced, knobArea, plates());
        }
        if (advanced)
        {
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
