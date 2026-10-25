#include "TimePanels.h"
#include "../PluginProcessor.h"
#include "HelpContent.h"

namespace velvox::ui
{
    using dsp::ModuleId;

    namespace
    {
        juce::String de (const char* utf8) { return juce::String::fromUTF8 (utf8); }

        // Live pictures repaint at 30 Hz (the editor frame runs at 60)
        bool liveFrame (int& counter) noexcept { return (++counter & 1) == 0; }

        void setReal (juce::RangedAudioParameter* p, float value)
        {
            if (p != nullptr)
                p->setValueNotifyingHost (p->convertTo0to1 (value));
        }

        juce::String msText (float ms)
        {
            return ms >= 1000.0f ? juce::String (ms / 1000.0f, 2) + " s" : juce::String (juce::roundToInt (ms)) + " ms";
        }
    }

    //==========================================================================
    // DELAY: echo map

    EchoMapView::EchoMapView (VelvoxAudioProcessor& p, int k) : proc (p), instance (k)
    {
        tempoSource = [this]
        {
            auto& pulse = proc.getChain().getPulse();
            return pulse.displayHostTempo.load() ? (double) pulse.displayBpm.load() : 0.0;
        };
        info = std::make_unique<HoverInfo> (*this, [] { return juce::String ("Echo"); },
                                            [this] { return readout(); },
                                            [] { return de ("Ziehen: links/rechts = Zeit, hoch/runter = Feedback."); });
    }

    EchoMapView::~EchoMapView() { endDrag(); }

    juce::String EchoMapView::idFor (const char* base) const
    {
        if (instance > 0)
        {
            const auto id = dsp::instanceParamId (base, instance);
            if (proc.getState().getParameter (id) != nullptr)
                return id;
        }
        return base;
    }

    juce::RangedAudioParameter* EchoMapView::param (const char* base) const
    {
        return proc.getState().getParameter (idFor (base));
    }

