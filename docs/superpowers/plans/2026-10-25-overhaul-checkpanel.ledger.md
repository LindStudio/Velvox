# Ledger — plan: docs/superpowers/plans/2026-10-25-overhaul-checkpanel.md

Baseline (dfcb278): build ok; Headliner Vocal Check 0.6 463 checks before the new test.

Task 1: complete (headroom sign; tests: Headliner Vocal Check 0.6 465/465; RED: "Kaum Headroom (3 dBFS)")
Task 1: Ruling: new `check::dbfsText` (signed, no unit) instead of changing `dbText (x, false)` — other texts use the unsigned form for magnitudes — cost: none.
