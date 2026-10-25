#include "ReverbPanel.h"
#include "../PluginProcessor.h"
#include "HelpContent.h"

namespace velvox::ui
{
    using dsp::ModuleId;

    namespace
    {
        juce::RangedAudioParameter* param (VelvoxAudioProcessor& proc, const char* base, int instance)
        {
            auto& state = proc.getState();
            if (instance > 0)
                if (auto* p = state.getParameter (dsp::instanceParamId (base, instance)))
                    return p;
            return state.getParameter (base);
        }

        float realValue (VelvoxAudioProcessor& proc, const char* base, int instance)
        {
            auto* p = param (proc, base, instance);
            return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f;
        }

        // The three parameters the depth moves, in gesture order
        constexpr const char* depthParams[3] { pid::reverbMix, pid::reverbPreDelay, pid::reverbSize };
    }

    //==========================================================================
    namespace reverbedit
    {
        float depth (VelvoxAudioProcessor& proc, int instance)
        {
            return dsp::reverbDepthFromSettings (juce::roundToInt (realValue (proc, pid::reverbType, instance)),
                                                 realValue (proc, pid::reverbMix, instance), realValue (proc, pid::reverbPreDelay, instance),
                                                 realValue (proc, pid::reverbSize, instance));
        }

        dsp::ReverbDepthPoint pointFor (VelvoxAudioProcessor& proc, int instance, float d)
        {
            return dsp::reverbDepthPath (juce::roundToInt (realValue (proc, pid::reverbType, instance)), d);
        }

        void beginDepthGesture (VelvoxAudioProcessor& proc, int instance)
        {
            for (auto* id : depthParams)
                if (auto* p = param (proc, id, instance))
                    p->beginChangeGesture();
        }

        void writeDepth (VelvoxAudioProcessor& proc, int instance, float d)
        {
            const auto point = pointFor (proc, instance, d);
            const float values[3] { point.mixPercent, point.preDelayMs, point.sizePercent };
            for (int i = 0; i < 3; ++i)
                if (auto* p = param (proc, depthParams[i], instance))
                {
                    const float v = p->convertTo0to1 (values[i]);
                    if (std::abs (v - p->getValue()) > 1.0e-7f)
                        p->setValueNotifyingHost (v);
                }
        }

        void endDepthGesture (VelvoxAudioProcessor& proc, int instance)
        {
            for (auto* id : depthParams)
                if (auto* p = param (proc, id, instance))
                    p->endChangeGesture();
        }

        void setDepth (VelvoxAudioProcessor& proc, int instance, float d)
        {
            beginDepthGesture (proc, instance);
            writeDepth (proc, instance, d);
            endDepthGesture (proc, instance);
        }

        void resetDepth (VelvoxAudioProcessor& proc, int instance)
        {
            beginDepthGesture (proc, instance);
            for (auto* id : depthParams)
                if (auto* p = param (proc, id, instance))
                    p->setValueNotifyingHost (p->getDefaultValue());
            endDepthGesture (proc, instance);
        }
    }

    //==========================================================================
    // The room
    namespace
    {
        // Room proportions per type at Size 100 %: front opening (fraction of the view) and back-wall
        // scale (smaller = deeper). Open spaces draw their ceiling and front pillars only faintly.
        struct Shape
        {
            float w, h, back;
            bool open;
        };

        constexpr Shape shapes[8] {
            { 0.60f, 0.80f, 0.54f, false },   // Room: compact, close
            { 0.80f, 0.64f, 0.58f, false },   // Plate: wide and shallow, low
            { 0.82f, 0.92f, 0.34f, false },   // Hall
            { 0.94f, 0.98f, 0.25f, false },   // Large Hall
            { 0.54f, 0.70f, 0.60f, false },   // Ambience: small studio air
            { 0.96f, 1.00f, 0.20f, true },    // Atmosphere: huge, open
            { 0.92f, 0.90f, 0.21f, true },    // Cinematic: deep, dark
            { 0.96f, 1.00f, 0.23f, true },    // Ethereal: wide, floating
        };

