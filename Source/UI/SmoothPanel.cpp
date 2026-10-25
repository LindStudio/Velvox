#include "SmoothPanel.h"
#include "../PluginProcessor.h"
#include "HelpContent.h"

namespace velvox::ui
{
    using dsp::ModuleId;

    namespace
    {
        juce::String de (const char* utf8) { return juce::String::fromUTF8 (utf8); }

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
        : Knob ("Speed", hue), proc (p), instance (inst)
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
        setDescription (de ("Wie schnell Smooth zupackt und loslässt: stellt Attack und Release gemeinsam."));
        sync();
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
        m.addItem ("Standardwert", [safe] { if (safe != nullptr) safe->resetToDefault(); });
        m.addItem (de ("Wert eingeben …"), [safe] { if (safe != nullptr) safe->slider.showTextBox(); });
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
                setCaption (mode, "Mode");
                auto& delta = toggle (pid::smoothDelta, "Delta");
                auto& depth = knob (pid::smoothAmount, "Depth");
                auto& sharpness = knob (pid::smoothSensitivity, "Sharpness");
                speed = &own (std::make_unique<SpeedKnob> (p, getInstance(), moduleColour (ModuleId::smooth)));
                knob (pid::smoothAttack, "Attack");
                knob (pid::smoothRelease, "Release");
                knob (pid::smoothOutput, "Output");
                knob (pid::smoothLow, "Low");
                knob (pid::smoothHigh, "High");
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
