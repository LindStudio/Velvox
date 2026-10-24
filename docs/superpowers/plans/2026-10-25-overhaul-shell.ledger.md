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

Task 3: complete (ValueText.{h,cpp}: parseValueText + gestures:: reset/step/copy/paste/menu; FineSlider binds a parameter:
fine 1/10, Alt+click and double-click default, own right-click menu, typed values with units; HoverInfo helper; HSlider;
Segments with double-click/Alt reset, wheel step, right-click menu, hover card; LookAndFeel::drawLinearSlider; tooltip delay 600.
GestureTests 66 checks)

- Ruling: copy/paste of values uses a plugin-internal clipboard shared by all Headliner instances of the process, not the system
  clipboard — why: copying a knob value must never wipe what the user copied elsewhere. Cost: values cannot be pasted into other
  apps (they are visible in the menu item text instead).
- Ruling: clicking on a horizontal slider's track does not jump the value (relative drag like the knobs) — why: one gesture
  model for all controls, no accidental jumps. Cost: no "click to position".
- Ruling: the hover card shows name, the parameter's short description and the value with unit; the old appended lines
  ("Doppelklick: Standardwert. Shift/Strg: fein." and the auto-mix role) are gone — why: spec 2.5/2.6 "name, short description,
  value"; gestures are explained in the help page "controls".
- Ruling: switches (Toggle) keep click-only behaviour — spec 2.6 lists knobs, sliders, graph handles and segment controls.
