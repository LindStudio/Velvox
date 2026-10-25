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

Task 3: complete (SPACE insert -> send). (a) Undo: a gesture step now also records the routing and mix-law strings at the
outermost gesture's begin and end; when code reacting to the gesture changed them (insertEngagedInstances' SPACE conversion,
modules switched on by a gesture joining the chain), undo restores them after the values, redo re-applies them. A step with
such a change never coalesces. (b) No dip: VocalChain::onlySilentSendSwitches() – when the pending routing differs from the
active one only in the insert/send flag of delay/reverb instances whose return is <= -30 dB and whose dry gain is 1, the new
routing is taken over at once instead of under the 2 x 8 ms routing fade (no latency change possible: same instances).
Test (MacroTests): "SPACE drag on a fresh instance: the insert -> send switch is part of the undo step and has no dip"
(failed first: dip -4.25 dB, undo left the send; now -0.20 dB, undo restores the inserts, redo the send).
- Ruling: "silent" = return smoother <= -30 dB at the switch (not exactly 0) — in a host the macro value can reach the audio
  thread one block before the routing, so the return may have started ramping; at -30 dB moving the wet from the insert point
  to the end of the chain is inaudible, a full-signal fade is not. Cost if wrong: a tiny step in a -30 dB wet signal.
- Ruling: recording routing side effects in gesture steps also makes "module switched on by a gesture" undo remove the
  instance it inserted (exact undo); before, the instance stayed in the chain after undo (inaudible, off).

Task 4: complete (Vorher/Nachher). An undo step recorded while "Vorher" is shown (a gesture or a transaction) asks
`UndoHistory::takeCompareBase` → `AutoMixController::endCompareForEdit()`: the compare ends (showingPrevious = false), the
edited state becomes the new "Nachher" (mixSnapshot), and the step is recorded as a whole-state step from the OLD "Nachher"
state (the mix with tweaks the user left) to the edited state (+ raw extras for parameters a snapshot leaves out).
Test (CompareTests): "An edit while Vorher is shown ends the compare and keeps the edit; undo returns to Nachher exactly"
(failed first: compare stayed on, Nachher discarded the edit, undo landed on a mixed state).
- Ruling: "editing in Vorher ends the compare and keeps the edit as the new state" (not "Nachher updates the snapshot") — why:
  nothing is ever discarded silently, the Vorher button still shows the state before the mix afterwards, and the undo history
  stays exact: Vorher/Nachher switches are not steps, so a step recorded on the Vorher side would otherwise undo into the
  Nachher state with Vorher values; rebasing the step onto the Nachher state makes undo land exactly where the user was before
  pressing Vorher, and every older step was recorded on that side. Cost if wrong: after such an edit the auto-mix result is
  only reachable through undo (one step), not through the Nachher button.

Task 5: complete (bridge suspend per instance). `eqbands::ScopedBridgeSuspend (apvts)` / `isActive (apvts)`: depth per APVTS in
a mutex-guarded registry (the key is erased at depth 0, so a later instance at the same address starts clean); LegacyBridge
checks the message thread first, then its own APVTS. Call sites: setStateInformation, ParameterSnapshot::applyTo,
AutoMixController::applyResult, the bridge's own no-recursion guard, EqModelTests. Test (EqModelTests / "Velvox EQ role
bands"): "The bridge suspend is per plugin instance (two processors)" (failed first with the process-wide counter: B was
suspended by A and its edit did not reach the role band).
- Ruling: a mutex, not an atomic map — the suspend can be taken by setStateInformation on a host thread; it is never used on
  the audio thread (parameterChanged returns before isActive off the message thread).
