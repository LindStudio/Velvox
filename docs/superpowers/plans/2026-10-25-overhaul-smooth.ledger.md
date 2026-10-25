# Ledger – overhaul/smooth (Headliner 0.6, Smooth DSP quality)

Task 0: merged feature/automix (EQ track, fast-forward f592357) before any change; baseline suite 22255 checks, 0 failed.
Task 1: complete (STFT engine: FFT 1024 @ 44.1/48 kHz, x2 per rate doubling, hop N/4, latency = N; removed-part synthesis;
  tests "constant latency", "Amount 0 and off are an exact delayed copy", "keeps the phase", "on/off click-free" – first two failed
  on the old engine, the phase / click tests passed on both)
Task 2: complete (harmonic-peak-contour detection, 1.5-oct robust envelope, depth law calibrated to the old removed energy;
  tests "narrow resonance on a steady voice" (the old engine does nothing there: RefHost render of the same signal 0.00 dB),
  "on a voice ... not broadband", "hard mode deeper"; old tone test threshold changed, see Ruling 6)
Task 3: complete (weighting curve smoothW{1..6}{Freq,Gain}, `weightingDbAt`, read per instance; tests failed to compile before)
Task 4: complete (`getSpectrum` SeqLock feed, `speedToTimes`; implemented together with Task 1, the tests were written afterwards
  and passed at once – not watched failing)
Task 5: complete (VocalChain::getLatencyFor + bypass-delay capacity include every smooth instance in the routing; test
  "SMOOTH in the chain" failed before: latency 0, bypass misaligned)

## Rulings

- Ruling 1: latency model = Saturation's (the instance delays by its latency whenever it is in the routing, also while off) —
  the brief says "constant while an instance exists", and on/off then never changes the host latency — cost if wrong: an
  idle-but-present Smooth adds 21 ms (1024 samples) latency; the user removes the tile to get rid of it.
- Ruling 2: FFT 1024 / hop 256 at 48 kHz (latency 1024 = the spec's limit), scaled with the rate (2048 @ 96 kHz, 4096 @ 192 kHz,
  512 below 32 kHz) — 2048 (soothe2's latency) is not needed: the 1024 engine already beats soothe2 on every gated metric —
  cost if wrong: slightly coarser resolution below ~150 Hz (irrelevant for the 800 Hz default low limit).
- Ruling 3: synthesise only the removed part X·(1−g) and subtract it from the delayed input — Amount 0 / nothing to remove is a
  bit-exact delayed copy and Delta = exactly the removed signal — cost if wrong: none found (one extra subtraction).
- Ruling 4: detection on the harmonic-peak contour (running max over ±100 Hz or ±1/12 oct) — a plain smoothed spectrum takes the
  ripple between the partials of high voices for resonances (a 450 Hz harmonic voice lost 1.1–2.7 dB broadband with no
  resonance present); the contour removes most of that — cost if wrong: very narrow resonances between two partials are seen
  slightly weaker (Q12 resonance on a 150 Hz voice: −0.5…−0.9 dB vs soothe2 −1.1 dB at equal vocal action).
- Ruling 5: Amount mapping `depth = 0.145·√A` (×1.3 hard), cap 3+9A dB (6+18A hard), threshold 3 − 2.5·Sensitivity dB above the
  envelope, soft knee 3 dB, progressive slope (doubles at 6 dB excess) — calibrated so the removed energy on the six bench
  clips matches 0.5 at the same Amount (A 25/50/75/100: −17.1/−14.7/−13.3/−12.4 dB vs old −17.0/−15.0/−13.5/−12.3) — Amount 0 is now
  transparent (old: −20 dB removed at 0 %) — cost if wrong: Amount feels different on non-vocal material.
- Ruling 6: (withdrawn) an intermediate tuning needed a lower threshold in the 0.3 tone test; the final engine cuts that
  +23 dB tone by ~10 dB at Amount 80 %, so the original > 4 dB expectation is kept unchanged.
- Ruling 7: weighting curve = 6 points (`smoothW{1..6}Freq` 20–20000 Hz, defaults 150/400/1000/2500/6000/14000; `smoothW{1..6}Gain`
  −12…+12 dB, default 0), Fritsch–Carlson monotone cubic in log f through the points, flat outside; + dB lowers the threshold by
  that much and scales the maximum cut by 2^(dB/12) — six free points match soothe2's 4 bands + 2 cuts and are simpler to drag
  than typed bands — cost if wrong: no per-point Q (width follows the point spacing).
- Ruling 8: Speed (main control) is a UI link of the existing attack/release (`SmoothModule::speedToTimes`, 0.5 = 5/80 ms,
  log-scaled 25/400 … 1/16 ms) – no new parameter — cost if wrong: the panel must write two parameters per gesture (one undo step).
- Ruling 9: when an instance joins the chain mid-stream (`reset()`), the module stays silent until its delay line is refilled
  (one FFT length) and then fades in over 8 ms — otherwise the signal restarted abruptly after the 21 ms gap (step 0.20 in the
  module-toggle test, limit 0.05) — `prepare()` does not do this (fresh start = real silence) — cost if wrong: a 21 ms gap + fade
  when Smooth is first switched on while playing.
- Ruling 10: bench: `Calib.step` (bisection on a value grid) for soothe2 depth (it rejects values like 1.25 and the reference
  was then dropped); extra cases light (A 35), sharp (Sens 85), hard (mode hard, A 60, soothe2 hard), each calibrated —
  backwards compatible (step 0 = old behaviour).
- Ruling 11: tempo-locked Stutter / Pulse after latent modules follow the song position delayed by the latency in front of
  them (`VocalChain::latencyBefore`) — Smooth sits before them in the default order, and with 21 ms latency the gates were off
  the grid once the host compensated the latency (test "Stutter / Pulse after a latent SMOOTH stay on the host grid" failed:
  0.30 / 0.17 difference) — also fixes the same offset behind Saturation (3 samples) and Pitch (120 ms) – a quality fix for
  old projects with Pitch before Stutter/Pulse (CHANGELOG) — cost if wrong: none known; free-running (not playing) unchanged.
- Ruling 12: the 0.3 migration test compared the pruned chain against "every module" including an idle Smooth; Smooth now
  delays even when idle, and the tempo-locked Stutter behind it then gates a shifted signal, so that comparison chain drops
  Smooth (its idle behaviour is covered by "SMOOTH in the chain") — cost if wrong: none; pruning itself is unchanged.
Task 6: complete (bench smooth: 26 metrics, 0 FAIL in 4 cases; after-renders + listening files in
  velvox-bench\2026-10-25\modules-after\smooth\ (listen\, extra\ sibilant/harsh/boxy incl. old Smooth, soothe2 at equal
  removed energy, deltas); CPU ≈ 1.1 % real time per instance at 48 kHz (1.31 % processor with Smooth on − 0.18 % idle);
  QUALITY_BASELINE smooth block updated)
