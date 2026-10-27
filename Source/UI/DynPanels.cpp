#include "DynPanels.h"
#include "../PluginProcessor.h"
#include "HelpContent.h"
#include "ValueText.h"
#include "../Text/I18n.h"

namespace velvox::ui
{
    using dsp::ModuleId;

    namespace
    {
        const juce::String minus = juce::String::fromUTF8 ("−");
        const juce::Colour grPink (0xfff472b6);
        const juce::String grLabel ("GR");

        // Scale labels ("−12"), built once - paint never allocates
        const juce::String& dbScaleLabel (int v)
        {
            static const auto labels = []
            {
                std::array<juce::String, 121> a;
                for (int i = 0; i < 121; ++i)
                    a[(size_t) i] = (i < 60 ? juce::String::fromUTF8 ("−") : juce::String()) + juce::String (std::abs (i - 60));
                return a;
            }();
            return labels[(size_t) juce::jlimit (0, 120, v + 60)];
        }

        // "-18.0 dB" with a real minus sign; `plus` adds "+" to positive values
        juce::String dbText (float db, bool plus = false, int decimals = 1)
        {
            const float r = std::round (db * 10.0f) / 10.0f;
            const auto num = juce::String (std::abs (r == 0.0f ? 0.0f : r), decimals);
            return (r < 0.0f ? minus : (plus && r > 0.0f ? juce::String ("+") : juce::String())) + num + " dB";
        }

        juce::String hzText (float f)
        {
            return f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz";
        }

        juce::String ratioText (float ratio) { return juce::String (ratio, 1) + ":1"; }

        float wheelNotches (const juce::MouseWheelDetails& w)
        {
            const float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? -w.deltaX : w.deltaY;
            return juce::jlimit (-3.0f, 3.0f, (w.isReversed ? -d : d) / 0.11f);
        }

        bool fineMods (const juce::ModifierKeys& m) { return m.isShiftDown() || m.isCtrlDown() || m.isCommandDown(); }

        // Horizontal dB guides with labels at the left edge of `r`
        void dbGuides (juce::Graphics& g, juce::Rectangle<float> r, std::initializer_list<float> values,
                       const std::function<float (float)>& yFor, bool labelsRight = false)
        {
            g.setFont (medium (10.0f));
            for (float v : values)
            {
                const float y = yFor (v);
                g.setColour (colours::accent.withAlpha (0.035f));
                g.fillRect (juce::Rectangle<float> (r.getX(), y, r.getWidth(), 1.0f));
                g.setColour (colours::textFaint);
                const auto& text = dbScaleLabel (juce::roundToInt (v));
                const auto box = labelsRight ? juce::Rectangle<float> (r.getRight() + 4.0f, y - 7.0f, 30.0f, 13.0f)
                                             : juce::Rectangle<float> (r.getX() + 3.0f, y - 14.0f, 30.0f, 12.0f);
                g.drawText (text, box, juce::Justification::left, false);
            }
        }
    }

    //==========================================================================
    namespace dynedit
    {
        juce::String paramId (const char* base, int instance) { return dsp::instanceParamId (base, instance); }

        float value (VelvoxAudioProcessor& proc, const juce::String& id)
        {
            auto* p = proc.getState().getParameter (id);
            return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f;
        }
    }

    //==========================================================================
    // HandleGraph

    HandleGraph::HandleGraph (VelvoxAudioProcessor& p, int inst, ModuleId module)
        : proc (p), instance (inst), hue (moduleColour (module))
    {
    }

    HandleGraph::~HandleGraph()
    {
        endDrag();
    }

    juce::StringArray HandleGraph::getEditedParameters() const
    {
        juce::StringArray ids;
        for (int h = 0; h < numHandles(); ++h)
            for (const auto& id : paramsOf (h))
                ids.addIfNotAlreadyThere (id);
        return ids;
    }

    juce::RangedAudioParameter* HandleGraph::param (const juce::String& id) const { return proc.getState().getParameter (id); }

    float HandleGraph::paramValue (const juce::String& id) const { return dynedit::value (proc, id); }

    void HandleGraph::writeParam (const juce::String& id, float realValue)
    {
        if (auto* p = param (id))
        {
            const auto range = p->getNormalisableRange();
            const float v = p->convertTo0to1 (juce::jlimit (range.start, range.end, realValue));
            if (std::abs (v - p->getValue()) > 1.0e-7f)
                p->setValueNotifyingHost (v);
        }
    }

    void HandleGraph::stepParam (const juce::String& id, float realValue)
    {
        if (auto* p = param (id))
        {
            const auto range = p->getNormalisableRange();
            gestures::setReal (*p, juce::jlimit (range.start, range.end, realValue));
        }
    }

    juce::MouseCursor HandleGraph::cursorFor (int) const { return juce::MouseCursor::DraggingHandCursor; }

    bool HandleGraph::paintDue (double time)
    {
        if (dragged >= 0 || lastPaint < 0.0 || time < lastPaint || time - lastPaint >= 1.0 / 30.0 - 1.0e-3)
        {
            lastPaint = time;
            return true;
        }
        return false;
    }

    bool HandleGraph::updateCard()
    {
        int h = dragged;
        if (h < 0 && hovered >= 0 && juce::Time::getMillisecondCounter() - hoverSince >= (juce::uint32) motion::hoverInfoDelayMs)
            h = hovered;
        const bool wasShown = cardShown;
        const auto oldTitle = cardTitle, oldValues = cardValues;
        cardShown = h >= 0;
        if (cardShown)
        {
            cardTitle = cardTitleFor (h);
            cardValues = cardValueFor (h);
            cardAnchor = anchorFor (h);
            cardWidth = (float) std::max (juce::GlyphArrangement::getStringWidthInt (semiBold (9.0f, 0.1f), cardTitle),
                                          juce::GlyphArrangement::getStringWidthInt (monoFont (11.0f), cardValues)) + 24.0f;
        }
        return wasShown != cardShown || oldTitle != cardTitle || oldValues != cardValues;
    }

