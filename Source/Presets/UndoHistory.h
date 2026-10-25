#pragma once

#include "ParameterSnapshot.h"
#include <optional>

namespace velvox
{
    // One global undo history for the plugin (message thread only):
    //   - every parameter gesture (knob drag, wheel step, typed value, menu reset ...) = one step,
    //   - routing edits, preset loads, the automatic mix and every other multi-parameter change
    //     run inside a transaction = one step,
    //   - host automation never records (it has no gestures on the plugin side).
    // Built on juce::UndoManager; each step stores the state before and after (values + routing),
    // so undo and redo are exact and never depend on the order of listeners.
    //
    // Transactions nest: only the outermost begin/end pair records a step. Gestures that happen
    // inside a transaction belong to it. While a step is being undone or redone nothing records.
    class UndoHistory final : public juce::ChangeBroadcaster
    {
    public:
        explicit UndoHistory (juce::AudioProcessorValueTreeState& state);
        ~UndoHistory() override;

        // ---- Transactions (e.g. Vocal Check "Anwenden": add a module and set five values = one step)
        void beginTransaction (const juce::String& name);
        void endTransaction();
        bool isInTransaction() const noexcept { return depth > 0; }

        struct ScopedTransaction
        {
            ScopedTransaction (UndoHistory& h, const juce::String& name) : history (h) { history.beginTransaction (name); }
            ~ScopedTransaction() { history.endTransaction(); }
            UndoHistory& history;
            JUCE_DECLARE_NON_COPYABLE (ScopedTransaction)
        };

        template <typename Fn>
        void perform (const juce::String& name, Fn&& fn)
        {
            ScopedTransaction t (*this, name);
            fn();
        }

        // Changes that must never become a step of their own (the auto-mix Vorher/Nachher switch)
        void beginSuppress() noexcept { ++suppressDepth; }
        void endSuppress() noexcept { suppressDepth = std::max (0, suppressDepth - 1); }

        // True while a step is undone / redone or a suppressed change (the auto-mix compare) is applied:
        // values set then are restored state, not edits (the Simple-slider mapper ignores them)
        bool isApplyingHistory() const noexcept { return restoring || suppressDepth > 0; }

        // ---- History
        bool undo();
        bool redo();
        bool canUndo() const;
        bool canRedo() const;
        juce::String getUndoName() const;
        juce::String getRedoName() const;
        int getNumSteps() const;   // steps that can be undone (for tests)
        void clear();

        // Asked before a step is recorded: a state the step must undo to instead of the one it saw
        // (the processor answers with the auto-mix "Nachher" state while "Vorher" is shown, and ends
        // that compare). The step then becomes a whole-state step from that state to the current one.
        std::function<std::optional<ParameterSnapshot>()> takeCompareBase;

        // Parameters whose quick repeats coalesce together with what code moved in reaction (the Simple
        // sliders, which write their modules). Others coalesce only as single-parameter steps; a
        // multi-parameter step (a graph point, the reverb depth) stays one step per wheel event.
        std::function<bool (int parameterIndex)> coalescesWithSideEffects;

        // Clock (ms) for coalescing quick repeats; tests set their own, nullptr = the real one
        void setClockForTests (std::function<juce::uint32()> clock) { clockForTests = std::move (clock); }

        // ---- Fed by the processor's AudioProcessorListener (message thread; ignored elsewhere)
        void gestureBegan (int parameterIndex);
        void gestureEnded (int parameterIndex);

    private:
        class SnapshotAction;
        class ValueAction;
        void addStep (std::unique_ptr<juce::UndoableAction> action, const juce::String& name);
        bool isRecording() const noexcept;

        juce::AudioProcessorValueTreeState& apvts;
        juce::UndoManager manager { 0, 200 };   // the last 200 steps, whatever their size
        int depth = 0;
        int suppressDepth = 0;
        bool restoring = false;
        juce::String transactionName;
        ParameterSnapshot transactionBefore;
        juce::var transactionPreset;
        std::vector<float> transactionRaw;     // every parameter (normalised) when the transaction began
        int openGestures = 0;                 // gestures begun and not ended yet (nested ones included)
        std::map<int, float> gestureBefore;    // parameter index -> normalised value when its gesture began
        std::vector<int> gestureOrder;
        std::function<juce::uint32()> clockForTests;
        juce::var gestureRouting, gestureLaw;  // routing / mix law when the outermost gesture began
        juce::uint32 nowMs() const;

        // Wheel / step coalescing: JUCE's Slider::mouseWheelMove and ValueText::stepBy open and close
        // a gesture per notch. A step whose gesture began on the same parameter as the last step's and
        // that starts within coalesceMs of its end (continuing from its value, with nothing recorded,
        // undone or redone between) extends that step instead of adding one. Single-parameter steps only,
        // except for coalescesWithSideEffects parameters (0.6.1: a Simple slider and its modules).
        static constexpr juce::uint32 coalesceMs = 400;
        ValueAction* coalesceCandidate = nullptr;   // the newest step, owned by `manager`
        juce::uint32 lastStepMs = 0, gestureStartMs = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UndoHistory)
    };
}
