// Smooth panel "soothe-Stil" (spec 5.4): live spectrum with the removed energy in pink, the weighting curve
// with six draggable points, draggable low / high limits; Depth, Sharpness, Speed (Attack + Release linked
// through SmoothModule::speedToTimes, one undo step), Delta.

#include "TestHelpers.h"
#include "PluginEditor.h"
#include "UI/SmoothPanel.h"
#include "DSP/SmoothModule.h"
#include "SyntheticVocal.h"

using namespace velvox;
using namespace velvox::test;
using dsp::ModuleId;

namespace
{
    juce::Rectangle<int> panelSize() { return VelvoxAudioProcessorEditor::panelArea().withZeroOrigin(); }

    juce::String idFor (const char* base, int instance) { return dsp::instanceParamId (base, instance); }

    float real (Harness& h, const juce::String& id) { return h.proc.getState().getRawParameterValue (id)->load(); }

    void setReal (Harness& h, const juce::String& id, float v)
    {
        auto* p = h.proc.getState().getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (v));
    }

    struct HostLog final : juce::AudioProcessorListener
    {
        explicit HostLog (juce::AudioProcessor& p) : proc (p) { proc.addListener (this); }
        ~HostLog() override { proc.removeListener (this); }
        void audioProcessorParameterChanged (juce::AudioProcessor*, int index, float) override { events.add ("v" + juce::String (index)); }
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
        void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int index) override { events.add ("b" + juce::String (index)); }
        void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int index) override { events.add ("e" + juce::String (index)); }
        juce::AudioProcessor& proc;
        juce::StringArray events;
    };

    int indexOf (Harness& h, const juce::String& id) { return h.proc.getState().getParameter (id)->getParameterIndex(); }

    // Position of a time on the Speed link (inverse of speedToTimes, per parameter)
    float attackPos (float ms) { return std::log (ms / 25.0f) / std::log (1.0f / 25.0f); }
    float releasePos (float ms) { return std::log (ms / 400.0f) / std::log (16.0f / 400.0f); }

    void addSecondSmooth (Harness& h)
    {
        auto r = h.proc.getRouting();
        if (! r.contains (ModuleId::smooth, 0))
            r.insertDefault (ModuleId::smooth, 0);
        r.insertDefault (ModuleId::smooth, 1);
        h.proc.setRouting (r);
    }

    template <typename T>
    T* findChild (juce::Component& parent)
    {
        for (auto* c : parent.getChildren())
        {
            if (auto* t = dynamic_cast<T*> (c))
                return t;
            if (auto* t = findChild<T> (*c))
                return t;
        }
        return nullptr;
    }
}

class SmoothPanelTests final : public juce::UnitTest
{
public:
    SmoothPanelTests() : juce::UnitTest ("Headliner smooth panel", "Headliner") {}