        constexpr float rippleFlatten = 0.24f;   // floor ellipses: height / width
    }

    class ReverbRoom::DotHandle final : public juce::Component
    {
    public:
        explicit DotHandle (ReverbRoom& r) : room (r)
        {
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            info = std::make_unique<HoverInfo> (*this, [] { return juce::String ("Depth"); },
                                                [this] { return room.getReadout(); },
                                                [] { return juce::String::fromUTF8 ("Drag back for more space: Mix, Pre-Delay and Size move together."); });
        }
        void refreshInfo() { info->refresh(); }

        void mouseDown (const juce::MouseEvent& e) override { room.mouseDown (e.getEventRelativeTo (&room)); }
        void mouseDrag (const juce::MouseEvent& e) override { room.mouseDrag (e.getEventRelativeTo (&room)); }
        void mouseUp (const juce::MouseEvent& e) override { room.mouseUp (e.getEventRelativeTo (&room)); }
        void mouseDoubleClick (const juce::MouseEvent& e) override { room.mouseDoubleClick (e.getEventRelativeTo (&room)); }
        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override { room.mouseWheelMove (e.getEventRelativeTo (&room), w); }

    private:
        ReverbRoom& room;
        std::unique_ptr<HoverInfo> info;
    };

    ReverbRoom::ReverbRoom (VelvoxAudioProcessor& p, int inst) : proc (p), instance (inst)
    {
        handle = std::make_unique<DotHandle> (*this);
        addAndMakeVisible (*handle);
        readParameters();
    }

    ReverbRoom::~ReverbRoom()
    {
        if (dragging)
            reverbedit::endDepthGesture (proc, instance);
    }

    juce::Component& ReverbRoom::getDotHandle() noexcept { return *handle; }

    void ReverbRoom::readParameters()
    {
        type = juce::jlimit (0, 7, juce::roundToInt (realValue (proc, pid::reverbType, instance)));
        mix = realValue (proc, pid::reverbMix, instance);
        preDelay = realValue (proc, pid::reverbPreDelay, instance);
        size = realValue (proc, pid::reverbSize, instance);
        const std::array<float, 4> key { (float) type, mix, preDelay, size };
        if (key == depthKey)
            return;
        depthKey = key;
        paramDepth = dsp::reverbDepthFromSettings (type, mix, preDelay, size);
        readout = "Mix " + juce::String (juce::roundToInt (mix)) + juce::String::fromUTF8 (" % · Pre ")
                + juce::String (juce::roundToInt (preDelay)) + juce::String::fromUTF8 (" ms · Size ")
                + juce::String (juce::roundToInt (size)) + " %";
    }

    float ReverbRoom::perspective (float t) const noexcept
    {
        const float back = std::max (0.05f, geo.front.isEmpty() ? 1.0f : geo.back.getWidth() / geo.front.getWidth());
        return 1.0f / (1.0f + juce::jlimit (0.0f, 1.0f, t) * (1.0f / back - 1.0f));
    }

    juce::Point<float> ReverbRoom::floorPoint (float t, float across) const noexcept
    {
        const float s = perspective (t);
        const auto& vp = geo.vanishing;
        const float x = geo.front.getCentreX() + across * geo.front.getWidth() * 0.5f;
        return { vp.x + (x - vp.x) * s, vp.y + (geo.front.getBottom() - vp.y) * s };
    }

