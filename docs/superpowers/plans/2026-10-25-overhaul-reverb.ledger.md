# Ledger - plan: docs/superpowers/plans/2026-10-25-overhaul-reverb.md

Pre-flight: Task 2 consumes Task 1's `Tests/ReverbMetrics.h` (modal ringing helper) - consistent. Task 4 consumes all. No conflicts.

Baseline (impulse, all types, damping 7000, size 60): mid-band RT60 = set decay (1 kHz ratio 0.95-1.02), but the
broadband T30 is 10-31 % short because HF (where white-noise energy lives) decays faster (damping); Plate ringing 11.1-11.7 dB
at every decay; Plate with modulation 100 % -> 7.3 dB (modulation is the main lever).

Task 1: Ruling: "decay" = broadband T30 of a white impulse inside the reverb's own band (input low cut .. output high cut), measured like the bench (-5..-35 dB) - that is the Q6 gate and what VintageVerb does (its 1 kHz band rings ~10 % longer than the set value) - cost if wrong: with dark damping the mids now ring up to ~25 % longer than in 0.5 for the same setting (old projects change; listed as quality fix "decay matches the set time").
Task 1: Ruling: compensation = (1 / T30_model)^0.6 x (1 + 1.3 x meanLoop / decay), clamped 1..1.6; the exponent and the build-up weight are fitted to the renders (8 types x 0.5..6 s x damping 3..15 kHz), the model alone over-compensated by ~8-16 % and missed the build-up of long-line types at 0.5 s - cost if wrong: other sample rates / extreme settings drift a few % (44.1/96 kHz tested).
Task 1: Ruling: parameter description of reverbDecay changed to "Reverb time (T30 of the whole tail, all frequencies)." (text only, ID untouched) - cost if wrong: one string.
