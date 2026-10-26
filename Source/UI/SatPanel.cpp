#include "SatPanel.h"
#include "../PluginProcessor.h"
#include "../DSP/SaturationModule.h"
#include "../Parameters/ParameterLayout.h"
#include "HelpContent.h"

namespace velvox::ui
{
    namespace
    {
        juce::RangedAudioParameter* paramFor (APVTS& state, const char* baseId, int instance)
        {
            if (auto* p = state.getParameter (dsp::instanceParamId (baseId, instance)))
                return p;
            return state.getParameter (baseId);
        }

        juce::String idFor (APVTS& state, const char* baseId, int instance)
        {
            const auto id = dsp::instanceParamId (baseId, instance);
            return state.getParameter (id) != nullptr ? id : juce::String (baseId);
        }

        float realValue (const juce::RangedAudioParameter& p) { return p.convertFrom0to1 (p.getValue()); }

        void setRealValue (juce::RangedAudioParameter& p, float v)
        {
            const float n = p.convertTo0to1 (v);
            if (std::abs (n - p.getValue()) > 1.0e-7f)
                p.setValueNotifyingHost (n);
        }

        juce::String descriptionOf (const juce::String& id)
        {
            const auto* spec = findParameterSpec (id);
            if (spec == nullptr)
                spec = findParameterSpec (dsp::splitInstanceParamId (id).first);
            return spec != nullptr ? juce::String (spec->description) : juce::String();
        }

        juce::String hzText (float f)
        {
            return f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz";
        }

        constexpr float minHz = 20.0f, maxHz = 20000.0f;
        constexpr float xoverMinRatio = 1.5f;   // the DSP keeps high >= 1.5 x low
        const char* const bandNames[3] { "Low", "Mid", "High" };
    }

    //==========================================================================
    namespace satlook
    {
        juce::Colour characterColour (int c)
        {
            switch (juce::jlimit (0, 3, c))
            {
                case 0:  return juce::Colour (0xffffb454);   // Warm: amber
                case 1:  return colours::pink;               // Tape
                case 2:  return colours::violet;             // Tube
                default: return colours::cyan;               // Hard
            }
        }

        juce::String characterName (int c)
        {
            static const char* const names[4] { "Warm", "Tape", "Tube", "Hard" };
            return names[juce::jlimit (0, 3, c)];
        }

        // Icons in a unit square, built once
        const std::array<juce::Path, 4>& icons()
        {
            static const std::array<juce::Path, 4> paths = []
            {
                std::array<juce::Path, 4> p;
                // Warm: a sun
                p[0].addEllipse (0.3f, 0.3f, 0.4f, 0.4f);
                for (int i = 0; i < 8; ++i)
                {
                    const float a = juce::MathConstants<float>::twoPi * (float) i / 8.0f;
                    p[0].startNewSubPath (0.5f + 0.32f * std::cos (a), 0.5f + 0.32f * std::sin (a));
                    p[0].lineTo (0.5f + 0.46f * std::cos (a), 0.5f + 0.46f * std::sin (a));
                }
                // Tape: a cassette with two reels
                p[1].addRoundedRectangle (0.04f, 0.2f, 0.92f, 0.62f, 0.08f);
                p[1].addEllipse (0.2f, 0.34f, 0.22f, 0.22f);
                p[1].addEllipse (0.58f, 0.34f, 0.22f, 0.22f);
                p[1].startNewSubPath (0.31f, 0.56f);
                p[1].lineTo (0.69f, 0.56f);
                p[1].startNewSubPath (0.24f, 0.82f);
                p[1].lineTo (0.32f, 0.7f);
                p[1].lineTo (0.68f, 0.7f);
                p[1].lineTo (0.76f, 0.82f);
                // Tube: glass bulb, filament, pins
                p[2].startNewSubPath (0.29f, 0.84f);
                p[2].lineTo (0.29f, 0.34f);
                p[2].addCentredArc (0.5f, 0.34f, 0.21f, 0.21f, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi);
                p[2].lineTo (0.71f, 0.84f);
                p[2].closeSubPath();
                p[2].startNewSubPath (0.38f, 0.7f);
                p[2].lineTo (0.44f, 0.44f);
                p[2].lineTo (0.5f, 0.64f);
                p[2].lineTo (0.56f, 0.44f);
                p[2].lineTo (0.62f, 0.7f);
                p[2].startNewSubPath (0.4f, 0.84f);
                p[2].lineTo (0.4f, 0.98f);
                p[2].startNewSubPath (0.6f, 0.84f);
                p[2].lineTo (0.6f, 0.98f);
                // Hard: a clipped wave
                p[3].startNewSubPath (0.02f, 0.5f);
                p[3].lineTo (0.16f, 0.22f);
                p[3].lineTo (0.38f, 0.22f);
                p[3].lineTo (0.62f, 0.78f);
                p[3].lineTo (0.84f, 0.78f);
                p[3].lineTo (0.98f, 0.5f);
                return p;
            }();
            return paths;
        }

        void drawCharacterIcon (juce::Graphics& g, int character, juce::Rectangle<float> a, juce::Colour colour, float thickness)
        {
            g.setColour (colour);
            g.strokePath (icons()[(size_t) juce::jlimit (0, 3, character)],
                          juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                          juce::AffineTransform::scale (a.getWidth(), a.getHeight()).translated (a.getX(), a.getY()));
        }

