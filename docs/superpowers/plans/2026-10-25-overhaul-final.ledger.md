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

Task 6: complete (`git rm --cached tools/refhost/tests/__pycache__/*.pyc`; `__pycache__/` was already in .gitignore).
Check: `git ls-files | grep pycache` is empty. No unit test (repository hygiene, Ruling: nothing to execute).

Task 7: complete (combined latency test). RoutingTests "Combined latency: Sat + Smooth + Ride + Pitch + EQ in one chain
(44.1 / 192 kHz)": all five modules in the chain (amount 0 / shift 0, Sat drive 3 dB), reported latency == the chain's
getLatencyFor, impulse peak delay == reported (± 2 samples), global bypass == input delayed by exactly the reported latency.
Measured: 44.1 kHz reported 5203 / impulse 5204; 192 kHz reported 22220 / impulse 22220. The code was already correct, so the
test passed on first run; mutation check instead of a red run: reporting latency + 3 samples made 6 of its checks fail
(reported vs chain, impulse at 192 kHz, bypass alignment at both rates) — reverted.
- Ruling: ± 2 samples for the impulse peak (the linear-phase sat FIR and the pitch splicer put the peak within one sample of
  the nominal delay at 44.1 kHz); the bypass check is exact (1e-4).

Task 8: complete (guards). DynamicsHistory::reset() is now private (only prepare() calls it), with a comment why (it clears the
SPSC FrameFifo, legal only while neither add() nor pull() runs) — the guard is the compiler: DynamicsQualityTests'
"Dynamics feed: never blocks, drops when full" called reset() and failed to compile (C2248), now uses prepare(). SeqLockValue's
single-writer assumption is documented at store() and at syncRoutingFromState / syncMixLawFromState, which jassert
`VelvoxAudioProcessor::isStateWriterThread()` (message thread or MessageManagerLock holder; true without a message manager,
so command-line tools and tests without one never trip). Test (RoutingTests): "Routing / mix-law hand-over: only the message
thread writes (SeqLockValue single writer)" (red = did not compile before the helper existed; true on the message thread,
false on a juce::Thread).
- Ruling: jassert (debug only), no runtime lock — a host that restores state off the message thread is a host bug we want to
  see in debug builds; Release behaviour is unchanged.

Task 9: complete (UI). (a) The first header tab names the view: "AUTO MIX" in SIMPEL, "MODULE" in ADVANCED (TopBar::setMode
sets text + tooltip); the highlight still follows the shown tab, so in Advanced "MODULE" is lit while the chain is shown and
"VOCAL CHECK" while the check panel is shown. Help ("The tabs") and the tutorial step reworded. (b) SpectrumView draws the grid
lines under the spectrum and the frequency labels last, each on a rounded backing (background 72 %, text textSoft 85 %); new
static SpectrumView::drawFrequencyLabels (other users of drawFrequencyGrid unchanged). Tests (ShellTests): "The tab row names
the view: Advanced shows MODULE | VOCAL CHECK, Simple AUTO MIX | VOCAL CHECK" (failed first: AUTO MIX in Advanced) and "Simple
spectrum: the frequency labels are drawn above the fill, on a backing" (label-box contrast 0.043 before, 0.566 after; gate 0.3).
Snapshots looked at: C:\Users\jakob\dev\velvox-wt\shots-final\simple-mixed.png (labels readable over the filled spectrum),
C:\Users\jakob\dev\velvox-wt\shots-final\advanced-tabs.png (SIMPEL | ADVANCED, MODULE lit | VOCAL CHECK).
- Ruling: rename the tab in Advanced ("MODULE") rather than show no highlighted tab — a lit tab that names exactly what is on
  screen is the clearest; the stored view state (uiMode / uiTab) is unchanged, Auto Mix stays reachable via SIMPEL and the
  Details overlay. Cost if wrong: one label.
Full suite after Tasks 1-9: 30527 checks, 0 failed (+ Headliner/Velvox categories re-run after the help text edit: 0 failed).
