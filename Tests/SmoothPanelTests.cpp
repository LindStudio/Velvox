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

        //======================================================================
        // The graph
        beginTest ("Graph: geometry round trips; points sit on the weighting curve; it edits the 14 handle parameters");
        {
            Harness h;
            Graph g (h);
            auto& v = g.view;
            expectWithinAbsoluteError (v.freqForX (v.xForFreq (1000.0f)), 1000.0f, 0.5f);
            expectWithinAbsoluteError (v.weightForY (v.yForWeight (6.0f)), 6.0f, 0.01f);
            expect (v.yForWeight (12.0f) < v.yForWeight (-12.0f), "up = more weight");
            setReal (h, smoothedit::weightGainId (0, 2), 4.0f);
            g.update();
            const auto p = v.pointPosition (2);
            expectWithinAbsoluteError (p.x, v.xForFreq (1000.0f), 0.5f);
            expectWithinAbsoluteError (p.y, v.yForWeight (4.0f), 0.5f);
            expectEquals (v.pointAt (p), 2);
            expectEquals (v.pointAt (p.translated (0.0f, 60.0f)), -1);
            const auto ids = v.getEditedParameters();
            expectEquals (ids.size(), 14);
            for (int i = 0; i < 6; ++i)
                expect (ids.contains (smoothedit::weightFreqId (0, i)) && ids.contains (smoothedit::weightGainId (0, i)));
            expect (ids.contains (pid::smoothLow) && ids.contains (pid::smoothHigh));
        }

        beginTest ("Graph: drag a point = freq (x) and weight (y), one step, gestures; Shift fine; clamped");
        {
            Harness h;
            Graph g (h);
            auto& v = g.view;
            auto& undo = h.proc.getUndo();
            undo.clear();
            HostLog log (h.proc);
            const auto p = v.pointPosition (2);
            const juce::Point<float> delta (v.xForFreq (2000.0f) - v.xForFreq (1000.0f), v.yForWeight (6.0f) - v.yForWeight (0.0f));
            g.drag (p, delta);
            expectWithinAbsoluteError (real (h, smoothedit::weightFreqId (0, 2)), 2000.0f, 25.0f, "one octave right");
            expectWithinAbsoluteError (real (h, smoothedit::weightGainId (0, 2)), 6.0f, 0.1f, "+6 dB up");
            expectEquals (undo.getNumSteps(), 1, "one step");
            expectEquals (log.events[0], "b" + juce::String (indexOf (h, smoothedit::weightFreqId (0, 2))), "gesture at mouse-down");
            expect (log.events[log.events.size() - 1].startsWith ("e"));
            expectEquals (v.getDraggedPoint(), -1);

            const auto p2 = v.pointPosition (2);
            g.drag (p2, delta, juce::ModifierKeys::shiftModifier);
            expectWithinAbsoluteError (real (h, smoothedit::weightGainId (0, 2)), 6.6f, 0.05f, "Shift: 1/10");
            expectWithinAbsoluteError (real (h, smoothedit::weightFreqId (0, 2)), 2000.0f * std::pow (2.0f, 0.1f), 30.0f);

            g.drag (v.pointPosition (2), { 0.0f, -2000.0f });
            expectWithinAbsoluteError (real (h, smoothedit::weightGainId (0, 2)), 12.0f, 0.001f, "clamped at +12 dB");
            expect (undo.undo() && undo.undo() && undo.undo());
            expectWithinAbsoluteError (real (h, smoothedit::weightFreqId (0, 2)), 1000.0f, 0.5f, "undone");
        }

        beginTest ("Graph: a point dragged across another stays the grabbed point");
        {
            Harness h;
            Graph g (h);
            auto& v = g.view;
            const auto p = v.pointPosition (2);
            const float dx = v.xForFreq (5000.0f) - p.x;   // past point 4 (2.5 kHz)
            v.mouseDown (mouseAt (v, p, p, false));
            for (int i = 1; i <= 10; ++i)
            {
                v.mouseDrag (mouseAt (v, p.translated (dx * (float) i / 10.0f, 0.0f), p, true));
                expectEquals (v.getDraggedPoint(), 2);
            }
            v.mouseUp (mouseAt (v, p.translated (dx, 0.0f), p, true));
            expectWithinAbsoluteError (real (h, smoothedit::weightFreqId (0, 2)), 5000.0f, 60.0f);
            expectWithinAbsoluteError (real (h, smoothedit::weightFreqId (0, 3)), 2500.0f, 1.0f, "the passed point stays");
            g.update();
            const float w = dsp::SmoothModule::weightingDbAt (v.getSettings(), 3000.0f);
            expect (std::isfinite (w), "curve finite with crossed points");
        }

        beginTest ("Graph: double-click / Alt+click reset the point; wheel = +-0.5 dB per notch; one step each");
        {
            Harness h;
            Graph g (h);
            auto& v = g.view;
            auto& undo = h.proc.getUndo();
            setReal (h, smoothedit::weightFreqId (0, 1), 600.0f);
            setReal (h, smoothedit::weightGainId (0, 1), -5.0f);
            g.update();
            undo.clear();
            auto p = v.pointPosition (1);
            v.mouseDown (mouseAt (v, p, p, false));
            v.mouseUp (mouseAt (v, p, p, false));
            v.mouseDown (mouseAt (v, p, p, false, {}, 2));
            v.mouseDoubleClick (mouseAt (v, p, p, false, {}, 2));
            v.mouseDrag (mouseAt (v, p.translated (30.0f, -30.0f), p, true, {}, 2));
            v.mouseUp (mouseAt (v, p.translated (30.0f, -30.0f), p, true, {}, 2));
            expectWithinAbsoluteError (real (h, smoothedit::weightFreqId (0, 1)), 400.0f, 0.5f, "double-click: default freq (no drag after it)");
            expectWithinAbsoluteError (real (h, smoothedit::weightGainId (0, 1)), 0.0f, 0.001f, "and gain");
            expectEquals (undo.getNumSteps(), 1, "double-click = one step");

            setReal (h, smoothedit::weightGainId (0, 1), -5.0f);
            g.update();
            undo.clear();
            p = v.pointPosition (1);
            v.mouseDown (mouseAt (v, p, p, false, juce::ModifierKeys::altModifier));
            v.mouseUp (mouseAt (v, p, p, false, juce::ModifierKeys::altModifier));
            expectWithinAbsoluteError (real (h, smoothedit::weightGainId (0, 1)), 0.0f, 0.001f, "Alt+click");
            expectEquals (undo.getNumSteps(), 1);

            g.update();
            undo.clear();
            p = v.pointPosition (1);
            v.mouseWheelMove (mouseAt (v, p, p, false), wheel (0.11f));
            expectWithinAbsoluteError (real (h, smoothedit::weightGainId (0, 1)), SmoothGraph::wheelGainDb, 0.01f, "wheel up");
            v.mouseWheelMove (mouseAt (v, p, p, false), wheel (-0.22f));
            expectWithinAbsoluteError (real (h, smoothedit::weightGainId (0, 1)), -SmoothGraph::wheelGainDb, 0.01f, "two notches down");
            expectEquals (undo.getNumSteps(), 2);
        }

        beginTest ("Graph: low / high limit edges drag, reset, wheel; clamped to the parameter range");
        {
            Harness h;
            Graph g (h);
            auto& v = g.view;
            auto& undo = h.proc.getUndo();
            undo.clear();
            const juce::Point<float> low (v.edgeX (SmoothGraph::Edge::low), v.plotArea().getBottom() - 10.0f);
            expect (v.edgeAt (low) == SmoothGraph::Edge::low, "low edge found");
            expect (v.edgeAt ({ v.edgeX (SmoothGraph::Edge::high), low.y }) == SmoothGraph::Edge::high, "high edge found");
            g.drag (low, { v.xForFreq (1600.0f) - v.xForFreq (800.0f), 0.0f });
            expectWithinAbsoluteError (real (h, pid::smoothLow), 1600.0f, 20.0f, "low edge one octave up");
            expectEquals (undo.getNumSteps(), 1);
            g.drag ({ v.edgeX (SmoothGraph::Edge::low), low.y }, { -2000.0f, 0.0f });
            expectWithinAbsoluteError (real (h, pid::smoothLow), 100.0f, 0.5f, "clamped at 100 Hz");
            const juce::Point<float> at (v.edgeX (SmoothGraph::Edge::low), low.y);
            v.mouseWheelMove (mouseAt (v, at, at, false), wheel (0.11f));
            expectWithinAbsoluteError (real (h, pid::smoothLow), 100.0f * std::pow (2.0f, SmoothGraph::wheelEdgeOct), 0.5f, "wheel 1/24 oct");
            v.mouseDown (mouseAt (v, at, at, false, juce::ModifierKeys::altModifier));
            v.mouseUp (mouseAt (v, at, at, false, juce::ModifierKeys::altModifier));
            expectWithinAbsoluteError (real (h, pid::smoothLow), 800.0f, 0.5f, "Alt+click: default");
        }

        beginTest ("Graph: menus (point: Zurücksetzen / Alle flach + panel items; edge: value menu); card while dragging");
        {
            Harness h;
            auto panel = createDetailPanel (h.proc, (int) ModuleId::smooth);
            panel->setBounds (panelSize());
            auto* v = findChild<SmoothGraph> (*panel);
            expect (v != nullptr, "the panel's visual is the graph");
            if (v == nullptr)
                return;
            auto texts = [] (juce::PopupMenu m)
            {
                juce::StringArray out;
                for (juce::PopupMenu::MenuItemIterator it (m, true); it.next();)
                    out.add (it.getItem().text);
                return out;
            };
            setReal (h, smoothedit::weightGainId (0, 4), 3.0f);
            const auto pm = texts (v->buildPointMenu (4));
            for (auto* t : { "Zurücksetzen", "Alle flach", "Modul zurücksetzen" })
                expect (pm.contains (juce::String::fromUTF8 (t)), juce::String::fromUTF8 (t) + " in " + pm.joinIntoString (", "));
            auto pointMenu = v->buildPointMenu (4);
            juce::PopupMenu::MenuItemIterator it (pointMenu, true);
            h.proc.getUndo().clear();
            while (it.next())
                if (it.getItem().text == "Alle flach" && it.getItem().action)
                    it.getItem().action();
            expectWithinAbsoluteError (real (h, smoothedit::weightGainId (0, 4)), 0.0f, 0.001f, "Alle flach");
            expectEquals (h.proc.getUndo().getNumSteps(), 1);
            const auto em = texts (v->buildEdgeMenu (SmoothGraph::Edge::high));
            expect (em.contains ("Standardwert") && em.contains (juce::String::fromUTF8 ("Wert eingeben …")), em.joinIntoString (", "));

            const auto p = v->pointPosition (3);
            v->mouseDown (mouseAt (*v, p, p, false));
            v->mouseDrag (mouseAt (*v, p.translated (10.0f, 0.0f), p, true));
            v->update (ui::LiveData());
            expect (v->isCardShown(), "card while dragging");
            expect (v->getCardTitle().contains ("4"), v->getCardTitle());
            expect (v->getCardValues().contains ("kHz") && v->getCardValues().contains ("dB"), v->getCardValues());
            v->mouseUp (mouseAt (*v, p.translated (10.0f, 0.0f), p, true));
        }

        beginTest ("Graph: the removed energy shows at the resonance (pink), nothing far from it; none while off");
        {
            Harness h;
            auto r = h.proc.getRouting();
            if (! r.contains (ModuleId::smooth))
                r.insertDefault (ModuleId::smooth, 0);
            h.proc.setRouting (r);
            h.set (pid::smoothOn, 1.0f);
            h.set (pid::smoothAmount, 80.0f);
            h.set (pid::smoothSensitivity, 60.0f);
            h.set (pid::smoothLow, 500.0f);
            Graph g (h);
            expect (! g.view.hasSpectrum() || g.view.shownRemovedDb (3000.0f) < 0.01f, "nothing before audio");
            juce::Random rnd (17);
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            double phase = 0.0;
            for (int b = 0; b < 300; ++b)
            {
                for (int i = 0; i < 512; ++i)
                {
                    const float tone = 0.25f * (float) std::sin (phase);
                    phase += juce::MathConstants<double>::twoPi * 3000.0 / 48000.0;
                    for (int c = 0; c < 2; ++c)
                        buf.setSample (c, i, 0.2f * (rnd.nextFloat() * 2.0f - 1.0f) + tone);
                }
                h.proc.processBlock (buf, midi);
                if (b > 200)
                    g.update();
            }
            expect (g.view.hasSpectrum());
            const float at = g.view.shownRemovedDb (3000.0f), away = g.view.shownRemovedDb (1000.0f);
            logMessage ("removed at 3 kHz " + juce::String (at, 1) + " dB, at 1 kHz " + juce::String (away, 1) + " dB");
            expectGreaterThan (at, 1.0f, "pink at the resonance");
            expectLessThan (away, 0.5f * at, "much less away from it");
            expect (g.view.shownInputDb (3000.0f) > g.view.shownInputDb (1000.0f), "input peak at 3 kHz");
            expect (g.view.getPeakText().contains ("kHz"), g.view.getPeakText());

            h.set (pid::smoothOn, 0.0f);
            for (int i = 0; i < 3; ++i)
                g.update();
            expectWithinAbsoluteError (g.view.shownRemovedDb (3000.0f), 0.0f, 1.0e-4f, "off: no pink");
        }

        beginTest ("Graph instance 2 edits only the _2 parameters");
        {
            Harness h;
            addSecondSmooth (h);
            Graph g (h, 1);
            auto& v = g.view;
            g.drag (v.pointPosition (0), { 0.0f, v.yForWeight (3.0f) - v.yForWeight (0.0f) });
            expectWithinAbsoluteError (real (h, smoothedit::weightGainId (1, 0)), 3.0f, 0.1f);
            expectWithinAbsoluteError (real (h, smoothedit::weightGainId (0, 0)), 0.0f, 0.001f, "instance 1 untouched");
            expect (v.getEditedParameters().contains ("smoothHigh_2"));
        }
    }

    // A free-standing graph (as in the panel's visual area) fed with the processor's settings
    struct Graph
    {
        explicit Graph (Harness& harness, int instance = 0) : h (harness), view (harness.proc, instance)
        {
            view.setBounds (0, 0, 940, 300);
            update();
        }
        void update()
        {
            live->settings = h.proc.readEffectiveSettings();
            live->sampleRate = 48000.0;
            live->time += 1.0 / 30.0;
            view.update (*live);
        }
        void drag (juce::Point<float> start, juce::Point<float> by, juce::ModifierKeys mods = {})
        {
            view.mouseDown (mouseAt (view, start, start, false, mods));
            for (int i = 1; i <= 10; ++i)
                view.mouseDrag (mouseAt (view, start + by * ((float) i / 10.0f), start, true, mods));
            view.mouseUp (mouseAt (view, start + by, start, true, mods));
            update();
        }
        Harness& h;
        ui::SmoothGraph view;
        std::unique_ptr<ui::LiveData> live = std::make_unique<ui::LiveData>();
    };

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
