#include "UndoHistory.h"
#include "../StateKeys.h"
#include <optional>

namespace velvox
{
    namespace
    {
        const juce::Identifier presetNameProperty { "presetName" };   // PresetManager::presetNameProperty

        // A parameter (normalised) a snapshot does not hold, changed inside a transaction
        struct RawChange { int index; float before, after; };
    }

    // Whole-state step: values and routing before and after
    class UndoHistory::SnapshotAction final : public juce::UndoableAction
    {
    public:
        SnapshotAction (UndoHistory& h, ParameterSnapshot b, ParameterSnapshot a)
            : owner (h), before (std::move (b)), after (std::move (a)) {}

        // The preset field follows the step (a preset load undone shows the old name again)
        juce::var presetBefore, presetAfter;
        // Parameters snapshots leave alone (input gain, bypass, level match) that the transaction
        // changed itself, e.g. a Vocal Check fix of the input level
        std::vector<RawChange> extras;

        bool perform() override
        {
            if (first)   // already applied when it was recorded
            {
                first = false;
                return true;
            }
            return apply (after, presetAfter, false);
        }

        bool undo() override { return apply (before, presetBefore, true); }
        int getSizeInUnits() override { return 1; }

    private:
        bool apply (const ParameterSnapshot& s, const juce::var& presetName, bool undoing)
        {
            const juce::ScopedValueSetter<bool> guard (owner.restoring, true);
            s.applyTo (owner.apvts, false);
            owner.apvts.state.setProperty (presetNameProperty, presetName, nullptr);
            const auto& params = owner.apvts.processor.getParameters();
            for (const auto& c : extras)
                if (juce::isPositiveAndBelow (c.index, params.size()))
                {
                    auto* p = params[c.index];
                    p->beginChangeGesture();
                    p->setValueNotifyingHost (undoing ? c.before : c.after);
                    p->endChangeGesture();
                }
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

        // Coalescing: a single-parameter step that continues this one (same parameter, starting
        // where this one ended) becomes part of it
        bool extend (const Change& next, bool nextChangesStructure)
        {
            if (nextChangesStructure || changes.size() != 1 || changes[0].index != next.index || std::abs (changes[0].after - next.before) > 1.0e-6f)
                return false;
            changes[0].after = next.after;
            return true;
        }

        // Routing / mix law the gesture changed as a side effect (e.g. SPACE turning a fresh delay /
        // reverb insert into a send): restored after the values, so the step is exact
        struct Structure { juce::var routingBefore, routingAfter, lawBefore, lawAfter; };
        std::optional<Structure> structure;

        bool perform() override
        {
            if (first)
            {
                first = false;
                return true;
            }
            for (const auto& c : changes)
                apply (c.index, c.after);
            if (structure)
                applyStructure (structure->routingAfter, structure->lawAfter);
            return true;
        }

        bool undo() override
        {
            for (auto it = changes.rbegin(); it != changes.rend(); ++it)
                apply (it->index, it->before);
            if (structure)
                applyStructure (structure->routingBefore, structure->lawBefore);
            return true;
        }

        int getSizeInUnits() override { return 1; }

    private:
        void applyStructure (const juce::var& routing, const juce::var& law)
        {
            const juce::ScopedValueSetter<bool> guard (owner.restoring, true);
            owner.apvts.state.setProperty (statekeys::routing, routing, nullptr);
            owner.apvts.state.setProperty (statekeys::mixLaw, law, nullptr);   // after the routing (fresh instances)
        }

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
        transactionRaw.clear();
        for (auto* p : apvts.processor.getParameters())
            transactionRaw.push_back (p->getValue());
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
        // Parameters the snapshot leaves out (bypass, input gain, level match) that changed inside
        // the transaction travel with the step as raw values
        std::vector<RawChange> extras;
        const auto& params = apvts.processor.getParameters();
        for (int i = 0; i < params.size() && i < (int) transactionRaw.size(); ++i)
        {
            const float now = params[i]->getValue();
            if (std::abs (now - transactionRaw[(size_t) i]) < 1.0e-7f)
                continue;
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (params[i]); rp != nullptr && after.values.count (rp->getParameterID()) == 0)
                extras.push_back ({ i, transactionRaw[(size_t) i], now });
        }
        // An unchanged state records nothing
        if (after.values == transactionBefore.values && after.routing == transactionBefore.routing
            && after.mixLaw == transactionBefore.mixLaw && extras.empty())
            return;
        // Edited while the auto-mix "Vorher" was shown: undo goes back to the "Nachher" state
        if (takeCompareBase != nullptr)
            if (auto base = takeCompareBase())
                transactionBefore = std::move (*base);
        auto action = std::make_unique<SnapshotAction> (*this, std::move (transactionBefore), std::move (after));
        action->extras = std::move (extras);
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
        if (openGestures == 0)
        {
            gestureStartMs = nowMs();
            gestureRouting = apvts.state.getProperty (statekeys::routing);
            gestureLaw = apvts.state.getProperty (statekeys::mixLaw);
        }
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

