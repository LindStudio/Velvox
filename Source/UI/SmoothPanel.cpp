#include "SmoothPanel.h"
#include "../PluginProcessor.h"
#include "HelpContent.h"
#include "../Text/I18n.h"

namespace velvox::ui
{
    using dsp::ModuleId;

    namespace
    {
        using i18n::tr;

        juce::RangedAudioParameter* param (VelvoxAudioProcessor& proc, const juce::String& id)
        {
            return proc.getState().getParameter (id);
        }

        float realValue (VelvoxAudioProcessor& proc, const juce::String& id)
        {
            auto* p = param (proc, id);
            return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f;
        }

        void writeReal (juce::RangedAudioParameter* p, float value)
        {
            if (p == nullptr)
                return;
            const float v = p->convertTo0to1 (value);
            if (std::abs (v - p->getValue()) > 1.0e-7f)
                p->setValueNotifyingHost (v);
        }

        // Position of a time on the Speed link (0 = slow end, 1 = fast end), from SmoothModule::speedToTimes
        float linkPosition (float ms, float slowMs, float fastMs)
        {
            return std::log (std::max (1.0e-3f, ms) / slowMs) / std::log (fastMs / slowMs);
        }
    }

    //==========================================================================
    namespace smoothedit
    {
        juce::String paramId (const char* base, int instance) { return dsp::instanceParamId (base, instance); }
        juce::String weightFreqId (int instance, int point) { return paramId (pid::smoothWFreq[juce::jlimit (0, 5, point)], instance); }
        juce::String weightGainId (int instance, int point) { return paramId (pid::smoothWGain[juce::jlimit (0, 5, point)], instance); }

        float speed (VelvoxAudioProcessor& proc, int instance)
        {
            const auto slow = dsp::SmoothModule::speedToTimes (0.0f), fast = dsp::SmoothModule::speedToTimes (1.0f);
            const float a = linkPosition (realValue (proc, paramId (pid::smoothAttack, instance)), slow.first, fast.first);
            const float r = linkPosition (realValue (proc, paramId (pid::smoothRelease, instance)), slow.second, fast.second);
            return juce::jlimit (0.0f, 1.0f, 0.5f * (a + r));
        }

        void beginSpeedGesture (VelvoxAudioProcessor& proc, int instance)
        {
            for (auto* id : { pid::smoothAttack, pid::smoothRelease })
                if (auto* p = param (proc, paramId (id, instance)))
                    p->beginChangeGesture();
        }

        void writeSpeed (VelvoxAudioProcessor& proc, int instance, float speed01)
        {
            const auto [attack, release] = dsp::SmoothModule::speedToTimes (juce::jlimit (0.0f, 1.0f, speed01));
            writeReal (param (proc, paramId (pid::smoothAttack, instance)), attack);
            writeReal (param (proc, paramId (pid::smoothRelease, instance)), release);
        }

        void endSpeedGesture (VelvoxAudioProcessor& proc, int instance)
        {
            // Reverse order: the outermost gesture (attack) ends last and records the step
            for (auto* id : { pid::smoothRelease, pid::smoothAttack })
                if (auto* p = param (proc, paramId (id, instance)))
                    p->endChangeGesture();
        }

        void setSpeed (VelvoxAudioProcessor& proc, int instance, float speed01)
        {
            beginSpeedGesture (proc, instance);
            writeSpeed (proc, instance, speed01);
            endSpeedGesture (proc, instance);
        }

        void resetSpeed (VelvoxAudioProcessor& proc, int instance)
        {
            beginSpeedGesture (proc, instance);
            for (auto* id : { pid::smoothAttack, pid::smoothRelease })
                if (auto* p = param (proc, paramId (id, instance)))
                    p->setValueNotifyingHost (p->getDefaultValue());
            endSpeedGesture (proc, instance);
        }

        void resetPoint (VelvoxAudioProcessor& proc, int instance, int point)
        {
            auto* f = param (proc, weightFreqId (instance, point));
            auto* g = param (proc, weightGainId (instance, point));
            if (f == nullptr || g == nullptr)
                return;
            f->beginChangeGesture();
            g->beginChangeGesture();
            f->setValueNotifyingHost (f->getDefaultValue());
            g->setValueNotifyingHost (g->getDefaultValue());
            g->endChangeGesture();
            f->endChangeGesture();
        }

        void flattenAll (VelvoxAudioProcessor& proc, int instance)
        {
            std::array<juce::RangedAudioParameter*, dsp::SmoothSettings::numWeights> gains {};
            for (int i = 0; i < dsp::SmoothSettings::numWeights; ++i)
                gains[(size_t) i] = param (proc, weightGainId (instance, i));
            for (auto* g : gains)
                if (g != nullptr)
                    g->beginChangeGesture();
            for (auto* g : gains)
                writeReal (g, 0.0f);
            for (auto it = gains.rbegin(); it != gains.rend(); ++it)
                if (*it != nullptr)
                    (*it)->endChangeGesture();
        }

        void resetParameter (VelvoxAudioProcessor& proc, const juce::String& id)
        {
            if (auto* p = param (proc, id))
                gestures::resetToDefault (*p);
        }

        dsp::SmoothSettings settings (VelvoxAudioProcessor& proc, int instance)
        {
            const auto v = [&] (const char* id) { return realValue (proc, paramId (id, instance)); };
            dsp::SmoothSettings s;
            s.enabled = v (pid::smoothOn) > 0.5f;
            s.amount = v (pid::smoothAmount) * 0.01f;
            s.sensitivity = v (pid::smoothSensitivity) * 0.01f;
            s.lowHz = v (pid::smoothLow);
            s.highHz = v (pid::smoothHigh);
            s.attackMs = v (pid::smoothAttack);
            s.releaseMs = v (pid::smoothRelease);
            s.mode = juce::roundToInt (v (pid::smoothMode));
            s.outputDb = v (pid::smoothOutput);
            s.delta = v (pid::smoothDelta) > 0.5f;
            for (int i = 0; i < dsp::SmoothSettings::numWeights; ++i)
            {
                s.weightFreq[(size_t) i] = realValue (proc, weightFreqId (instance, i));
                s.weightGain[(size_t) i] = realValue (proc, weightGainId (instance, i));
            }
            return s;
        }
    }