        void drawCharacterTile (juce::Graphics& g, int character, juce::Rectangle<float> bounds, bool selected, bool hover)
        {
            const auto c = characterColour (character);
            const auto r = bounds.reduced (0.5f);
            g.setColour (colours::control);
            g.fillRoundedRectangle (r, 9.0f);
            if (selected)
            {
                g.setGradientFill (juce::ColourGradient (c.withAlpha (0.22f), r.getX(), r.getY(), c.withAlpha (0.07f), r.getX(), r.getBottom(), false));
                g.fillRoundedRectangle (r, 9.0f);
            }
            else if (hover)
            {
                g.setColour (juce::Colours::white.withAlpha (0.03f));
                g.fillRoundedRectangle (r, 9.0f);
            }
            g.setColour (selected ? c.withAlpha (0.85f) : (hover ? colours::border.brighter (0.6f) : colours::border.brighter (0.15f)));
            g.drawRoundedRectangle (r, 9.0f, selected ? 1.3f : 1.0f);

            const float labelH = 16.0f;
            const float icon = juce::jlimit (14.0f, 26.0f, std::min (r.getWidth() * 0.4f, r.getHeight() - labelH - 18.0f));
            const auto iconArea = juce::Rectangle<float> (icon, icon).withCentre ({ r.getCentreX(), r.getY() + (r.getHeight() - labelH) * 0.5f + 3.0f });
            if (selected)
                drawGlow (g, iconArea.getCentre(), icon * 1.1f, 0.22f, c);
            drawCharacterIcon (g, character, iconArea, selected ? c : (hover ? colours::textSoft : colours::textMuted), selected ? 1.8f : 1.5f);
            g.setColour (selected ? c.interpolatedWith (juce::Colours::white, 0.55f) : (hover ? colours::textSoft : colours::textMuted));
            g.setFont (semiBold (10.5f, 0.12f));
            g.drawText (characterName (character).toUpperCase(), r.withTop (iconArea.getBottom() + 4.0f).withHeight (labelH),
                        juce::Justification::centred, false);
        }
    }

    //==========================================================================
    SatLink::SatLink (APVTS& state, int instance)
    {
        drives = { paramFor (state, pid::satLowDrive, instance), paramFor (state, pid::satMidDrive, instance),
                   paramFor (state, pid::satHighDrive, instance) };
        link = paramFor (state, pid::satLink, instance);
        for (auto* d : drives)
            if (d != nullptr)
                d->addListener (this);
    }

    SatLink::~SatLink()
    {
        for (auto* d : drives)
            if (d != nullptr)
                d->removeListener (this);
    }

    int SatLink::bandOf (int parameterIndex) const noexcept
    {
        for (int b = 0; b < 3; ++b)
            if (drives[(size_t) b] != nullptr && drives[(size_t) b]->getParameterIndex() == parameterIndex)
                return b;
        return -1;
    }

    void SatLink::parameterGestureChanged (int parameterIndex, bool starting)
    {
        if (writing || ! juce::MessageManager::existsAndIsCurrentThread())
            return;
        const int band = bandOf (parameterIndex);
        if (band < 0)
            return;

        if (starting)
        {
            if (driving >= 0 || link == nullptr || link->getValue() < 0.5f)
                return;
            driving = band;
            for (int b = 0; b < 3; ++b)
                start[(size_t) b] = realValue (*drives[(size_t) b]);
            const juce::ScopedValueSetter<bool> own (writing, true);
            for (int b = 0; b < 3; ++b)
                if (b != band)
                    drives[(size_t) b]->beginChangeGesture();
        }
        else if (band == driving)
        {
            const juce::ScopedValueSetter<bool> own (writing, true);
            for (int b = 0; b < 3; ++b)
                if (b != band)
                    drives[(size_t) b]->endChangeGesture();
            driving = -1;
        }
    }

    void SatLink::parameterValueChanged (int parameterIndex, float newValue)
    {
        // Host automation may arrive on the audio thread: never act there (and never without a gesture)
        if (writing || driving < 0 || ! juce::MessageManager::existsAndIsCurrentThread())
            return;
        const int band = bandOf (parameterIndex);
        if (band != driving)
            return;

        const auto& range = drives[(size_t) band]->getNormalisableRange();
        const auto linked = dsp::linkedBandDrives (start, band, drives[(size_t) band]->convertFrom0to1 (newValue), range.start, range.end);
        const juce::ScopedValueSetter<bool> own (writing, true);
        for (int b = 0; b < 3; ++b)
            if (b != band)
            {
                auto* p = drives[(size_t) b];
                const float target = p->convertTo0to1 (linked[(size_t) b]);
                if (std::abs (target - p->getValue()) > 1.0e-6f)
                    p->setValueNotifyingHost (target);
            }
    }

    //==========================================================================
    // Gestures shared by the crossover handles and the band areas (spec 2.6)
    class SatView::Handle : public juce::Component, private juce::Timer
    {
    public:
        explicit Handle (SatView& o) : owner (o)
        {
            info = std::make_unique<HoverInfo> (*this,
                [this] { auto* p = parameter(); return p != nullptr ? p->getName (48) : juce::String(); },
                [this] { auto* p = parameter(); return p != nullptr ? p->getCurrentValueAsText() + " " + p->getLabel() : juce::String(); },
                [this] { auto* p = parameter(); return p != nullptr ? descriptionOf (p->getParameterID()) : juce::String(); });
        }

        ~Handle() override { endWheel(); endDrag(); }

        // The parameter this handle edits (the band area of a single band edits satDrive)
        virtual juce::RangedAudioParameter* parameter() const = 0;
        // Value for a drag: from the value at mouse-down and the (fine-scaled) movement since
        virtual float dragTo (float startValue, juce::Point<float> movement) const = 0;
        virtual float wheelTo (float value, int steps, bool fine) const = 0;
        virtual void pressed() {}

