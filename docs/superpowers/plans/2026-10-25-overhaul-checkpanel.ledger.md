# Ledger — plan: docs/superpowers/plans/2026-10-25-overhaul-checkpanel.md

Baseline (dfcb278): build ok; Headliner Vocal Check 0.6 463 checks before the new test.

Task 1: complete (headroom sign; tests: Headliner Vocal Check 0.6 465/465; RED: "Kaum Headroom (3 dBFS)")
Task 1: Ruling: new `check::dbfsText` (signed, no unit) instead of changing `dbText (x, false)` — other texts use the unsigned form for magnitudes — cost: none.
Task 2: complete (undo; tests: Headliner Vocal Check 0.6 489/489; RED with Host::history = nullptr: "one step -- expected 1, actual 6 / 9", input gain not undone)
Task 2: Ruling (reconciles check Ruling 3): `applyResolvedFix (apvts, fix, UndoHistory*)` — with the history one `ScopedTransaction (fix.name)` and no
  apvts.undoManager use; without it the old undoManager path stays (stand-alone planner test). The controller always passes the processor's
  history (`Host::history`) — why: spec Q8 one global history, the shell owns it — cost if wrong: none (the processor's apvts has no undoManager).
Task 2: Ruling: UndoHistory transactions now also carry the parameters that snapshots exclude (input gain, bypass, level match) when the
  transaction itself changed them ("Sehr leise" / "Kaum Headroom" fix the input gain; before, such a step was not recorded at all) — why: undo
  must restore every changed parameter — cost if wrong: an auto-mix / preset transaction that changes input gain would now undo it too (none does).
Task 2: Ruling: the "applied on this measurement" guard also compares the values the fix left: after Ctrl+Z the finding is applicable again
  (`wasApplied (id)` added for the tile's "Angewendet" state) — cost if wrong: none.
Known red on the base (not this track): "Headliner panel frame / Every panel ... reachable" fails 15 checks for satMultiband/satXover*/satLow*/
  satMid*/satHigh*/satLink, pulseShape, pulseAmount — new DSP-track parameters whose panels belong to the sat/pulse panel tracks.
