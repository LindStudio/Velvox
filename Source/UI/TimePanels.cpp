#include "TimePanels.h"
#include "../PluginProcessor.h"
#include "HelpContent.h"
#include "../Text/I18n.h"
#include "../Text/ParamText.h"

namespace velvox::ui
{
    using dsp::ModuleId;

    namespace
    {
        using i18n::tr;

        // Upper case including ä / ö / ü (juce::String::toUpperCase leaves them as they are)
        juce::String capitals (const juce::String& s)
        {
            return s.toUpperCase().replace (juce::String::fromUTF8 ("ä"), juce::String::fromUTF8 ("Ä"))
                                  .replace (juce::String::fromUTF8 ("ö"), juce::String::fromUTF8 ("Ö"))
                                  .replace (juce::String::fromUTF8 ("ü"), juce::String::fromUTF8 ("Ü"));
        }

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
        tempoSource = [this] { return proc.getChain().getDisplayTempo(); };
        info = std::make_unique<HoverInfo> (*this, [] { return juce::String ("Echo"); },
                                            [this] { return readout(); },
                                            [] { return tr ("Ziehen: links/rechts = Zeit, hoch/runter = Feedback.", "Drag: left/right = time, up/down = feedback."); });
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
            menu.addItem (tr ("Zeit und Feedback zurücksetzen", "Reset time and feedback"), [safe] { if (safe != nullptr) safe->resetEcho(); });
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
        proc.getUndo().perform (tr ("Echo zurücksetzen", "Reset echo"), [this]
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
                time = &knob (pid::delayTime, tr ("Zeit", "Time"));
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
                knob (pid::delayToReverb, tr ("Ins Reverb", "Into Reverb"));
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
    }

    //==========================================================================
    std::unique_ptr<ModuleView> createTimeModuleView (VelvoxAudioProcessor& p, ModuleId id, int instance)
    {
        switch (id)
        {
            case ModuleId::delay: return std::make_unique<EchoMapView> (p, instance);
            default: break;
        }
        return nullptr;
    }

    std::unique_ptr<DetailPanel> createTimeModulePanel (VelvoxAudioProcessor& p, ModuleId id)
    {
        switch (id)
        {
            case ModuleId::delay: return std::make_unique<DelayPanel> (p);
            default: break;
        }
        return nullptr;
    }
}