        void refreshInfo() { info->refresh(); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            endWheel();
            ignore = true;
            auto* p = parameter();
            if (p == nullptr)
                return;
            if (e.mods.isPopupMenu())
            {
                gestures::showMenu (*this, *p, nullptr);
                return;
            }
            if (e.mods.isAltDown())
            {
                gestures::resetToDefault (*p);
                return;
            }
            ignore = false;
            pressed();
            dragParam = p;
            startValue = realValue (*p);
            movement = {};
            last = e.position;
            p->beginChangeGesture();
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (ignore || dragParam == nullptr)
                return;
            // Shift / Ctrl: 1/10 of the movement from here on, no jump (like every knob)
            const float scale = e.mods.isShiftDown() || e.mods.isCtrlDown() ? 1.0f / (float) FineSlider::fineFactor : 1.0f;
            movement += (e.position - last) * scale;
            last = e.position;
            setRealValue (*dragParam, dragTo (startValue, movement));
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            endDrag();
            ignore = false;
        }

        void mouseDoubleClick (const juce::MouseEvent&) override
        {
            if (auto* p = parameter())
                gestures::resetToDefault (*p);
        }

        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
        {
            auto* p = parameter();
            float d = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? -wheel.deltaX : wheel.deltaY;
            if (wheel.isReversed)
                d = -d;
            if (p == nullptr || std::abs (d) < 1.0e-4f)
                return;
            if (wheelParam != p)
            {
                endWheel();
                wheelParam = p;
                p->beginChangeGesture();
            }
            setRealValue (*p, wheelTo (realValue (*p), d > 0.0f ? 1 : -1, e.mods.isShiftDown() || e.mods.isCtrlDown()));
            startTimer (450);   // one undo step per wheel movement: the gesture ends when the wheel rests
        }

        void mouseEnter (const juce::MouseEvent&) override { owner.repaint(); }
        void mouseExit (const juce::MouseEvent&) override { owner.repaint(); }

    protected:
        SatView& owner;

    private:
        void timerCallback() override { endWheel(); }

        void endWheel()
        {
            stopTimer();
            if (wheelParam != nullptr)
                wheelParam->endChangeGesture();
            wheelParam = nullptr;
        }

        void endDrag()
        {
            if (dragParam != nullptr)
                dragParam->endChangeGesture();
            dragParam = nullptr;
        }

        std::unique_ptr<HoverInfo> info;
        juce::RangedAudioParameter* dragParam = nullptr;
        juce::RangedAudioParameter* wheelParam = nullptr;
        float startValue = 0.0f;
        juce::Point<float> movement, last;
        bool ignore = false;
    };

    // Crossover handle: a slim vertical grip on the boundary between two bands
    class SatView::Crossover final : public SatView::Handle
    {
    public:
        Crossover (SatView& o, int k) : Handle (o), which (k)
        {
            setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        }

        juce::RangedAudioParameter* parameter() const override { return owner.proc.getState().getParameter (owner.xoverIds[which]); }

        float dragTo (float start, juce::Point<float> movement) const override
        {
            const float pxPerOctave = owner.strip.getWidth() / std::log2 (maxHz / minHz);
            return guard (start * std::pow (2.0f, movement.x / std::max (1.0f, pxPerOctave)));
        }

        float wheelTo (float value, int steps, bool fine) const override
        {
            return guard (value * std::pow (2.0f, (float) steps / (fine ? 240.0f : 24.0f)));
        }

        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            const bool active = isMouseOverOrDragging();
            const float x = r.getCentreX();
            g.setColour (juce::Colours::white.withAlpha (active ? 0.75f : 0.32f));
            g.fillRect (juce::Rectangle<float> (x - 0.75f, r.getY(), 1.5f, r.getHeight()));
            // Grip in the middle
            const auto grip = juce::Rectangle<float> (8.0f, 26.0f).withCentre ({ x, r.getCentreY() });
            g.setColour (colours::raisedHi);
            g.fillRoundedRectangle (grip, 4.0f);
            g.setColour (juce::Colours::white.withAlpha (active ? 0.9f : 0.5f));
            g.drawRoundedRectangle (grip, 4.0f, 1.0f);
            for (float dy : { -4.0f, 0.0f, 4.0f })
                g.fillRect (juce::Rectangle<float> (3.0f, 1.0f).withCentre ({ x, grip.getCentreY() + dy }));
        }

    private:
        // The bands keep their order: low <= high / 1.5, high >= low x 1.5 (the DSP guard), inside the range
        float guard (float hz) const
        {
            auto* p = parameter();
            const auto range = p->getNormalisableRange();
            float lo = range.start, hi = range.end;
            if (which == 0)
                hi = std::min (hi, owner.raw (owner.xoverIds[1]) / xoverMinRatio);
            else
                lo = std::max (lo, owner.raw (owner.xoverIds[0]) * xoverMinRatio);
            return juce::jlimit (lo, std::max (lo, hi), hz);
        }