    float EchoMapView::heightForLevel (float linear) noexcept
    {
        if (! (linear > 0.0f))
            return 0.0f;
        return juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (linear, -200.0f) - floorDb) / -floorDb);
    }

    void EchoMapView::update (const LiveData& d)
    {
        const auto& cs = d.settings;
        settings = cs.delay;
        on = cs.delayOn;
        synced = cs.delaySync;
        division = cs.delayDivision;
        timeMs = cs.delayTimeMs;
        sampleRate = d.sampleRate > 0.0 ? d.sampleRate : 48000.0;
        hostBpm = tempoSource ? tempoSource() : 0.0;
        time = d.time;
        duckGain = juce::Decibels::decibelsToGain (-d.delayDuck.ago (0));

        // The delay runs on the note value only while the host sends a tempo
        hostSynced = false;
        effectiveMs = timeMs;
        if (synced)
            if (const float t = dsp::DelayModule::timeForTempo (hostBpm, division); t > 0.0f)
            {
                effectiveMs = t;
                hostSynced = true;
            }
        // The read-out follows the gliding time of the running delay
        shownMs = on ? proc.getChain().getDelay (instance).displayTimeMs.load() : effectiveMs;
        if (! (shownMs > 0.0f))
            shownMs = effectiveMs;

        recompute();

        // Echo n lights up while the vocal of n echo times ago was loud
        constexpr float frameMs = 1000.0f / 60.0f;
        for (int i = 0; i < numTaps; ++i)
        {
            const int ago = juce::jlimit (0, LiveData::historySize - 1, juce::roundToInt (taps[(size_t) i].timeMs / frameMs));
            const float level = juce::jlimit (0.0f, 1.0f, juce::jmap (d.inputDb.ago (ago), -50.0f, -12.0f, 0.0f, 1.0f));
            lit[(size_t) i] = on ? std::max (level, lit[(size_t) i] * 0.85f) : 0.0f;
        }

        auto text = readout();
        if (text != readoutText)
            readoutText = std::move (text);
        if (liveFrame (frameCount) || dragged >= 0)
            repaint();
    }

    void EchoMapView::recompute()
    {
        auto s = settings;
        s.timeMs = effectiveMs;
        numTaps = dsp::DelayModule::echoTaps (s, sampleRate, taps.data(), maxTaps, floorDb);

        if (dragged >= 0)   // the window stays put while an echo is dragged
            return;
        const float w = juce::jlimit (600.0f, 8000.0f, (effectiveMs + settings.spreadMs) * 6.0f);
        if (std::abs (w - windowMs) <= 0.5f && numAxis > 0)
            return;
        windowMs = w;
        // Time axis: about six labels
        const float steps[] { 50.0f, 100.0f, 250.0f, 500.0f, 1000.0f, 2000.0f };
        float step = steps[5];
        for (float st : steps)
            if (windowMs / st <= 7.0f) { step = st; break; }
        numAxis = 0;
        for (float t = step; t < windowMs * 0.98f && numAxis < (int) axisMs.size(); t += step)
        {
            axisMs[(size_t) numAxis] = t;
            axisText[(size_t) numAxis] = msText (t);
            ++numAxis;
        }
    }

    void EchoMapView::resized()
    {
        plot = getLocalBounds().toFloat().reduced (24.0f, 14.0f).withTrimmedTop (18.0f).withTrimmedBottom (16.0f);
    }

    float EchoMapView::xForMs (float ms) const noexcept
    {
        // 0 ms (the dry voice) a little in from the left edge
        const float x0 = plot.getX() + 14.0f;
        return x0 + (plot.getRight() - 8.0f - x0) * ms / windowMs;
    }

    float EchoMapView::getRepeatRatio() const noexcept
    {
        if (numTaps < 2)
            return 0.0f;
        const float a = std::max (taps[0].levelL, taps[0].levelR), b = std::max (taps[1].levelL, taps[1].levelR);
        return a > 0.0f ? b / a : 0.0f;
    }

    juce::Rectangle<float> EchoMapView::getBarBounds (int i, bool left) const noexcept
    {
        if (i < 0 || i >= numTaps)
            return {};
        const auto& t = taps[(size_t) i];
        const float h = heightForLevel (left ? t.levelL : t.levelR) * plot.getHeight() * 0.5f * duckGain;
        if (h <= 0.0f || t.timeMs > windowMs)
            return {};
        const float x = xForMs (t.timeMs), cy = plot.getCentreY();
        return left ? juce::Rectangle<float> (x - 4.0f, cy - h, 8.0f, h) : juce::Rectangle<float> (x - 4.0f, cy, 8.0f, h);
    }

    int EchoMapView::tapAt (juce::Point<float> p) const noexcept
    {
        if (! plot.expanded (6.0f).contains (p))
            return -1;
        int best = -1;
        float bestD = 12.0f;
        for (int i = 0; i < numTaps; ++i)
        {
            if (taps[(size_t) i].timeMs > windowMs)
                break;
            const float d = std::abs (xForMs (taps[(size_t) i].timeMs) - p.x);
            if (d < bestD) { bestD = d; best = i; }
        }
        return best;
    }

    juce::String EchoMapView::readout() const
    {
        juce::String t;
        if (hostSynced)
            t << delaydiv::names()[juce::jlimit (0, delaydiv::names().size() - 1, division)] << "   ";
        t << msText (shownMs);
        if (auto* fb = param (pid::delayFeedback))
            t << "   FB " << juce::roundToInt (fb->convertFrom0to1 (fb->getValue())) << " %";
        return t;
    }

    void EchoMapView::mouseMove (const juce::MouseEvent& e)
    {
        const int h = tapAt (e.position);
        if (h != hovered)
        {
            hovered = h;
            repaint();
        }
        setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    }

    void EchoMapView::mouseExit (const juce::MouseEvent&)
    {
        if (hovered >= 0)
        {
            hovered = -1;
            repaint();
        }
    }

    void EchoMapView::mouseDown (const juce::MouseEvent& e)
    {
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu menu;
            juce::Component::SafePointer<EchoMapView> safe (this);
            menu.addItem (de ("Zeit und Feedback zurücksetzen"), [safe] { if (safe != nullptr) safe->resetEcho(); });
            if (auto* host = findParentComponentOfClass<gestures::PanelMenuHost>())
            {
                menu.addSeparator();
                host->addPanelItems (menu);
            }
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (200));
            return;
        }
        const int k = tapAt (e.position);
        if (k < 0)
            return;
        if (e.mods.isAltDown())
        {
            resetEcho();
            return;
        }

        endDrag();
        dragged = k;
        // Echo n sits at about n x time and follows the pointer: the time moves by 1/n of the pointer
        dragK = std::max (1, juce::roundToInt (taps[(size_t) k].timeMs / std::max (1.0f, effectiveMs)));
        dragStartTime = effectiveMs;
        dragStartTapMs = taps[(size_t) k].timeMs;
        dragStartRatio = getRepeatRatio();
        dragDx = dragDy = 0.0f;
        lastPos = e.position;
        dragFree = ! hostSynced;
        freeByShift = false;

        for (auto* id : { pid::delayTime, pid::delayDivision, pid::delaySync, pid::delayFeedback })
            if (auto* p = param (id))
            {
                p->beginChangeGesture();
                gestureParams.push_back (p);
            }
        repaint();
    }

    void EchoMapView::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragged < 0)
            return;

        // On the note grid, Shift leaves it: free milliseconds from here on (and Shift then is not "fine")
        if (! dragFree && e.mods.isShiftDown())
        {
            dragFree = true;
            freeByShift = true;
            setReal (param (pid::delaySync), 0.0f);
        }
        const bool fine = e.mods.isCtrlDown() || e.mods.isCommandDown() || (e.mods.isShiftDown() && ! freeByShift);
        const float scale = fine ? 0.1f : 1.0f;
        dragDx += (e.position.x - lastPos.x) * scale;
        dragDy += (e.position.y - lastPos.y) * scale;
        lastPos = e.position;

        // Time: the grabbed echo follows the pointer
        const float msPerPixel = windowMs / std::max (1.0f, xForMs (windowMs) - xForMs (0.0f));
        const float newTime = dragStartTime + dragDx * msPerPixel / (float) dragK;
        if (dragFree)
        {
            setReal (param (pid::delayTime), juce::jlimit (1.0f, 2000.0f, newTime));
        }
        else
        {
            int best = division;
            float bestD = 1.0e9f;
            for (int i = 0; i < delaydiv::names().size(); ++i)
            {
                const float t = dsp::DelayModule::timeForTempo (hostBpm, i);
                const float d = std::abs (std::log (std::max (1.0f, t) / std::max (1.0f, newTime)));
                if (t > 0.0f && d < bestD) { bestD = d; best = i; }
            }
            setReal (param (pid::delayDivision), (float) best);
        }

        // Feedback: the height ratio from one repeat to the next follows the vertical move
        auto s = settings;
        s.timeMs = effectiveMs;
        const float ratio = std::max (0.0f, dragStartRatio - dragDy / pixelsPerRatio);
        setReal (param (pid::delayFeedback), 100.0f * dsp::DelayModule::feedbackForRatio (s, sampleRate, ratio));
        repaint();
    }

    void EchoMapView::mouseUp (const juce::MouseEvent&)
    {
        endDrag();
    }

    void EchoMapView::endDrag()
    {
        for (auto* p : gestureParams)
            p->endChangeGesture();
        gestureParams.clear();
        if (dragged >= 0)
        {
            dragged = -1;
            repaint();
        }
    }

    void EchoMapView::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (tapAt (e.position) >= 0)
        {
            endDrag();
            resetEcho();
        }
    }

    void EchoMapView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
    {
        if (auto* fb = param (pid::delayFeedback); fb != nullptr && w.deltaY != 0.0f)
        {
            const float step = (e.mods.isShiftDown() || e.mods.isCtrlDown()) ? 0.2f : 2.0f;
            gestures::setReal (*fb, juce::jlimit (0.0f, 90.0f, fb->convertFrom0to1 (fb->getValue()) + (w.deltaY > 0.0f ? step : -step)));
        }
    }

    void EchoMapView::resetEcho()
    {
        proc.getUndo().perform (de ("Echo zurücksetzen"), [this]
        {
            for (auto* id : { pid::delayTime, pid::delayFeedback, pid::delayDivision })
                if (auto* p = param (id))
                    gestures::resetToDefault (*p);
        });
    }

    void EchoMapView::paint (juce::Graphics& g)
    {
        const auto hue = moduleColour (ModuleId::delay);
        const float alpha = on ? 1.0f : 0.45f;
        const auto r = getLocalBounds().toFloat().reduced (24.0f, 12.0f);
        const float cy = plot.getCentreY();

        // Read-out (time, feedback) and the side labels
        g.setFont (monoFont (11.5f));
        g.setColour (colours::text.withAlpha (0.4f + 0.6f * alpha));
        g.drawText (readoutText, r.withHeight (14.0f), juce::Justification::topRight, false);
        g.setFont (semiBold (9.0f, 0.12f));
        g.setColour (colours::textFaint);
        g.drawText ("L", juce::Rectangle<float> (plot.getX() - 18.0f, plot.getY(), 12.0f, 12.0f), juce::Justification::centred, false);
        g.drawText ("R", juce::Rectangle<float> (plot.getX() - 18.0f, plot.getBottom() - 12.0f, 12.0f, 12.0f), juce::Justification::centred, false);

        // Grid: the time axis and the centre line
        g.setFont (medium (10.0f));
        for (int i = 0; i < numAxis; ++i)
        {
            const float x = xForMs (axisMs[(size_t) i]);
            g.setColour (colours::accent.withAlpha (0.03f));
            g.fillRect (juce::Rectangle<float> (x, plot.getY(), 1.0f, plot.getHeight()));
            g.setColour (colours::textFaint);
            g.drawText (axisText[(size_t) i], juce::Rectangle<float> (x - 30.0f, plot.getBottom() + 3.0f, 60.0f, 12.0f),
                        juce::Justification::centred, false);
        }
        g.setColour (colours::borderSoft.brighter (0.3f));
        g.fillRect (juce::Rectangle<float> (plot.getX(), cy - 0.5f, plot.getWidth(), 1.0f));

        // Playhead: loops through the window while the delay runs
        if (on)
        {
            const float sx = xForMs ((float) std::fmod (time * 1000.0, (double) windowMs));
            g.setColour (hue.withAlpha (0.2f));
            g.fillRect (juce::Rectangle<float> (sx, plot.getY(), 1.0f, plot.getHeight()));
        }

        // The dry voice at 0 ms
        const float x0 = xForMs (0.0f);
        g.setColour (juce::Colours::white.withAlpha (0.7f * alpha));
        g.fillRoundedRectangle (juce::Rectangle<float> (3.0f, plot.getHeight()).withCentre ({ x0, cy }), 1.5f);

        // Echoes: L above, R below; brighter while the vocal of one echo time ago was loud
        for (int i = 0; i < numTaps; ++i)
        {
            if (taps[(size_t) i].timeMs > windowMs)
                break;
            const bool active = i == dragged || (dragged < 0 && i == hovered);
            const float l = lit[(size_t) i];
            for (bool left : { true, false })
            {
                const auto bar = getBarBounds (i, left);
                if (bar.isEmpty())
                    continue;
                const juce::Point<float> tip (bar.getCentreX(), left ? bar.getY() + 3.0f : bar.getBottom() - 3.0f);
                if (l > 0.02f || active)
                    drawGlow (g, tip, 18.0f, (0.35f * l + (active ? 0.35f : 0.0f)) * alpha, hue);
                const auto top = hue.interpolatedWith (juce::Colours::white, 0.12f + 0.4f * l + (active ? 0.3f : 0.0f));
                g.setGradientFill (juce::ColourGradient (top.withAlpha ((0.85f + 0.15f * l) * alpha), 0.0f, left ? bar.getY() : bar.getBottom(),
                                                         hue.withAlpha (0.3f * alpha), 0.0f, cy, false));
                g.fillRoundedRectangle (bar, 2.0f);
            }
            if (active)
            {
                const auto both = getBarBounds (i, true).getUnion (getBarBounds (i, false));
                g.setColour (juce::Colours::white.withAlpha (0.75f));
                g.drawRoundedRectangle (both.expanded (3.0f), 4.0f, 1.0f);
            }
        }
    }

    //==========================================================================
    // PULSE: gain curve with the live playhead

    namespace
    {
        bool sameCurve (const dsp::PulseSettings& a, const dsp::PulseSettings& b) noexcept
        {
            return a.shape == b.shape && a.amount == b.amount && a.depth == b.depth && a.interpolation == b.interpolation
                   && a.tension == b.tension && a.x == b.x && a.y == b.y;
        }

        juce::String percent (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
    }

    PulseCurveView::PulseCurveView (VelvoxAudioProcessor& p) : proc (p)
    {
        settings.shape = -1;   // first update builds the paths
        info = std::make_unique<HoverInfo> (*this,
                                            [this] { return hovered >= 0 ? "Punkt " + juce::String (hovered + 1) : juce::String ("Pulse"); },
                                            [this] { return hoverText; },
                                            [this] { return hovered >= 0 ? de ("Ziehen: Position im Takt und Lautstärke.")
                                                                         : de ("Die Lautstärke über einen Zyklus."); });
    }

    PulseCurveView::~PulseCurveView()
    {
        for (auto* prm : gestureParams)
            prm->endChangeGesture();
    }

    void PulseCurveView::resized()
    {
        auto r = getLocalBounds().toFloat().reduced (16.0f, 12.0f);
        r.removeFromTop (18.0f);
        meter = r.removeFromRight (8.0f);
        r.removeFromRight (18.0f);
        plot = r.reduced (8.0f, 8.0f);
        pathsValid = false;
        rebuildPaths();
    }

    float PulseCurveView::yForGain (float g) const noexcept
    {
        return plot.getBottom() - plot.getHeight() * juce::jlimit (0.0f, 1.0f, g);
    }

    float PulseCurveView::drawnGainAt (float phase) const noexcept
    {
        const float pos = juce::jlimit (0.0f, 1.0f, phase) * (float) tableSize;
        const int i = std::min (tableSize - 1, (int) pos);
        const float f = pos - (float) i;
        return gainTable[(size_t) i] + (gainTable[(size_t) i + 1] - gainTable[(size_t) i]) * f;
    }

    juce::Point<float> PulseCurveView::pointPosition (int i) const noexcept
    {
        i = juce::jlimit (0, 5, i);
        return { plot.getX() + plot.getWidth() * settings.x[(size_t) i], yForGain (settings.y[(size_t) i]) };
    }

    void PulseCurveView::rebuildPaths()
    {
        if (settings.shape < 0)
            return;
        curvePath.clear();
        appliedPath.clear();
        appliedFill.clear();
        for (int i = 0; i <= tableSize; ++i)
        {
            const float ph = (float) i / (float) tableSize;
            const float evalPhase = std::min (ph, 0.99999f);
            gainTable[(size_t) i] = dsp::PulseModule::gainAt (settings, evalPhase);
            const float x = plot.getX() + plot.getWidth() * ph;
            const float c = yForGain (dsp::PulseModule::evaluateShape (settings, evalPhase));
            const float a = yForGain (gainTable[(size_t) i]);
            if (i == 0)
            {
                curvePath.startNewSubPath (x, c);
                appliedPath.startNewSubPath (x, a);
                appliedFill.startNewSubPath (x, plot.getBottom());
            }
            else
            {
                curvePath.lineTo (x, c);
                appliedPath.lineTo (x, a);
            }
            appliedFill.lineTo (x, a);
        }
        appliedFill.lineTo (plot.getRight(), plot.getBottom());
        appliedFill.closeSubPath();
        pathsValid = true;
    }

    void PulseCurveView::update (const LiveData& d)
    {
        const auto& s = d.settings.pulse;
        if (! pathsValid || ! sameCurve (s, settings))
        {
            settings = s;
            rebuildPaths();
            repaint();
        }
        settings = s;
        const auto& m = proc.getChain().getPulse();
        phase = s.enabled ? m.displayPhase.load() : -1.0f;
        gain.target = s.enabled ? m.displayGain.load() : 1.0f;
        gain.tick (0.5f);
        const float newBpm = m.displayBpm.load();
        const bool newHost = m.displayHostTempo.load();
        if (readoutText.isEmpty() || newBpm != bpm || newHost != hostTempo || s.rate != shownRate)
        {
            shownRate = s.rate;
            bpm = newBpm;
            hostTempo = newHost;
            readoutText = dsp::PulseModule::rateNames()[s.rate] + "   " + juce::String (bpm, 1) + " BPM" + (hostTempo ? "" : " manuell");
        }
        if (hovered < 0)
            hoverText = dsp::PulseModule::shapeNames()[juce::jlimit (0, 4, s.shape)] + "   Amount " + percent (s.amount);
        if (liveFrame (frameCount) || dragged >= 0)
            repaint();
    }

    int PulseCurveView::pointAt (juce::Point<float> p) const noexcept
    {
        if (! showsPoints())
            return -1;
        int best = -1;
        float bestD = 12.0f;
        for (int i = 0; i < 6; ++i)
        {
            const float d = pointPosition (i).getDistanceFrom (p);
            if (d < bestD) { bestD = d; best = i; }
        }
        return best;
    }

    void PulseCurveView::mouseMove (const juce::MouseEvent& e)
    {
        const int h = pointAt (e.position);
        if (h != hovered)
        {
            hovered = h;
            if (h >= 0)
                hoverText = percent (settings.x[(size_t) h]) + "   " + percent (settings.y[(size_t) h]);
            repaint();
        }
        setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    }

    void PulseCurveView::mouseExit (const juce::MouseEvent&)
    {
        if (hovered >= 0)
        {
            hovered = -1;
            repaint();
        }
    }

    void PulseCurveView::resetPoint (int i)
    {
        proc.getUndo().perform ("Punkt " + juce::String (i + 1) + de (" zurücksetzen"), [this, i]
        {
            for (auto* id : { pid::pulseX[i], pid::pulseY[i] })
                if (auto* prm = proc.getState().getParameter (id))
                    gestures::resetToDefault (*prm);
        });
    }

    void PulseCurveView::mouseDown (const juce::MouseEvent& e)
    {
        const int p = pointAt (e.position);
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu menu;
            juce::Component::SafePointer<PulseCurveView> safe (this);
            if (p >= 0)
                menu.addItem ("Punkt " + juce::String (p + 1) + de (" zurücksetzen"), [safe, p] { if (safe != nullptr) safe->resetPoint (p); });
            if (auto* host = findParentComponentOfClass<gestures::PanelMenuHost>())
            {
                if (p >= 0)
                    menu.addSeparator();
                host->addPanelItems (menu);
            }
            if (menu.getNumItems() > 0)
                menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (200));
            return;
        }
        if (p < 0)
            return;
        if (e.mods.isAltDown())
        {
            resetPoint (p);
            return;
        }
        dragged = p;
        dragValue = { settings.x[(size_t) p], settings.y[(size_t) p] };
        lastPos = e.position;
        for (auto* id : { pid::pulseX[p], pid::pulseY[p] })
            if (auto* prm = proc.getState().getParameter (id))
            {
                prm->beginChangeGesture();
                gestureParams.push_back (prm);
            }
    }

    void PulseCurveView::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragged < 0)
            return;
        const float scale = (e.mods.isShiftDown() || e.mods.isCtrlDown() || e.mods.isCommandDown()) ? 0.1f : 1.0f;
        dragValue.x += (e.position.x - lastPos.x) / std::max (1.0f, plot.getWidth()) * scale;
        dragValue.y -= (e.position.y - lastPos.y) / std::max (1.0f, plot.getHeight()) * scale;
        dragValue = { juce::jlimit (0.0f, 1.0f, dragValue.x), juce::jlimit (0.0f, 1.0f, dragValue.y) };
        lastPos = e.position;
        setReal (proc.getState().getParameter (pid::pulseX[dragged]), dragValue.x);
        setReal (proc.getState().getParameter (pid::pulseY[dragged]), dragValue.y);
        hoverText = percent (dragValue.x) + "   " + percent (dragValue.y);
    }

    void PulseCurveView::mouseUp (const juce::MouseEvent&)
    {
        for (auto* prm : gestureParams)
            prm->endChangeGesture();
        gestureParams.clear();
        dragged = -1;
    }

    void PulseCurveView::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (const int p = pointAt (e.position); p >= 0)
        {
            mouseUp (e);
            resetPoint (p);
        }
    }

    void PulseCurveView::paint (juce::Graphics& g)
    {
        const auto hue = moduleColour (ModuleId::pulse);
        const float alpha = settings.enabled ? 1.0f : 0.45f;

        g.setFont (monoFont (11.5f));
        g.setColour (colours::text.withAlpha (0.4f + 0.6f * alpha));
        g.drawText (readoutText, getLocalBounds().toFloat().reduced (16.0f, 12.0f).withHeight (14.0f), juce::Justification::topRight, false);

        // Grid: beats (bright) and sixteenths of the cycle, quarter levels
        for (int i = 1; i < 16; ++i)
        {
            g.setColour (colours::accent.withAlpha (i % 4 == 0 ? 0.06f : 0.025f));
            g.fillRect (juce::Rectangle<float> (plot.getX() + plot.getWidth() * (float) i / 16.0f, plot.getY(), 1.0f, plot.getHeight()));
        }
        for (int i = 0; i <= 4; ++i)
        {
            g.setColour (colours::accent.withAlpha (i == 0 || i == 4 ? 0.06f : 0.03f));
            g.fillRect (juce::Rectangle<float> (plot.getX(), plot.getY() + plot.getHeight() * (float) i / 4.0f, plot.getWidth(), 1.0f));
        }

        // What is processed: the gain (filled), and the shape itself as a fine line
        g.setGradientFill (juce::ColourGradient (hue.withAlpha (0.26f * alpha), 0.0f, plot.getY(), hue.withAlpha (0.03f * alpha), 0.0f, plot.getBottom(), false));
        g.fillPath (appliedFill);
        g.setColour (colours::textMuted.withAlpha (showsPoints() ? 0.55f : 0.3f));
        g.strokePath (curvePath, juce::PathStrokeType (1.0f));
        g.setColour (hue.withAlpha (0.25f * alpha));
        g.strokePath (appliedPath, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (hue.interpolatedWith (juce::Colours::white, 0.25f).withAlpha (alpha));
        g.strokePath (appliedPath, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Playhead and the gain applied right now
        if (phase >= 0.0f)
        {
            const float x = plot.getX() + plot.getWidth() * phase;
            const float y = yForGain (gain.current);
            g.setGradientFill (juce::ColourGradient (hue.withAlpha (0.0f), x - 70.0f, 0.0f, hue.withAlpha (0.12f), x, 0.0f, false));
            g.fillRect (juce::Rectangle<float> (x - 70.0f, plot.getY(), 70.0f, plot.getHeight()).getIntersection (plot));
            g.setColour (juce::Colours::white.withAlpha (0.5f));
            g.fillRect (juce::Rectangle<float> (x, plot.getY(), 1.0f, plot.getHeight()));
            drawGlow (g, { x, y }, 14.0f, 0.7f, hue);
            g.setColour (juce::Colours::white);
            g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ x, y }));
        }

        // Custom: the six points
        if (showsPoints())
            for (int i = 0; i < 6; ++i)
            {
                const auto pt = pointPosition (i);
                const bool active = i == hovered || i == dragged;
                if (active)
                    drawGlow (g, pt, 16.0f, 0.5f, hue);
                g.setColour (colours::panelInset);
                g.fillEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre (pt));
                g.setColour ((active ? juce::Colours::white : hue.interpolatedWith (juce::Colours::white, 0.3f)).withAlpha (alpha));
                g.drawEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre (pt), 1.6f);
            }

        // Gain meter
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (meter.withTrimmedTop (8.0f).withTrimmedBottom (8.0f), 3.0f);
        auto fill = meter.withTrimmedTop (8.0f).withTrimmedBottom (8.0f);
        fill = fill.withTop (fill.getBottom() - fill.getHeight() * juce::jlimit (0.0f, 1.0f, gain.current));
        g.setColour (hue.withAlpha (alpha));
        g.fillRoundedRectangle (fill, 3.0f);
    }

    //==========================================================================
    // PULSE: shape tiles

    PulseShapeTiles::PulseShapeTiles (VelvoxAudioProcessor& p) : proc (p)
    {
        parameter = proc.getState().getParameter (pid::pulseShape);
        getProperties().set ("paramId", juce::String (pid::pulseShape));
        custom = proc.readEffectiveSettings().pulse;
        selected = parameter != nullptr ? juce::roundToInt (parameter->convertFrom0to1 (parameter->getValue())) : 0;
        juce::String description;
        if (const auto* spec = findParameterSpec (pid::pulseShape))
            description = spec->description;
        info = std::make_unique<HoverInfo> (*this, [] { return juce::String ("Kurve"); },
                                            [this] { return dsp::PulseModule::shapeNames()[juce::jlimit (0, numTiles - 1, hovered >= 0 ? hovered : selected)]; },
                                            [description] { return description; });
    }

    PulseShapeTiles::~PulseShapeTiles() = default;

    void PulseShapeTiles::resized()
    {
        auto r = getLocalBounds();
        const int gap = 8;
        const int w = (r.getWidth() - gap * (numTiles - 1)) / numTiles;
        for (int i = 0; i < numTiles; ++i)
        {
            tiles[(size_t) i] = r.removeFromLeft (w);
            r.removeFromLeft (gap);
            rebuildPath (i);
        }
    }

    void PulseShapeTiles::rebuildPath (int i)
    {
        auto s = custom;
        s.shape = i;
        const auto area = tiles[(size_t) i].toFloat().reduced (12.0f, 8.0f).withTrimmedBottom (14.0f);
        auto& path = paths[(size_t) i];
        path.clear();
        constexpr int n = 64;
        for (int k = 0; k <= n; ++k)
        {
            const float ph = std::min ((float) k / (float) n, 0.99999f);
            const float x = area.getX() + area.getWidth() * (float) k / (float) n;
            const float y = area.getBottom() - area.getHeight() * dsp::PulseModule::evaluateShape (s, ph);
            if (k == 0)
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);
        }
    }

    void PulseShapeTiles::refresh (const dsp::PulseSettings& s)
    {
        const int sel = juce::jlimit (0, numTiles - 1, s.shape);
        const bool customChanged = s.x != custom.x || s.y != custom.y || s.interpolation != custom.interpolation || s.tension != custom.tension;
        if (customChanged)
        {
            custom = s;
            rebuildPath (0);
        }
        if (sel != selected || customChanged)
        {
            selected = sel;
            repaint();
        }
    }

    int PulseShapeTiles::tileAt (juce::Point<int> p) const noexcept
    {
        for (int i = 0; i < numTiles; ++i)
            if (tiles[(size_t) i].contains (p))
                return i;
        return -1;
    }

    void PulseShapeTiles::chooseTile (int i)
    {
        if (parameter == nullptr || i < 0 || i >= numTiles)
            return;
        gestures::setReal (*parameter, (float) i);
        selected = i;
        repaint();
    }

    void PulseShapeTiles::mouseMove (const juce::MouseEvent& e)
    {
        const int h = tileAt (e.getPosition());
        if (h != hovered)
        {
            hovered = h;
            repaint();
        }
    }

    void PulseShapeTiles::mouseExit (const juce::MouseEvent&)
    {
        hovered = -1;
        repaint();
    }

    void PulseShapeTiles::mouseDown (const juce::MouseEvent& e)
    {
        if (parameter == nullptr)
            return;
        if (e.mods.isPopupMenu())
        {
            gestures::showMenu (*this, *parameter, nullptr);
            return;
        }
        if (e.mods.isAltDown())
        {
            gestures::resetToDefault (*parameter);
            return;
        }
        chooseTile (tileAt (e.getPosition()));
    }

    void PulseShapeTiles::mouseDoubleClick (const juce::MouseEvent&)
    {
        if (parameter != nullptr)
            gestures::resetToDefault (*parameter);
    }

    void PulseShapeTiles::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
    {
        const float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? -w.deltaX : w.deltaY;
        if (parameter != nullptr && std::abs (d) > 1.0e-4f)
            gestures::stepBy (*parameter, (d > 0.0f) != w.isReversed ? -1 : 1);   // up = previous (to the left)
    }

    void PulseShapeTiles::paint (juce::Graphics& g)
    {
        static const juce::StringArray upper = []
        {
            juce::StringArray a;
            for (const auto& n : dsp::PulseModule::shapeNames())
                a.add (n.toUpperCase());
            return a;
        }();
        for (int i = 0; i < numTiles; ++i)
        {
            const auto t = tiles[(size_t) i].toFloat();
            const bool sel = i == selected, hot = i == hovered;
            g.setColour (sel ? accent.withAlpha (0.16f) : colours::control);
            g.fillRoundedRectangle (t, 7.0f);
            g.setColour (sel ? accent.withAlpha (0.9f) : (hot ? colours::border.brighter (0.6f) : colours::border));
            g.drawRoundedRectangle (t.reduced (0.5f), 7.0f, sel ? 1.2f : 1.0f);
            g.setColour (sel ? accent.interpolatedWith (juce::Colours::white, 0.35f) : colours::textMuted.withAlpha (hot ? 1.0f : 0.7f));
            g.strokePath (paths[(size_t) i], juce::PathStrokeType (sel ? 1.8f : 1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setFont (semiBold (9.0f, 0.12f));
            g.setColour (sel ? colours::textStrong : colours::knobName);
            g.drawText (upper[i], t.withTrimmedTop (t.getHeight() - 17.0f), juce::Justification::centred, false);
        }
    }

    //==========================================================================
    // STUTTER: cuts on the waveform

    StutterWaveView::StutterWaveView (VelvoxAudioProcessor& p) : proc (p)
    {
        setInterceptsMouseClicks (false, false);
    }

    void StutterWaveView::resized()
    {
        auto r = getLocalBounds().toFloat().reduced (16.0f, 12.0f);
        r.removeFromTop (20.0f);
        strip = r.removeFromBottom (8.0f);
        r.removeFromBottom (10.0f);
        wave = r;
    }

    void StutterWaveView::update (const LiveData& d)
    {
        const auto& s = d.settings.chop;
        on = s.enabled;
        currentStep = on ? d.chopStep : -1;
        const auto& pats = dsp::ChopModule::patterns();
        const int pattern = juce::jlimit (0, (int) pats.size() - 1, s.pattern);
        if (pattern != shownPattern || s.rate != shownRate)
        {
            shownPattern = pattern;
            shownRate = s.rate;
            steps = pats[(size_t) pattern].steps;
            readoutText = juce::String (pats[(size_t) pattern].name) + "   "
                          + dsp::ChopModule::rateNames()[juce::jlimit (0, dsp::ChopModule::rateNames().size() - 1, s.rate)];
        }
        numFrames = proc.getChain().getChop().readDisplay (frames.data(), (int) frames.size());
        if (liveFrame (frameCount))
            repaint();
    }

    int StutterWaveView::countShadedSlices (char type) const noexcept
    {
        const int first = std::max (0, numFrames - windowFrames);
        int count = 0;
        for (int i = first; i < numFrames;)
        {
            const auto& f = frames[(size_t) i];
            int j = i;
            bool cut = false;
            while (j < numFrames && frames[(size_t) j].step == f.step && frames[(size_t) j].type == f.type)
                { cut = isMuted (frames[(size_t) j]) || cut; ++j; }
            if ((type == '.' && cut) || (type == 'r' && f.type == 'r'))
                ++count;
            i = j;
        }
        return count;
    }

    void StutterWaveView::paint (juce::Graphics& g)
    {
        const auto hue = moduleColour (ModuleId::chop);
        const float alpha = on ? 1.0f : 0.45f;
        const float cy = wave.getCentreY(), half = wave.getHeight() * 0.5f;
        const float pxPerFrame = wave.getWidth() / (float) windowFrames;
        const int first = std::max (0, numFrames - windowFrames);
        // Frame i ends at this x (the newest frame at the right edge)
        const auto xEnd = [&] (int i) { return wave.getRight() - (float) (numFrames - 1 - i) * pxPerFrame; };

        g.setFont (monoFont (11.5f));
        g.setColour (colours::text.withAlpha (0.4f + 0.6f * alpha));
        g.drawText (readoutText, getLocalBounds().toFloat().reduced (16.0f, 12.0f).withHeight (14.0f), juce::Justification::topRight, false);

        // Slices: muted ones darkened, repeated ones tinted and framed
        for (int i = first; i < numFrames;)
        {
            const auto& f = frames[(size_t) i];
            int j = i;
            bool cut = false;
            while (j < numFrames && frames[(size_t) j].step == f.step && frames[(size_t) j].type == f.type)
                { cut = isMuted (frames[(size_t) j]) || cut; ++j; }
            const float x0 = xEnd (i) - pxPerFrame, x1 = xEnd (j - 1);
            const auto slice = juce::Rectangle<float>::leftTopRightBottom (std::max (wave.getX(), x0), wave.getY(), x1, wave.getBottom());
            if (cut)
            {
                g.setColour (hue.withAlpha (0.07f * alpha));
                g.fillRect (slice);
                g.setColour (hue.withAlpha (0.35f * alpha));
                g.fillRect (slice.withHeight (2.0f));
                g.fillRect (slice.withTop (slice.getBottom() - 2.0f));
            }
            else if (f.type == 'r')
            {
                g.setColour (hue.withAlpha (0.16f * alpha));
                g.fillRect (slice);
                g.setColour (hue.withAlpha (0.7f * alpha));
                g.fillRect (slice.withWidth (1.0f));
                if (slice.getWidth() >= 16.0f)
                {
                    g.setFont (semiBold (9.0f, 0.1f));
                    g.drawText ("R", slice.withHeight (16.0f), juce::Justification::centred, false);
                }
            }
            i = j;
        }

        // Centre line, then the waveform per pixel column: the vocal in grey, what passes in the module colour
        g.setColour (colours::borderSoft.brighter (0.3f));
        g.fillRect (juce::Rectangle<float> (wave.getX(), cy - 0.5f, wave.getWidth(), 1.0f));
        if (numFrames > 0)
        {
            const int columns = (int) wave.getWidth();
            int i = first;
            for (int c = 0; c < columns; ++c)
            {
                const float xr = wave.getX() + (float) (c + 1);
                float peak = 0.0f, passed = 0.0f;
                bool repeat = false, any = false;
                while (i < numFrames && xEnd (i) <= xr)
                {
                    const auto& f = frames[(size_t) i++];
                    any = true;
                    peak = std::max (peak, f.peak);
                    passed = std::max (passed, f.peak * juce::jlimit (0.0f, 1.0f, f.gain));
                    repeat = repeat || f.type == 'r';
                }
                if (! any)
                    continue;
                const float hIn = std::sqrt (juce::jlimit (0.0f, 1.0f, peak)) * half * 0.95f;
                const float hOut = std::sqrt (juce::jlimit (0.0f, 1.0f, passed)) * half * 0.95f;
                const float x = wave.getX() + (float) c;
                g.setColour (colours::textFaint.withAlpha (0.55f * alpha));
                g.fillRect (juce::Rectangle<float> (x, cy - hIn, 1.0f, 2.0f * hIn));
                if (hOut > 0.25f)
                {
                    g.setColour ((repeat ? hue.interpolatedWith (juce::Colours::white, 0.45f) : hue.interpolatedWith (juce::Colours::white, 0.15f)).withAlpha (alpha));
                    g.fillRect (juce::Rectangle<float> (x, cy - hOut, 1.0f, 2.0f * hOut));
                }
            }
        }
        // Now
        g.setColour (juce::Colours::white.withAlpha (0.5f * alpha));
        g.fillRect (juce::Rectangle<float> (wave.getRight() - 1.0f, wave.getY(), 1.0f, wave.getHeight()));

        // The 16 steps of the pattern, the current one lit
        const int n = 16;
        const float gap = 4.0f, w = (strip.getWidth() - gap * (float) (n - 1)) / (float) n;
        for (int k = 0; k < n && steps[k] != 0; ++k)
        {
            const auto cell = juce::Rectangle<float> (strip.getX() + (float) k * (w + gap), strip.getY(), w, strip.getHeight());
            const char t = steps[k];
            const bool now = k == currentStep;
            if (t == 'x')
                g.setColour (hue.withAlpha ((now ? 0.95f : 0.45f) * alpha));
            else if (t == 'r')
                g.setColour (hue.interpolatedWith (juce::Colours::white, 0.4f).withAlpha ((now ? 0.95f : 0.3f) * alpha));
            else
                g.setColour (now ? colours::textMuted : colours::control.brighter (0.15f));
            g.fillRoundedRectangle (cell, 2.0f);
        }
    }

    //==========================================================================
    namespace
    {
        // DELAY: echo map + note value or ms (by Sync), Sync, Feedback, Mix, Ping-Pong; the rest in Mehr
        class DelayPanel final : public DetailPanel
        {
        public:
            explicit DelayPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Delay", help::shortDescription ((int) ModuleId::delay), pid::delayOn)
            {
                division = &segments (pid::delayDivision, { "1/4", "1/8D", "1/8", "1/16" }, true);
                setCaption (*division, "Note");
                syncSwitch = &toggle (pid::delaySync, "Sync");
                pingSwitch = &toggle (pid::delayPingPong, "Ping-Pong");
                segments (pid::duckSource, { "Vocal", "Sidechain" });
                time = &knob (pid::delayTime, "Time");
                feedback = &knob (pid::delayFeedback, "Feedback");
                knob (pid::delayLowCut, "Low Cut");
                knob (pid::delayHighCut, "High Cut");
                knob (pid::delayDuckAmount, "Ducking");
                knob (pid::delayDuckThreshold, "Duck Thresh");
                knob (pid::delayDuckAttack, "Duck Attack");
                knob (pid::delayDuckRelease, "Duck Release");
                knob (pid::delayDrive, "Drive");
                knob (pid::delayDiffusion, "Diffusion");
                knob (pid::delayMod, "Modulation");
                knob (pid::delaySpread, "Spread");
                knob (pid::delayToReverb, "Into Reverb");
                mix = &knob (pid::delayMix, "Mix");
                showSynced (apvts.getRawParameterValue (pidFor (pid::delaySync))->load() > 0.5f);
                setMixKnob (mix);
                gr.setTooltip ("Ducking (dB)");
                placeInVisual ({ &gr });
            }

            void refresh() override
            {
                DetailPanel::refresh();
                gr.push (live != nullptr ? live->delayDuck.ago (0) : 0.0f);
                const bool synced = apvts.getRawParameterValue (pidFor (pid::delaySync))->load() > 0.5f;
                if (synced != shownSynced)
                    showSynced (synced);
            }

        private:
            // Note value (synced) or milliseconds (free) in the first main slot; the other one waits in Mehr
            void showSynced (bool synced)
            {
                shownSynced = synced;
                juce::Component* toMain = synced ? static_cast<juce::Component*> (division) : static_cast<juce::Component*> (time);
                juce::Component* toMore = synced ? static_cast<juce::Component*> (time) : static_cast<juce::Component*> (division);
                getMoreDrawer().addAndMakeVisible (toMore);
                if (auto* k = dynamic_cast<Knob*> (toMore))
                {
                    k->setProminence (Knob::Prominence::secondary);
                    k->setDialSize (32);
                }
                setMainControls ({ toMain, syncSwitch, feedback, mix, pingSwitch });
            }

            void layoutVisual (juce::Rectangle<int> area) override { placeMeter (gr, area); }

            Segments* division = nullptr;
            Knob* time = nullptr;
            Knob* feedback = nullptr;
            Knob* mix = nullptr;
            Toggle* syncSwitch = nullptr;
            Toggle* pingSwitch = nullptr;
            bool shownSynced = false;
            GainReductionMeter gr { colours::accent, "Duck" };
        };

        // PULSE ("like Kickstart"): the big Amount and the shape tiles in the visual next to the curve;
        // Rate, Smooth, Mix in the main row; depth, timing, the Custom curve's interpolation / tension and
        // the manual tempo in Mehr
        class PulsePanel final : public DetailPanel
        {
        public:
            explicit PulsePanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Pulse", help::shortDescription ((int) ModuleId::pulse), pid::pulseOn)
            {
                amount = &knob (pid::pulseAmount, "Amount");
                tiles = &own (std::make_unique<PulseShapeTiles> (p));
                auto& rate = segments (pid::pulseRate, { "1/1", "1/2", "1/4", "1/8", "1/16" }, true);
                setCaption (rate, "Rate");
                auto& smooth = knob (pid::pulseSmooth, "Smooth");
                auto& mix = knob (pid::pulseMix, "Mix");
                knob (pid::pulseDepth, "Depth");
                knob (pid::pulseAttack, "Attack");
                knob (pid::pulseRelease, "Release");
                knob (pid::pulseTension, "Tension");
                segments (pid::pulseInterp);
                bpm = &knob (pid::pulseBpm, "Manual BPM");
                setMainControls ({ &rate, &smooth, &mix });
                placeInVisual ({ amount, tiles });
                amount->setDialSize (112);
                tiles->setAccent (moduleColour (ModuleId::pulse));
            }

            void refresh() override
            {
                DetailPanel::refresh();
                tiles->refresh (live != nullptr ? live->settings.pulse : proc.readEffectiveSettings().pulse);
                // The manual tempo only matters when the host sends none
                const bool host = proc.getChain().getPulse().displayHostTempo.load();
                bpm->setAlpha (host ? 0.4f : (apvts.getRawParameterValue (pid::pulseOn)->load() > 0.5f ? 1.0f : 0.55f));
            }

        private:
            void layoutVisual (juce::Rectangle<int> area) override
            {
                // Amount column on the left, the tiles under the curve
                auto r = area.reduced (10);
                const int column = std::min (180, r.getWidth() / 4);
                amount->setBounds (r.removeFromLeft (column).withSizeKeepingCentre (column, std::min (r.getHeight(), 176)));
                r.removeFromLeft (14);
                // The curve view keeps a 24 px margin around its plot: shift it so plot and tiles line up
                tiles->setBounds (r.removeFromBottom (std::min (64, r.getHeight() / 3)).withTrimmedRight (6));
                if (auto* v = getVisual())
                    v->setBounds (juce::Rectangle<int>::leftTopRightBottom (r.getX() - 24, area.getY() + 1, area.getRight() - 1, r.getBottom() + 4));
            }

            Knob* amount = nullptr;
            Knob* bpm = nullptr;
            PulseShapeTiles* tiles = nullptr;
        };

        // STUTTER (internal: chop): the cuts on the scrolling vocal; Rate, Depth, Smooth, Pattern
        class StutterPanel final : public DetailPanel
        {
        public:
            explicit StutterPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Stutter", help::shortDescription ((int) ModuleId::chop), pid::chopOn)
            {
                auto& rate = segments (pid::chopRate);
                setCaption (rate, "Rate");
                auto& depth = knob (pid::chopDepth, "Depth");
                auto& smooth = knob (pid::chopSmooth, "Smooth");
                auto& pattern = choice (pid::chopPattern, {});
                setCaption (pattern, "Pattern");
                setMainControls ({ &rate, &depth, &smooth, &pattern });
            }
        };
    }

    //==========================================================================
    std::unique_ptr<ModuleView> createTimeModuleView (VelvoxAudioProcessor& p, ModuleId id, int instance)
    {
        switch (id)
        {
            case ModuleId::delay: return std::make_unique<EchoMapView> (p, instance);
            case ModuleId::pulse: return std::make_unique<PulseCurveView> (p);
            case ModuleId::chop:  return std::make_unique<StutterWaveView> (p);
            default: break;
        }
        return nullptr;
    }

    std::unique_ptr<DetailPanel> createTimeModulePanel (VelvoxAudioProcessor& p, ModuleId id)
    {
        switch (id)
        {
            case ModuleId::delay: return std::make_unique<DelayPanel> (p);
            case ModuleId::pulse: return std::make_unique<PulsePanel> (p);
            case ModuleId::chop:  return std::make_unique<StutterPanel> (p);
            default: break;
        }
        return nullptr;
    }
}