    //==========================================================================
    SpeedKnob::SpeedKnob (VelvoxAudioProcessor& p, int inst, juce::Colour hue)
        : Knob (i18n::tr ("Tempo", "Speed"), hue), proc (p), instance (inst)
    {
        slider.setRange (0.0, 1.0, 0.0);
        slider.textFromValueFunction = [] (double v) { return textFor ((float) v); };
        slider.valueFromTextFunction = [this] (const juce::String& text)
        {
            const auto digits = text.retainCharacters ("0123456789.,-").replaceCharacter (',', '.');
            if (digits.isEmpty())
                return slider.getValue();   // not a value: keep it
            return juce::jlimit (0.0, 1.0, digits.getDoubleValue() / 100.0);
        };
        // Double-click / Alt+click = the Attack / Release defaults, handled here: the defaults may lie off
        // the shown value (hand-set times), and a slider reset to an equal value would not reach them
        slider.setDoubleClickReturnValue (false, 0.5);
        slider.onDragStart = [this]
        {
            if (! inGesture)
            {
                inGesture = true;
                smoothedit::beginSpeedGesture (proc, instance);
            }
        };
        slider.onDragEnd = [this]
        {
            if (inGesture)
            {
                inGesture = false;
                smoothedit::endSpeedGesture (proc, instance);
            }
        };
        slider.onValueChange = [this]
        {
            if (syncing)
                return;
            if (inGesture)
                smoothedit::writeSpeed (proc, instance, (float) slider.getValue());
            else
                smoothedit::setSpeed (proc, instance, (float) slider.getValue());
        };
        slider.addMouseListener (this, false);
        setDescription (tr ("Wie schnell Smooth zupackt und loslässt: stellt Attack und Release gemeinsam.",
                            "How fast Smooth grabs and lets go: sets Attack and Release together."));
        sync();
        snapToValue();
    }

    SpeedKnob::~SpeedKnob()
    {
        slider.removeMouseListener (this);
        if (inGesture)
            smoothedit::endSpeedGesture (proc, instance);
    }

    juce::String SpeedKnob::textFor (float speed01)
    {
        return juce::String (juce::roundToInt (juce::jlimit (0.0f, 1.0f, speed01) * 100.0f)) + " %";
    }

    void SpeedKnob::show (float speed01)
    {
        const juce::ScopedValueSetter<bool> guard (syncing, true);
        slider.setValue (speed01, juce::dontSendNotification);   // the value box follows; nothing is written
    }

    void SpeedKnob::sync()
    {
        if (inGesture)
            return;
        const float s = smoothedit::speed (proc, instance);
        if (std::abs ((float) slider.getValue() - s) > 1.0e-4f)
            show (s);
    }

    void SpeedKnob::resetToDefault()
    {
        if (inGesture)
        {
            // Inside the drag the click opened (Alt+click, the second click of a double-click): same step
            for (auto* id : { pid::smoothAttack, pid::smoothRelease })
                if (auto* p = proc.getState().getParameter (smoothedit::paramId (id, instance)))
                    p->setValueNotifyingHost (p->getDefaultValue());
            ignoreDrag = true;
        }
        else
        {
            smoothedit::resetSpeed (proc, instance);
        }
        show (smoothedit::speed (proc, instance));
    }

    juce::PopupMenu SpeedKnob::buildMenu()
    {
        juce::Component::SafePointer<SpeedKnob> safe (this);
        juce::PopupMenu m;
        m.addItem (tr ("Standardwert", "Default value"), [safe] { if (safe != nullptr) safe->resetToDefault(); });
        m.addItem (tr ("Wert eingeben …", "Enter value …"), [safe] { if (safe != nullptr) safe->slider.showTextBox(); });
        if (auto* panel = findParentComponentOfClass<DetailPanel>())
        {
            m.addSeparator();
            panel->addPanelItems (m);
        }
        return m;
    }

