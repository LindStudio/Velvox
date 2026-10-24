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