    void HandleGraph::paintCard (juce::Graphics& g) const
    {
        if (! cardShown)
            return;
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

    juce::PopupMenu HandleGraph::buildHandleMenu (int handle)
    {
        const auto ids = handle >= 0 && handle < numHandles() ? paramsOf (handle) : juce::StringArray();
        auto* p = ids.isEmpty() ? nullptr : param (ids[0]);
        if (p == nullptr)
            return {};
        juce::Component::SafePointer<HandleGraph> safe (this);
        return gestures::buildMenu (*p, findParentComponentOfClass<DetailPanel>(), [safe, handle] { if (safe != nullptr) safe->showValueEditor (handle); });
    }

    void HandleGraph::showValueEditor (int handle)
    {
        const auto ids = paramsOf (handle);
        auto* p = ids.isEmpty() ? nullptr : param (ids[0]);
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
        const auto id = ids[0];
        valueEditor->onReturnKey = [this, id]
        {
            if (auto* prm = param (id))
                if (const auto v = parseValueText (valueEditor->getText(), *prm))
                    gestures::setReal (*prm, *v);
            valueEditor->setVisible (false);
            parametersEdited();
            repaint();
        };
        valueEditor->setText (p->getCurrentValueAsText(), false);
        valueEditor->setBounds (juce::Rectangle<int> (84, 22).withCentre (anchorFor (handle).toInt().translated (0, -24)).constrainedWithin (getLocalBounds()));
        valueEditor->setVisible (true);
        valueEditor->selectAll();
        if (isShowing())
            valueEditor->grabKeyboardFocus();
    }

    void HandleGraph::beginDrag (int handle)
    {
        dragged = handle;
        for (const auto& id : paramsOf (handle))
            if (auto* p = param (id))
                p->beginChangeGesture();
        beginHandleDrag (handle);
    }

    void HandleGraph::endDrag()
    {
        if (dragged < 0)
            return;
        const auto ids = paramsOf (dragged);
        dragged = -1;
        // Reverse order: the outermost gesture ends last and records the step
        for (int i = ids.size(); --i >= 0;)
            if (auto* p = param (ids[i]))
                p->endChangeGesture();
    }

    void HandleGraph::resetHandle (int handle)
    {
        const auto ids = paramsOf (handle);
        for (const auto& id : ids)
            if (auto* p = param (id))
                p->beginChangeGesture();
        for (const auto& id : ids)
            if (auto* p = param (id))
                p->setValueNotifyingHost (p->getDefaultValue());
        for (int i = ids.size(); --i >= 0;)
            if (auto* p = param (ids[i]))
                p->endChangeGesture();
    }

    void HandleGraph::mouseMove (const juce::MouseEvent& e)
    {
        const int h = handleAt (e.position);
        if (h != hovered)
        {
            hovered = h;
            hoverSince = juce::Time::getMillisecondCounter();
            setMouseCursor (h >= 0 ? cursorFor (h) : juce::MouseCursor::NormalCursor);
            updateCard();
            repaint();
        }
    }

    void HandleGraph::mouseExit (const juce::MouseEvent&)
    {
        hovered = -1;
        updateCard();
        repaint();
    }

    void HandleGraph::mouseDown (const juce::MouseEvent& e)
    {
        endDrag();
        ignoreDrag = false;
        const int h = handleAt (e.position);
        if (e.mods.isPopupMenu())
        {
            ignoreDrag = true;
            if (h < 0)
            {
                // Empty space: the panel's own menu (reset module, presets ...)
                if (auto* parent = getParentComponent())
                    parent->mouseDown (e.getEventRelativeTo (parent));
                return;
            }
            buildHandleMenu (h).showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (
                localAreaToGlobal (juce::Rectangle<int> (1, 1).withCentre (e.position.toInt()))));
            return;
        }
        if (h < 0)
            return;
        if (e.mods.isAltDown())
        {
            ignoreDrag = true;
            resetHandle (h);
            parametersEdited();
            updateCard();
            repaint();
            return;
        }
        dragLast = e.position;
        beginDrag (h);
        updateCard();
        repaint();
    }

    void HandleGraph::mouseDrag (const juce::MouseEvent& e)
    {
        if (ignoreDrag || dragged < 0)
            return;
        // Relative movement: Shift / Ctrl switch to fine (1/10) at any moment without a jump
        const auto delta = e.position - dragLast;
        dragLast = e.position;
        if (delta.isOrigin())
            return;
        dragHandle (dragged, delta, fineMods (e.mods) ? 0.1f : 1.0f);
        parametersEdited();
        updateCard();
        repaint();
    }

    void HandleGraph::mouseUp (const juce::MouseEvent&)
    {
        endDrag();
        ignoreDrag = false;
        parametersEdited();
        updateCard();
        repaint();
    }

