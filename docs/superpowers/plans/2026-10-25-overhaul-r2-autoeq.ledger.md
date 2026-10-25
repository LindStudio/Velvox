# Ledger — plan: docs/superpowers/plans/2026-10-25-overhaul-r2-autoeq.md

Baseline (cc26e0b = 0.6.0 + 0.6.1 spec): bench 0.6.0 on VocalSet val (27 unique excerpts of f7, f8, m8, m9; one duplicate
file name `m9_caro_vibrato.wav` collapses) mean chosen 1.015 (base 1.19); 13 real vocals (FL Studio Audio: Splice hooks,
MYLK dry chorus, separated stems) mean chosen 11.516 (base 13.23). Renders/CSV in the session scratchpad (`b060_val`, `b060_real`).

Task 1: complete (commit a71c106; bench CSV gets appended columns lowmid_in/out, air_in/out — old columns unchanged)
Task 2: complete (commit 243651f, tests: Velvox auto EQ 5/5 at that point; RED was the missing Resonance::sustained/q/persistence)
Task 3: complete (commit 243651f, tests: Velvox auto EQ 131/131)
Task 3: note: the AutoEq unit and its tests were written in one step (the tests could not compile without the header), so the
RED of Task 3 was a compile failure, not an assertion failure — cost: none.
Task 4: complete (commit d2d3900, tests: Velvox auto EQ 190/190 (RED: notch band / overview / SMOOTH, 3 failures), Velvox automatic mix 6591/6591)
Task 5: engine tonal verification + variants (see commit), tests: Velvox auto EQ 216/216, Velvox automatic mix green.
Task 5: note: the engine test was added after the code (it asserts the closed loop never ends further from the target than
the open-loop plan) — it passes; it did not exist as a RED first — cost: none.

Ruling: new roles `autoTone, autoNotch1, autoNotch2, autoNotch3` are APPENDED to dsp::EqRole (indices 8-11) and to the hidden,
non-automatable role choice list — saved states store the index, so old projects keep their roles; one role per band keeps
findBand/findOrCreateBand semantics (unique role) intact — cost if wrong: a 12-entry hidden choice instead of 8.
Ruling: "max ~6 bands" counts every band the auto-mix uses in EQ 1, the low cut included (so 5 tone/notch bands at most);
largest |gain| wins when over budget; free slots limit it further — cost if wrong: one band fewer than the spec allows.
Ruling: one Source/UI edit — `EqPanel.cpp` roleTag shows "AUTO" for the new roles (otherwise the bands look like user bands) —
cost: a one-switch merge hot-spot with the audition track.
Ruling: Vocal Check (presence / mud) and user bands are never written and are COUNTED in the model ("fixed" bands): a resonance
a kept band already cuts (remaining excess < 8 dB) gets no notch; broad moves see the kept bands' response — avoids stacking
with Vocal Check fixes in both orders — cost if wrong: an auto notch is skipped where the kept cut is too shallow.
Ruling: "sustained" = the peak shows (> 3.5 dB within ±1 band) in >= 3 of 4 consecutive parts of the voiced frames AND is
narrow (>= 3 dB over the bands 1/6 octave away); Q estimate 16/12/10 by how many neighbours are within 4 dB — a held note's
partial or a singer's formant is not notched — cost if wrong: short resonances (< 1/4 of the take) are left to SMOOTH.
Ruling: notch depth = −clamp(0.6 · remaining excess, 3, 9) dB (×0.6 conservative), threshold remaining excess ≥ 8 dB (the
profiler's own resonance threshold is 7 dB) — cost if wrong: mild resonances (7-8 dB) stay.
Ruling: one problem, one tool — a resonance the EQ notches is no longer a reason for SMOOTH (SMOOTH stays for harshness and
un-notched upper resonances) — golden shift in "Planner: tone decisions" (3 kHz resonance → notch instead of SMOOTH).
Ruling: open loop (planner) only cuts what is clearly too much (low mids > −1, body > −3, presence > −3.5, top > −6.5 dB) and
lifts a dull top (< −16.5 dB, shelf where the content is: 9 kHz, else 4.5 kHz) plus air (< −30 dB) and the 0.6.0 body lift;
lifts of body / low mids are left to the closed loop — on VocalSet the chain itself raises body/low mids by 3-10 dB, an
open-loop lift would overshoot — cost if wrong: one more verification round on thin takes.
Ruling: closed loop in the engine's verification rounds (max 4, as before): rendered body/low-mids/presence/top outside the
window by > 0.5 dB → the owning broad band moves 80 % of the way (model-solved), within the limits, new bands within the
budget; the air shelf is not corrected in a round where the "chain too bright" rule already lowered it — cost: up to 3
more renders on takes outside the windows.
Ruling: "Höhen sanfter" variant is skipped when the base plan already cuts the highs (air ≤ −1 dB); the EQ 2 variant also
moves an autoTone presence lift — golden shift in "Optimizer: a bright take gets a softer-highs variant" (base plan or variant).
Task 5: Ruling (tuning round 2 of max 3): broad moves no longer aim one measure each but minimise the objective's tonal +
harshness terms on the model (0.5 dB grid, 1 dB margin inside the windows, 0.03 per dB of band size, must gain > 0.05) — round 1
cut a bright take's top with the 5 kHz shelf and lost its (already low) air band (m8_row_vibrato 2.16 -> 5.31); the optimiser
picks the 2-5 kHz bell there — cost if wrong: the model's preferences differ from what a listener wants (hearing test pending).
Bench round 1 (per-measure solve): val 1.015 -> 0.949, real 11.516 -> 11.002. Round 2 (optimiser): val 0.273, real 10.069.
Merge: feature/automix ec3a8d5 (chain + macros) merged cleanly (92695ef); 85d5c08 (audition) merged, conflict only in the
CMakeLists test registrations (both kept) (de4e2ea).
Ruling: golden shift in "Velvox EQ auto-mix" (5 synthetic cases recorded from 0.5): EQ decisions now follow AutoEq (values,
chosen candidates and output trims updated, reasons in the test comment; autoTone checked as well) — the spec asks for exactly
this change — cost: none beyond the documented shift.
Ruling: "clean dry" / "stereo with rumble" lose the 0.6.0 body shelf cut (body −1.7/−2.3 dB is inside the window) — the old
taste rule (cut at > −3 dB) is replaced by the target curve — cost if wrong: slightly fuller low end on such takes.
Task 6: complete (bench on the merged head identical to round 2: VocalSet val 1.015 -> 0.273 (20 better / 3 worse), real
13 vocals 11.516 -> 10.069 (9 / 2); listening files + blind kit in C:\Users\jakob\dev\velvox-bench\2026-10-25\autoeq\;
BENCHMARK, AUTOMIX_OBJECTIVE, ARCHITECTURE, CHANGELOG updated; full suite on the merged head (85d5c08 + this track):
32670 checks passed, 0 failed)
Note for the coordinator: the macros track's MacroMapper/Macros still write the air / warmth role bands, which the auto-mix
also owns (as in 0.6.0); after an auto-mix the air band can be a 5 kHz CUT shelf, so the Simple PRESENCE slider then moves
that cut shelf. Not changed here (macros are the other track's scope).