    void ReverbRoom::layoutRoom()
    {
        const auto v = getLocalBounds().toFloat().reduced (14.0f, 12.0f);
        const auto& shape = shapes[(size_t) type];
        const float s = juce::jlimit (0.0f, 1.0f, size * 0.01f);
        const float w = shape.w * (0.72f + 0.28f * s), h = shape.h * (0.78f + 0.22f * s);
        const float back = std::min (0.8f, shape.back + 0.22f * (1.0f - s));
        open = shape.open;

        geo.front = juce::Rectangle<float> (v.getWidth() * w, v.getHeight() * h).withCentre (v.getCentre());
        geo.vanishing = { geo.front.getCentreX(), geo.front.getY() + geo.front.getHeight() * 0.42f };
        const auto toBack = [&] (juce::Point<float> p) { return geo.vanishing + (p - geo.vanishing) * back; };
        geo.back = juce::Rectangle<float> (toBack (geo.front.getTopLeft()), toBack (geo.front.getBottomRight()));

        floorPath.clear();
        floorPath.startNewSubPath (geo.front.getBottomLeft());
        floorPath.lineTo (geo.front.getBottomRight());
        floorPath.lineTo (geo.back.getBottomRight());
        floorPath.lineTo (geo.back.getBottomLeft());
        floorPath.closeSubPath();
        backgroundValid = false;
    }

    void ReverbRoom::resized()
    {
        layoutRoom();
        roomKey[0] = -1.0f;   // the dot follows in update()
        dot = floorPoint (depthToT (shownDepth), 0.0f);
    }

    void ReverbRoom::update (const LiveData& d)
    {
        const auto inst = d.settings.instance (instance);
        heard = inst.reverb;
        const bool nowOn = inst.reverbOn;
        bool changed = false;

        const auto oldKey = depthKey;
        readParameters();
        changed = changed || oldKey != depthKey;

        const std::array<float, 8> key { (float) type, size, heard.decaySeconds, heard.dampingHz, heard.bassMultiplier,
                                         heard.lowCutHz, heard.highCutHz, nowOn ? 1.0f : 0.0f };
        if (key != roomKey || nowOn != on)
        {
            roomKey = key;
            on = nowOn;
            layoutRoom();
            const double sr = d.sampleRate > 0.0 ? d.sampleRate : 48000.0;
            for (int b = 0; b < numDecayBands; ++b)
                decayBands[(size_t) b] = dsp::ReverbModule::predictedRt60 (heard, decayBandHz (b), sr);
            for (size_t i = 0; i < decayCurve.size(); ++i)
            {
                const float hz = 100.0f * std::pow (100.0f, (float) i / (float) (decayCurve.size() - 1));   // 100 Hz .. 10 kHz
                decayCurve[i] = dsp::ReverbModule::predictedRt60 (heard, hz, sr);
            }
            changed = true;
        }

        // Dot: the drag value while dragging, else the parameters
        const float depthNow = dragging ? dragDepth : paramDepth;
        if (std::abs (depthNow - shownDepth) > 1.0e-5f || changed)
        {
            shownDepth = depthNow;
            depthText = juce::String (juce::roundToInt (shownDepth * 100.0f)) + " %";
            changed = true;
        }
        const float t = depthToT (shownDepth);
        dotRadius = 4.0f + 9.0f * perspective (t);
        dot = floorPoint (t, 0.0f).translated (0.0f, -dotRadius * 0.9f);
        const int hs = juce::roundToInt (std::max (28.0f, dotRadius * 2.0f + 16.0f));
        const auto hb = juce::Rectangle<int> (hs, hs).withCentre (dot.roundToInt());
        if (handle->getBounds() != hb)
            handle->setBounds (hb);
        handle->refreshInfo();

        // Live: the vocal's level makes the dot glow; onsets send reflections rippling out over the floor
        const float dt = lastTime < 0.0 ? 0.0f : (float) juce::jlimit (0.0, 0.1, d.time - lastTime);
        lastTime = d.time;
        const float newLevel = on && d.hasSignal ? juce::jlimit (0.0f, 1.0f, juce::jmap (d.inputDb.ago (0), -50.0f, -6.0f, 0.0f, 1.0f)) : 0.0f;
        duckGain = juce::Decibels::decibelsToGain (-d.reverbDuck.ago (0));
        const float life = juce::jlimit (0.4f, 5.0f, heard.decaySeconds * 0.8f);
        bool animating = false;
        for (auto& r : ripples)
            if (r.age >= 0.0f)
            {
                r.age += dt;
                if (r.age > life)
                    r.age = -1.0f;
                else
                    animating = true;
            }
        const bool onset = newLevel > 0.25f && (newLevel > levelSlow + 0.12f || d.time - lastSpawn > std::max (0.35, 0.25 * heard.decaySeconds));
        if (on && onset && d.time - lastSpawn > 0.12)
        {
            auto* slot = &ripples[0];
            for (auto& r : ripples)
                if (r.age < 0.0f || r.age > slot->age)
                {
                    slot = &r;
                    if (r.age < 0.0f)
                        break;
                }
            slot->age = 0.0f;
            slot->strength = newLevel;
            lastSpawn = d.time;
            animating = true;
        }
        levelSlow += (newLevel - levelSlow) * 0.08f;
        animating = animating || std::abs (newLevel - level) > 0.01f;
        level = newLevel;

        // Parameter changes at once; animation at most 30 frames per second
        if (changed || (animating && d.time - lastPaint >= 1.0 / 30.0 - 1.0e-3))
        {
            lastPaint = d.time;
            repaint();
        }
    }

