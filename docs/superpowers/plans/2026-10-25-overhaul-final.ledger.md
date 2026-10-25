# Headliner 0.6 – final fix pass (ledger)

Branch `overhaul/final` from feature/automix fb30ccd. Plan: `2026-10-25-overhaul-final.md`.

Task 1: complete (wheel undo flooding). UndoHistory coalesces a single-parameter gesture step into the previous step when it is
on the same parameter, starts within 400 ms of the end of that step, continues from its end value, and nothing was recorded,
undone or redone between (`coalesceCandidate` reset by every addStep / undo / redo / clear). `setClockForTests()` added.
Tests (UndoTests): "Wheel: 30 notches on a knob are one step (coalesced within 400 ms)" (a real juce::Slider + SliderAttachment,
30 mouseWheelMove calls; failed first with 30 steps), "Wheel: separated gestures stay separate steps" (pause, another parameter
between, after an undo, automation between).
- Ruling: the rule is time + same parameter + continuity, not "came from a wheel" — UndoHistory cannot see the source, JUCE's
  Slider wheel and ValueText::stepBy open/close one gesture per notch. Consequence: a drag, double-click reset or typed value on
  the SAME parameter within 400 ms of the previous step on it merges into that step. Cost if wrong: one undo reverts two quick
  edits of one control (never edits of different controls).
- Ruling: tests that performed several synthetic actions on one parameter in zero time and counted one step each now run with a
  test clock 1 s per call (DynPanelTests "Compressor graph: knee point drag…", VoicesPanelTests "Choir stage: horizontal drag…",
  UndoTests "Undo during an open gesture does nothing"); SmoothPanelTests "Graph: double-click / Alt+click reset… wheel" now
  expects the wheel-up + wheel-down pair to be one step (that is the fix).
- Ruling: the EQ graph's own wheel gesture timer (EqPanel.cpp, 450 ms) is left in place — it is not trivially redundant (it keeps
  one host gesture open across the notches, which hosts record as one automation touch) and it is consistent with coalescing.

Task 2: complete (mix law in undo). Transactions compare the snapshot's `mixLaw` too (a law-only change is a step; snapshots
already captured and restored it); `switchToTrueMixLaw` ("Alte Mix-Kurve – umstellen") runs as one transaction
"Mix-Kurve umstellen". Test (MixLawTests): "\"Alte Mix-Kurve - umstellen\" is one undo step; undo / redo restore the law"
(failed first: 0 steps, undo did not restore).
- Ruling: only the user command is wrapped, not setLegacyMixLaw itself — markFreshInstances calls it from routing changes that
  can happen inside an open gesture, and a transaction begun there would drop that gesture's start values.
