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
Task 1: complete (commit 9e75469, tests: Headliner Vocal Check 0.6 13/13, Vocal Check diagnoses 22/22, Velvox 0.3 modules 97/97)
Task 2: complete (commit be2529a, tests: Headliner Vocal Check 0.6 40/40, diagnoses green)
Task 2: Ruling: f0 test tolerance 6 % + notes on the semitone grid instead of 4 % median — the synthetic take's random semitones put the true median ~1 semitone low — cost if wrong: none (grid check is stricter).
Task 2: Ruling: template deviation moved from Task 2 to Task 3 (needs the template) — cost: none.
Task 3: complete (commit 271e713, tests: Headliner Vocal Check 0.6 345/345)
Task 3: Ruling: the VocalSet template falls ~20 dB between 3.5 and 4.5 kHz (its recording chain, not voices in general) — template deviation is used only for mud/boxy (< 1 kHz, up to 2.5 kHz at most); harshness uses the presence window plus a self-referenced bump/resonance detector — cost if wrong: broad harsh bumps localised less precisely.
Task 3: Ruling: template smoothness test restricted to 300 Hz - 3 kHz (below F0 the steep fall is real) — cost: none.
Task 4: complete (commit HEAD~0 "rule engine", tests: Headliner Vocal Check 0.6 388/388, Vocal Check diagnoses 24/24, Velvox 0.3 modules 96/96)
Task 4: Ruling: calibration CLI (--check-eval, planned for Task 7) built during Task 4, because the thresholds could not be set without it — cost: none.
Task 4: Ruling: tone curve = averaged spectral envelope of pitched frames (harmonic peaks interpolated), template regenerated from it; LTAS only without pitch — removes the note-dependent harmonic comb; cost: low-frequency features between harmonics are under-sampled (+6 dB bell reads ~+4.5).
Task 4: Ruling: mud/boxy fire on a deviation >= 2.5 dB AND >= 2.0x the spread of the template voices in that band (z-score) — on VocalSet val a +6 dB bell moves the curve by ~3-4 dB while natural voices spread 2-3 dB, so recall at +6 dB is physically limited; precision kept ~0.8 — cost if wrong: mild mud/boxiness is not reported.
Task 4: Ruling: sibilance rule uses the loudest S-band moments (95th pct of 5-10 kHz frame level) minus the vowel level, target <= -9 dB (party/pop), -10 (acoustic); calibrated on the 13 pop originals of the 2026-10-24 bench (the sibilant ones by the listening test, 05/06/07/08/13, sit at -6..-4); the S/vowel classification measure (objective) is kept as the fact — it jumped when a boost reclassified frames. Cost: classical singers (VocalSet, very weak S) are not flagged even with +8 dB at 7 kHz.
Task 4: Ruling: resonance threshold 12 dB over the +-1/2 octave line (pair of bands, line fit) — VocalSet singers have natural 13-17 dB singer's-formant peaks at 2.5-4 kHz that count as false positives in the evaluation; 12 dB balances them against pop takes where a +10 dB Q8 resonance reads ~10-13 dB — cost if wrong: some classical singer's-formant rings are reported.
Task 4: Ruling: synthetic unit tests use +12 dB mud / +20 dB Q12 resonance (the synthetic voice's steep formant slopes hide smaller ones); +6 dB behaviour is measured on real vocals in the evaluation — cost: none.
Task 4: Ruling: material factor reduced to 0.9..1.0 (4..10 s voice) — 10-20 s takes were pushed under the threshold — cost: slightly more findings on short takes.
Task 4: Ruling: noise shows at SNR <= 30 dB (profiler threshold) — cost: none.
Merge: feature/automix (mixlaw track, c46dba8) merged into overhaul/check; conflict only in CMakeLists test list.
