# Ledger – overhaul/shell

Baseline before any change: full suite 11764 checks passed, 0 failed.

Task 1: complete (UndoHistory in Source/Presets, processor GestureListener, preset load + auto-mix + chain edits as steps;
UndoTests 34 checks; UI/UX/preset/routing/automix suites green)

- Ruling: the global history is a juce::UndoManager holding our own actions (one-parameter value steps for gestures, before/after
  ParameterSnapshot steps for transactions) instead of passing an UndoManager into the APVTS — why: the APVTS writes parameter values
  into its tree from a timer, so gesture boundaries and tree writes do not line up and steps would split or merge unpredictably;
  explicit before/after snapshots make undo exact and testable. Cost if wrong: bypass / input gain / level-match changes made inside
  a transaction are not restored by undoing it (ParameterSnapshot excludes them); their own gestures are still undoable.
- Ruling: undo/redo are refused while a gesture is open (a drag in progress) — why: never apply half a gesture. Cost: Ctrl+Z during
  a drag does nothing.
- Ruling: the auto-mix Vorher/Nachher switch never records a step (UndoHistory::beginSuppress), the auto-mix apply and "restore
  previous" are one step each — why: listening is not editing. Hook: AutoMixController::onApplying (merge hot-spot, small).
- Ruling: loading a project (setStateInformation) clears the history.

Task 2: complete (A/B slots removed from processor, TopBar and AutoMixController hooks; ABSlots of 0.5 read and dropped;
Tests/AbCompareTests.cpp replaced by Tests/CompareTests.cpp, 15 checks)

- Ruling: removed assertion family "A/B slots" (AbCompareTests: B copy of A, copy both directions, slots saved; RoutingTests
  "A/B slots include routing"; VelvoxTests "A/B comparison") — why: spec 2.2 removes the feature and its state. Kept intent:
  render-identity / no-click on switching and persistence now test the auto-mix Vorher/Nachher snapshot (CompareTests).
- Ruling: a 0.5 session with ABSlots loads the saved top-level parameters (that is the slot that was active when saved); the inactive
  slot is discarded and ABSlots is never written again — why: spec Q3 "load the active slot"; cost if wrong: the user loses the
  inactive slot of old projects (it was a compare scratch state).
- Finding for the eq track (not fixed here, DSP): switching hpfSlope while audio runs makes a step of ~0.16 on a 0.3 sine
  (EqualizerModule slope change without crossfade). Seen when Vorher/Nachher swaps a mix whose low-cut slope differs.
  CompareTests pins hpfSlope so the compare-switch test stays about the switch.