    //==========================================================================
    // Drawing

    void ReverbRoom::renderBackground (float scale)
    {
        const int w = std::max (1, juce::roundToInt ((float) getWidth() * scale)), h = std::max (1, juce::roundToInt ((float) getHeight() * scale));
        background = juce::Image (juce::Image::ARGB, w, h, true);
        backgroundScale = scale;
        backgroundValid = true;
        juce::Graphics g (background);
        g.addTransform (juce::AffineTransform::scale (scale));

        const auto hue = moduleColour (ModuleId::reverb);
        const float a = on ? 1.0f : 0.45f;
        const auto line = colours::textFaint;
        const auto& f = geo.front;
        const auto& b = geo.back;

        // Light at the back of the room (the space), floor fading towards the viewer
        g.setGradientFill (juce::ColourGradient (hue.withAlpha (0.09f * a), b.getCentreX(), b.getCentreY(),
                                                 hue.withAlpha (0.0f), b.getCentreX() + f.getWidth() * 0.45f, b.getCentreY(), true));
        g.fillRect (f);
        g.setColour (hue.withAlpha (0.035f * a));
        g.fillRect (b);
        g.setGradientFill (juce::ColourGradient (hue.withAlpha (0.07f * a), 0.0f, b.getBottom(), hue.withAlpha (0.01f * a), 0.0f, f.getBottom(), false));
        g.fillPath (floorPath);

        // Floor grid: lines towards the back and depth lines in perspective
        g.setColour (line.withAlpha (0.16f * a));
        for (int i = -3; i <= 3; ++i)
        {
            const float across = (float) i / 3.0f;
            const auto p0 = floorPoint (0.0f, across), p1 = floorPoint (1.0f, across);
            g.drawLine ({ p0, p1 }, 0.8f);
        }
        for (float t : { 0.2f, 0.4f, 0.6f, 0.8f })
            g.drawLine ({ floorPoint (t, -1.0f), floorPoint (t, 1.0f) }, 0.8f);

        // Walls: edges from the front corners to the back wall, back wall outline
        const float ceiling = open ? 0.10f : 0.26f;
        g.setColour (line.withAlpha (0.55f * a));
        g.drawLine ({ f.getBottomLeft(), b.getBottomLeft() }, 1.0f);
        g.drawLine ({ f.getBottomRight(), b.getBottomRight() }, 1.0f);
        g.drawLine ({ b.getBottomLeft(), b.getBottomRight() }, 1.0f);
        g.setColour (line.withAlpha (0.40f * a));
        g.drawLine ({ b.getBottomLeft(), b.getTopLeft() }, 1.0f);
        g.drawLine ({ b.getBottomRight(), b.getTopRight() }, 1.0f);
        g.setColour (line.withAlpha (ceiling * a));
        g.drawLine ({ b.getTopLeft(), b.getTopRight() }, 1.0f);
        g.drawLine ({ f.getTopLeft(), b.getTopLeft() }, 1.0f);
        g.drawLine ({ f.getTopRight(), b.getTopRight() }, 1.0f);
        g.setColour (line.withAlpha ((open ? 0.06f : 0.14f) * a));
        g.drawLine ({ f.getBottomLeft(), f.getTopLeft() }, 1.0f);
        g.drawLine ({ f.getBottomRight(), f.getTopRight() }, 1.0f);
        g.setColour (line.withAlpha (0.30f * a));
        g.drawLine ({ f.getBottomLeft(), f.getBottomRight() }, 1.0f);

        // Decay over frequency (100 Hz .. 10 kHz, log) as a faint profile on the back wall
        float maxRt = 0.1f;
        for (auto v : decayCurve)
            maxRt = std::max (maxRt, v);
        const auto area = b.reduced (b.getWidth() * 0.08f, b.getHeight() * 0.14f);
        juce::Path curve, fill;
        for (size_t i = 0; i < decayCurve.size(); ++i)
        {
            const float x = area.getX() + area.getWidth() * (float) i / (float) (decayCurve.size() - 1);
            const float y = area.getBottom() - area.getHeight() * 0.7f * decayCurve[i] / maxRt;
            if (i == 0)
            {
                curve.startNewSubPath (x, y);
                fill.startNewSubPath (x, area.getBottom());
            }
            else
                curve.lineTo (x, y);
            fill.lineTo (x, y);
        }
        fill.lineTo (area.getRight(), area.getBottom());
        fill.closeSubPath();
        g.setColour (hue.withAlpha (0.035f * a));
        g.fillPath (fill);
        g.setColour (hue.withAlpha (0.30f * a));
        g.strokePath (curve, juce::PathStrokeType (1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void ReverbRoom::paint (juce::Graphics& g)
    {
        const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
        if (! backgroundValid || std::abs (scale - backgroundScale) > 1.0e-3f || background.isNull())
            renderBackground (scale);
        g.drawImageTransformed (background, juce::AffineTransform::scale (1.0f / backgroundScale));

        const auto hue = moduleColour (ModuleId::reverb);
        const float a = on ? 1.0f : 0.45f;
        const float wet = juce::jlimit (0.0f, 1.0f, mix / 60.0f);
        const float sAtDot = perspective (depthToT (shownDepth));
        const auto base = floorPoint (depthToT (shownDepth), 0.0f);

        // Reflections rippling out over the floor (as long as the tail rings, quieter when ducked)
        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (floorPath);
            const float life = juce::jlimit (0.4f, 5.0f, heard.decaySeconds * 0.8f);
            const float reach = geo.front.getWidth() * 0.5f * sAtDot * (0.5f + 0.5f * size * 0.01f);
            for (const auto& r : ripples)
                if (r.age >= 0.0f)
                {
                    const float k = r.age / life;
                    const float rx = reach * (0.06f + 0.94f * std::sqrt (k));
                    const float alpha = 0.55f * r.strength * (0.25f + 0.75f * wet) * (1.0f - k) * (1.0f - k) * duckGain * a;
                    g.setColour (hue.withAlpha (alpha));
                    g.drawEllipse (juce::Rectangle<float> (rx * 2.0f, rx * 2.0f * rippleFlatten).withCentre (base), 1.0f);
                }
        }

        // The space around the vocal: wider and brighter with more reverb
        drawGlow (g, dot, (40.0f + 90.0f * wet) * (0.5f + 0.5f * sAtDot), (0.06f + 0.16f * wet) * a * (0.6f + 0.4f * duckGain), hue);

        // Its reflection on the floor, then the dot (glowing with the vocal's level)
        g.setColour (hue.withAlpha (0.22f * a));
        g.fillEllipse (juce::Rectangle<float> (dotRadius * 2.4f, dotRadius * 2.4f * rippleFlatten).withCentre (base));
        drawGlow (g, dot, dotRadius * (2.6f + 1.4f * level), (0.30f + 0.45f * level) * a, hue);
        g.setColour (hue.withAlpha (a));
        g.fillEllipse (juce::Rectangle<float> (dotRadius * 2.0f, dotRadius * 2.0f).withCentre (dot));
        g.setColour (juce::Colours::white.withAlpha ((0.55f + 0.4f * level) * a));
        g.fillEllipse (juce::Rectangle<float> (dotRadius * 0.9f, dotRadius * 0.9f).withCentre (dot));

        // Read-outs: depth always, the three values while dragging
        const auto r = getLocalBounds().toFloat().reduced (14.0f, 10.0f);
        g.setFont (semiBold (9.0f, 0.12f));
        g.setColour (colours::textFaint);
        g.drawText ("DEPTH", r.withSize (44.0f, 14.0f), juce::Justification::centredLeft, false);
        g.setFont (monoFont (11.0f));
        g.setColour (on ? colours::text : colours::textMuted);
        g.drawText (depthText, r.withSize (60.0f, 14.0f).translated (44.0f, 0.0f), juce::Justification::centredLeft, false);
        if (dragging)
        {
            g.setColour (colours::textMuted);
            g.drawText (readout, r.withSize (260.0f, 14.0f).translated (0.0f, 16.0f), juce::Justification::centredLeft, false);
        }
    }

    //==========================================================================
    // Gestures

    juce::PopupMenu ReverbRoom::buildMenu()
    {
        juce::Component::SafePointer<ReverbRoom> safe (this);
        const auto to = [safe] (float d)
        {
            return [safe, d] { if (safe != nullptr) { reverbedit::setDepth (safe->proc, safe->instance, d); safe->readParameters(); safe->repaint(); } };
        };
        juce::PopupMenu m;
        m.addSectionHeader ("Depth");
        m.addItem ("Vorne", to (0.1f));
        m.addItem ("Mitte", to (0.5f));
        m.addItem ("Hinten", to (0.9f));
        m.addItem ("Standardwerte", [safe] { if (safe != nullptr) { reverbedit::resetDepth (safe->proc, safe->instance); safe->repaint(); } });
        if (auto* panel = findParentComponentOfClass<DetailPanel>())
        {
            m.addSeparator();
            panel->addPanelItems (m);
        }
        return m;
    }

    float ReverbRoom::currentPointerY (const juce::MouseEvent& e) const { return e.position.y; }

    void ReverbRoom::mouseDown (const juce::MouseEvent& e)
    {
        ignoreDrag = false;
        if (e.mods.isPopupMenu())
        {
            ignoreDrag = true;
            buildMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (handle.get()));
            return;
        }
        if (e.mods.isAltDown())
        {
            ignoreDrag = true;
            reverbedit::resetDepth (proc, instance);
            readParameters();
            repaint();
            return;
        }
        if (dragging)
            return;
        dragging = true;
        dragDepth = paramDepth;
        lastPointerY = currentPointerY (e);
        reverbedit::beginDepthGesture (proc, instance);
    }

    void ReverbRoom::mouseDrag (const juce::MouseEvent& e)
    {
        if (! dragging || ignoreDrag)
            return;
        const float y = currentPointerY (e);
        const float dy = lastPointerY - y;
        lastPointerY = y;
        if (dy == 0.0f)
            return;
        const bool fine = e.mods.isShiftDown() || e.mods.isCtrlDown() || e.mods.isCommandDown();
        dragDepth = juce::jlimit (0.0f, 1.0f, dragDepth + dy / (pixelsPerDepth * (fine ? 10.0f : 1.0f)));
        reverbedit::writeDepth (proc, instance, dragDepth);
        shownDepth = dragDepth;
        depthText = juce::String (juce::roundToInt (shownDepth * 100.0f)) + " %";
        readParameters();
        repaint();
    }

    void ReverbRoom::mouseUp (const juce::MouseEvent&)
    {
        if (! dragging)
            return;
        dragging = false;
        reverbedit::endDepthGesture (proc, instance);
        readParameters();
        repaint();
    }

    void ReverbRoom::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (e.mods.isPopupMenu())
            return;
        // Inside the gesture the second click opened: the reset belongs to it (one step)
        ignoreDrag = true;
        reverbedit::resetDepth (proc, instance);
        readParameters();
        repaint();
    }