        const int which;
    };

    // Band area: the band itself - click selects, vertical drag = its drive
    class SatView::BandArea final : public SatView::Handle
    {
    public:
        BandArea (SatView& o, int b) : Handle (o), band (b)
        {
            setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        }

        juce::RangedAudioParameter* parameter() const override
        {
            return owner.proc.getState().getParameter (owner.multiband ? owner.driveIds[band] : owner.singleDriveId);
        }

        float dragTo (float start, juce::Point<float> movement) const override
        {
            const auto range = parameter()->getNormalisableRange();
            return juce::jlimit (range.start, range.end, start - movement.y * (range.end - range.start) / (float) FineSlider::normalPixels);
        }

        float wheelTo (float value, int steps, bool fine) const override
        {
            const auto range = parameter()->getNormalisableRange();
            return juce::jlimit (range.start, range.end, value + (float) steps * (fine ? 0.05f : 0.5f));
        }

        void pressed() override
        {
            if (owner.multiband && owner.selected != band)
            {
                owner.setSelectedBand (band);
                if (owner.onSelectBand)
                    owner.onSelectBand (band);
            }
        }

    private:
        const int band;
    };

    //==========================================================================
    SatView::SatView (VelvoxAudioProcessor& p, int k) : proc (p), instance (k)
    {
        auto& s = p.getState();
        multibandId = idFor (s, pid::satMultiband, k);
        xoverIds[0] = idFor (s, pid::satXoverLow, k);
        xoverIds[1] = idFor (s, pid::satXoverHigh, k);
        const char* const on[3] { pid::satLowOn, pid::satMidOn, pid::satHighOn };
        const char* const drive[3] { pid::satLowDrive, pid::satMidDrive, pid::satHighDrive };
        const char* const character[3] { pid::satLowChar, pid::satMidChar, pid::satHighChar };
        for (int b = 0; b < 3; ++b)
        {
            onIds[b] = idFor (s, on[b], k);
            driveIds[b] = idFor (s, drive[b], k);
            charIds[b] = idFor (s, character[b], k);
        }
        singleDriveId = idFor (s, pid::satDrive, k);
        singleCharId = idFor (s, pid::satCharacter, k);
        enabledId = idFor (s, pid::satOn, k);

        for (int b = 0; b < 3; ++b)
        {
            bands[(size_t) b] = std::make_unique<BandArea> (*this, b);
            addAndMakeVisible (*bands[(size_t) b]);
        }
        for (int i = 0; i < 2; ++i)
        {
            crossovers[(size_t) i] = std::make_unique<Crossover> (*this, i);
            addAndMakeVisible (*crossovers[(size_t) i]);
        }
        for (int b = 0; b < 3; ++b)
        {
            switches[(size_t) b] = std::make_unique<Toggle> (s, onIds[b].toRawUTF8(), bandNames[b]);
            switches[(size_t) b]->setColour (juce::ToggleButton::tickColourId, moduleColour (dsp::ModuleId::sat));
            addAndMakeVisible (*switches[(size_t) b]);

            auto solo = std::make_unique<AuditionButton>();
            solo->setDescription (juce::String::fromUTF8 ("Nur dieses Band hören. Klick: an / aus, gedrückt halten: solange gehalten."));
            solo->onChange = [this, b] (bool on)
            {
                auto& sat = proc.getChain().getSaturation (instance);
                if (on)
                    sat.setSoloBand (b);
                else if (sat.getSoloBand() == b)
                    sat.setSoloBand (-1);
                syncSolo();
            };
            addChildComponent (*solo);
            solos[(size_t) b] = std::move (solo);
        }

        multiband = raw (multibandId) > 0.5f;
        enabled = raw (enabledId) > 0.5f;
        xover[0] = raw (xoverIds[0]);
        xover[1] = raw (xoverIds[1]);
    }

    SatView::~SatView()
    {
        proc.getChain().getSaturation (instance).setSoloBand (-1);   // a monitoring state: closing the panel ends it
    }

    AuditionButton& SatView::getSoloButton (int band) { return *solos[(size_t) juce::jlimit (0, 2, band)]; }

    void SatView::syncSolo()
    {
        auto& sat = proc.getChain().getSaturation (instance);
        // No solo without the bands, nor while the panel is hidden inside a window
        const auto* top = getTopLevelComponent();
        if (sat.getSoloBand() >= 0 && (! multiband || (top != nullptr && top->isOnDesktop() && ! isShowing())))
            sat.setSoloBand (-1);
        const int solo = sat.getSoloBand();
        if (solo != soloShown)
        {
            soloShown = solo;
            repaint();
        }
        for (int b = 0; b < 3; ++b)
        {
            auto& s = *solos[(size_t) b];
            s.setVisible (multiband);
            s.setActive (solo == b);
            s.setAccent (satlook::characterColour (shownCharacter (b)));
        }
    }

    float SatView::raw (const juce::String& id) const
    {
        auto* v = proc.getState().getRawParameterValue (id);
        return v != nullptr ? v->load() : 0.0f;
    }

    juce::Component& SatView::getCrossoverHandle (int which) { return *crossovers[(size_t) juce::jlimit (0, 1, which)]; }
    juce::Component& SatView::getBandArea (int band) { return *bands[(size_t) juce::jlimit (0, 2, band)]; }
    Toggle& SatView::getBandSwitch (int band) { return *switches[(size_t) juce::jlimit (0, 2, band)]; }

    void SatView::setSelectedBand (int band)
    {
        band = juce::jlimit (0, 2, band);
        if (band == selected)
            return;
        selected = band;
        rebuildCurve();
        repaint();
    }

    int SatView::shownCharacter (int band) const noexcept
    {
        if (! multiband)
            return haveLive ? shown.character : juce::roundToInt (raw (singleCharId));
        band = juce::jlimit (0, 2, band);
        return haveLive ? shown.bands[(size_t) band].character : juce::roundToInt (raw (charIds[band]));
    }

    float SatView::shownDrive (int band) const noexcept
    {
        if (! multiband)
            return haveLive ? shown.driveDb : raw (singleDriveId);
        band = juce::jlimit (0, 2, band);
        return haveLive ? shown.bands[(size_t) band].driveDb : raw (driveIds[band]);
    }

    float SatView::xForFrequency (float hz) const noexcept
    {
        const float t = std::log (juce::jlimit (minHz, maxHz, hz) / minHz) / std::log (maxHz / minHz);
        return strip.getX() + strip.getWidth() * t;
    }

    float SatView::frequencyForX (float x) const noexcept
    {
        const float t = juce::jlimit (0.0f, 1.0f, (x - strip.getX()) / std::max (1.0f, strip.getWidth()));
        return minHz * std::pow (maxHz / minHz, t);
    }

    juce::Rectangle<float> SatView::getBandBounds (int band) const
    {
        if (! multiband)
            return band == 0 ? strip : juce::Rectangle<float>();
        const float edges[4] { strip.getX(), xForFrequency (xover[0]), xForFrequency (xover[1]), strip.getRight() };
        band = juce::jlimit (0, 2, band);
        return { edges[band], strip.getY(), std::max (0.0f, edges[band + 1] - edges[band]), strip.getHeight() };
    }

    void SatView::resized()
    {
        auto r = getLocalBounds().toFloat().reduced (14.0f, 12.0f);
        strip = r.removeFromTop (std::max (96.0f, std::floor (r.getHeight() * 0.58f)));
        r.removeFromTop (18.0f);   // frequency labels under the strip
        auto mini = r;
        curveBox = mini.removeFromLeft (std::min (mini.getHeight() * 1.15f, mini.getWidth() * 0.34f));
        mini.removeFromLeft (16.0f);
        harmonicsBox = mini;
        layoutChildren();
        curveCharacter = -1;   // the curve depends on the box size
        rebuildCurve();
    }

    void SatView::layoutChildren()
    {
        for (int b = 0; b < 3; ++b)
        {
            const bool visible = multiband || b == 0;
            auto& area = *bands[(size_t) b];
            area.setVisible (visible);
            area.setBounds (getBandBounds (b).toNearestInt());

            auto& sw = *switches[(size_t) b];
            sw.setVisible (multiband);
            const auto br = getBandBounds (b);
            // On switch at the top left, the headphone at the top right
            sw.setBounds (juce::Rectangle<float> (br.getX() + 8.0f, br.getY() + 7.0f, juce::jlimit (30.0f, 76.0f, br.getWidth() - 46.0f), 22.0f).toNearestInt());
            auto& solo = *solos[(size_t) b];
            solo.setVisible (multiband);
            solo.setBounds (juce::Rectangle<float> (br.getRight() - 30.0f, br.getY() + 7.0f, 22.0f, 22.0f).toNearestInt());
        }
        for (int i = 0; i < 2; ++i)
        {
            auto& h = *crossovers[(size_t) i];
            h.setVisible (multiband);
            const float x = xForFrequency (xover[i]);
            h.setBounds (juce::Rectangle<float> (x - 7.0f, strip.getY(), 14.0f, strip.getHeight()).toNearestInt());
        }
    }

    void SatView::rebuildCurve()
    {
        const int band = multiband ? selected : 0;
        const int c = shownCharacter (band);
        const float d = shownDrive (band);
        if (c == curveCharacter && std::abs (d - curveDrive) < 0.01f)
            return;
        curveCharacter = c;
        curveDrive = d;

        // Transfer curve over -1 .. 1 (y clipped to +-1.2)
        constexpr int n = 97;
        std::array<float, n> x {}, y {};
        for (int i = 0; i < n; ++i)
            x[(size_t) i] = -1.0f + 2.0f * (float) i / (float) (n - 1);
        dsp::SaturationModule::transferCurve (c, d, x.data(), y.data(), n);
        const auto plot = curveBox.reduced (10.0f);
        curvePath.clear();
        for (int i = 0; i < n; ++i)
        {
            const juce::Point<float> pt (juce::jmap (x[(size_t) i], -1.0f, 1.0f, plot.getX(), plot.getRight()),
                                         juce::jmap (juce::jlimit (-1.2f, 1.2f, y[(size_t) i]), -1.2f, 1.2f, plot.getBottom(), plot.getY()));
            if (i == 0)
                curvePath.startNewSubPath (pt);
            else
                curvePath.lineTo (pt);
        }

        // Harmonics of a -12 dBFS sine (H1..H8, dB relative to H1)
        harmonics = dsp::SaturationModule::harmonicProfile (c, d, -12.0f);
        double sum = 0.0;
        for (size_t k = 1; k < harmonics.size(); ++k)
            sum += std::pow (10.0, harmonics[k] / 10.0);
        thdPercent = (float) (100.0 * std::sqrt (sum));
    }

    void SatView::update (const LiveData& d)
    {
        ++frame;
        bool changed = false;
        const bool mb = raw (multibandId) > 0.5f;
        const float x0 = raw (xoverIds[0]), x1 = raw (xoverIds[1]);
        if (mb != multiband || std::abs (x0 - xover[0]) > 0.01f || std::abs (x1 - xover[1]) > 0.01f)
        {
            multiband = mb;
            xover[0] = x0;
            xover[1] = x1;
            layoutChildren();
            changed = true;
        }

        const auto& s = d.settings.sat;
        if (! haveLive || s.enabled != shown.enabled || s.character != shown.character || s.driveDb != shown.driveDb || s.mix != shown.mix)
            changed = true;
        for (int b = 0; b < 3; ++b)
            if (s.bands[(size_t) b].on != shown.bands[(size_t) b].on || s.bands[(size_t) b].driveDb != shown.bands[(size_t) b].driveDb
                || s.bands[(size_t) b].character != shown.bands[(size_t) b].character)
                changed = true;
        shown = s;
        haveLive = true;
        enabled = s.enabled;
        rebuildCurve();
        syncSolo();

        // Drive meters: band input level + drive = how hard the band hits its curve (fast up, slow down)
        const auto& m = proc.getChain().getSaturation (instance).getMeters();
        for (int b = 0; b < 3; ++b)
        {
            const float in = m.inDb[(size_t) b].load (std::memory_order_relaxed);
            const bool bandOn = enabled && (multiband ? shown.bands[(size_t) b].on : b == 0);
            const float target = bandOn && in > -99.0f ? in + shownDrive (b) : -100.0f;
            auto& v = meterDb[(size_t) b];
            const float next = target > v ? v + (target - v) * 0.6f : v + (target - v) * 0.12f;
            if (std::abs (next - v) > 0.05f)
                changed = true;
            v = next;
        }

        for (auto& h : crossovers)
            h->refreshInfo();
        for (auto& h : bands)
            h->refreshInfo();

        // Live data at 30 Hz at most, and only when something moved
        if (changed && (frame % 2 == 0))
            repaint();
    }

    void SatView::paint (juce::Graphics& g)
    {
        const float alpha = enabled ? 1.0f : 0.45f;

        // Strip: frequency grid, then the bands
        g.setColour (juce::Colours::black.withAlpha (0.28f));
        g.fillRoundedRectangle (strip.expanded (0.0f, 0.0f), 8.0f);
        for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            const float x = xForFrequency (f);
            const bool major = f == 100.0f || f == 1000.0f || f == 10000.0f;
            g.setColour (colours::accent.withAlpha (major ? 0.06f : 0.03f));
            g.fillRect (juce::Rectangle<float> (x, strip.getY() + 4.0f, 1.0f, strip.getHeight() - 8.0f));
            if (major)
                drawCaption (g, f >= 1000.0f ? juce::String (juce::roundToInt (f / 1000.0f)) + "k" : juce::String (juce::roundToInt (f)),
                             juce::Rectangle<float> (x - 20.0f, strip.getBottom() + 3.0f, 40.0f, 12.0f), juce::Justification::centred,
                             colours::textFaint, 8.0f);
        }

        const int count = multiband ? 3 : 1;
        for (int b = 0; b < count; ++b)
        {
            const auto br = getBandBounds (b).reduced (multiband ? 2.0f : 0.0f, 0.0f);
            if (br.getWidth() < 2.0f)
                continue;
            const bool on = ! multiband || raw (onIds[b]) > 0.5f;
            const bool isSelected = multiband && b == selected;
            const bool hover = bands[(size_t) b]->isMouseOverOrDragging();
            const int character = shownCharacter (b);
            const float drive = shownDrive (b);
            const auto c = on ? satlook::characterColour (character) : colours::textFaint;
            // While a band is soloed the others step back
            const float a = alpha * (on ? 1.0f : 0.5f) * (soloShown >= 0 && soloShown != b ? 0.4f : 1.0f);
            const float amount = juce::jlimit (0.0f, 1.0f, drive / 24.0f);

            // Body: the character colour, stronger with more drive
            g.setGradientFill (juce::ColourGradient (c.withAlpha ((0.07f + 0.13f * amount + (hover ? 0.03f : 0.0f)) * a), br.getX(), br.getY(),
                                                     c.withAlpha (0.015f * a), br.getX(), br.getBottom(), false));
            g.fillRoundedRectangle (br, 8.0f);

            // Live drive meter: rises from the bottom with input level + drive (-42 .. +6 dB), below the labels
            const float top = br.getY() + (multiband ? 36.0f : 12.0f);
            const float level = juce::jlimit (0.0f, 1.0f, juce::jmap (meterDb[(size_t) b], -42.0f, 6.0f, 0.0f, 1.0f));
            if (on && level > 0.001f)
            {
                const float room = std::max (8.0f, br.getBottom() - (top + 46.0f));
                const auto heat = br.withTop (br.getBottom() - room * level);
                g.setGradientFill (juce::ColourGradient (c.withAlpha (0.34f * a), heat.getX(), heat.getY(),
                                                         c.withAlpha (0.05f * a), heat.getX(), heat.getBottom(), false));
                g.fillRoundedRectangle (heat, 6.0f);
                g.setColour (c.withAlpha (0.85f * a));
                g.fillRect (juce::Rectangle<float> (heat.getX() + 6.0f, heat.getY(), std::max (0.0f, heat.getWidth() - 12.0f), 1.5f));
            }

            g.setColour (isSelected ? c.withAlpha (0.9f * a) : c.withAlpha ((hover ? 0.35f : 0.18f) * a));
            g.drawRoundedRectangle (br.reduced (0.5f), 8.0f, isSelected ? 1.4f : 1.0f);

            // Character icon + name, drive value (as far as the band is wide)
            auto text = juce::Rectangle<float> (br.getX() + 10.0f, top, br.getWidth() - 18.0f, 18.0f);
            if (text.getWidth() >= 18.0f)
            {
                satlook::drawCharacterIcon (g, character, text.removeFromLeft (16.0f).withSizeKeepingCentre (16.0f, 16.0f), c.withAlpha (a), 1.4f);
                text.removeFromLeft (6.0f);
                const auto name = satlook::characterName (character).toUpperCase();
                g.setFont (semiBold (10.5f, 0.12f));
                if (juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), name) <= (int) text.getWidth())
                {
                    g.setColour (c.interpolatedWith (juce::Colours::white, 0.35f).withAlpha (a));
                    g.drawText (name, text, juce::Justification::centredLeft, false);
                }
            }
            const auto driveText = juce::String (drive, 1) + " dB";
            g.setFont (monoFont (14.0f));
            if (juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), driveText) <= (int) br.getWidth() - 18)
            {
                g.setColour (colours::text.withAlpha ((on ? 0.95f : 0.5f) * alpha));
                g.drawText (driveText, juce::Rectangle<float> (br.getX() + 10.0f, top + 21.0f, br.getWidth() - 18.0f, 18.0f),
                            juce::Justification::centredLeft, false);
            }
        }

        // Mini: transfer curve and harmonics of the selected band
        const int band = multiband ? selected : 0;
        const bool bandOn = ! multiband || raw (onIds[band]) > 0.5f;
        const auto c = bandOn ? satlook::characterColour (shownCharacter (band)) : colours::textFaint;
        g.setColour (juce::Colours::black.withAlpha (0.28f));
        g.fillRoundedRectangle (curveBox, 7.0f);
        const auto plot = curveBox.reduced (10.0f);
        g.setColour (colours::accent.withAlpha (0.06f));
        g.fillRect (juce::Rectangle<float> (plot.getCentreX(), plot.getY(), 1.0f, plot.getHeight()));
        g.fillRect (juce::Rectangle<float> (plot.getX(), plot.getCentreY(), plot.getWidth(), 1.0f));
        g.setColour (colours::accent.withAlpha (0.1f));
        g.drawLine (plot.getX(), juce::jmap (-1.0f, -1.2f, 1.2f, plot.getBottom(), plot.getY()),
                    plot.getRight(), juce::jmap (1.0f, -1.2f, 1.2f, plot.getBottom(), plot.getY()), 1.0f);
        g.setColour (c.withAlpha (0.16f * alpha));
        g.strokePath (curvePath, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (c.withAlpha (alpha));
        g.strokePath (curvePath, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        auto hb = harmonicsBox;
        auto head = hb.removeFromTop (14.0f);
        drawCaption (g, juce::String::fromUTF8 ("Obertöne"), head, juce::Justification::centredLeft, colours::textFaint, 8.0f);
        drawCaption (g, "THD " + (thdPercent < 10.0f ? juce::String (thdPercent, 1) : juce::String (juce::roundToInt (thdPercent))) + " %", head, juce::Justification::centredRight, colours::textDim, 8.0f);
        hb.removeFromTop (4.0f);
        auto labels = hb.removeFromBottom (13.0f);
        const int nBars = 7;   // H2 .. H8
        const float slot = hb.getWidth() / (float) nBars;
        for (int i = 0; i < nBars; ++i)
        {
            const float db = harmonics[(size_t) i + 1];
            const float h = juce::jlimit (0.0f, 1.0f, juce::jmap (db, -84.0f, 0.0f, 0.0f, 1.0f)) * hb.getHeight();
            const float w = std::min (22.0f, slot * 0.56f);
            const auto bar = juce::Rectangle<float> (w, h).withPosition (hb.getX() + slot * (float) i + (slot - w) * 0.5f, hb.getBottom() - h);
            g.setColour (colours::accent.withAlpha (0.035f));
            g.fillRoundedRectangle (bar.withTop (hb.getY()), 3.0f);
            if (h > 0.5f)
            {
                g.setGradientFill (juce::ColourGradient (c.withAlpha (0.9f * alpha), 0.0f, bar.getY(), c.withAlpha (0.2f * alpha), 0.0f, bar.getBottom(), false));
                g.fillRoundedRectangle (bar, 3.0f);
            }
            drawCaption (g, juce::String (i + 2), juce::Rectangle<float> (bar.getX() - 6.0f, labels.getY() + 1.0f, w + 12.0f, 12.0f),
                         juce::Justification::centred, colours::textFaint, 8.0f);
        }
    }

    void SatView::paintOverChildren (juce::Graphics& g)
    {
        if (! multiband)
            return;
        // Crossover frequencies: a small pill at the foot of each handle
        g.setFont (medium (10.5f));
        for (int i = 0; i < 2; ++i)
        {
            const auto text = hzText (xover[i]);
            const float w = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), text) + 14.0f;
            auto pill = juce::Rectangle<float> (w, 18.0f).withCentre ({ xForFrequency (xover[i]), strip.getBottom() - 15.0f });
            pill = pill.withX (juce::jlimit (strip.getX() + 2.0f, strip.getRight() - w - 2.0f, pill.getX()));
            const bool active = crossovers[(size_t) i]->isMouseOverOrDragging();
            g.setColour (colours::raisedHi.withAlpha (0.94f));
            g.fillRoundedRectangle (pill, 9.0f);
            g.setColour (juce::Colours::white.withAlpha (active ? 0.6f : 0.18f));
            g.drawRoundedRectangle (pill.reduced (0.5f), 9.0f, 1.0f);
            g.setColour (active ? colours::textStrong : colours::textSoft);
            g.drawText (text, pill, juce::Justification::centred, false);
        }
    }

    std::unique_ptr<ModuleView> createSatView (VelvoxAudioProcessor& p, int instance)
    {
        return std::make_unique<SatView> (p, instance);
    }

    //==========================================================================
    namespace
    {
        // Shows one of its children at its full size (the tiles of the selected band)
        class Stack final : public juce::Component
        {
        public:
            void resized() override
            {
                for (auto* c : getChildren())
                    c->setBounds (getLocalBounds());
            }
        };

        class SatPanel final : public DetailPanel
        {
        public:
            explicit SatPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Saturation", help::shortDescription ((int) dsp::ModuleId::sat), pid::satOn),
                  link (p.getState(), getInstance())
            {
                // Index 0 = single band (the 0.2 - 0.5 parameters), 1 - 3 = Low / Mid / High
                const char* const charBases[4] { pid::satCharacter, pid::satLowChar, pid::satMidChar, pid::satHighChar };
                const char* const driveBases[4] { pid::satDrive, pid::satLowDrive, pid::satMidDrive, pid::satHighDrive };
                auto& stack = own (std::make_unique<Stack>());
                for (int i = 0; i < 4; ++i)
                {
                    auto& t = segments (charBases[i], { "Warm", "Tape", "Tube", "Hard" });
                    auto* tp = &t;
                    t.setTilePainter ([tp] (juce::Graphics& g, int segment, juce::Rectangle<float> r, bool selected, bool hover)
                    {
                        satlook::drawCharacterTile (g, tp->getChoiceForSegment (segment), r, selected, hover);
                    });
                    stack.addChildComponent (t);   // out of Mehr, into the main row
                    tiles[(size_t) i] = &t;

                    auto& d = knob (driveBases[i], "Drive");
                    d.setDialSize (120);
                    drives[(size_t) i] = &d;
                }
                auto& multibandSwitch = toggle (pid::satMultiband, "Multiband");
                linkSwitch = &toggle (pid::satLink, "Link");
                mixKnob = &knob (pid::satMix, "Mix");   // 0.6.1: a main control
                knob (pid::satOutput, "Output");
                xovers[0] = &knob (pid::satXoverLow, "Low / Mid");
                xovers[1] = &knob (pid::satXoverHigh, "Mid / High");

                tileStack = &stack;
                multiband = &multibandSwitch;
                setMainControls ({ &stack, &multibandSwitch, linkSwitch, mixKnob });
                placeInVisual ({ drives[0], drives[1], drives[2], drives[3] });
                multibandId = pidFor (pid::satMultiband);
                sync();
            }

            void refresh() override
            {
                DetailPanel::refresh();
                sync();
            }

            void resized() override
            {
                DetailPanel::resized();
                // Main row: tall character tiles, the two switches, then Mix - centred
                auto row = getMainRowArea().withTrimmedTop (6);
                if (row.isEmpty() || tileStack == nullptr)
                    return;
                const bool wide = row.getWidth() > 700;
                const int swW = wide ? 128 : 108, gap = wide ? 28 : 16, mixW = wide ? 96 : 84;
                const int tilesW = juce::jlimit (160, 372, row.getWidth() - 2 * (swW + gap) - (mixW + gap));
                const int total = tilesW + 2 * (swW + gap) + (mixW + gap);
                int x = row.getX() + std::max (0, (row.getWidth() - total) / 2);
                tileStack->setBounds (juce::Rectangle<int> (x, row.getY(), tilesW, row.getHeight()).withSizeKeepingCentre (tilesW, std::min (76, row.getHeight() - 8)));
                x += tilesW + gap;
                multiband->setBounds (juce::Rectangle<int> (x, row.getY(), swW, row.getHeight()).withSizeKeepingCentre (swW, 28));
                x += swW + gap;
                linkSwitch->setBounds (juce::Rectangle<int> (x, row.getY(), swW, row.getHeight()).withSizeKeepingCentre (swW, 28));
                x += swW + gap;
                if (mixKnob != nullptr)
                    mixKnob->setBounds (juce::Rectangle<int> (x, row.getY() - 6, mixW, row.getHeight() + 6));
            }

        private:
            void layoutVisual (juce::Rectangle<int> area) override
            {
                // The big Drive in its own column right of the strip, the picture gets the rest
                const int column = juce::jlimit (150, 196, area.getWidth() / 4);
                auto r = area.reduced (8);
                const auto driveArea = r.removeFromRight (column);
                for (auto* d : drives)
                    if (d != nullptr)
                        d->setBounds (driveArea.withSizeKeepingCentre (column, std::min (driveArea.getHeight(), 196)));
                if (auto* v = dynamic_cast<SatView*> (getVisual()))
                {
                    v->setBounds (area.withTrimmedRight (column + 8).reduced (1));
                    if (view != v)
                    {
                        view = v;
                        juce::Component::SafePointer<SatPanel> safe (this);
                        v->onSelectBand = [safe] (int) { if (safe != nullptr) safe->sync(); };
                        sync();
                    }
                }
            }

            // Which band the tiles and the Drive show: single band, or the band selected in the strip
            void sync()
            {
                const bool mb = apvts.getRawParameterValue (multibandId)->load() > 0.5f;
                const int index = mb ? 1 + (view != nullptr ? view->getChosenBand() : 1) : 0;
                if (index != shownIndex)
                {
                    shownIndex = index;
                    for (int i = 0; i < 4; ++i)
                    {
                        tiles[(size_t) i]->setVisible (i == index);
                        drives[(size_t) i]->setVisible (i == index);
                    }
                    static const char* const names[4] { "Drive", "Low Drive", "Mid Drive", "High Drive" };
                    drives[(size_t) index]->setLabel (names[index]);
                }
                // Link and the crossovers only matter with three bands
                const bool on = apvts.getRawParameterValue (pidFor (pid::satOn))->load() > 0.5f;
                const float a = (on ? 1.0f : 0.55f) * (mb ? 1.0f : 0.45f);
                for (juce::Component* c : { (juce::Component*) linkSwitch, (juce::Component*) xovers[0], (juce::Component*) xovers[1] })
                    if (c != nullptr && std::abs (c->getAlpha() - a) > 0.001f)
                        c->setAlpha (a);
            }

            SatLink link;
            std::array<Segments*, 4> tiles {};
            std::array<Knob*, 4> drives {};
            std::array<Knob*, 2> xovers {};
            juce::Component* tileStack = nullptr;
            Toggle* multiband = nullptr;
            Toggle* linkSwitch = nullptr;
            Knob* mixKnob = nullptr;
            SatView* view = nullptr;
            juce::String multibandId;
            int shownIndex = -1;
        };
    }

    std::unique_ptr<DetailPanel> createSatPanel (VelvoxAudioProcessor& p)
    {
        return std::make_unique<SatPanel> (p);
    }
}
