# Ledger – overhaul track "eq"

Baseline before any change: full suite 11764 checks passed, 0 failed. Golden auto-mix EQ decisions of 0.5 recorded (Task 6).
Verified before changes: processor with routing "eq" only is bit-exact with Tests/LegacyEqV05.h (verbatim 0.5 module) -> valid null reference.

Task 1: complete (Source/DSP/EqDesign.{h,cpp}, Tests/EqTests.cpp "Velvox EQ designs": 615 checks)
Ruling: matched design = Vicanek 2016 (impulse-invariant poles, numerator matched at DC + match point + slope there); cuts = inverse of the boost, high-shelf boost = inverse of the cut, so poles always sit at the lower corner — prototyped in Python over a grid; the plain 3-point (DC/f0/Nyquist) and least-squares fits were clearly worse — cost if wrong: small response deviations near Nyquist, no stability risk (tested grid of 145k designs).
Ruling: shelves are matched at their zero frequency f0*sqrt(A) (not at f0): worst error for resonant shelves dropped from ~30 dB to ~1.5 dB — cost: none known.
Ruling: high cut sections use the generic 3-condition match (b2 free) instead of Vicanek's b2 = 0 form (2.7 vs 3.9 dB worst in the -30 dB stop band, < 1 dB above -6 dB) — cost: steep high cuts above fs/4 deviate up to ~2.5 dB from analog below -20 dB.
Ruling: below w0 = 0.0035 rad (~27 Hz at 48 kHz) the bilinear (RBJ) design is used; there it is exact to < 0.3 dB and the matched formulas lose precision — cost: a coefficient change of a few hundredths of a dB when a band sweeps across that frequency.
Ruling: cut filters are Butterworth and ignore Q; tilt is first order (6 dB/oct transition), gain = full tilt (lows -g/2, highs +g/2), ignores Q; band pass has 0 dB peak gain; notch zeros exactly at f0 — cost: no resonant cuts (could be added later via Q without new IDs).
Ruling: resonant shelves right at Nyquist fall back to matching the analog Nyquist value (slope condition infeasible), zeros are kept inside radius sqrt(0.9999) — cost: up to ~2 dB deviation at Q >= 12 near Nyquist.
Accuracy (tests): bell 16 kHz @ 48 kHz (Q 0.5-4, +-6..18 dB) <= 0.5 dB to 20 kHz (legacy bilinear: > 2 dB); bells/shelves Q 0.3-4, +-18 dB, f0 <= 0.4 fs: <= 1.5 dB at 44.1 kHz, <= 1.25 dB at 48 kHz, <= 0.2 dB at 96 kHz.

Task 2: complete (EqualizerModule = 16 bands; Tests/EqModuleTests.cpp 43 checks; Tests/LegacyEqV05.h + Tests/EqTestHelpers.h). Plugin-level wiring follows in Task 3/4 (suite red in between).
Results: legacy bands 1-5 null against the 0.5 module bit-exact (4 settings x 44.1/48/96 kHz) and <= -120 dB under fast automation; 16 bells = 0.32 % of one core stereo 48 kHz (0.5 EQ: 0.09 %); click test (fast sweeps, steps, type/slope/on/legacy switches) stays at 1.03 x the static second difference — with an instant topology switch it fails at 4.0 x (mutation check).
Ruling: smoothing = the 0.5 scheme per band (freq/Q multiplicative, gain linear, 50 ms; redesign every 16 samples), on/off and type/slope/design changes = 30 ms fade out, switch with cleared state, fade in — cost: a 60 ms dip of that band on a type change.
Ruling: the first settings after prepare()/reset() are adopted without a sweep (0.5 swept from its defaults for 50 ms after every prepare) — cost: the first 50 ms after a (re)start differ from 0.5 renders; steady state is bit-identical.
Ruling: a free slot is a band with all values at their defaults (off, Bell, 1 kHz, 0 dB, Q 1, 24 dB/oct, role none), compared with a small tolerance; the panel's "delete" must reset a band to these defaults — cost: an untouched disabled default band counts as free (it holds nothing).