    void ReverbRoom::stepDepth (float delta)
    {
        reverbedit::setDepth (proc, instance, juce::jlimit (0.0f, 1.0f, paramDepth + delta));
        readParameters();
        repaint();
    }

    void ReverbRoom::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
    {
        if (dragging)
            return;
        const float d = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? -wheel.deltaX : wheel.deltaY;
        if (d == 0.0f)
            return;
        const float notches = juce::jlimit (-3.0f, 3.0f, (wheel.isReversed ? -d : d) / 0.11f);
        const bool fine = e.mods.isShiftDown() || e.mods.isCtrlDown() || e.mods.isCommandDown();
        stepDepth (notches * wheelStep * (fine ? 0.1f : 1.0f));
    }

    std::unique_ptr<ModuleView> createReverbRoom (VelvoxAudioProcessor& proc, int instance)
    {
        return std::make_unique<ReverbRoom> (proc, instance);
    }

    //==========================================================================
    namespace
    {
        // REVERB (spec 5.12): the room with the depth dot; Room, Size, Decay, Mix; everything else in Mehr
        class ReverbPanel final : public DetailPanel
        {
        public:
            explicit ReverbPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Reverb", help::shortDescription ((int) ModuleId::reverb), pid::reverbOn)
            {
                auto& type = choice (pid::reverbType, {});
                setCaption (type, "Room");
                type.box.setScrollWheelEnabled (true);
                segments (pid::duckSource, { "Vocal", "Sidechain" });
                auto& size = knob (pid::reverbSize, "Size");
                auto& decay = knob (pid::reverbDecay, "Decay");
                auto& mix = knob (pid::reverbMix, "Mix");
                knob (pid::reverbPreDelay, "Pre-Delay");
                knob (pid::reverbLowCut, "Low Cut");
                knob (pid::reverbHighCut, "High Cut");
                knob (pid::reverbDamping, "Damping");
                knob (pid::reverbBass, "Bass Decay");
                knob (pid::reverbWidth, "Width");
                knob (pid::reverbEarly, "Early");
                knob (pid::reverbDiffusion, "Diffusion");
                knob (pid::reverbMod, "Movement");
                knob (pid::reverbDuckAmount, "Ducking");
                knob (pid::reverbDuckThreshold, "Duck Thresh");
                knob (pid::reverbDuckAttack, "Duck Attack");
                knob (pid::reverbDuckRelease, "Duck Release");
                setMainControls ({ &type, &size, &decay, &mix });
                setMixKnob (&mix);
                gr.setTooltip ("Ducking (dB)");
                placeInVisual ({ &gr });
                gr.setVisible (apvts.getRawParameterValue (pidFor (pid::reverbDuckAmount))->load() > 0.0f);
            }

            void refresh() override
            {
                DetailPanel::refresh();
                gr.push (live != nullptr ? live->reverbDuck.ago (0) : 0.0f);
                // The ducking meter only while ducking is set (the room keeps the whole width otherwise)
                const bool ducking = apvts.getRawParameterValue (pidFor (pid::reverbDuckAmount))->load() > 0.0f;
                if (ducking != gr.isVisible())
                {
                    gr.setVisible (ducking);
                    resized();
                }
            }

            bool showsDuckMeter() const noexcept { return gr.isVisible(); }

        private:
            void layoutVisual (juce::Rectangle<int> area) override
            {
                if (gr.isVisible())
                    placeMeter (gr, area);
            }
            GainReductionMeter gr { colours::accent, "Duck" };
        };
    }

    std::unique_ptr<DetailPanel> createReverbPanel (VelvoxAudioProcessor& proc)
    {
        return std::make_unique<ReverbPanel> (proc);
    }
}
