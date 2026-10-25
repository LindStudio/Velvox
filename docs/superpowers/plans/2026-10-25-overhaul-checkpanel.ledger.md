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
Task 3: complete (VocalCheckView; tests: Headliner Vocal Check view 158/158 incl. Task 4; RED: shell tests failed while the view was not shown — "check view visible", "summary in the panel", "link opens the tab")
Task 3: Ruling: genre selector = three own segment buttons (genre is a state property, not a parameter, so Segments cannot bind it); not part of undo (check Ruling: view setting) — cost: none.
Task 3: Ruling: severity colours high = warning red, medium = amber #F59E0B (from the picked mockup), low = Check colour #38BDF8; score ring >= 80 ok-green,
  >= 60 amber, else red; "—" without a score. Amber is a local constant in VocalCheckView.cpp (Theme.h untouched: shell hot-spot) — cost: one colour outside the tokens.
Task 3: Ruling: a tile that was applied on the current measurement shows "✓ Angewendet" instead of Anwenden; findings without an applicable fix show no Anwenden
  (and are not counted by "Alles anwenden"); the tile's tooltip shows the engine's detail + why; Anwenden's tooltip lists the parameters + inserted modules (preview).
Task 3: Ruling: analysis controls (power, Laufend | Capture, Start/Stop, Reset) also live in the tab header, because the tab replaced the old check panel that held them — cost: none.
Task 4: complete (shell integration, Check module panel summary, snapshots, help text)
Task 4: Ruling: the VOCAL CHECK tab shows `VocalCheckView` (editor member, both modes, no DetailPanel); the Check module panel in the chain keeps its live
  picture and main row and replaces the old long findings list by a compact `CheckSummary` (small ring, "n Befunde", "Zum VOCAL CHECK →" which switches
  the tab via the new `DetailPanel::onShowVocalCheck`) — why: one place to act on findings, no permanent text in the module panel — cost: none.
Task 4: Ruling: CheckView (ModuleViews.cpp) keeps its English labels like the other module views (hot-spot file of every panel track) — cost: mixed languages until a global pass.
Task 4: old expectations "VOCAL CHECK shows the check panel (getDetail)" in DesignTests/ShellTests updated to getCheckView().
Merge: feature/automix (39c0632) merged; conflict only in CMakeLists (source + test lists).
Final: full suite after the merge 28741 checks passed, 27 failed — only the known reachability checks of other panel tracks (sat*, pulseShape/Amount, smoothW*).
Screenshots: C:\Users\jakob\dev\velvox-wt\shots-checkpanel\ (tab-simple, tab-advanced-ignored, tab-empty, tab-off, tab-min (889 px), module-panel).
