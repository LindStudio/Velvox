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
