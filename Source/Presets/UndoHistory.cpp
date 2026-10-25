#include "UndoHistory.h"

namespace velvox
{
    namespace
    {
        const juce::Identifier presetNameProperty { "presetName" };   // PresetManager::presetNameProperty
    }

    // Whole-state step: values and routing before and after
    class UndoHistory::SnapshotAction final : public juce::UndoableAction
    {
    public:
        SnapshotAction (UndoHistory& h, ParameterSnapshot b, ParameterSnapshot a)
            : owner (h), before (std::move (b)), after (std::move (a)) {}

        // The preset field follows the step (a preset load undone shows the old name again)
        juce::var presetBefore, presetAfter;

        bool perform() override
        {
            if (first)   // already applied when it was recorded
            {
                first = false;
                return true;
            }
            return apply (after, presetAfter);
        }

        bool undo() override { return apply (before, presetBefore); }
        int getSizeInUnits() override { return 1; }

    private:
        bool apply (const ParameterSnapshot& s, const juce::var& presetName)
        {
            const juce::ScopedValueSetter<bool> guard (owner.restoring, true);
            s.applyTo (owner.apvts, false);
            owner.apvts.state.setProperty (presetNameProperty, presetName, nullptr);
            return true;
        }

        UndoHistory& owner;
        ParameterSnapshot before, after;
        bool first = true;
    };

    // Parameter values (normalised) changed by one gesture: a knob drag, a typed value, a reset - plus
    // the parameters that code reacting to it changed meanwhile (e.g. the EQ's legacy bridge)
    class UndoHistory::ValueAction final : public juce::UndoableAction
    {
    public:
        struct Change { int index; float before, after; };
        ValueAction (UndoHistory& h, std::vector<Change> c) : owner (h), changes (std::move (c)) {}

        bool perform() override
        {
            if (first)
            {
                first = false;
                return true;
            }
            for (const auto& c : changes)
                apply (c.index, c.after);
            return true;
        }

        bool undo() override
        {
            for (auto it = changes.rbegin(); it != changes.rend(); ++it)
                apply (it->index, it->before);
            return true;
        }

        int getSizeInUnits() override { return 1; }

    private:
        void apply (int index, float value)
        {
            const auto& params = owner.apvts.processor.getParameters();
            if (! juce::isPositiveAndBelow (index, params.size()))
                return;
            const juce::ScopedValueSetter<bool> guard (owner.restoring, true);
            auto* p = params[index];
            p->beginChangeGesture();
            p->setValueNotifyingHost (value);
            p->endChangeGesture();
        }

        UndoHistory& owner;
        std::vector<Change> changes;
        bool first = true;
    };

    //==========================================================================
    UndoHistory::UndoHistory (juce::AudioProcessorValueTreeState& state) : apvts (state) {}
    UndoHistory::~UndoHistory() = default;

    bool UndoHistory::isRecording() const noexcept
    {
        return ! restoring && suppressDepth == 0;
    }

    void UndoHistory::beginTransaction (const juce::String& name)
    {
        if (depth++ > 0)
            return;
        transactionName = name;
        transactionBefore = ParameterSnapshot::capture (apvts, name);
        transactionPreset = apvts.state.getProperty (presetNameProperty);
        gestureBefore.clear();   // gestures inside belong to the transaction
        gestureOrder.clear();
        openGestures = 0;
    }

    void UndoHistory::endTransaction()
    {
        if (depth <= 0)
        {
            jassertfalse;   // end without begin
            return;
        }
        if (--depth > 0)
            return;
        if (! isRecording())
            return;

        auto after = ParameterSnapshot::capture (apvts, transactionName);
        // Excluded parameters (bypass, input gain, level match) are compared through values only:
        // an unchanged state records nothing
        if (after.values == transactionBefore.values && after.routing == transactionBefore.routing)
            return;
        auto action = std::make_unique<SnapshotAction> (*this, std::move (transactionBefore), std::move (after));
        action->presetBefore = transactionPreset;
        action->presetAfter = apvts.state.getProperty (presetNameProperty);
        addStep (std::move (action), transactionName);
    }

    void UndoHistory::gestureBegan (int index)
    {
        if (! juce::MessageManager::existsAndIsCurrentThread() || depth > 0 || ! isRecording())
            return;
        const auto& params = apvts.processor.getParameters();
        if (! juce::isPositiveAndBelow (index, params.size()))
            return;
        // Gestures that start while another one is open belong to it (one step)
        if (gestureBefore.count (index) == 0)
        {
            gestureBefore[index] = params[index]->getValue();
            gestureOrder.push_back (index);
        }
        ++openGestures;
    }

    void UndoHistory::gestureEnded (int index)
    {
        if (! juce::MessageManager::existsAndIsCurrentThread() || openGestures <= 0 || gestureBefore.count (index) == 0)
            return;
        if (--openGestures > 0)
            return;

        // The outermost gesture ended: one step for everything it changed
        std::vector<ValueAction::Change> changes;
        juce::String name;
        const auto& params = apvts.processor.getParameters();
        for (auto i : gestureOrder)
        {
            const float before = gestureBefore[i], after = params[i]->getValue();
            if (std::abs (after - before) < 1.0e-7f)
                continue;
            changes.push_back ({ i, before, after });
            if (name.isEmpty())
                name = params[i]->getName (64);
        }
        gestureBefore.clear();
        gestureOrder.clear();
        if (changes.empty() || depth > 0 || ! isRecording())
            return;
        addStep (std::make_unique<ValueAction> (*this, std::move (changes)), name);
    }

    void UndoHistory::addStep (std::unique_ptr<juce::UndoableAction> action, const juce::String& name)
    {
        manager.beginNewTransaction (name);
        manager.perform (action.release(), name);
        manager.beginNewTransaction();
        sendChangeMessage();
    }

    bool UndoHistory::undo()
    {
        // A drag in progress: finish it first (never undo half a gesture)
        if (openGestures > 0 || depth > 0)
            return false;
        const bool done = manager.undo();
        if (done)
            sendChangeMessage();
        return done;
    }

    bool UndoHistory::redo()
    {
        if (openGestures > 0 || depth > 0)
            return false;
        const bool done = manager.redo();
        if (done)
            sendChangeMessage();
        return done;
    }

    bool UndoHistory::canUndo() const { return manager.canUndo(); }
    bool UndoHistory::canRedo() const { return manager.canRedo(); }
    juce::String UndoHistory::getUndoName() const { return manager.getUndoDescription(); }
    juce::String UndoHistory::getRedoName() const { return manager.getRedoDescription(); }

    int UndoHistory::getNumSteps() const
    {
        return manager.getUndoDescriptions().size();
    }

    void UndoHistory::clear()
    {
        manager.clearUndoHistory();
        gestureBefore.clear();
        gestureOrder.clear();
        openGestures = 0;
        sendChangeMessage();
    }
}
