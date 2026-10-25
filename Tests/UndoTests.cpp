// Global undo history (spec 8 Q8): parameter gestures, routing edits, preset loads and grouped
// transactions are one step each; undo / redo are exact.

#include "TestHelpers.h"
#include "UI/ChainView.h"

using namespace velvox;
using namespace velvox::test;
using dsp::ModuleId;

namespace
{
    // A user edit: what every control does (gesture around the change)
    void gesture (Harness& h, const char* id, float value)
    {
        auto* p = h.proc.getState().getParameter (id);
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }
}

class UndoTests final : public juce::UnitTest
{
public:
    UndoTests() : juce::UnitTest ("Headliner undo history", "Headliner") {}

    void runTest() override
    {
        beginTest ("A parameter gesture is one step; undo and redo are exact");
        {
            Harness h;
            auto& undo = h.proc.getUndo();
            expect (! undo.canUndo(), "fresh instance: nothing to undo");
            const float before = h.get (pid::compRatio);
            gesture (h, pid::compRatio, 6.0f);
            expect (undo.canUndo());
            expectEquals (undo.getNumSteps(), 1);
            expect (undo.undo());
            expectWithinAbsoluteError (h.get (pid::compRatio), before, 1.0e-3f, "undo restores");
            expectEquals (undo.getNumSteps(), 0, "undo does not record a new step");
            expect (undo.redo());
            expectWithinAbsoluteError (h.get (pid::compRatio), 6.0f, 1.0e-3f, "redo re-applies");
        }

        beginTest ("Three gestures are three steps; a drag with many values is still one");
        {
            Harness h;
            auto& undo = h.proc.getUndo();
            gesture (h, pid::compRatio, 4.0f);
            gesture (h, pid::compThreshold, -30.0f);
            auto* p = h.proc.getState().getParameter (pid::midGain);
            p->beginChangeGesture();
            for (float v : { 1.0f, 2.0f, 3.0f, 4.0f })
                p->setValueNotifyingHost (p->convertTo0to1 (v));
            p->endChangeGesture();
            expectEquals (undo.getNumSteps(), 3);
            undo.undo();
            expectWithinAbsoluteError (h.get (pid::midGain), 0.0f, 1.0e-3f, "whole drag undone");
            expectWithinAbsoluteError (h.get (pid::compThreshold), -30.0f, 1.0e-3f, "earlier step untouched");
        }

        beginTest ("Gestures started by code while a gesture is open belong to it (one step)");
        {
            Harness h;
            auto* outer = h.proc.getState().getParameter (pid::compRatio);
            outer->beginChangeGesture();
            outer->setValueNotifyingHost (outer->convertTo0to1 (6.0f));
            gesture (h, pid::compThreshold, -40.0f);   // e.g. a bridge reacting to the change
            outer->endChangeGesture();
            expectEquals (h.proc.getUndo().getNumSteps(), 1);
            h.proc.getUndo().undo();
            expectWithinAbsoluteError (h.get (pid::compRatio), 3.0f, 1.0e-3f);
            expectWithinAbsoluteError (h.get (pid::compThreshold), -18.0f, 1.0e-3f, "both undone");
        }

        beginTest ("Host automation (no gesture) never records");
        {
            Harness h;
            h.set (pid::compRatio, 9.0f);
            expect (! h.proc.getUndo().canUndo());
        }

        beginTest ("Undo during an open gesture does nothing");
        {
            Harness h;
            juce::uint32 now = 1000;
            h.proc.getUndo().setClockForTests ([&now] { return now; });
            gesture (h, pid::compRatio, 4.0f);
            now += 1000;   // a separate drag later (a quick repeat would coalesce with the first step)
            auto* p = h.proc.getState().getParameter (pid::compRatio);
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (8.0f));
            expect (! h.proc.getUndo().undo(), "refused while dragging");
            expectWithinAbsoluteError (h.get (pid::compRatio), 8.0f, 1.0e-3f);
            p->endChangeGesture();
            expect (h.proc.getUndo().undo());
            expectWithinAbsoluteError (h.get (pid::compRatio), 4.0f, 1.0e-3f);
        }

        beginTest ("Routing edits are one step each, with the values of the new instance");
        {
            Harness h;
            ui::ChainView view (h.proc);
            view.setBounds (0, 0, 980, ui::ChainView::preferredHeight);
            const auto before = h.proc.getRouting();
            view.insertModule (ModuleId::eq, 1);
            expect (h.proc.getRouting().contains (ModuleId::eq, 1));
            expectEquals (h.proc.getUndo().getNumSteps(), 1, "insert = one step");
            expect (h.proc.getUndo().undo());
            expect (h.proc.getRouting() == before, "insert undone");
            expect (h.proc.getUndo().redo());
            expect (h.proc.getRouting().contains (ModuleId::eq, 1), "redo inserts again");
            expect (view.undo(), "the chain view's undo is the global one");
            expect (h.proc.getRouting() == before);
        }

