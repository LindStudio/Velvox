# Ledger – refhost track (Headliner 0.6)

Task 1: complete (RefHostCore.h: renderOffline with latency compensation, findParameter, setParameterText; 5 RefHost tests, 35 checks)
Task 2: complete (HeadlinerRefHost target; all 15 probed reference plugins/sub-plugins load and list their parameters)

## Rulings

- Ruling: Headliner modules are rendered through the in-process `VelvoxAudioProcessor` with a one-slot routing (insert), module
  on, safety clip / level match off – not by calling the DSP classes directly and not by hosting the built Headliner.vst3. —
  Other tracks change module APIs; the processor + parameter IDs are the stable interface, and this measures what the user hears
  (incl. the mix law). — Cost if wrong: chain-level stages colour the measurement; checked: EQ at 0 dB nulls below −90 dB.
- Ruling: the refhost CMake lines (test file + new target) live in their own block at the end of CMakeLists.txt instead of the long
  `VelvoxTests` source line. — Every track adds tests to that line; a separate block merges cleanly. — Cost if wrong: none.
- Ruling: analysis in Python (numpy 2.5 / scipy 1.18), rendering in C++. — Faster to iterate on metrics; DSP agents only need
  one command. — Cost if wrong: a Python dependency for measurements (already used by tools/bench).
- Ruling: the host renders at the input file's sample rate; bench.py writes 48 kHz inputs (VocalSet is 44.1 kHz, resampled with
  scipy `resample_poly(160, 147)`). — Keeps the C++ tool simple. — Cost if wrong: none (documented).
- Ruling: parameter values are given as display text (`--set "Band 1 Gain=6"`); the host tries the plugin's own parser, verifies
  by reading the text back, and otherwise searches the normalized range (400-point grid + ternary refinement). Numbers on a
  non-numeric switch/choice are step indices. Headliner IDs with plain numbers are set exactly via `convertTo0to1`. —
  Some plugins' text parsers are unreliable through VST3. — Cost if wrong: ±0.5 % parameter error (tolerance of the readback).
Task 3: complete (qmetrics.py, 22 metric unit tests on synthetic signals with known answers)
Task 4: complete (bench.py / modules.py / signals.py; 8 bench tests; all 15 modules run end-to-end in ~5 min)
Task 5: complete (docs/QUALITY_BASELINE.md: per-module tables, ranked gaps, Q6 tolerances; renders in velvox-bench\2026-10-25\modules)
Merge: feature/automix c46dba8 (mixlaw) merged per coordinator note; baseline re-rendered with the fresh true dry/wet law.

- Ruling: "matched settings" = same values where controls mean the same; otherwise the reference is calibrated to the same
  amount of action (Pro-DS threshold -> sibilance reduction, soothe2 depth -> removed energy, Saturn 2 drive -> THD at
  1 kHz/-12 dBFS; grid + bisection). — Comparing artefacts only makes sense at equal effect. — Cost if wrong: a probe that
  mis-measures the action biases the comparison; probes are printed per run.
- Ruling: EQ accuracy is judged against Headliner's own analog prototypes (absolute 0.5 dB) and cramping by a
  definition-free self-consistency test (15 kHz bell vs the same bell at 1.5 kHz ×10) against Pro-Q 4. — Pro-Q 4 uses other
  Q/slope definitions (24 dB/oct cut is -6 dB at the corner), so a direct curve match would flag definition, not quality.
- Ruling: Pro-C 2 "Vocal" is program dependent (≈12:1 static), so comp ratio/threshold are gated against the set values,
  ballistics and THD against Pro-C 2 Vocal. Smash static-curve values are info only (parallel blend has no single ratio).
- Ruling: Graillon 3 tracks a pure sine 24–36 ct flat, so pitch tone tests use absolute limits (≤ 2 ct, ≤ 1 ct jitter,
  ≤ -60 dB spurious); vocal pitch error and latency are gated against Graillon. HNR change is info only.
- Ruling: Smooth runs at Amount 75 in the bench (soothe2 depth 0 already removes ~15 dB-below-dry energy; at Amount 50
  Headliner acts less than soothe2's minimum). Character metrics (sat H2/H3, low-level THD, crest) are info, not gated.
- Ruling: every bench input gets a 0.25 s silent pre-roll that is trimmed from the output. — A Reverb type switch in the
  first block discards an impulse at sample 0 (hall IR came out silent); a DAW also settles settings before playback.
  — Cost if wrong: start-up transients are not measured here (VelvoxTests covers clicks/pops).
- Ruling: the primary (first-listed) reference sets the verdict; the other references are shown for context.
- Ruling: references that fail to load are probed once per module (`--list`) and recorded in `ref_errors`; the module then
  runs Headliner-only. All 15 references loaded on this PC; none needed an authorization dialog.
- Deferred: no parameter automation during renders (zipper/time-change tests stay in VelvoxTests); per-voice tracking for the
  new Doubler engine to be added to proc_mod_widen by the doubler agent.