    void HandleGraph::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (e.mods.isPopupMenu() || e.mods.isAltDown())
            return;
        const int h = dragged >= 0 ? dragged : handleAt (e.position);
        if (h < 0)
            return;
        ignoreDrag = true;
        if (dragged == h)
        {
            // Inside the gesture the second click opened: the reset belongs to it (one step)
            for (const auto& id : paramsOf (h))
                if (auto* p = param (id))
                    p->setValueNotifyingHost (p->getDefaultValue());
        }
        else
        {
            resetHandle (h);
        }
        parametersEdited();
        updateCard();
        repaint();
    }

    void HandleGraph::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
    {
        const int h = wheelHandleAt (e.position);
        const float notches = wheelNotches (w);
        if (h < 0 || notches == 0.0f || dragged >= 0)
        {
            if (h < 0)
                ModuleView::mouseWheelMove (e, w);
            return;
        }
        wheelHandle (h, notches, fineMods (e.mods) ? 0.1f : 1.0f);
        parametersEdited();
        updateCard();
        repaint();
    }

    //==========================================================================
    // COMPRESSOR graph

    CompGraph::CompGraph (VelvoxAudioProcessor& p, int inst) : HandleGraph (p, inst, ModuleId::comp)
    {
        settingsKey.fill (-1.0e9f);
        parametersEdited();
    }

    juce::Rectangle<float> CompGraph::levelArea() const noexcept
    {
        auto r = getLocalBounds().toFloat().reduced (12.0f, 12.0f);
        const float side = std::max (40.0f, std::min (r.getHeight(), r.getWidth() * 0.38f));
        r.removeFromRight (side + 18.0f);
        return r;
    }

    juce::Rectangle<float> CompGraph::curveArea() const noexcept
    {
        const auto r = getLocalBounds().toFloat().reduced (12.0f, 12.0f);
        const float side = std::max (40.0f, std::min (r.getHeight(), r.getWidth() * 0.38f));
        return { r.getRight() - side, r.getY() + (r.getHeight() - side) * 0.5f, side, side };
    }

    float CompGraph::xForInput (float db) const noexcept
    {
        const auto c = curveArea();
        return juce::jmap (juce::jlimit (floorDb, 0.0f, db), floorDb, 0.0f, c.getX(), c.getRight());
    }

    float CompGraph::yForOutput (float db) const noexcept
    {
        const auto c = curveArea();
        return juce::jmap (juce::jlimit (floorDb, 0.0f, db), floorDb, 0.0f, c.getBottom(), c.getY());
    }

    juce::Point<float> CompGraph::kneePoint() const noexcept
    {
        return { xForInput (s.thresholdDb), yForOutput (dsp::CompressorModule::transferDb (s, s.thresholdDb)) };
    }

    juce::Point<float> CompGraph::segmentPoint() const noexcept
    {
        const float x = s.thresholdDb + 0.75f * (0.0f - s.thresholdDb);
        return { xForInput (x), yForOutput (dsp::CompressorModule::transferDb (s, x)) };
    }

    dsp::DynamicsFrame CompGraph::getFrame (int ago) const noexcept
    {
        if (ago < 0 || ago >= numFrames)
            return {};
        return frames[(size_t) ((head - 1 - ago + 2 * maxFrames) % maxFrames)];
    }

    int CompGraph::handleAt (juce::Point<float> p) const
    {
        const auto knee = kneePoint();
        if (knee.getDistanceFrom (p) <= hitRadius)
            return kneeHandle;
        const auto c = curveArea();
        if (! c.expanded (4.0f).contains (p) || p.x < knee.x + hitRadius)
            return -1;
        const float in = juce::jmap (p.x, c.getX(), c.getRight(), floorDb, 0.0f);
        if (in <= s.thresholdDb + std::max (1.0f, 0.5f * s.kneeDb))
            return -1;
        const float y = yForOutput (dsp::CompressorModule::transferDb (s, std::min (0.0f, in)));
        return std::abs (p.y - y) <= 8.0f ? ratioHandle : -1;
    }

    int CompGraph::wheelHandleAt (juce::Point<float> p) const
    {
        return curveArea().expanded (4.0f).contains (p) ? kneeWheel : -1;
    }

    juce::StringArray CompGraph::paramsOf (int handle) const
    {
        switch (handle)
        {
            case kneeHandle:  return { pidFor (pid::compThreshold) };
            case ratioHandle: return { pidFor (pid::compRatio) };
            case kneeWheel:   return { pidFor (pid::compKnee) };
            default:          return {};
        }
    }

    void CompGraph::beginHandleDrag (int handle)
    {
        dragValue = handle == kneeHandle ? paramValue (pidFor (pid::compThreshold))
                                         : std::log (std::max (1.0f, paramValue (pidFor (pid::compRatio))));
    }

    void CompGraph::dragHandle (int handle, juce::Point<float> delta, float scale)
    {
        if (handle == kneeHandle)
        {
            // Along the dominant axis: right or up = higher threshold
            const float d = std::abs (delta.x) >= std::abs (delta.y) ? delta.x : -delta.y;
            const float dbPerPx = -floorDb / std::max (1.0f, curveArea().getWidth());
            dragValue = juce::jlimit (floorDb, 0.0f, dragValue + d * dbPerPx * scale);
            writeParam (pidFor (pid::compThreshold), dragValue);
        }
        else if (handle == ratioHandle)
        {
            // Log scale, 220 px = 1:1 .. 20:1, down = more compression
            dragValue = juce::jlimit (0.0f, std::log (20.0f), dragValue + delta.y * std::log (20.0f) / 220.0f * scale);
            writeParam (pidFor (pid::compRatio), std::exp (dragValue));
        }
    }

    void CompGraph::wheelHandle (int handle, float notches, float fine)
    {
        if (handle == kneeWheel)
            stepParam (pidFor (pid::compKnee), paramValue (pidFor (pid::compKnee)) + notches * wheelKneeDb * fine);
    }

    juce::String CompGraph::cardTitleFor (int handle) const
    {
        return handle == kneeHandle ? "THRESHOLD" : handle == ratioHandle ? "RATIO" : "KNEE";
    }

    juce::String CompGraph::cardValueFor (int handle) const
    {
        if (handle == kneeHandle)
            return dbText (paramValue (pidFor (pid::compThreshold)));
        if (handle == ratioHandle)
            return ratioText (paramValue (pidFor (pid::compRatio)));
        return dbText (paramValue (pidFor (pid::compKnee)));
    }

    juce::Point<float> CompGraph::anchorFor (int handle) const
    {
        if (handle == kneeHandle)
            return kneePoint();
        if (handle == ratioHandle)
            return segmentPoint();
        return { curveArea().getCentreX(), curveArea().getY() + 20.0f };
    }

    juce::MouseCursor CompGraph::cursorFor (int handle) const
    {
        return handle == ratioHandle ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::DraggingHandCursor;
    }

    void CompGraph::readSettings (const dsp::InstanceSettings& b)
    {
        s = b.comp;
        on = b.compOn;
        const std::array<float, 6> key { s.thresholdDb, s.ratio, s.kneeDb, s.makeupDb + (s.autoMakeup ? 100.0f : 0.0f), on ? 1.0f : 0.0f, 0.0f };
        if (key != settingsKey)
        {
            settingsKey = key;
            rebuildCurve();
            rebuildLevels();   // the threshold line lives there too
            repaint();
        }
    }

    void CompGraph::parametersEdited()
    {
        readSettings (proc.readEffectiveSettings().instance (instance));
    }

    void CompGraph::resized()
    {
        rebuildLevels();
        rebuildCurve();
    }

    void CompGraph::rebuildCurve()
    {
        curve.clear();
        const auto c = curveArea();
        if (c.getWidth() < 4.0f)
            return;
        constexpr int n = 120;
        for (int i = 0; i <= n; ++i)
        {
            const float in = floorDb + (0.0f - floorDb) * (float) i / (float) n;
            const auto pt = juce::Point<float> (xForInput (in), yForOutput (dsp::CompressorModule::transferDb (s, in)));
            if (i == 0)
                curve.startNewSubPath (pt);
            else
                curve.lineTo (pt);
        }
    }

    void CompGraph::rebuildLevels()
    {
        for (auto* p : { &inputFill, &outputFill, &outputLine, &grFill, &grLine })
            p->clear();
        const auto r = levelArea();
        const int n = std::min (numFrames, framesShown);
        if (n < 2 || r.getWidth() < 4.0f)
            return;
        const float step = r.getWidth() / (float) (framesShown - 1);
        const auto yLevel = [&] (float db) { return juce::jmap (juce::jlimit (floorDb, 0.0f, db), floorDb, 0.0f, r.getBottom(), r.getY()); };
        const auto yGr = [&] (float db) { return r.getY() + r.getHeight() * juce::jlimit (0.0f, -floorDb, db) / -floorDb; };

        inputFill.startNewSubPath (r.getRight(), r.getBottom());
        outputFill.startNewSubPath (r.getRight(), r.getBottom());
        grFill.startNewSubPath (r.getRight(), r.getY());
        for (int i = 0; i < n; ++i)
        {
            const auto f = getFrame (i);
            const float x = r.getRight() - step * (float) i;
            inputFill.lineTo (x, yLevel (f.inputDb));
            outputFill.lineTo (x, yLevel (f.outputDb));
            grFill.lineTo (x, yGr (f.gainReductionDb));
            if (i == 0)
            {
                outputLine.startNewSubPath (x, yLevel (f.outputDb));
                grLine.startNewSubPath (x, yGr (f.gainReductionDb));
            }
            else
            {
                outputLine.lineTo (x, yLevel (f.outputDb));
                grLine.lineTo (x, yGr (f.gainReductionDb));
            }
        }
        const float xEnd = r.getRight() - step * (float) (n - 1);
        inputFill.lineTo (xEnd, r.getBottom());
        inputFill.closeSubPath();
        outputFill.lineTo (xEnd, r.getBottom());
        outputFill.closeSubPath();
        grFill.lineTo (xEnd, r.getY());
        grFill.closeSubPath();
    }

    void CompGraph::update (const LiveData& d)
    {
        readSettings (d.settings.instance (instance));
        bool changed = false;

        // Drain the feed. What queued up before this graph existed (panel closed) is stale - a full queue
        // keeps its OLDEST frames - so the first update discards it
        auto& module = proc.getChain().getCompressor (instance);
        const bool keep = primed;
        primed = true;
        for (int guard = 0; guard < 64; ++guard)
        {
            const int n = module.history.pull (scratch.data(), (int) scratch.size());
            for (int i = 0; keep && i < n; ++i)
            {
                frames[(size_t) head] = scratch[(size_t) i];
                head = (head + 1) % maxFrames;
                numFrames = std::min (maxFrames, numFrames + 1);
            }
            changed = changed || n > 0;
            if (n < (int) scratch.size())
                break;
        }
        if (changed)
            rebuildLevels();

        const float gr = on ? std::max (0.0f, module.currentGainReductionDb.load (std::memory_order_relaxed)) : 0.0f;
        if (std::abs (gr - grNow) > 0.01f || (gr == 0.0f) != (grNow == 0.0f))
        {
            grNow = gr;
            const auto text = (grNow >= 0.05f ? minus : juce::String()) + juce::String (grNow, 1);
            if (text != grText)
            {
                grText = text;
                changed = true;
            }
        }
        changed = updateCard() || changed;
        if (changed && paintDue (d.time))
            repaint();
    }

    void CompGraph::paint (juce::Graphics& g)
    {
        const auto r = levelArea();
        const auto c = curveArea();
        const float a = on ? 1.0f : 0.45f;
        const auto yLevel = [&] (float db) { return juce::jmap (juce::jlimit (floorDb, 0.0f, db), floorDb, 0.0f, r.getBottom(), r.getY()); };

        // ---- Level display
        dbGuides (g, r, { -6.0f, -12.0f, -18.0f, -24.0f, -30.0f, -36.0f, -42.0f, -48.0f }, yLevel);
        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (r.toNearestInt());
            g.setColour (colours::textFaint.withAlpha (0.34f * a));
            g.fillPath (inputFill);
            g.setGradientFill (juce::ColourGradient (hue.withAlpha (0.42f * a), 0.0f, r.getY() + r.getHeight() * 0.3f,
                                                     hue.withAlpha (0.06f * a), 0.0f, r.getBottom(), false));
            g.fillPath (outputFill);
            g.setColour (hue.interpolatedWith (juce::Colours::white, 0.3f).withAlpha (0.9f * a));
            g.strokePath (outputLine, juce::PathStrokeType (1.2f));
            g.setColour (grPink.withAlpha (0.16f * a));
            g.fillPath (grFill);
            g.setColour (grPink.withAlpha (0.95f * a));
            g.strokePath (grLine, juce::PathStrokeType (1.5f));

            // The threshold across the display
            const float ty = yLevel (s.thresholdDb);
            const float dashes[] { 4.0f, 4.0f };
            g.setColour (hue.withAlpha (0.55f * a));
            g.drawDashedLine (juce::Line<float> (r.getX(), ty, r.getRight(), ty), dashes, 2, 1.0f);
        }

        // Gain reduction read-out
        {
            auto box = juce::Rectangle<float> (r.getRight() - 120.0f, r.getY() + 4.0f, 112.0f, 26.0f);
            g.setColour (on && grNow >= 0.05f ? colours::text : colours::textFaint);
            g.setFont (display (22.0f));
            g.drawText (grText, box, juce::Justification::centredRight, false);
            g.setFont (semiBold (9.0f, 0.12f));
            g.setColour (grPink.withAlpha (0.9f * a));
            g.drawText (grLabel, box.translated (0.0f, 24.0f).withHeight (12.0f), juce::Justification::centredRight, false);
        }

        // ---- Transfer curve
        const auto frame = c.expanded (6.0f);
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillRoundedRectangle (frame, 8.0f);
        g.setColour (colours::lineSoft);
        g.drawRoundedRectangle (frame.reduced (0.5f), 8.0f, 1.0f);
        for (int i = 1; i < 5; ++i)
        {
            const float t = (float) i / 5.0f;
            g.setColour (colours::accent.withAlpha (0.035f));
            g.fillRect (juce::Rectangle<float> (c.getX() + c.getWidth() * t, c.getY(), 1.0f, c.getHeight()));
            g.fillRect (juce::Rectangle<float> (c.getX(), c.getY() + c.getHeight() * t, c.getWidth(), 1.0f));
        }
        g.setColour (colours::accent.withAlpha (0.08f));
        g.drawLine (c.getX(), c.getBottom(), c.getRight(), c.getY(), 1.0f);

        const auto knee = kneePoint();
        {
            const float dashes[] { 2.0f, 3.0f };
            g.setColour (hue.withAlpha (0.35f * a));
            g.drawDashedLine (juce::Line<float> (knee.x, knee.y, knee.x, c.getBottom()), dashes, 2, 1.0f);
        }
        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (c.expanded (2.0f).toNearestInt());
            const bool segHot = getDraggedHandle() == ratioHandle || (getDraggedHandle() < 0 && getHoveredHandle() == ratioHandle);
            g.setColour (hue.withAlpha (0.18f * a));
            g.strokePath (curve, juce::PathStrokeType (segHot ? 8.0f : 6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (hue.interpolatedWith (juce::Colours::white, segHot ? 0.45f : 0.25f).withAlpha (a));
            g.strokePath (curve, juce::PathStrokeType (segHot ? 2.4f : 1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Where the signal sits now
        if (on && numFrames > 0)
        {
            const float in = getFrame (0).inputDb;
            if (in > floorDb + 1.0f)
            {
                const auto dot = juce::Point<float> (xForInput (in), yForOutput (dsp::CompressorModule::transferDb (s, in)));
                drawGlow (g, dot, 12.0f, 0.5f, juce::Colours::white);
                g.setColour (juce::Colours::white);
                g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (dot));
            }
        }

        // The knee point (threshold handle)
        {
            const bool hot = getDraggedHandle() == kneeHandle || (getDraggedHandle() < 0 && getHoveredHandle() == kneeHandle);
            const float size = hot ? 14.0f : 11.0f;
            if (hot)
                drawGlow (g, knee, 18.0f, 0.4f * a, hue);
            g.setColour (colours::panelInset);
            g.fillEllipse (juce::Rectangle<float> (size + 3.0f, size + 3.0f).withCentre (knee));
            g.setColour (hue.interpolatedWith (juce::Colours::white, 0.25f).withAlpha (a));
            g.fillEllipse (juce::Rectangle<float> (size, size).withCentre (knee));
        }

        paintCard (g);
    }

    //==========================================================================
    // DE-ESSER graph

    DeEssGraph::DeEssGraph (VelvoxAudioProcessor& p, int inst) : HandleGraph (p, inst, ModuleId::deess)
    {
        spectrum = std::make_unique<SpectrumSource> (proc.getChain().getDeEsser (instance).spectrumTap, unusedTap);
        settingsKey.fill (-1.0e9f);
        parametersEdited();
    }

    juce::Rectangle<float> DeEssGraph::plotArea() const noexcept
    {
        return getLocalBounds().toFloat().withTrimmedTop (10.0f).withTrimmedBottom (4.0f).reduced (10.0f, 0.0f);
    }

    float DeEssGraph::xForFreq (float hz) const noexcept
    {
        return SpectrumView::xForFrequency (juce::jlimit (SpectrumSource::minHz, SpectrumSource::maxHz, hz), plotArea());
    }

    float DeEssGraph::freqForX (float x) const noexcept
    {
        const auto r = plotArea();
        const float t = (x - r.getX()) / std::max (1.0f, r.getWidth());
        return juce::jlimit (SpectrumSource::minHz, SpectrumSource::maxHz,
                             SpectrumSource::minHz * std::pow (SpectrumSource::maxHz / SpectrumSource::minHz, t));
    }

    float DeEssGraph::yForLevel (float db) const noexcept
    {
        const auto r = plotArea();
        return juce::jmap (juce::jlimit (bottomDb, topDb, db), bottomDb, topDb, r.getBottom(), r.getY());
    }

    float DeEssGraph::yForReduction (float db) const noexcept
    {
        const auto r = plotArea();
        return r.getY() + r.getHeight() * reductionDepth * juce::jlimit (0.0f, 1.0f, db / reductionScaleDb);
    }

    juce::Rectangle<float> DeEssGraph::bandArea() const noexcept
    {
        const auto r = plotArea();
        const float x0 = xForFreq (s.freq), x1 = std::max (x0 + 4.0f, xForFreq (bandHighHz));
        return { x0, r.getY(), std::min (x1, r.getRight()) - x0, r.getHeight() };
    }

    float DeEssGraph::bandShade() const noexcept
    {
        return juce::jlimit (0.0f, 1.0f, 0.08f + 0.92f * reduction.current / std::max (3.0f, s.rangeDb));
    }

    float DeEssGraph::shelfShare (float hz) const noexcept
    {
        if (s.mode == 1)
            return 1.0f;   // wideband: the whole voice dips
        const float t = (hz / std::max (1.0f, s.freq)) * (hz / std::max (1.0f, s.freq));
        return t * t / (1.0f + t * t);
    }

    float DeEssGraph::shownInputDb (float hz) const noexcept
    {
        const auto& curve = spectrum->getInput();
        const float pos = (float) (SpectrumSource::numPoints - 1) * std::log (juce::jlimit (SpectrumSource::minHz, SpectrumSource::maxHz, hz) / SpectrumSource::minHz)
                          / std::log (SpectrumSource::maxHz / SpectrumSource::minHz);
        const int i = juce::jlimit (0, SpectrumSource::numPoints - 2, (int) pos);
        const float t = juce::jlimit (0.0f, 1.0f, pos - (float) i);
        return curve[(size_t) i] + t * (curve[(size_t) i + 1] - curve[(size_t) i]);
    }

    float DeEssGraph::shownOutputDb (float hz) const noexcept
    {
        return shownInputDb (hz) - (s.listen ? 0.0f : reduction.current * shelfShare (hz));
    }

    int DeEssGraph::handleAt (juce::Point<float> p) const
    {
        return bandArea().expanded (6.0f, 0.0f).contains (p) ? 0 : -1;
    }

    juce::StringArray DeEssGraph::paramsOf (int handle) const
    {
        return handle == 0 ? juce::StringArray { pidFor (pid::deessFreq), pidFor (pid::deessRange) } : juce::StringArray();
    }

    void DeEssGraph::beginHandleDrag (int)
    {
        dragFreq = paramValue (pidFor (pid::deessFreq));
        dragRange = paramValue (pidFor (pid::deessRange));
    }

    void DeEssGraph::dragHandle (int, juce::Point<float> delta, float scale)
    {
        auto* f = param (pidFor (pid::deessFreq));
        if (f == nullptr)
            return;
        const auto fr = f->getNormalisableRange();
        const float ratio = SpectrumSource::maxHz / SpectrumSource::minHz;
        dragFreq = juce::jlimit (fr.start, fr.end, dragFreq * std::pow (ratio, delta.x * scale / std::max (1.0f, plotArea().getWidth())));
        const float dbPerPx = reductionScaleDb / std::max (1.0f, plotArea().getHeight() * reductionDepth);
        dragRange = juce::jlimit (0.0f, 24.0f, dragRange + delta.y * dbPerPx * scale);
        writeParam (pidFor (pid::deessFreq), dragFreq);
        writeParam (pidFor (pid::deessRange), dragRange);
    }

    void DeEssGraph::wheelHandle (int, float notches, float fine)
    {
        const auto id = pidFor (pid::deessFreq);
        stepParam (id, paramValue (id) * std::exp2 (notches * wheelOct * fine));
    }

    juce::String DeEssGraph::cardTitleFor (int) const { return i18n::tr ("S-BAND", "SIBILANCE BAND"); }

    juce::String DeEssGraph::cardValueFor (int) const
    {
        return hzText (paramValue (pidFor (pid::deessFreq))) + "   Range " + dbText (-paramValue (pidFor (pid::deessRange)));
    }

    juce::Point<float> DeEssGraph::anchorFor (int) const
    {
        const auto b = bandArea();
        return { b.getX() + 6.0f, b.getY() + 46.0f };
    }

    void DeEssGraph::parametersEdited()
    {
        const auto b = proc.readEffectiveSettings().instance (instance);
        s = b.deess;
        on = b.deessOn;
        freqText = hzText (s.freq);
        rebuildPaths();
        repaint();
    }

    void DeEssGraph::resized() { rebuildPaths(); }

    void DeEssGraph::rebuildPaths()
    {
        inputLine.clear();
        outputLine.clear();
        outputFill.clear();
        const auto r = plotArea();
        if (r.getWidth() < 4.0f)
            return;
        const auto& curve = spectrum->getInput();
        const auto x = [&] (int i) { return r.getX() + r.getWidth() * (float) i / (float) (SpectrumSource::numPoints - 1); };
        outputFill.startNewSubPath (r.getX(), r.getBottom());
        for (int i = 0; i < SpectrumSource::numPoints; ++i)
        {
            const float hz = SpectrumSource::frequencyAt ((float) i);
            const float in = curve[(size_t) i];
            const float out = in - (s.listen ? 0.0f : reduction.current * shelfShare (hz));
            if (i == 0)
            {
                inputLine.startNewSubPath (x (i), yForLevel (in));
                outputLine.startNewSubPath (x (i), yForLevel (out));
            }
            else
            {
                inputLine.lineTo (x (i), yForLevel (in));
                outputLine.lineTo (x (i), yForLevel (out));
            }
            outputFill.lineTo (x (i), yForLevel (out));
        }
        outputFill.lineTo (r.getRight(), r.getBottom());
        outputFill.closeSubPath();
    }

    void DeEssGraph::update (const LiveData& d)
    {
        bool changed = false;
        const auto b = d.settings.instance (instance);
        auto& module = proc.getChain().getDeEsser (instance);
        const float high = module.bandHighHz.load (std::memory_order_relaxed);
        const std::array<float, 6> key { b.deess.freq, b.deess.rangeDb, (float) b.deess.mode, b.deess.listen ? 1.0f : 0.0f, b.deessOn ? 1.0f : 0.0f, high };
        if (key != settingsKey)
        {
            settingsKey = key;
            s = b.deess;
            on = b.deessOn;
            bandHighHz = high;
            freqText = hzText (s.freq);
            changed = true;
        }

        // Spectrum and reduction, at most 30 times a second
        if (lastRead < 0.0 || d.time < lastRead || d.time - lastRead >= 1.0 / 30.0 - 1.0e-3)
        {
            lastRead = d.time;
            spectrum->update (proc.getSampleRate() > 0.0 ? proc.getSampleRate() : d.sampleRate);
            if (! haveSpectrum)
                for (float v : spectrum->getInput())
                    haveSpectrum = haveSpectrum || v > SpectrumSource::floorDb + 1.0f;
            const float target = on ? std::max (0.0f, module.currentReductionDb.load (std::memory_order_relaxed)) : 0.0f;
            if (target >= reduction.current)
                reduction.snap (target);
            else
                reduction.tick (0.3f);
            reduction.target = target;
            const auto text = reduction.current >= 0.05f ? minus + juce::String (reduction.current, 1) + " dB" : juce::String();
            if (text != reductionText)
                reductionText = text;
            changed = true;
        }
        if (changed)
            rebuildPaths();
        changed = updateCard() || changed;
        if (changed && paintDue (d.time))
            repaint();
    }

    void DeEssGraph::paint (juce::Graphics& g)
    {
        const auto r = plotArea();
        const float a = on ? 1.0f : 0.45f;
        SpectrumView::drawFrequencyGrid (g, r, true);
        {
            // The ends of the axis (20 Hz / 20 kHz) where they do not crowd the 50 / 10k labels
            g.setColour (colours::textFaint);
            g.setFont (medium (11.0f));
            const float labelY = r.getBottom() - 18.0f;
            if (SpectrumView::xForFrequency (50.0f, r) - r.getX() >= 36.0f)
                g.drawText ("20", juce::Rectangle<float> (r.getX() + 4.0f, labelY, 30.0f, 14.0f), juce::Justification::left, false);
            if (r.getRight() - (SpectrumView::xForFrequency (10000.0f, r) + 4.0f + 24.0f) >= 30.0f)
                g.drawText ("20k", juce::Rectangle<float> (r.getRight() - 30.0f, labelY, 26.0f, 14.0f), juce::Justification::right, false);
        }

        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (r.toNearestInt());
            // What leaves the de-esser (filled) over what comes in (grey line)
            g.setGradientFill (juce::ColourGradient (hue.withAlpha (0.30f * a), 0.0f, r.getY() + r.getHeight() * 0.3f,
                                                     hue.withAlpha (0.03f * a), 0.0f, r.getBottom(), false));
            g.fillPath (outputFill);
            g.setColour (hue.withAlpha (0.65f * a));
            g.strokePath (outputLine, juce::PathStrokeType (1.2f));
            g.setColour (colours::textMuted.withAlpha (0.55f * a));
            g.strokePath (inputLine, juce::PathStrokeType (1.0f));
        }

        // The S band: shaded by the reduction, the reduction bar hanging from the top, the Range as its floor
        const auto band = bandArea();
        const float shade = bandShade();
        const bool hot = getDraggedHandle() == 0 || (getDraggedHandle() < 0 && getHoveredHandle() == 0);
        g.setGradientFill (juce::ColourGradient (hue.withAlpha ((0.06f + 0.30f * shade) * a), 0.0f, band.getY(),
                                                 hue.withAlpha ((0.02f + 0.08f * shade) * a), 0.0f, band.getBottom(), false));
        g.fillRect (band);
        if (s.mode == 1)
        {
            // Wideband: the whole voice dips - a faint wash left of the band as well
            g.setColour (hue.withAlpha (0.10f * shade * a));
            g.fillRect (r.withRight (band.getX()));
        }
        const float redY = yForReduction (reduction.current), rangeY = yForReduction (s.rangeDb);
        const auto bar = juce::Rectangle<float>::leftTopRightBottom (band.getX() + 1.0f, band.getY(), band.getRight(), redY);
        g.setColour (hue.interpolatedWith (juce::Colours::white, 0.2f).withAlpha (0.42f * a));
        g.fillRect (bar);
        {
            const float dashes[] { 4.0f, 3.0f };
            g.setColour (hue.interpolatedWith (juce::Colours::white, 0.4f).withAlpha ((hot ? 0.95f : 0.6f) * a));
            g.drawDashedLine (juce::Line<float> (band.getX(), rangeY, band.getRight(), rangeY), dashes, 2, 1.2f);
        }
        g.setColour (hue.withAlpha ((hot ? 1.0f : 0.75f) * a));
        g.fillRect (juce::Rectangle<float> (band.getX() - 0.75f, band.getY(), 1.5f, band.getHeight()));
        g.fillRoundedRectangle (juce::Rectangle<float> (hot ? 7.0f : 5.0f, 24.0f).withCentre ({ band.getX(), band.getCentreY() }), 2.5f);

        // Read-outs: band start, live reduction
        g.setFont (monoFont (11.0f));
        g.setColour (colours::text.withAlpha (a));
        g.drawText (freqText, juce::Rectangle<float> (band.getX() + 8.0f, std::max (band.getY() + 4.0f, rangeY + 4.0f), 90.0f, 14.0f),
                    juce::Justification::centredLeft, false);
        if (reductionText.isNotEmpty())
        {
            g.setFont (display (20.0f));
            g.setColour (colours::text);
            g.drawText (reductionText, juce::Rectangle<float> (r.getRight() - 130.0f, r.getY() + 2.0f, 122.0f, 24.0f), juce::Justification::centredRight, false);
        }

        paintCard (g);
    }

    //==========================================================================
    // VOCAL SMASH stage

    SmashStage::SmashStage (VelvoxAudioProcessor& p, int inst) : proc (p), instance (inst)
    {
        setInterceptsMouseClicks (false, false);
    }

    void SmashStage::setLayout (juce::Rectangle<float> trace, juce::Rectangle<float> column)
    {
        traceArea = trace;
        columnArea = column;
        customLayout = true;
        rebuild();
        repaint();
    }

    void SmashStage::rebuild()
    {
        if (! customLayout)
        {
            auto r = getLocalBounds().toFloat().reduced (14.0f, 14.0f);
            columnArea = r.removeFromRight (40.0f);
            r.removeFromRight (16.0f);
            traceArea = r;
        }
        grFill.clear();
        grLine.clear();
        const auto t = traceArea;
        const int n = std::min (numFrames, framesShown);
        if (n < 2 || t.getWidth() < 4.0f)
            return;
        const float step = t.getWidth() / (float) (framesShown - 1);
        const auto y = [&] (float db) { return t.getY() + t.getHeight() * juce::jlimit (0.0f, scaleDb, db) / scaleDb; };
        grFill.startNewSubPath (t.getRight(), t.getY());
        for (int i = 0; i < n; ++i)
        {
            const float v = grFrames[(size_t) ((head - 1 - i + 2 * maxFrames) % maxFrames)];
            const float x = t.getRight() - step * (float) i;
            grFill.lineTo (x, y (v));
            if (i == 0)
                grLine.startNewSubPath (x, y (v));
            else
                grLine.lineTo (x, y (v));
        }
        grFill.lineTo (t.getRight() - step * (float) (n - 1), t.getY());
        grFill.closeSubPath();
    }

    void SmashStage::update (const LiveData& d)
    {
        on = d.settings.instance (instance).smash.enabled;
        auto& module = proc.getChain().getSmash (instance);
        bool changed = false;
        const bool keep = primed;   // the first update discards what queued up while no panel was open
        primed = true;
        for (int guard = 0; guard < 64; ++guard)
        {
            const int n = module.history.pull (scratch.data(), (int) scratch.size());
            for (int i = 0; keep && i < n; ++i)
            {
                grFrames[(size_t) head] = scratch[(size_t) i].gainReductionDb;
                head = (head + 1) % maxFrames;
                numFrames = std::min (maxFrames, numFrames + 1);
            }
            changed = changed || n > 0;
            if (n < (int) scratch.size())
                break;
        }
        if (changed)
            rebuild();

        const float target = on ? std::max (0.0f, module.currentGainReductionDb.load (std::memory_order_relaxed)) : 0.0f;
        const float before = gr.current;
        if (target >= gr.current)
            gr.snap (target);
        else
        {
            gr.target = target;
            gr.tick (0.25f);
        }
        if (gr.current >= peakDb || ! on)
        {
            peakDb = gr.current;
            peakTime = d.time;
        }
        else if (d.time - peakTime > peakHoldSeconds || d.time < peakTime)
        {
            peakDb = std::max (gr.current, peakDb - 0.6f);
        }
        const auto text = gr.current >= 0.05f ? minus + juce::String (gr.current, 1) : juce::String ("0.0");
        if (text != grText)
            grText = text;
        changed = changed || std::abs (before - gr.current) > 0.01f;
        if (changed && (lastPaint < 0.0 || d.time < lastPaint || d.time - lastPaint >= 1.0 / 30.0 - 1.0e-3))
        {
            lastPaint = d.time;
            repaint();
        }
    }

    void SmashStage::paint (juce::Graphics& g)
    {
        const auto hue = moduleColour (ModuleId::smash);
        const float a = on ? 1.0f : 0.45f;
        const auto t = traceArea;
        const auto yT = [&] (float db) { return t.getY() + t.getHeight() * juce::jlimit (0.0f, scaleDb, db) / scaleDb; };

        // Gain reduction over time, hanging from the top
        if (t.getWidth() > 20.0f)
        {
            g.setFont (medium (10.0f));
            for (float v : { 6.0f, 12.0f, 18.0f })
            {
                const float y = yT (v);
                g.setColour (colours::accent.withAlpha (0.035f));
                g.fillRect (juce::Rectangle<float> (t.getX(), y, t.getWidth(), 1.0f));
                g.setColour (colours::textFaint);
                g.drawText (dbScaleLabel (-juce::roundToInt (v)), juce::Rectangle<float> (t.getX() + 3.0f, y - 14.0f, 30.0f, 12.0f), juce::Justification::left, false);
            }
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (t.toNearestInt());
            g.setGradientFill (juce::ColourGradient (hue.withAlpha (0.26f * a), 0.0f, t.getY(), hue.withAlpha (0.03f * a), 0.0f, t.getBottom(), false));
            g.fillPath (grFill);
            g.setColour (hue.interpolatedWith (juce::Colours::white, 0.35f).withAlpha (0.95f * a));
            g.strokePath (grLine, juce::PathStrokeType (1.5f));
        }

        // The GR column with peak hold
        const auto col = columnArea;
        if (col.getHeight() > 20.0f)
        {
            auto bar = col.withTrimmedTop (22.0f).withTrimmedBottom (16.0f);
            g.setColour (colours::control);
            g.fillRoundedRectangle (bar, 5.0f);
            const auto yC = [&] (float db) { return bar.getY() + bar.getHeight() * juce::jlimit (0.0f, scaleDb, db) / scaleDb; };
            const auto lit = bar.withBottom (yC (gr.current));
            g.setGradientFill (juce::ColourGradient (hue.interpolatedWith (grPink, 0.35f).withAlpha (a), 0.0f, bar.getY(),
                                                     hue.withAlpha (0.7f * a), 0.0f, bar.getBottom(), false));
            g.fillRoundedRectangle (lit, 5.0f);
            if (peakDb > 0.1f)
            {
                g.setColour (colours::text.withAlpha (0.85f * a));
                g.fillRect (juce::Rectangle<float> (bar.getX(), yC (peakDb) - 1.0f, bar.getWidth(), 2.0f));
            }
            g.setFont (medium (9.5f));
            g.setColour (colours::textFaint);
            for (float v : { 6.0f, 12.0f, 18.0f, 24.0f })
                g.fillRect (juce::Rectangle<float> (bar.getX() - 4.0f, yC (v), 3.0f, 1.0f));
            g.setFont (monoFont (11.0f));
            g.setColour (on ? colours::text : colours::textFaint);
            g.drawText (grText, col.withHeight (16.0f).expanded (14.0f, 0.0f), juce::Justification::centred, false);
            g.setFont (semiBold (9.0f, 0.12f));
            g.setColour (colours::knobName);
            g.drawText (grLabel, col.withTop (col.getBottom() - 13.0f).expanded (10.0f, 0.0f), juce::Justification::centred, false);
        }
    }

    //==========================================================================
    // LEVEL RIDE word bars

    RideBars::RideBars (VelvoxAudioProcessor& p, int inst) : HandleGraph (p, inst, ModuleId::ride)
    {
        parametersEdited();
    }

    juce::Rectangle<float> RideBars::plotArea() const noexcept
    {
        return getLocalBounds().toFloat().reduced (12.0f, 12.0f).withTrimmedLeft (inset).withTrimmedTop (16.0f).withTrimmedRight (34.0f);
    }

    float RideBars::yForLevel (float db) const noexcept
    {
        const auto r = plotArea();
        return juce::jmap (juce::jlimit (bottomDb, topDb, db), bottomDb, topDb, r.getBottom(), r.getY());
    }

    float RideBars::xForSample (std::int64_t sample) const noexcept
    {
        const auto r = plotArea();
        const double age = (double) (now - sample) / std::max (1.0, fs);
        return r.getRight() - r.getWidth() * (float) (age / secondsShown);
    }

    dsp::RideSegment RideBars::getSegment (int ago) const noexcept
    {
        if (ago < 0 || ago >= numSegments)
            return {};
        return segments[(size_t) ((head - 1 - ago + 2 * maxSegments) % maxSegments)];
    }

    juce::Rectangle<float> RideBars::getSegmentRect (int ago, bool after) const noexcept
    {
        const auto seg = getSegment (ago);
        const float x0 = xForSample (seg.startSample), x1 = xForSample (seg.startSample + seg.lengthSamples);
        const float y = yForLevel (after ? seg.levelAfterDb : seg.levelBeforeDb);
        return juce::Rectangle<float>::leftTopRightBottom (x0, y, std::max (x0 + 1.0f, x1), plotArea().getBottom());
    }

    int RideBars::handleAt (juce::Point<float> p) const
    {
        return plotArea().expanded (0.0f, 6.0f).contains (p) && std::abs (p.y - targetY()) <= 6.0f ? 0 : -1;
    }

    juce::StringArray RideBars::paramsOf (int handle) const
    {
        return handle == 0 ? juce::StringArray { pidFor (pid::rideTarget) } : juce::StringArray();
    }

    void RideBars::beginHandleDrag (int) { dragTarget = paramValue (pidFor (pid::rideTarget)); }

    void RideBars::dragHandle (int, juce::Point<float> delta, float scale)
    {
        auto* p = param (pidFor (pid::rideTarget));
        if (p == nullptr)
            return;
        const auto range = p->getNormalisableRange();
        const float dbPerPx = (topDb - bottomDb) / std::max (1.0f, plotArea().getHeight());
        dragTarget = juce::jlimit (range.start, range.end, dragTarget - delta.y * dbPerPx * scale);
        writeParam (pidFor (pid::rideTarget), dragTarget);
    }

    void RideBars::wheelHandle (int, float notches, float fine)
    {
        const auto id = pidFor (pid::rideTarget);
        stepParam (id, paramValue (id) + notches * wheelDb * fine);
    }

    juce::String RideBars::cardTitleFor (int) const { return i18n::tr ("ZIEL", "TARGET"); }
    juce::String RideBars::cardValueFor (int) const { return dbText (paramValue (pidFor (pid::rideTarget))); }
    juce::Point<float> RideBars::anchorFor (int) const { return { plotArea().getCentreX(), targetY() }; }

    void RideBars::parametersEdited()
    {
        const auto b = proc.readEffectiveSettings().instance (instance);
        on = b.ride.enabled;
        target = b.ride.targetDb;
        targetText = dbText (target);
        repaint();
    }

    void RideBars::rebuild() {}

    void RideBars::update (const LiveData& d)
    {
        bool changed = false;
        const auto b = d.settings.instance (instance);
        if (b.ride.targetDb != target || b.ride.enabled != on)
        {
            target = b.ride.targetDb;
            on = b.ride.enabled;
            targetText = dbText (target);
            changed = true;
        }
        fs = proc.getSampleRate() > 0.0 ? proc.getSampleRate() : d.sampleRate;

        auto& module = proc.getChain().getRide (instance);
        for (int guard = 0; guard < 16; ++guard)
        {
            const int n = module.segments.pull (scratch.data(), (int) scratch.size());
            for (int i = 0; i < n; ++i)
            {
                segments[(size_t) head] = scratch[(size_t) i];
                head = (head + 1) % maxSegments;
                numSegments = std::min (maxSegments, numSegments + 1);
            }
            changed = changed || n > 0;
            if (n < (int) scratch.size())
                break;
        }
        const auto pos = module.samplePosition.load (std::memory_order_relaxed);
        if (pos != now)
        {
            now = pos;
            changed = true;
        }
        const float g = on ? module.currentGainDb.load (std::memory_order_relaxed) : 0.0f;
        gain.target = g;
        if (gain.tick (0.3f))
            changed = true;
        voice = on && module.voiceActive.load (std::memory_order_relaxed);
        liveLevel = module.detectedLevelDb.load (std::memory_order_relaxed);
        const auto text = dbText (gain.current, true);
        if (text != gainText)
            gainText = text;
        changed = updateCard() || changed;
        if (changed && paintDue (d.time))
            repaint();
    }

    void RideBars::paint (juce::Graphics& g)
    {
        const auto r = plotArea();
        const float a = on ? 1.0f : 0.45f;
        dbGuides (g, r, { -12.0f, -24.0f, -36.0f, -48.0f }, [this] (float db) { return yForLevel (db); }, true);

        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (r.toNearestInt());
            for (int i = 0; i < numSegments; ++i)
            {
                const auto seg = getSegment (i);
                if (xForSample (seg.startSample + seg.lengthSamples) < r.getX())
                    break;   // older ones are further left still
                const auto before = getSegmentRect (i, false).reduced (1.0f, 0.0f);
                const auto after = getSegmentRect (i, true).reduced (1.0f, 0.0f);
                if (before.getWidth() < 1.0f)
                    continue;
                g.setColour (colours::textFaint.withAlpha (0.30f * a));
                g.fillRoundedRectangle (before, 2.5f);
                g.setGradientFill (juce::ColourGradient (hue.withAlpha (0.85f * a), 0.0f, after.getY(),
                                                         hue.withAlpha (0.25f * a), 0.0f, r.getBottom(), false));
                g.fillRoundedRectangle (after, 2.5f);
                // The level before as a grey cap (visible above and inside the bar)
                g.setColour (colours::textSoft.withAlpha (0.75f * a));
                g.fillRect (juce::Rectangle<float> (before.getX(), before.getY() - 1.0f, before.getWidth(), 2.0f));
            }
            if (voice)
            {
                const float y = yForLevel (liveLevel + gain.current);
                g.setColour (hue.withAlpha (0.9f));
                g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ r.getRight() - 4.0f, y }));
            }
        }

        // Target line with its grip
        const float ty = targetY();
        const bool hot = getDraggedHandle() == 0 || (getDraggedHandle() < 0 && getHoveredHandle() == 0);
        {
            const float dashes[] { 6.0f, 4.0f };
            g.setColour (hue.withAlpha ((hot ? 1.0f : 0.8f) * a));
            g.drawDashedLine (juce::Line<float> (r.getX(), ty, r.getRight(), ty), dashes, 2, hot ? 2.0f : 1.4f);
            g.fillRoundedRectangle (juce::Rectangle<float> (22.0f, hot ? 8.0f : 6.0f).withCentre ({ r.getX() + 11.0f, ty }), 3.0f);
            g.setFont (monoFont (10.5f));
            g.drawText (targetText, juce::Rectangle<float> (r.getX() + 28.0f, ty - 16.0f, 80.0f, 13.0f), juce::Justification::centredLeft, false);
        }

        // Current ride gain
        g.setFont (display (20.0f));
        g.setColour (on ? colours::text : colours::textFaint);
        g.drawText (gainText, juce::Rectangle<float> (r.getRight() - 130.0f, getLocalBounds().toFloat().getY() + 4.0f, 126.0f, 22.0f),
                    juce::Justification::centredRight, false);

        paintCard (g);
    }

    //==========================================================================
    // Panels
    namespace
    {
        // COMPRESSOR (spec 5.2): Threshold, Ratio, Attack, Release; Mehr: Knee, Makeup + Auto, Mix, Detector, Auto release, SC filter
        class CompPanel final : public DetailPanel
        {
        public:
            explicit CompPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Compressor", help::shortDescription ((int) ModuleId::comp), pid::compOn)
            {
                auto& threshold = knob (pid::compThreshold, "Threshold");
                auto& ratio = knob (pid::compRatio, "Ratio");
                auto& attack = knob (pid::compAttack, "Attack");
                auto& release = knob (pid::compRelease, "Release");
                auto& detector = segments (pid::compDetector);
                setCaption (detector, i18n::tr ("Detektor", "Detector"));
                toggle (pid::compAutoRelease, "Auto Release");
                toggle (pid::compAutoMakeup, "Auto Makeup");
                knob (pid::compKnee, "Knee");
                knob (pid::compMakeup, "Makeup");
                knob (pid::compMix, "Mix");
                knob (pid::compScHpf, "SC Filter");
                setMainControls ({ &threshold, &ratio, &attack, &release });
            }
        };

        // DE-ESSER (spec 5.3): Threshold, Range, Mode, Listen; Mehr: Detection, Frequency (also the band in the graph)
        class DeEssPanel final : public DetailPanel
        {
        public:
            explicit DeEssPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "De-Esser", help::shortDescription ((int) ModuleId::deess), pid::deessOn)
            {
                auto& mode = segments (pid::deessMode, { "Split", "Wide" });
                setCaption (mode, i18n::tr ("Modus", "Mode"));
                auto& listen = toggle (pid::deessListen, i18n::tr ("Abhören", "Listen"));
                auto& detection = segments (pid::deessDetection);
                setCaption (detection, i18n::tr ("Erkennung", "Detection"));
                auto& threshold = knob (pid::deessThreshold, "Threshold");
                auto& range = knob (pid::deessRange, "Range");
                // Range is a reduction: shown as "−9.0 dB" (typed values count either way, "9" = "-9")
                range.slider.textFromValueFunction = [] (double v) { return dbText (-(float) v); };
                range.slider.valueFromTextFunction = [] (const juce::String& t)
                {
                    return std::abs (t.replaceCharacter (',', '.').retainCharacters ("0123456789.").getDoubleValue());
                };
                range.slider.updateText();
                knob (pid::deessFreq, i18n::tr ("Frequenz", "Frequency"));
                setMainControls ({ &threshold, &range, &mode, &listen });
            }
        };

        // VOCAL SMASH (spec 5.7): the big SMASH control between the GR history and the GR column; Blend, Tone, Output
        class SmashPanel final : public DetailPanel
        {
        public:
            explicit SmashPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Vocal Smash", help::shortDescription ((int) ModuleId::smash), pid::smashOn)
            {
                amount = &knob (pid::smashAmount, "Smash");
                knob (pid::smashAttack, "Attack");
                knob (pid::smashRelease, "Release");
                auto& blend = knob (pid::smashBlend, "Blend");
                auto& tone = knob (pid::smashTone, "Tone");
                auto& output = knob (pid::smashOutput, "Output");
                setMainControls ({ &blend, &tone, &output });
                placeInVisual ({ amount });
                amount->setProminence (Knob::Prominence::primary);
                amount->setDialSize (dialsize::smash);
            }

        private:
            void layoutVisual (juce::Rectangle<int> area) override
            {
                const auto a = area.reduced (14, 10);
                constexpr int captionH = 44;
                const int dial = juce::jlimit (120, 200, a.getHeight() - captionH - 20);
                const int column = 46;
                const int knobW = dial + 40;
                const int knobX = a.getRight() - column - 28 - knobW;
                amount->setDialSize (dial);
                amount->setBounds (juce::Rectangle<int> (knobX, a.getY(), knobW, a.getHeight()).withSizeKeepingCentre (knobW, dial + captionH));
                if (auto* stage = dynamic_cast<SmashStage*> (getVisual()))
                {
                    const auto origin = stage->getPosition().toFloat();
                    const auto local = a.toFloat().translated (-origin.x, -origin.y);
                    const auto col = juce::Rectangle<float> (local.getRight() - (float) column, local.getY(), (float) column, local.getHeight());
                    const auto trace = local.withRight (local.getX() + (float) (knobX - a.getX()) - 24.0f);
                    stage->setLayout (trace, col);
                }
            }

            Knob* amount = nullptr;
        };

        // LEVEL RIDE (spec 5.5, mockup "Ride 2"): the big Ausgleich next to the word bars; Target, Speed, Ignore Below
        class RidePanel final : public DetailPanel
        {
        public:
            explicit RidePanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Level Ride", help::shortDescription ((int) ModuleId::ride), pid::rideOn)
            {
                amount = &knob (pid::rideAmount, i18n::tr ("Ausgleich", "Leveling"));
                auto& target = knob (pid::rideTarget, i18n::tr ("Ziel", "Target"));
                auto& response = knob (pid::rideResponse, i18n::tr ("Tempo", "Speed"));
                auto& silence = knob (pid::rideSilence, i18n::tr ("Ignorieren unter", "Ignore Below"));
                knob (pid::rideMaxBoost, i18n::tr ("Max. Anheben", "Max Boost"));
                knob (pid::rideMaxCut, i18n::tr ("Max. Absenken", "Max Cut"));
                knob (pid::ridePhrase, "Phrase");
                knob (pid::rideTransient, i18n::tr ("Transienten", "Transients"));
                setMainControls ({ &target, &response, &silence });
                placeInVisual ({ amount });
                amount->setProminence (Knob::Prominence::primary);
            }

        private:
            void layoutVisual (juce::Rectangle<int> area) override
            {
                const auto a = area.reduced (12, 8);
                constexpr int captionH = 44;
                const int dial = juce::jlimit (90, 150, std::min (a.getHeight() - captionH - 30, a.getWidth() / 4));
                const int column = std::max (dial + 44, 150);
                amount->setDialSize (dial);
                amount->setBounds (a.withWidth (column).withSizeKeepingCentre (dial + 40, dial + captionH));
                if (auto* bars = dynamic_cast<RideBars*> (getVisual()))
                    bars->setPlotInset ((float) (column + 4));
            }

            Knob* amount = nullptr;
        };
    }

    std::unique_ptr<ModuleView> createDynView (VelvoxAudioProcessor& proc, ModuleId module, int instance)
    {
        switch (module)
        {
            case ModuleId::comp:  return std::make_unique<CompGraph> (proc, instance);
            case ModuleId::deess: return std::make_unique<DeEssGraph> (proc, instance);
            case ModuleId::smash: return std::make_unique<SmashStage> (proc, instance);
            case ModuleId::ride:  return std::make_unique<RideBars> (proc, instance);
            default:              return nullptr;
        }
    }

    std::unique_ptr<DetailPanel> createDynPanel (VelvoxAudioProcessor& proc, ModuleId module)
    {
        switch (module)
        {
            case ModuleId::comp:  return std::make_unique<CompPanel> (proc);
            case ModuleId::deess: return std::make_unique<DeEssPanel> (proc);
            case ModuleId::smash: return std::make_unique<SmashPanel> (proc);
            case ModuleId::ride:  return std::make_unique<RidePanel> (proc);
            default:              return nullptr;
        }
    }
}