    void runTest() override
    {
        using namespace ui;

        //======================================================================
        // Speed link
        beginTest ("setSpeed writes speedToTimes into Attack and Release; speed() reads it back");
        {
            Harness h;
            for (float sp : { 0.0f, 0.25f, 0.5f, 0.8f, 1.0f })
            {
                smoothedit::setSpeed (h.proc, 0, sp);
                const auto [a, r] = dsp::SmoothModule::speedToTimes (sp);
                const auto tag = "speed " + juce::String (sp);
                expectWithinAbsoluteError (real (h, pid::smoothAttack), a, 0.01f, tag + " attack");
                expectWithinAbsoluteError (real (h, pid::smoothRelease), r, 0.05f, tag + " release");
                expectWithinAbsoluteError (smoothedit::speed (h.proc, 0), sp, 0.01f, tag + " read back");
            }
        }

        beginTest ("A Speed change is one undo step and one host gesture group (attack, release)");
        {
            Harness h;
            auto& undo = h.proc.getUndo();
            undo.clear();
            HostLog log (h.proc);
            smoothedit::setSpeed (h.proc, 0, 0.9f);
            expectEquals (undo.getNumSteps(), 1, "one step");
            const int ia = indexOf (h, pid::smoothAttack), ir = indexOf (h, pid::smoothRelease);
            const int firstValue = std::min (log.events.indexOf ("v" + juce::String (ia)), log.events.indexOf ("v" + juce::String (ir)));
            expect (log.events.indexOf ("b" + juce::String (ia)) == 0 && log.events.indexOf ("b" + juce::String (ir)) == 1, log.events.joinIntoString (" "));
            expect (firstValue > 1, "values inside the gesture");
            expect (log.events.indexOf ("e" + juce::String (ir)) > firstValue && log.events.indexOf ("e" + juce::String (ia)) == log.events.size() - 1,
                    "attack's gesture ends last");
            expect (undo.undo());
            expectWithinAbsoluteError (real (h, pid::smoothAttack), 5.0f, 0.01f, "undo attack");
            expectWithinAbsoluteError (real (h, pid::smoothRelease), 80.0f, 0.05f, "undo release");
        }

        beginTest ("Speed off the link: shows the mean position; the first move puts both on the link (one step)");
        {
            Harness h;
            setReal (h, pid::smoothAttack, 50.0f);
            setReal (h, pid::smoothRelease, 16.0f);
            const float want = juce::jlimit (0.0f, 1.0f, 0.5f * (attackPos (50.0f) + releasePos (16.0f)));
            expectWithinAbsoluteError (smoothedit::speed (h.proc, 0), want, 0.01f, "mean of the two positions");
            h.proc.getUndo().clear();
            smoothedit::setSpeed (h.proc, 0, 0.5f);
            expectWithinAbsoluteError (real (h, pid::smoothAttack), 5.0f, 0.01f);
            expectWithinAbsoluteError (real (h, pid::smoothRelease), 80.0f, 0.05f);
            expectEquals (h.proc.getUndo().getNumSteps(), 1);
            smoothedit::resetSpeed (h.proc, 0);
            expectWithinAbsoluteError (real (h, pid::smoothAttack), 5.0f, 0.01f, "reset = defaults");
        }

        beginTest ("SpeedKnob: drag = one step on both, double-click = defaults, wheel = one step, text, sync");
        {
            Harness h;
            SpeedKnob k (h.proc, 0, juce::Colours::cyan);
            k.setBounds (0, 0, 90, 96);
            expectEquals (k.slider.getTextFromValue (k.slider.getValue()), juce::String ("50 %"));
            auto& undo = h.proc.getUndo();
            undo.clear();
            HostLog log (h.proc);
            const auto c = k.slider.getLocalBounds().getCentre().toFloat();
            k.slider.mouseDown (mouseAt (k.slider, c, c, false));
            for (int i = 1; i <= 10; ++i)
                k.slider.mouseDrag (mouseAt (k.slider, c.translated (0.0f, -4.4f * (float) i), c, true));
            expect (k.isInGesture(), "gesture open while dragging");
            k.slider.mouseUp (mouseAt (k.slider, c.translated (0.0f, -44.0f), c, true));
            expect (! k.isInGesture());
            // 44 px of 220 = +0.2
            const auto [a, r] = dsp::SmoothModule::speedToTimes (0.7f);
            expectWithinAbsoluteError (real (h, pid::smoothAttack), a, 0.05f, "drag moves attack along the link");
            expectWithinAbsoluteError (real (h, pid::smoothRelease), r, 0.5f, "and release");
            expectEquals (undo.getNumSteps(), 1, "one step");
            expect (log.events[0] == "b" + juce::String (indexOf (h, pid::smoothAttack)), "gesture opened at mouse-down");

            k.mouseDoubleClick (mouseAt (k.slider, c, c, false, {}, 2));
            expectWithinAbsoluteError (real (h, pid::smoothAttack), 5.0f, 0.01f, "double-click: default attack");
            expectWithinAbsoluteError (real (h, pid::smoothRelease), 80.0f, 0.05f, "double-click: default release");
            expectEquals (undo.getNumSteps(), 2);

            k.slider.mouseWheelMove (mouseAt (k.slider, c, c, false), wheel (0.11f));
            expect (smoothedit::speed (h.proc, 0) > 0.505f, "wheel up = faster");
            expectEquals (undo.getNumSteps(), 3, "one step per wheel event");

            setReal (h, pid::smoothAttack, 25.0f);
            setReal (h, pid::smoothRelease, 400.0f);
            k.sync();
            expectWithinAbsoluteError ((float) k.slider.getValue(), 0.0f, 0.01f, "sync shows the slow end");
            expectEquals (undo.getNumSteps(), 3, "sync never writes");

            auto menu = k.buildMenu();
            juce::StringArray texts;
            for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                texts.add (it.getItem().text);
            expect (texts.contains ("Standardwert") && texts.contains (juce::String::fromUTF8 ("Wert eingeben …")), texts.joinIntoString (", "));
        }

        beginTest ("Instance 2: Speed writes only the _2 parameters");
        {
            Harness h;
            addSecondSmooth (h);
            smoothedit::setSpeed (h.proc, 1, 1.0f);
            expectWithinAbsoluteError (real (h, idFor (pid::smoothAttack, 1)), 1.0f, 0.01f);
            expectWithinAbsoluteError (real (h, idFor (pid::smoothRelease, 1)), 16.0f, 0.05f);
            expectWithinAbsoluteError (real (h, pid::smoothAttack), 5.0f, 0.01f, "instance 1 untouched");
            expectEquals (smoothedit::weightGainId (1, 2), juce::String ("smoothW3Gain_2"));
            expectEquals (smoothedit::weightFreqId (0, 0), juce::String ("smoothW1Freq"));
        }

        beginTest ("Point reset, flatten all: one step each");
        {
            Harness h;
            setReal (h, smoothedit::weightFreqId (0, 2), 3000.0f);
            setReal (h, smoothedit::weightGainId (0, 2), 6.0f);
            setReal (h, smoothedit::weightGainId (0, 4), -4.0f);
            h.proc.getUndo().clear();
            smoothedit::resetPoint (h.proc, 0, 2);
            expectWithinAbsoluteError (real (h, smoothedit::weightFreqId (0, 2)), 1000.0f, 0.5f);
            expectWithinAbsoluteError (real (h, smoothedit::weightGainId (0, 2)), 0.0f, 0.001f);
            expectWithinAbsoluteError (real (h, smoothedit::weightGainId (0, 4)), -4.0f, 0.001f, "other point kept");
            smoothedit::flattenAll (h.proc, 0);
            expectWithinAbsoluteError (real (h, smoothedit::weightGainId (0, 4)), 0.0f, 0.001f, "flattened");
            expectEquals (h.proc.getUndo().getNumSteps(), 2);
            const auto st = smoothedit::settings (h.proc, 0);
            expectWithinAbsoluteError (st.weightFreq[2], 1000.0f, 0.5f, "settings mirror the parameters");
        }

        //======================================================================
        // Panel structure
        beginTest ("Panel: Depth, Sharpness, Speed, Delta; Attack, Release, Mode, Output, Low, High in Mehr");
        {
            Harness h;
            auto panel = createDetailPanel (h.proc, (int) ModuleId::smooth);
            panel->setBounds (panelSize());
            const auto& main = panel->getMainControls();
            expectEquals ((int) main.size(), 4);
            if (main.size() == 4)
            {
                auto* depth = dynamic_cast<Knob*> (main[0]);
                auto* sharp = dynamic_cast<Knob*> (main[1]);
                auto* speed = dynamic_cast<SpeedKnob*> (main[2]);
                auto* delta = dynamic_cast<Toggle*> (main[3]);
                expect (depth != nullptr && depth->getParamId() == pid::smoothAmount && depth->getLabel() == "DEPTH", "Depth");
                expect (sharp != nullptr && sharp->getParamId() == pid::smoothSensitivity && sharp->getLabel() == "SHARPNESS", "Sharpness");
                expect (speed != nullptr && speed->getLabel() == "SPEED" && speed->getDialSize() >= 50, "Speed (linked, large)");
                expect (delta != nullptr && delta->getParamId() == pid::smoothDelta, "Delta");
            }
            juce::StringArray more;
            for (auto* c : panel->getMoreControls())
            {
                if (auto* k = dynamic_cast<Knob*> (c)) more.add (k->getParamId());
                if (auto* s = dynamic_cast<Segments*> (c)) more.add (s->getParamId());
            }
            for (auto* id : { pid::smoothAttack, pid::smoothRelease, pid::smoothMode, pid::smoothOutput, pid::smoothLow, pid::smoothHigh })
                expect (more.contains (id), juce::String ("Mehr holds ") + id);
            expectEquals (more.size(), 6, "nothing else in Mehr: " + more.joinIntoString (", "));
        }

        beginTest ("Panel for instance 2 binds the _2 parameters; its Speed follows them");
        {
            Harness h;
            addSecondSmooth (h);
            auto panel = createDetailPanel (h.proc, dsp::selectionCode (ModuleId::smooth, 1));
            panel->setBounds (panelSize());
            const auto& main = panel->getMainControls();
            auto* depth = main.empty() ? nullptr : dynamic_cast<Knob*> (main[0]);
            expect (depth != nullptr && depth->getParamId() == idFor (pid::smoothAmount, 1));
            setReal (h, idFor (pid::smoothAttack, 1), 1.0f);
            setReal (h, idFor (pid::smoothRelease, 1), 16.0f);
            panel->refresh();
            auto* speed = main.size() > 2 ? dynamic_cast<SpeedKnob*> (main[2]) : nullptr;
            expect (speed != nullptr && std::abs (speed->slider.getValue() - 1.0) < 0.01, "Speed shows instance 2's times");
        }
    }

    static juce::MouseEvent mouseAt (juce::Component& c, juce::Point<float> pos, juce::Point<float> downPos, bool dragged,
                                     juce::ModifierKeys mods = {}, int clicks = 1)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, mods,
                                 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, downPos, now, clicks, dragged);
    }

    static juce::MouseWheelDetails wheel (float dy)
    {
        juce::MouseWheelDetails w;
        w.deltaX = 0.0f;
        w.deltaY = dy;
        w.isReversed = false;
        w.isSmooth = false;
        w.isInertial = false;
        return w;
    }
};

static SmoothPanelTests smoothPanelTests;
