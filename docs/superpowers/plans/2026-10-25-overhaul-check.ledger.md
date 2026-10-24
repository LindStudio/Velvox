# SDD ledger — plan: docs/superpowers/plans/2026-10-25-overhaul-check.md

Baseline (f4be906): full suite 11764 checks passed, 0 failed (3 min).

Pre-flight: Task 4 consumes Task 1 types (Finding fields, FixAction) and Task 2/3 measurements/targets; Task 5 consumes FixAction/EqBandRequest (Task 1);
Task 6 consumes planFix/applyResolvedFix (Task 5) and evaluate (Task 4). Names checked against the Interfaces blocks: consistent.

Ruling: no genre selector exists in the plugin; "per genre" = `Genre { party (default, = auto-mix calibration), pop, acoustic }` stored as
state property, voice class (high/low by f0) and style (spoken) adjust the windows — cost if wrong: UI has an extra setting nobody uses.
Ruling: fixes are absolute targets computed from *amounts*: module before Check and audible → current + amount, else the more extreme of
current and neutral + amount; plus a per-generation guard and an analyzer restart after apply (continuous mode) — this makes "Anwenden"
twice idempotent in both chain orders — cost if wrong: after apply the list empties until 4 s of new voice were heard.
Ruling: undo = `apvts.undoManager` transaction (the shell track supplies the UndoManager; today it is nullptr so apply is simply not undoable
until the merge) — cost if wrong: coordinator must route the transaction through the shell's history API instead.
Ruling: breaths are not detected (no module could act on it; not reliably separable from soft consonants on 85 ms frames) — cost if wrong:
one missing finding type.
Ruling: VocalSet template from the train singers (f1–f6, m1–m7), calibration/evaluation on val (f7, f8, m8, m9) + MYLK — keeps the
evaluation out-of-sample; held-out singers untouched.