        // Routing / law changed by code reacting to the gesture (the SPACE macro's insert -> send)
        std::optional<ValueAction::Structure> structure;
        const auto routingNow = apvts.state.getProperty (statekeys::routing), lawNow = apvts.state.getProperty (statekeys::mixLaw);
        if (routingNow != gestureRouting || lawNow != gestureLaw)
            structure = ValueAction::Structure { gestureRouting, routingNow, gestureLaw, lawNow };

        // Edited while the auto-mix "Vorher" was shown: one whole-state step from "Nachher" to here
        if (takeCompareBase != nullptr)
            if (auto base = takeCompareBase())
            {
                auto after = ParameterSnapshot::capture (apvts, name);
                std::vector<RawChange> extras;   // edited parameters a snapshot leaves out (input gain ...)
                for (const auto& c : changes)
                    if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (params[c.index]); rp != nullptr && after.values.count (rp->getParameterID()) == 0)
                        extras.push_back ({ c.index, c.before, c.after });
                auto action = std::make_unique<SnapshotAction> (*this, std::move (*base), std::move (after));
                action->extras = std::move (extras);
                action->presetBefore = action->presetAfter = apvts.state.getProperty (presetNameProperty);
                addStep (std::move (action), name);
                return;
            }

        // A quick repeat on the same parameter (wheel notches, arrow steps): part of the last step
        if (coalesceCandidate != nullptr && changes.size() == 1 && gestureStartMs - lastStepMs <= coalesceMs
            && manager.canUndo() && ! manager.canRedo() && coalesceCandidate->extend (changes[0], structure.has_value()))
        {
            lastStepMs = nowMs();
            sendChangeMessage();
            return;
        }

        const bool single = changes.size() == 1;
        auto action = std::make_unique<ValueAction> (*this, std::move (changes));
        action->structure = std::move (structure);
        auto* raw = action.get();
        addStep (std::move (action), name);
        if (single)
            coalesceCandidate = raw;
    }

    juce::uint32 UndoHistory::nowMs() const
    {
        return clockForTests != nullptr ? clockForTests() : juce::Time::getMillisecondCounter();
    }

    void UndoHistory::addStep (std::unique_ptr<juce::UndoableAction> action, const juce::String& name)
    {
        coalesceCandidate = nullptr;   // a new step never extends an older one
        lastStepMs = nowMs();
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
        coalesceCandidate = nullptr;
        const bool done = manager.undo();
        if (done)
            sendChangeMessage();
        return done;
    }

    bool UndoHistory::redo()
    {
        if (openGestures > 0 || depth > 0)
            return false;
        coalesceCandidate = nullptr;
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
        coalesceCandidate = nullptr;
        gestureBefore.clear();
        gestureOrder.clear();
        openGestures = 0;
        sendChangeMessage();
    }
}