    void SpeedKnob::mouseDown (const juce::MouseEvent& e)
    {
        ignoreDrag = false;
        if (e.mods.isPopupMenu())
        {
            buildMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&slider));
            return;
        }
        if (e.mods.isAltDown())
            resetToDefault();
    }

    void SpeedKnob::mouseDrag (const juce::MouseEvent&)
    {
        // After a reset click the slider still sees the drag: keep the reset values
        if (ignoreDrag && inGesture)
        {
            const float s = smoothedit::speed (proc, instance);
            if (std::abs ((float) slider.getValue() - s) > 1.0e-4f)
            {
                for (auto* id : { pid::smoothAttack, pid::smoothRelease })
                    if (auto* p = proc.getState().getParameter (smoothedit::paramId (id, instance)))
                        p->setValueNotifyingHost (p->getDefaultValue());
                show (smoothedit::speed (proc, instance));
            }
        }
    }

    void SpeedKnob::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (! e.mods.isPopupMenu())
            resetToDefault();
    }

    //==========================================================================
    // The graph
    namespace
    {
        constexpr float hitRadius = 11.0f, edgeHit = 6.0f;
        const juce::Colour pink (0xfff472b6);

        juce::String hzText (float f)
        {
            return f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz";
        }

        juce::String dbText (float db)
        {
            const float r = std::round (db * 10.0f) / 10.0f;
            return (r > 0.0f ? "+" : "") + juce::String (r == 0.0f ? 0.0f : r, 1) + " dB";
        }

        float wheelNotches (const juce::MouseWheelDetails& w)
        {
            const float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? -w.deltaX : w.deltaY;
            return juce::jlimit (-3.0f, 3.0f, (w.isReversed ? -d : d) / 0.11f);
        }

        bool fineMods (const juce::ModifierKeys& m) { return m.isShiftDown() || m.isCtrlDown() || m.isCommandDown(); }
    }

    SmoothGraph::SmoothGraph (VelvoxAudioProcessor& p, int inst) : proc (p), instance (inst)
    {
        const float ratio = SpectrumSource::maxHz / SpectrumSource::minHz;
        for (int i = 0; i < numColumns; ++i)
            colHz[(size_t) i] = SpectrumSource::minHz * std::pow (ratio, ((float) i + 0.5f) / (float) numColumns);
        inputDb.fill (spectrumBottomDb - 30.0f);
        removedDb.fill (0.0f);
        paramKey.fill (-1.0e9f);
        readParameters();
        rebuildCurve();
        rebuildSpectrum();
    }

    SmoothGraph::~SmoothGraph()
    {
        endDrag();
    }

    std::unique_ptr<ModuleView> createSmoothGraph (VelvoxAudioProcessor& proc, int instance)
    {
        return std::make_unique<SmoothGraph> (proc, instance);
    }

    juce::StringArray SmoothGraph::getEditedParameters() const
    {
        juce::StringArray ids;
        for (int i = 0; i < dsp::SmoothSettings::numWeights; ++i)
        {
            ids.add (smoothedit::weightFreqId (instance, i));
            ids.add (smoothedit::weightGainId (instance, i));
        }
        ids.add (smoothedit::paramId (pid::smoothLow, instance));
        ids.add (smoothedit::paramId (pid::smoothHigh, instance));
        return ids;
    }

    //==========================================================================
    // Geometry

    juce::Rectangle<float> SmoothGraph::plotArea() const noexcept
    {
        // Room for the read-out above and the frequency labels below
        return getLocalBounds().toFloat().withTrimmedTop (20.0f).withTrimmedBottom (22.0f).reduced (10.0f, 0.0f);
    }

    float SmoothGraph::xForFreq (float hz) const noexcept
    {
        const auto r = plotArea();
        return r.getX() + r.getWidth() * std::log (std::max (1.0f, hz) / SpectrumSource::minHz) / std::log (SpectrumSource::maxHz / SpectrumSource::minHz);
    }

    float SmoothGraph::freqForX (float x) const noexcept
    {
        const auto r = plotArea();
        const float t = (x - r.getX()) / std::max (1.0f, r.getWidth());
        return juce::jlimit (SpectrumSource::minHz, SpectrumSource::maxHz,
                             SpectrumSource::minHz * std::pow (SpectrumSource::maxHz / SpectrumSource::minHz, t));
    }

    float SmoothGraph::yForWeight (float db) const noexcept
    {
        const auto r = plotArea().reduced (0.0f, 12.0f);   // the points at +-12 dB stay fully visible
        return juce::jmap (db, -rangeDb, rangeDb, r.getBottom(), r.getY());
    }

    float SmoothGraph::weightForY (float y) const noexcept
    {
        const auto r = plotArea().reduced (0.0f, 12.0f);
        return juce::jmap (y, r.getBottom(), r.getY(), -rangeDb, rangeDb);
    }

    float SmoothGraph::yForLevel (float db, float hz) const noexcept
    {
        const auto r = plotArea();
        const float tilted = db + tiltDbPerOct * std::log2 (std::max (1.0f, hz) / 1000.0f);
        return juce::jmap (juce::jlimit (spectrumBottomDb, spectrumTopDb, tilted), spectrumBottomDb, spectrumTopDb, r.getBottom(), r.getY());
    }

    juce::Point<float> SmoothGraph::pointPosition (int point) const noexcept
    {
        if (point < 0 || point >= dsp::SmoothSettings::numWeights)
            return {};
        const auto r = plotArea();
        return { juce::jlimit (r.getX(), r.getRight(), xForFreq (s.weightFreq[(size_t) point])),
                 yForWeight (juce::jlimit (-rangeDb, rangeDb, s.weightGain[(size_t) point])) };
    }

    int SmoothGraph::pointAt (juce::Point<float> p) const noexcept
    {
        int best = -1;
        float bestD = hitRadius;
        for (int i = 0; i < dsp::SmoothSettings::numWeights; ++i)
        {
            const float d = pointPosition (i).getDistanceFrom (p);
            if (d < bestD)
            {
                bestD = d;
                best = i;
            }
        }
        return best;
    }

    float SmoothGraph::edgeX (Edge e) const noexcept
    {
        return xForFreq (e == Edge::low ? s.lowHz : s.highHz);
    }

    SmoothGraph::Edge SmoothGraph::edgeAt (juce::Point<float> p) const noexcept
    {
        if (! plotArea().expanded (0.0f, 4.0f).contains (p))
            return Edge::none;
        const float dl = std::abs (p.x - edgeX (Edge::low)), dh = std::abs (p.x - edgeX (Edge::high));
        if (std::min (dl, dh) > edgeHit)
            return Edge::none;
        return dl <= dh ? Edge::low : Edge::high;
    }

    juce::String SmoothGraph::edgeParam (Edge e) const
    {
        return smoothedit::paramId (e == Edge::low ? pid::smoothLow : pid::smoothHigh, instance);
    }

    int SmoothGraph::columnFor (float hz) const noexcept
    {
        const float t = std::log (std::max (SpectrumSource::minHz, hz) / SpectrumSource::minHz) / std::log (SpectrumSource::maxHz / SpectrumSource::minHz);
        return juce::jlimit (0, numColumns - 1, (int) std::floor (t * (float) numColumns));
    }

    float SmoothGraph::shownRemovedDb (float hz) const noexcept { return removedDb[(size_t) columnFor (hz)]; }
    float SmoothGraph::shownInputDb (float hz) const noexcept { return inputDb[(size_t) columnFor (hz)]; }

    //==========================================================================
    // Live data

    void SmoothGraph::readParameters()
    {
        s = smoothedit::settings (proc, instance);
        on = s.enabled;
        decltype (paramKey) key {};
        for (int i = 0; i < dsp::SmoothSettings::numWeights; ++i)
        {
            key[(size_t) (2 * i)] = s.weightFreq[(size_t) i];
            key[(size_t) (2 * i + 1)] = s.weightGain[(size_t) i];
        }
        key[12] = s.lowHz;
        key[13] = s.highHz;
        key[14] = on ? 1.0f : 0.0f;
        if (key != paramKey)
        {
            paramKey = key;
            rebuildCurve();
            repaint();
        }
    }

    void SmoothGraph::rebuildCurve()
    {
        weightLine.clear();
        weightFill.clear();
        const auto r = plotArea();
        if (r.getWidth() < 2.0f)
            return;
        constexpr int n = 160;
        const float y0 = yForWeight (0.0f);
        for (int i = 0; i < n; ++i)
        {
            const float x = r.getX() + r.getWidth() * (float) i / (float) (n - 1);
            const float y = yForWeight (juce::jlimit (-rangeDb, rangeDb, dsp::SmoothModule::weightingDbAt (s, freqForX (x))));
            if (i == 0)
            {
                weightLine.startNewSubPath (x, y);
                weightFill.startNewSubPath (x, y0);
            }
            else
            {
                weightLine.lineTo (x, y);
            }
            weightFill.lineTo (x, y);
        }
        weightFill.lineTo (r.getRight(), y0);
        weightFill.closeSubPath();
    }

    void SmoothGraph::rebuildSpectrum()
    {
        inputLine.clear();
        outputLine.clear();
        outputFill.clear();
        removedFill.clear();
        const auto r = plotArea();
        if (r.getWidth() < 2.0f)
            return;
        outputFill.startNewSubPath (r.getX(), r.getBottom());
        for (int i = 0; i < numColumns; ++i)
        {
            const float hz = colHz[(size_t) i];
            const float x = xForFreq (hz);
            const float yi = yForLevel (inputDb[(size_t) i], hz), yo = yForLevel (inputDb[(size_t) i] - removedDb[(size_t) i], hz);
            if (i == 0)
            {
                inputLine.startNewSubPath (x, yi);
                outputLine.startNewSubPath (x, yo);
                removedFill.startNewSubPath (x, yi);
            }
            else
            {
                inputLine.lineTo (x, yi);
                outputLine.lineTo (x, yo);
                removedFill.lineTo (x, yi);
            }
            outputFill.lineTo (x, yo);
        }
        for (int i = numColumns; --i >= 0;)
        {
            const float hz = colHz[(size_t) i];
            removedFill.lineTo (xForFreq (hz), yForLevel (inputDb[(size_t) i] - removedDb[(size_t) i], hz));
        }
        removedFill.closeSubPath();
        outputFill.lineTo (r.getRight(), r.getBottom());
        outputFill.closeSubPath();
    }

    void SmoothGraph::resized()
    {
        rebuildCurve();
        rebuildSpectrum();
    }

    void SmoothGraph::update (const LiveData& d)
    {
        readParameters();
        bool changed = false;

        // The module's spectrum, at most 30 times a second
        if (lastRead < 0.0 || d.time - lastRead >= 1.0 / 30.0 - 1.0e-3 || d.time < lastRead)
        {
            lastRead = d.time;
            auto& module = proc.getChain().getSmooth (instance);
            const bool got = module.getSpectrum (frame) && frame.numBins > 1 && frame.binHz > 0.0f;
            if (got && on)
            {
                haveFrame = true;
                const int nb = std::min (frame.numBins, dsp::SmoothSpectrum::maxBins);
                const float ratio = SpectrumSource::maxHz / SpectrumSource::minHz;
                for (int i = 0; i < numColumns; ++i)
                {
                    const float fLo = SpectrumSource::minHz * std::pow (ratio, (float) i / (float) numColumns);
                    const float fHi = SpectrumSource::minHz * std::pow (ratio, (float) (i + 1) / (float) numColumns);
                    const int k0 = juce::jlimit (1, nb - 1, (int) std::ceil (fLo / frame.binHz));
                    const int k1 = juce::jlimit (1, nb - 1, (int) std::floor (fHi / frame.binHz));
                    float in, red;
                    if (k1 <= k0)
                    {
                        // Narrower than a bin (low frequencies): interpolate between the two nearest
                        const float pos = juce::jlimit (1.0f, (float) (nb - 1), colHz[(size_t) i] / frame.binHz);
                        const int ka = std::min (nb - 2, (int) pos);
                        const float t = pos - (float) ka;
                        in = frame.inputDb[(size_t) ka] + t * (frame.inputDb[(size_t) ka + 1] - frame.inputDb[(size_t) ka]);
                        red = frame.reductionDb[(size_t) ka] + t * (frame.reductionDb[(size_t) ka + 1] - frame.reductionDb[(size_t) ka]);
                    }
                    else
                    {
                        in = frame.inputDb[(size_t) k0];
                        red = frame.reductionDb[(size_t) k0];
                        for (int k = k0 + 1; k <= k1; ++k)
                        {
                            in = std::max (in, frame.inputDb[(size_t) k]);
                            red = std::max (red, frame.reductionDb[(size_t) k]);
                        }
                    }
                    if (! std::isfinite (in)) in = spectrumBottomDb - 30.0f;
                    if (! std::isfinite (red)) red = 0.0f;
                    red = juce::jlimit (0.0f, 60.0f, red);
                    auto& ci = inputDb[(size_t) i];
                    ci += (in - ci) * (in > ci ? 0.6f : 0.3f);
                    auto& cr = removedDb[(size_t) i];
                    cr = red > cr ? red : cr + (red - cr) * 0.35f;
                }
                changed = true;
            }
            else if (! on)
            {
                // Off: nothing is removed; the picture fades out
                bool any = false;
                for (int i = 0; i < numColumns; ++i)
                {
                    removedDb[(size_t) i] = 0.0f;
                    auto& ci = inputDb[(size_t) i];
                    if (ci > spectrumBottomDb - 30.0f)
                    {
                        ci = std::max (spectrumBottomDb - 30.0f, ci - 3.0f);
                        any = true;
                    }
                }
                changed = any || peakDb > 0.0f;
            }

            if (changed)
            {
                rebuildSpectrum();
                float best = 0.0f, bestHz = 0.0f;
                for (int i = 0; i < numColumns; ++i)
                    if (removedDb[(size_t) i] > best)
                    {
                        best = removedDb[(size_t) i];
                        bestHz = colHz[(size_t) i];
                    }
                if (std::abs (best - peakDb) > 0.05f || (best > 0.3f) != peakText.isNotEmpty())
                {
                    peakDb = best;
                    peakText = best > 0.3f ? juce::String::fromUTF8 ("−") + juce::String (best, 1) + juce::String::fromUTF8 (" dB · ") + hzText (bestHz)
                                           : juce::String();
                }
                if (best <= 0.0f)
                    peakDb = 0.0f;
            }
        }

        const bool wasShown = cardShown;
        const auto oldValues = cardValues;
        updateCard();
        changed = changed || wasShown != cardShown || oldValues != cardValues;

        if (changed && (d.time - lastPaint >= 1.0 / 30.0 - 1.0e-3 || d.time < lastPaint || lastPaint < 0.0 || dragPoint >= 0 || dragEdge != Edge::none))
        {
            lastPaint = d.time;
            repaint();
        }
    }

    void SmoothGraph::updateCard()
    {
        int point = dragPoint;
        Edge edge = dragEdge;
        if (point < 0 && edge == Edge::none && juce::Time::getMillisecondCounter() - hoverSince >= (juce::uint32) motion::hoverInfoDelayMs)
        {
            point = hoverPoint;
            edge = hoverEdge;
        }
        if (point >= 0)
        {
            cardTitle = tr ("PUNKT ", "POINT ") + juce::String (point + 1);
            cardValues = hzText (s.weightFreq[(size_t) point]) + "   " + dbText (s.weightGain[(size_t) point]);
            cardAnchor = pointPosition (point);
        }
        else if (edge != Edge::none)
        {
            cardTitle = edge == Edge::low ? tr ("UNTERGRENZE", "LOW LIMIT") : tr ("OBERGRENZE", "HIGH LIMIT");
            cardValues = hzText (edge == Edge::low ? s.lowHz : s.highHz);
            cardAnchor = { edgeX (edge), plotArea().getY() + 40.0f };
        }
        else
        {
            cardShown = false;
            return;
        }
        cardShown = true;
        cardWidth = (float) std::max (juce::GlyphArrangement::getStringWidthInt (semiBold (9.0f, 0.1f), cardTitle),
                                      juce::GlyphArrangement::getStringWidthInt (monoFont (11.0f), cardValues)) + 24.0f;
    }

    //==========================================================================
    // Drawing

    void SmoothGraph::paint (juce::Graphics& g)
    {
        const auto r = plotArea();
        const auto hue = moduleColour (ModuleId::smooth);
        const float a = on ? 1.0f : 0.45f;

        // Weighting axis (+12 .. -12 dB) and frequency labels
        static const juce::String dbLabels[] { "+12", "+6", "0", "-6", "-12" };
        static const float dbValues[] { 12.0f, 6.0f, 0.0f, -6.0f, -12.0f };
        g.setFont (medium (10.0f));
        for (int i = 0; i < 5; ++i)
        {
            const float y = yForWeight (dbValues[i]);
            g.setColour (colours::accent.withAlpha (dbValues[i] == 0.0f ? 0.09f : 0.03f));
            g.fillRect (juce::Rectangle<float> (r.getX(), y, r.getWidth(), 1.0f));
            g.setColour (colours::textFaint);
            g.drawText (dbLabels[i], juce::Rectangle<float> (r.getRight() - 30.0f, y - 13.0f, 28.0f, 12.0f), juce::Justification::right, false);
        }
        static const juce::String hzLabels[] { "50", "100", "200", "500", "1k", "2k", "5k", "10k" };
        static const float hzValues[] { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f };
        g.setColour (colours::textFaint);
        for (int i = 0; i < 8; ++i)
            g.drawText (hzLabels[i], juce::Rectangle<float> (xForFreq (hzValues[i]) - 20.0f, r.getBottom() + 5.0f, 40.0f, 14.0f),
                        juce::Justification::centred, false);
        // The ends of the axis (20 Hz / 20 kHz), inside the plot width
        g.drawText ("20", juce::Rectangle<float> (r.getX() + 2.0f, r.getBottom() + 5.0f, 30.0f, 14.0f), juce::Justification::centredLeft, false);
        g.drawText ("20k", juce::Rectangle<float> (r.getRight() - 32.0f, r.getBottom() + 5.0f, 30.0f, 14.0f), juce::Justification::centredRight, false);

        const float lo = xForFreq (std::min (s.lowHz, s.highHz)), hi = xForFreq (std::max (s.lowHz, s.highHz));
        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (r.toNearestInt());

            // Spectrum: what comes out filled in the Smooth colour, the input as a faint line, the removed part pink
            g.setGradientFill (juce::ColourGradient (hue.withAlpha (0.26f * a), 0.0f, r.getY() + r.getHeight() * 0.3f,
                                                     hue.withAlpha (0.03f * a), 0.0f, r.getBottom(), false));
            g.fillPath (outputFill);
            g.setColour (hue.withAlpha (0.55f * a));
            g.strokePath (outputLine, juce::PathStrokeType (1.1f));
            g.setColour (colours::textFaint.withAlpha (0.45f * a));
            g.strokePath (inputLine, juce::PathStrokeType (0.9f));
            if (on)
            {
                g.setColour (pink.withAlpha (0.78f));
                g.fillPath (removedFill);
            }

            // Untreated ranges shaded, the limit edges with their grips
            g.setColour (colours::background.withAlpha (0.62f));
            g.fillRect (r.withRight (lo));
            g.fillRect (r.withLeft (hi));
            for (auto e : { Edge::low, Edge::high })
            {
                const bool hot = e == dragEdge || (dragEdge == Edge::none && dragPoint < 0 && e == hoverEdge);
                const float x = edgeX (e);
                g.setColour (hue.withAlpha ((hot ? 0.9f : 0.45f) * a));
                g.fillRect (juce::Rectangle<float> (x - 0.5f, r.getY(), 1.0f, r.getHeight()));
                g.fillRoundedRectangle (juce::Rectangle<float> (hot ? 6.0f : 4.0f, 22.0f).withCentre ({ x, r.getY() + 14.0f }), 2.0f);
            }

            // The weighting curve
            const auto curveColour = hue.interpolatedWith (juce::Colours::white, 0.35f);
            g.setColour (curveColour.withAlpha (0.06f * a));
            g.fillPath (weightFill);
            g.setColour (curveColour.withAlpha (0.13f * a));
            g.strokePath (weightLine, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (curveColour.withAlpha (0.95f * a));
            g.strokePath (weightLine, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Points: filled when they weight, hollow when flat
        for (int i = 0; i < dsp::SmoothSettings::numWeights; ++i)
        {
            const auto p = pointPosition (i);
            const bool hot = i == dragPoint || (dragPoint < 0 && i == hoverPoint);
            const float size = hot ? 13.0f : 10.5f;
            if (hot)
                drawGlow (g, p, 18.0f, 0.4f * a, hue);
            g.setColour (colours::panelInset);
            g.fillEllipse (juce::Rectangle<float> (size + 2.0f, size + 2.0f).withCentre (p));
            g.setColour (hue.interpolatedWith (juce::Colours::white, 0.25f).withAlpha (a));
            if (std::abs (s.weightGain[(size_t) i]) > 0.05f || hot)
                g.fillEllipse (juce::Rectangle<float> (size, size).withCentre (p));
            else
                g.drawEllipse (juce::Rectangle<float> (size - 1.6f, size - 1.6f).withCentre (p), 1.6f);
        }

        // Read-out: the deepest cut right now
        if (peakText.isNotEmpty() && on)
        {
            const auto top = getLocalBounds().toFloat().reduced (12.0f, 3.0f).withHeight (14.0f);
            g.setFont (semiBold (9.0f, 0.12f));
            g.setColour (colours::textFaint);
            g.drawText ("MAX", top.withWidth (32.0f), juce::Justification::centredLeft, false);
            g.setFont (monoFont (11.0f));
            g.setColour (pink.interpolatedWith (juce::Colours::white, 0.3f));
            g.drawText (peakText, top.withTrimmedLeft (32.0f).withWidth (200.0f), juce::Justification::centredLeft, false);
        }

        // Hover / drag card
        if (cardShown)
        {
            const float h = 40.0f;
            auto box = juce::Rectangle<float> (cardWidth, h).withCentre (cardAnchor.translated (0.0f, -(h * 0.5f + 16.0f)));
            if (box.getY() < 2.0f)
                box.setY (cardAnchor.y + 18.0f);
            box = box.constrainedWithin (getLocalBounds().toFloat().reduced (3.0f));
            g.setColour (colours::raised.withAlpha (0.95f));
            g.fillRoundedRectangle (box, 8.0f);
            g.setColour (hue.withAlpha (0.6f));
            g.drawRoundedRectangle (box.reduced (0.5f), 8.0f, 1.0f);
            auto inner = box.reduced (12.0f, 5.0f);
            g.setColour (colours::textMuted);
            g.setFont (semiBold (9.0f, 0.1f));
            g.drawText (cardTitle, inner.removeFromTop (14.0f), juce::Justification::centredLeft, false);
            g.setColour (colours::text);
            g.setFont (monoFont (11.0f));
            g.drawText (cardValues, inner, juce::Justification::centredLeft, false);
        }
    }

    //==========================================================================
    // Menus

    void SmoothGraph::addPanelItems (juce::PopupMenu& m)
    {
        if (auto* panel = findParentComponentOfClass<DetailPanel>())
        {
            m.addSeparator();
            panel->addPanelItems (m);
        }
    }

    juce::PopupMenu SmoothGraph::buildPointMenu (int point)
    {
        juce::Component::SafePointer<SmoothGraph> safe (this);
        juce::PopupMenu m;
        if (point < 0 || point >= dsp::SmoothSettings::numWeights)
            return m;
        m.addSectionHeader (tr ("Punkt ", "Point ") + juce::String (point + 1));
        m.addItem (tr ("Zurücksetzen", "Reset"), [safe, point] { if (safe != nullptr) { smoothedit::resetPoint (safe->proc, safe->instance, point); safe->readParameters(); } });
        m.addItem (tr ("Alle flach", "Flatten all"), [safe] { if (safe != nullptr) { smoothedit::flattenAll (safe->proc, safe->instance); safe->readParameters(); } });
        addPanelItems (m);
        return m;
    }

    juce::PopupMenu SmoothGraph::buildEdgeMenu (Edge e)
    {
        auto* p = proc.getState().getParameter (edgeParam (e));
        if (e == Edge::none || p == nullptr)
            return {};
        juce::Component::SafePointer<SmoothGraph> safe (this);
        return gestures::buildMenu (*p, findParentComponentOfClass<DetailPanel>(), [safe, e] { if (safe != nullptr) safe->showEdgeValueEditor (e); });
    }

    void SmoothGraph::showEdgeValueEditor (Edge e)
    {
        auto* p = proc.getState().getParameter (edgeParam (e));
        if (p == nullptr)
            return;
        if (valueEditor == nullptr)
        {
            valueEditor = std::make_unique<juce::TextEditor>();
            valueEditor->setFont (monoFont (11.0f));
            valueEditor->setJustification (juce::Justification::centred);
            addChildComponent (*valueEditor);
            valueEditor->onEscapeKey = [this] { valueEditor->setVisible (false); };
            valueEditor->onFocusLost = [this] { valueEditor->setVisible (false); };
        }
        valueEditor->onReturnKey = [this, e]
        {
            if (auto* prm = proc.getState().getParameter (edgeParam (e)))
                if (const auto v = parseValueText (valueEditor->getText(), *prm))
                    gestures::setReal (*prm, *v);
            valueEditor->setVisible (false);
            readParameters();
        };
        valueEditor->setText (hzText (p->convertFrom0to1 (p->getValue())), false);
        valueEditor->setBounds (juce::Rectangle<int> (76, 22).withCentre (juce::Point<int> (juce::roundToInt (edgeX (e)), juce::roundToInt (plotArea().getY() + 40.0f)))
                                    .constrainedWithin (getLocalBounds()));
        valueEditor->setVisible (true);
        valueEditor->selectAll();
        if (isShowing())
            valueEditor->grabKeyboardFocus();
    }

    //==========================================================================
    // Mouse

    void SmoothGraph::mouseMove (const juce::MouseEvent& e)
    {
        const int hp = pointAt (e.position);
        const Edge he = hp >= 0 ? Edge::none : edgeAt (e.position);
        if (hp != hoverPoint || he != hoverEdge)
        {
            hoverPoint = hp;
            hoverEdge = he;
            hoverSince = juce::Time::getMillisecondCounter();
            setMouseCursor (hp >= 0 ? juce::MouseCursor::DraggingHandCursor
                                    : he != Edge::none ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
            updateCard();
            repaint();
        }
    }

    void SmoothGraph::mouseExit (const juce::MouseEvent&)
    {
        hoverPoint = -1;
        hoverEdge = Edge::none;
        updateCard();
        repaint();
    }

    void SmoothGraph::mouseDown (const juce::MouseEvent& e)
    {
        endDrag();
        ignoreDrag = false;
        const int point = pointAt (e.position);
        const Edge edge = point >= 0 ? Edge::none : edgeAt (e.position);

        if (e.mods.isPopupMenu())
        {
            ignoreDrag = true;
            juce::PopupMenu menu;
            if (point >= 0)
                menu = buildPointMenu (point);
            else if (edge != Edge::none)
                menu = buildEdgeMenu (edge);
            else
            {
                // Empty space: the panel's own menu (reset module, presets ...)
                if (auto* parent = getParentComponent())
                    parent->mouseDown (e.getEventRelativeTo (parent));
                return;
            }
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (juce::Rectangle<int> (1, 1).withCentre (e.position.toInt()))));
            return;
        }
        if (point < 0 && edge == Edge::none)
            return;
        if (e.mods.isAltDown())
        {
            ignoreDrag = true;
            if (point >= 0)
                smoothedit::resetPoint (proc, instance, point);
            else
                smoothedit::resetParameter (proc, edgeParam (edge));
            readParameters();
            return;
        }
        dragLast = e.position;
        if (point >= 0)
            beginPointDrag (point);
        else
            beginEdgeDrag (edge);
        updateCard();
        repaint();
    }

    void SmoothGraph::beginPointDrag (int point)
    {
        dragPoint = point;
        dragFreq = s.weightFreq[(size_t) point];
        dragGain = s.weightGain[(size_t) point];
        if (auto* f = proc.getState().getParameter (smoothedit::weightFreqId (instance, point)))
            f->beginChangeGesture();
        if (auto* g = proc.getState().getParameter (smoothedit::weightGainId (instance, point)))
            g->beginChangeGesture();
    }

    void SmoothGraph::beginEdgeDrag (Edge e)
    {
        dragEdge = e;
        dragFreq = e == Edge::low ? s.lowHz : s.highHz;
        if (auto* p = proc.getState().getParameter (edgeParam (e)))
            p->beginChangeGesture();
    }

    void SmoothGraph::endDrag()
    {
        if (dragPoint >= 0)
        {
            const int point = dragPoint;
            dragPoint = -1;
            // Reverse order: the outermost gesture (frequency) ends last and records the step
            if (auto* g = proc.getState().getParameter (smoothedit::weightGainId (instance, point)))
                g->endChangeGesture();
            if (auto* f = proc.getState().getParameter (smoothedit::weightFreqId (instance, point)))
                f->endChangeGesture();
        }
        if (dragEdge != Edge::none)
        {
            const auto e = dragEdge;
            dragEdge = Edge::none;
            if (auto* p = proc.getState().getParameter (edgeParam (e)))
                p->endChangeGesture();
        }
    }

    void SmoothGraph::mouseDrag (const juce::MouseEvent& e)
    {
        if (ignoreDrag || (dragPoint < 0 && dragEdge == Edge::none))
            return;
        // Relative movement: Shift / Ctrl switch to fine (1/10) at any moment without a jump
        const auto delta = e.position - dragLast;
        dragLast = e.position;
        if (delta.isOrigin())
            return;
        const float scale = fineMods (e.mods) ? 0.1f : 1.0f;

        if (dragPoint >= 0)
        {
            dragFreq = freqForX (xForFreq (dragFreq) + delta.x * scale);
            const float dbPerPixel = (weightForY (0.0f) - weightForY (1.0f));   // > 0: dB per pixel upwards
            dragGain = juce::jlimit (-rangeDb, rangeDb, dragGain - delta.y * dbPerPixel * scale);
            writeReal (proc.getState().getParameter (smoothedit::weightFreqId (instance, dragPoint)), dragFreq);
            writeReal (proc.getState().getParameter (smoothedit::weightGainId (instance, dragPoint)), dragGain);
        }
        else if (auto* p = proc.getState().getParameter (edgeParam (dragEdge)))
        {
            const auto range = p->getNormalisableRange();
            dragFreq = juce::jlimit (range.start, range.end, freqForX (xForFreq (dragFreq) + delta.x * scale));
            writeReal (p, dragFreq);
        }
        readParameters();
        updateCard();
        repaint();
    }

    void SmoothGraph::mouseUp (const juce::MouseEvent&)
    {
        endDrag();
        ignoreDrag = false;
        readParameters();
        updateCard();
        repaint();
    }

    void SmoothGraph::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (e.mods.isPopupMenu() || e.mods.isAltDown())
            return;
        const int point = dragPoint >= 0 ? dragPoint : pointAt (e.position);
        const Edge edge = point >= 0 ? Edge::none : (dragEdge != Edge::none ? dragEdge : edgeAt (e.position));
        if (point < 0 && edge == Edge::none)
            return;
        // Inside the gesture the second click opened: the reset belongs to it (one step)
        ignoreDrag = true;
        if (point >= 0)
        {
            if (dragPoint == point)
            {
                for (const auto& id : { smoothedit::weightFreqId (instance, point), smoothedit::weightGainId (instance, point) })
                    if (auto* p = proc.getState().getParameter (id))
                        p->setValueNotifyingHost (p->getDefaultValue());
            }
            else
                smoothedit::resetPoint (proc, instance, point);
        }
        else if (dragEdge == edge)
        {
            if (auto* p = proc.getState().getParameter (edgeParam (edge)))
                p->setValueNotifyingHost (p->getDefaultValue());
        }
        else
            smoothedit::resetParameter (proc, edgeParam (edge));
        readParameters();
        updateCard();
        repaint();
    }

    void SmoothGraph::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
    {
        const int point = pointAt (e.position);
        const Edge edge = point >= 0 ? Edge::none : edgeAt (e.position);
        const float notches = wheelNotches (wheel);
        if ((point < 0 && edge == Edge::none) || notches == 0.0f || dragPoint >= 0 || dragEdge != Edge::none)
        {
            if (point < 0 && edge == Edge::none)
                ModuleView::mouseWheelMove (e, wheel);
            return;
        }
        const float fine = fineMods (e.mods) ? 0.1f : 1.0f;
        if (point >= 0)
        {
            if (auto* g = proc.getState().getParameter (smoothedit::weightGainId (instance, point)))
                gestures::setReal (*g, juce::jlimit (-rangeDb, rangeDb, s.weightGain[(size_t) point] + notches * wheelGainDb * fine));
        }
        else if (auto* p = proc.getState().getParameter (edgeParam (edge)))
        {
            const auto range = p->getNormalisableRange();
            const float now = edge == Edge::low ? s.lowHz : s.highHz;
            gestures::setReal (*p, juce::jlimit (range.start, range.end, now * std::exp2 (notches * wheelEdgeOct * fine)));
        }
        readParameters();
        updateCard();
        repaint();
    }

    //==========================================================================
    namespace
    {
        // SMOOTH (spec 5.4): spectrum + weighting curve; Depth, Sharpness, Speed, Delta; the rest in Mehr
        class SmoothPanel final : public DetailPanel
        {
        public:
            explicit SmoothPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Smooth", help::shortDescription ((int) ModuleId::smooth), pid::smoothOn)
            {
                auto& mode = segments (pid::smoothMode);
                setCaption (mode, tr ("Modus", "Mode"));
                auto& delta = toggle (pid::smoothDelta, "Delta");
                auto& depth = knob (pid::smoothAmount, tr ("Tiefe", "Depth"));
                // German names with umlauts in capitals: the knob upper-cases its name, juce::String::toUpperCase leaves ä / ö / ü
                auto& sharpness = knob (pid::smoothSensitivity, tr ("SCHÄRFE", "Sharpness"));
                speed = &own (std::make_unique<SpeedKnob> (p, getInstance(), moduleColour (ModuleId::smooth)));
                knob (pid::smoothAttack, "Attack");
                knob (pid::smoothRelease, "Release");
                knob (pid::smoothOutput, "Output");
                knob (pid::smoothLow, tr ("Untergrenze", "Low"));
                knob (pid::smoothHigh, tr ("Obergrenze", "High"));
                setMainControls ({ &depth, &sharpness, speed, &delta });
            }

            void refresh() override
            {
                DetailPanel::refresh();
                speed->sync();
            }

        private:
            SpeedKnob* speed = nullptr;
        };
    }

    std::unique_ptr<DetailPanel> createSmoothPanel (VelvoxAudioProcessor& proc)
    {
        return std::make_unique<SmoothPanel> (proc);
    }
}
