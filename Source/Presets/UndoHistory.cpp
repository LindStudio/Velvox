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

    // One parameter (normalised values): a knob drag, a typed value, a reset
    class UndoHistory::ValueAction final : public juce::UndoableAction
    {
    public:
        ValueAction (UndoHistory& h, int index, float b, float a) : owner (h), parameter (index), before (b), after (a) {}

        bool perform() override
        {
            if (first)
            {
                first = false;
                return true;
            }
            return apply (after);
        }

        bool undo() override { return apply (before); }
        int getSizeInUnits() override { return 1; }

    private:
        bool apply (float value)
        {
            const auto& params = owner.apvts.processor.getParameters();
            if (! juce::isPositiveAndBelow (parameter, params.size()))
                return false;
            const juce::ScopedValueSetter<bool> guard (owner.restoring, true);
            auto* p = params[parameter];
            p->beginChangeGesture();
            p->setValueNotifyingHost (value);
            p->endChangeGesture();
            return true;
        }

        UndoHistory& owner;
        int parameter;
        float before, after;
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
        openGestures.clear();   // gestures inside belong to the transaction
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
        if (juce::isPositiveAndBelow (index, params.size()))
            openGestures[index] = params[index]->getValue();
    }

    void UndoHistory::gestureEnded (int index)
    {
        if (! juce::MessageManager::existsAndIsCurrentThread())
            return;
        const auto it = openGestures.find (index);
        if (it == openGestures.end())
            return;
        const float before = it->second;
        openGestures.erase (it);
        if (depth > 0 || ! isRecording())
            return;

        auto* p = apvts.processor.getParameters()[index];
        const float after = p->getValue();
        if (std::abs (after - before) < 1.0e-7f)
            return;
        addStep (std::make_unique<ValueAction> (*this, index, before, after), p->getName (64));
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
        if (! openGestures.empty() || depth > 0)
            return false;
        const bool done = manager.undo();
        if (done)
            sendChangeMessage();
        return done;
    }

    bool UndoHistory::redo()
    {
        if (! openGestures.empty() || depth > 0)
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
        openGestures.clear();
        sendChangeMessage();
    }
}
