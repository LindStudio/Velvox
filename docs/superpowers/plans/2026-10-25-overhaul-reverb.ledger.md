# Ledger - plan: docs/superpowers/plans/2026-10-25-overhaul-reverb.md

Pre-flight: Task 2 consumes Task 1's `Tests/ReverbMetrics.h` (modal ringing helper) - consistent. Task 4 consumes all. No conflicts.

Baseline (impulse, all types, damping 7000, size 60): mid-band RT60 = set decay (1 kHz ratio 0.95-1.02), but the
broadband T30 is 10-31 % short because HF (where white-noise energy lives) decays faster (damping); Plate ringing 11.1-11.7 dB
at every decay; Plate with modulation 100 % -> 7.3 dB (modulation is the main lever).
