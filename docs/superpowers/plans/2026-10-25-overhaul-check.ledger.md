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
Task 5: complete (commit "fix planner", tests: Headliner Vocal Check 0.6 421/421; RED was the missing CheckFixPlanner.h)
Task 5: Ruling: EqBandResolver is an abstract class with pure find()/write() (no side effects) instead of a direct findOrCreateEqBand call — keeps "Alles anwenden" one merged transaction; the coordinator's adapter maps write() onto the eq track's band search/creation — cost if wrong: a small adapter.
Task 5: Ruling: an existing low cut before VOCAL CHECK that still lets rumble through is raised by a third (x1.26) — cost if wrong: a slightly higher cut than needed.
Task 5: Ruling: changes to modules that are off and not switched on by the fix are skipped (overcompressed, noise, space, mono partly) — an empty plan means "no Anwenden" — cost: none.
Task 6: complete (commit "controller", tests: Headliner Vocal Check 0.6 452/452; RED was getVocalCheck() missing)
Task 6: Ruling: ignore list and genre are state-tree properties (checkIgnored, checkGenre) set without undo, like uiMode — they restore through replaceState, old projects have none — cost if wrong: "Ignorieren" is not undoable.
Task 6: Ruling: the applied-generation guard lives only in memory (the analysis frames are not saved either) — cost: none.
Merge: feature/automix (EQ track) merged; conflict only in the CMakeLists test list.
EQ: Ruling: the temporary LegacyEqResolver is replaced by EqBandsResolver on eqbands::findBand / findOrCreateBand through a value overlay (pure, so "Alles anwenden" stays one merged transaction; the APVTS wrapper findOrCreateEqBand writes directly and is not used) — cost: none.
EQ: Ruling: one band per role in the 0.6 EQ, so Vocal Check cuts use mud (< 1 kHz: mud, boxy, low resonances) and presence (>= 1 kHz: harshness, resonances), the free roles named by the coordinator; low cut, warmth and air requests use the existing lowCut / warmth / air role bands (shared with the automatic mix - two low cuts or two air shelves would stack) — cost if wrong: Vocal Check moves the auto-mix's low cut up / air shelf; and two findings in the same region share one band (the stronger cut wins in "Alles anwenden").
Task 7: complete (commits up to "real-vocal suite test", tests: full suite 22742 checks passed, 0 failed; Headliner Vocal Check real vocals 28/28 in 21 s)
Task 7: Ruling: devZ 1.8 (shown at z ~2 incl. the material factor) — z 2.0 hid too much on 10-20 s takes — cost: a few more mild mud/boxy findings.
Task 7: Ruling: the plan's goal "recall >= 0.8 at precision >= 0.8" is not reachable for mud/boxy/sibilance with +6/+8 dB injections on VocalSet (natural spread between voices is as large as the injected change; classical singers have very weak S). Reported as measured, nothing tuned to the eval set beyond the thresholds above — cost: those problems are detected only when they are larger.

Evaluation (VelvoxTests --check-eval; injections: mud +6 dB 300 Hz Q1.4, boxy +6 dB 700 Hz Q1.4, sibilance +8 dB 7 kHz Q1,
rumble 25-70 Hz noise 20 dB under the voice, resonance +10 dB 3.5 kHz Q8, 8 plosive thumps +12 dB; frequency must be +-1/3 octave;
FP = finding of that kind on the clean take or at the wrong frequency):
  VocalSet val (f7, f8, m8, m9 excerpts) + MYLK, 29 takes        recall / precision
    mud 0.10 / 0.43, boxy 0.17 / 0.62, sibilance 0.03 / 0.50, rumble 1.00 / 1.00, resonance 0.48 / 0.37, plosives 1.00 / 0.94; all 0.47 / 0.70
  same with 1.5x injections (+9 / +12 / +15 dB): mud 0.21 / 0.60, boxy 0.28 / 0.67, sibilance 0.21 / 0.86, resonance 0.55 / 0.40; all 0.54 / 0.73
  same with 2x injections (+12 / +16 / +20 dB):  mud 0.38 / 0.73, boxy 0.45 / 0.81, sibilance 0.52 / 0.94, resonance 0.66 / 0.44; all 0.67 / 0.77
  13 pop originals of the 2026-10-24 bench (hooks, partly wet): mud 0.15 / 0.29, boxy 0.08 / 0.50, sibilance 0.62 / 0.62 (the 5 "FP" are the takes
    the listening test called sibilant), rumble 1.00 / 1.00, resonance 0.38 / 0.62, plosives 1.00 / 1.00; all 0.54 / 0.75
  Resonance precision on VocalSet is low because its singers have natural 13-17 dB singer's-formant peaks at 2.5-4 kHz.
Processed vs unprocessed: VocalSet val + MYLK clean 79 findings (mean score 74) -> rendered through Headliner with the auto mix 24 findings (mean 95);
  pop originals 26 findings (78) -> auto mix 22 (89); MYLK 08_A score 86 -> 94 in the suite test, m9_row_straight 79 -> 100.
