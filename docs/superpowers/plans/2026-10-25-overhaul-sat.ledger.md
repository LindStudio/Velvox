# Ledger – overhaul/sat (Saturation 0.6)

Task 1: complete (bf1679a – SatOversampler, ADAA, mix at the high rate; aliasing −91.5 dB, THD unchanged, flat chain, latency 5, click-free)
Task 2: complete (a25b408 – transferCurve, harmonicProfile, SatMeters; character + loudness tests)
Task 3: complete (c00899a – multiband parameters, LR4 split, per-band shapers, Link helper, macros)
Task 4: complete (bench `sat` incl. new `tape3band` case vs Saturn 2 3-band: 25 metrics, 0 FAIL; QUALITY_BASELINE sat block updated; full suite green)

## Rulings

- Ruling: the last down stage is an order-11 elliptic low-pass at the 2x rate (stop band from the base Nyquist, 95 dB) followed by
  plain decimation, not a half-band — a half-band's transition is centred on the base Nyquist, so the 5th harmonic of 4987 Hz
  (24.9 kHz) folds back nearly unattenuated (that alone held 2x/4x designs at ≈ −40 dB). Cost if wrong: the wet path is band-limited
  to 0.4167 × fs (20 kHz at 48 kHz, 18.4 kHz at 44.1 kHz) with 0.05 dB ripple; group delay rises above 18 kHz.
- Ruling: 4x below 60 kHz, 2x at 88.2 kHz and above (same high rate ≥ 176 kHz) — halves CPU at high rates with the same quality.
  Cost if wrong: none measurable (96 kHz tests pass).
- Ruling: ADAA always on (double precision, fallback to the midpoint curve for |Δx| < 1e-6) — 4x alone was enough at 12 dB drive
  but not at 24 dB (Tape −51 dB without, −74 dB with). Cost: CPU 0.37 % → 0.93 % per instance (Saturn 2: 2.5 %).
- Ruling: the dry/wet mix happens inside the oversampled section (dry = the same ½(u[n]+u[n−1]) average ADAA applies to a line),
  plus a 3-tap FIR (c = 0.133) undoing that average's cos(ω/2) roll-off — at Mix 50 % the old code had a −6.4 dB comb at 18 kHz,
  now flat ±0.05 dB. Cost: the dry share at partial Mix passes the same (near-transparent) filters.
- Ruling: switching on/off or between single and multiband crossfades (20 ms) against the integer-delayed dry, after a 5 ms warm-up
  of the reset filters — the oversampled path and the pure delay differ in HF phase, an instant switch clicked (−48 dB splatter).
  Cost: a multiband toggle dips to dry for ~40 ms.
- Ruling: the DC blocker (10 Hz) acts on the added distortion (wet − dry) only, not on the whole wet. Pure quality fix: the wet's
  fundamental is no longer rolled off below 40 Hz (−0.26 dB at 40 Hz before). Cost: none.
- Ruling: the character set stays Warm / Tape / Tube / Hard with the identical curves and names (Hard = the spec's Crunch/Clip tile;
  the panel may label it so). Old projects keep their sound; the four are verified clearly different (harmonic signature H2..H5
  ≥ 6 dB apart pairwise). Cost if wrong: Warm and Tube are both asymmetric tanh (different amounts).
- Ruling: the level compensation is unchanged (RMS match of a −12 dBFS sine). On a −18 dBFS vocal it keeps the level within
  −1.1 … +1.7 dB over 0–24 dB drive (test tolerance ±2 dB; Hard at 24 dB is the +1.7). A better compensator would shift the level
  of every old project with saturation on. Cost: up to 1.7 dB louder at extreme drive.
- Ruling: multiband band drives are absolute (0–24 dB, default 6 = the satDrive default); in multiband mode satDrive / satCharacter
  are not used. "Link" (satLink, default on) is a panel behaviour: `dsp::linkedBandDrives` moves all band drives by the same delta,
  limited at the range ends, so automation of one band never writes the others. Macros (Warmth, Aggression) add their drive to the
  single-band drive and to every band. Cost if wrong: the UI track must call linkedBandDrives when Link is on.
- Ruling: multiband defaults: crossovers 250 Hz / 4 kHz (body / presence split for vocals), Low Warm, Mid Tape, High Tape, all on;
  high crossover kept ≥ 1.5 × low and ≤ 0.45 × fs. Cost: taste only.
- Ruling: LR4 = two cascaded TPT state-variable Butterworth sections (double) at the high rate; the low band passes the 2nd-order
  all-pass of the high crossover, so the sum is exactly AP(fc1)·AP(fc2) (null vs the analytic all-pass −146 dB). A band that is off
  passes linearly (same ½-sample average), crossfaded 20 ms.
- Ruling: latency per Saturation instance 3 → 5 samples at 48 kHz (constant, reported; still independent of settings). Cost: hosts
  re-compensate once when an old project loads.
- Ruling (bench): `Calib.also` (modules.py) applies the calibrated value to further parameters (all Saturn band drives); the old
  cases are unchanged. New case `tape3band` (Saturn 2: 3 bands, 250 / 4000 Hz, 24 dB/oct, Warm Tape everywhere).