        beginTest ("A preset load is one step");
        {
            Harness h;
            auto& pm = h.proc.getPresetManager();
            const float ratio = h.get (pid::compRatio);
            pm.loadPreset (1);
            expectEquals (h.proc.getUndo().getNumSteps(), 1, "one step for all values");
            h.proc.getUndo().undo();
            expectWithinAbsoluteError (h.get (pid::compRatio), ratio, 1.0e-3f);
            expect (h.proc.getRouting() == dsp::Routing::defaults(), "routing back as well");
        }

        beginTest ("Transactions group several edits (Vocal Check \"Anwenden\")");
        {
            Harness h;
            ui::ChainView view (h.proc);
            auto& undo = h.proc.getUndo();
            undo.beginTransaction ("Anwenden");
            view.insertModule (ModuleId::smooth);
            gesture (h, pid::smoothAmount, 70.0f);
            gesture (h, pid::compRatio, 5.0f);
            undo.beginTransaction ("nested");
            gesture (h, pid::midGain, -2.0f);
            undo.endTransaction();
            undo.endTransaction();
            expectEquals (undo.getNumSteps(), 1, "one step");
            expectEquals (undo.getUndoName(), juce::String ("Anwenden"));
            undo.undo();
            expect (! h.proc.getRouting().contains (ModuleId::smooth), "module removed again");
            expectWithinAbsoluteError (h.get (pid::midGain), 0.0f, 1.0e-3f);
            expectWithinAbsoluteError (h.get (pid::compRatio), 3.0f, 1.0e-3f);

            // An empty transaction records nothing
            undo.clear();
            undo.perform ("nothing", [] {});
            expect (! undo.canUndo());
        }

        beginTest ("Wheel: 30 notches on a knob are one step (coalesced within 400 ms)");
        {
            Harness h;
            juce::Slider knob (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
            juce::AudioProcessorValueTreeState::SliderAttachment attachment (h.proc.getState(), pid::compRatio, knob);
            knob.setBounds (0, 0, 60, 60);
            const float before = h.get (pid::compRatio);
            const auto start = juce::Time::getCurrentTime();
            for (int i = 0; i < 30; ++i)
            {
                const auto t = start + juce::RelativeTime::milliseconds (i + 1);   // JUCE drops duplicate wheel events
                const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), { 30.0f, 30.0f }, {}, 0.0f, 0.0f, 0.0f,
                                          0.0f, 0.0f, &knob, &knob, t, { 30.0f, 30.0f }, t, 0, false);
                knob.mouseWheelMove (e, juce::MouseWheelDetails { 0.0f, 0.1f, false, false, false });
            }
            expectGreaterThan (h.get (pid::compRatio), before + 0.5f, "the wheel moved the knob");
            expectEquals (h.proc.getUndo().getNumSteps(), 1, "one undo step");
            expect (h.proc.getUndo().undo());
            expectWithinAbsoluteError (h.get (pid::compRatio), before, 1.0e-3f, "undo restores the value before the first notch");
            expect (h.proc.getUndo().redo());
            expectGreaterThan (h.get (pid::compRatio), before + 0.5f, "redo restores the last notch");
        }

        beginTest ("Wheel: separated gestures stay separate steps");
        {
            Harness h;
            auto& undo = h.proc.getUndo();
            juce::uint32 now = 1000;
            undo.setClockForTests ([&now] { return now; });
            gesture (h, pid::compRatio, 4.0f);
            now += 1000;   // a pause: a new step
            gesture (h, pid::compRatio, 5.0f);
            expectEquals (undo.getNumSteps(), 2, "a pause separates");

            now += 100;
            gesture (h, pid::compThreshold, -30.0f);   // another parameter between
            now += 100;
            gesture (h, pid::compRatio, 6.0f);
            expectEquals (undo.getNumSteps(), 4, "another step between separates");

            now += 100;
            gesture (h, pid::compRatio, 7.0f);
            expectEquals (undo.getNumSteps(), 4, "a quick repeat coalesces");
            undo.undo();
            expectWithinAbsoluteError (h.get (pid::compRatio), 5.0f, 1.0e-3f, "undo of the coalesced step");
            now += 100;
            gesture (h, pid::compRatio, 8.0f);   // after an undo: never merged into the undone step
            expectEquals (undo.getNumSteps(), 4);
            undo.undo();
            expectWithinAbsoluteError (h.get (pid::compRatio), 5.0f, 1.0e-3f);

            // A step whose start value is not the end value of the last one (automation between) stays separate
            now += 100;
            gesture (h, pid::compRatio, 9.0f);
            h.set (pid::compRatio, 2.0f);
            now += 100;
            gesture (h, pid::compRatio, 10.0f);
            expectEquals (undo.getNumSteps(), 5);
            undo.setClockForTests (nullptr);
        }

        beginTest ("Loading a project clears the history");
        {
            Harness h;
            gesture (h, pid::compRatio, 4.0f);
            juce::MemoryBlock state;
            h.proc.getStateInformation (state);
            h.proc.setStateInformation (state.getData(), (int) state.getSize());
            expect (! h.proc.getUndo().canUndo());
        }
    }
};

static UndoTests undoTests;
