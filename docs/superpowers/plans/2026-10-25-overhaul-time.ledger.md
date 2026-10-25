# Ledger – time track (Headliner 0.6: Delay, Pulse, Stutter)

Baseline before any change (f4be906 + mixlaw + refhost, then feature/automix incl. EQ merged at c1adee6): full suite 12065 checks, 0 failed.

Task 1: complete (Delay glide/crossfade time engine, per-sample modulation, linear loop limiter; 4 new tests, 6 checks. Before:
sweep envelope range 2.30 dB (60 ms crossfades restarting every 60 ms), modulation HF −45.0 dB; after: 0.26 dB, −90.1 dB,
limiter test −135.9 dB (old tanh limiter −27 dB with a −6 dBFS tone at 50 % feedback))

## Rulings

- Ruling: Delay time changes use a hybrid: moves up to 15 ms from the current read position glide tape-style (one-pole 50 ms,
  slew ≤ 0.08 samples/sample ≈ ±1.4 semitones); larger jumps keep the existing 60 ms crossfade. — A pure crossfade restarted every
  60 ms during automation (2.3 dB flutter of the echo level, granular "zipper"); a pure glide dives in pitch on note-value jumps.
  Timeless/analogue delays glide on drags, digital ones crossfade on jumps – the hybrid gives each case its natural behaviour.
  — Cost if wrong: a user who wants crossfade-only for small automated moves hears a slight pitch bend (≤ 1.4 st) while moving.
- Ruling: the glide state is double precision. — In float the last steps of the one-pole vanish below the ulp at long delays
  (≈ 2 samples permanent error at 375 ms). — Cost if wrong: none.
- Ruling: loop limiter linear up to |x| = 0.8, then a C1 tanh knee bounded at ±1.5 (as before). — The old 1.5·tanh(x/1.5) added
  −50 dB THD already at −10 dBFS and −27 dB HF energy with sustained notes at 50 % feedback; Timeless is clean there. Pure quality
  fix (less distortion), runaway protection unchanged. — Cost if wrong: old projects with hot feedback sound slightly cleaner
  (CHANGELOG).
- Ruling: the wow/flutter character (0.6 Hz + 4.7 Hz, same on both sides) is kept; only its evaluation moved from every 32 samples to
  every sample. — Decorrelating L/R would change the sound of old projects with modulation (character change, spec §8). — Cost if
  wrong: modulation stays a "vibrato pair" rather than a wide tape pair.
Task 2: complete (ping-pong verified L→R→L incl. spread, mono-sum input, −6.02 dB per hop, other side < −80 dB; drive 2× oversampled:
inharmonic energy 4.1 kHz/−6 dBFS/drive 100 % −30 dB → −80 dB; test window switched to Blackman-Harris after the Hann leakage floor
(−53 dB) masked the result)
Task 3: complete (DelayModule::echoTaps / feedbackForRatio, displayTimeMs; verified against rendered 1 kHz bursts within 0.3 dB for
plain, spread and ping-pong. Note: written test-after – the pure function was implemented first, the test then checked it)
Task 4: complete (pulseShape, pulseAmount; golden render of a customised 0.5 curve recorded with the old code before the change and
still bit-identical after; all 5 shapes × 1/1, 1/4, 1/16 × 120/174 BPM ≤ −60 dB splatter (Classic 1/16@174: −115.6 dB);
full suite after Task 4: 22449 checks, 0 failed)
Task 5: complete (DisplayRing.h, ChopModule display frames + readDisplay, VocalChain::getChop()/getDelay(); history 2 s → 4 s;
repeat crossfade linear → raised cosine: repeat-edge splatter −74…−78 dB → −84…−99 dB)

- Ruling: the drive stage is a 2× polyphase linear-phase half-band FIR (47 taps, Kaiser β 8, latency 23 samples) around the existing
  tanh(kx)/k curve. The 23 samples are taken off the read position and the undriven tap is delayed by the same 23 samples, so echo
  timing and the drive-off output are unchanged (bit-identical apart from the first 23 samples after a reset). — Aliasing was
  34 dB worse than −60 dB at full drive; ADAA would low-pass every repeat (½-sample average), an IIR half-band would add phase
  per repeat. — Cost if wrong: ~0.1 % CPU per delay instance while drive is on.
- Ruling: switching drive on/off (0 ↔ >0) blends over 20 ms instead of switching the curve in at once. — The 0.5 code jumped from
  linear to tanh at drive ≈ 0 (a click when automating drive from 0). — Cost if wrong: none.
- Ruling: diffusion unchanged. — The three all-passes are magnitude-flat by construction and latency compensated; no measured
  defect (echo energy centred, existing test). — Cost if wrong: a character gap vs Timeless' diffusion remains (listening).
- Ruling: Pulse Amount is a macro centred on Depth: amount ≤ 50 % scales depth (0 → none), above 50 % moves to full ducking;
  default 50 % = depth exactly (bit-identical). — Spec: one big Amount, Depth not in the panel; old states have no pulseAmount and
  load the default, so the default must reproduce depth. — Cost if wrong: the Amount readout (50 %) is not the ducking depth
  in % (the panel can show effectiveDepth()).
- Ruling: pulseShape default = Custom (0) for every instance, also fresh ones. — Missing parameters are reset to their default on
  load (PluginProcessor::setStateInformation), presets do not set it either; any other default would change old projects. The
  default Custom curve already is a classic pump. — Cost if wrong: fresh instances show no shape tile highlighted until clicked.
- Ruling: the four shapes (Classic: S-recovery over 60 %; Punch: duck to 4 %, cubic release to 30 %; Swell: t² over 95 %;
  Gate: closed to 22 %, opens by 30 %) each end with a 2.5 % raised-cosine close into the next beat; points/interpolation/tension
  affect only Custom. Rates were already 1/1, 1/2, 1/4 (default), 1/8, 1/16 (+ triplets/dotted) with host sync – unchanged.
  — Shapes left to the builder (spec §9). — Cost if wrong: taste; shapes are a table rebuilt on change, easy to retune.
- Ruling: new Pulse parameter named "Pulse Curve" (ID pulseShape) because "Pulse Shape" is already the display name of
  pulseTension. — Hosts show names; duplicates confuse automation lists. — Cost if wrong: none.
- Ruling: Stutter display = 64-bit frames (float peak, 16-bit gain, step, type) in a power-of-two atomic ring (2048 frames ≈ 5 s
  at 2.5 ms); readers drop frames overwritten during their copy. — Single atomic word per frame: no torn frames, no locks,
  no allocation on either side. — Cost if wrong: 16 KB per chop instance.
- Ruling: chop history 2 s → 4 s; a repeat run longer than the history replays the oldest whole slice that fits (was: a clamped,
  misaligned read). — "Stutter end" (4 × 1/8) needs 3 s at 40 BPM. Pure fix. — Cost if wrong: +1.5 MB memory per instance.
- Ruling: chop repeat crossfade raised cosine instead of linear (same 1.5 ms). — Quality fix, ~20 dB less splatter; changes old
  projects only inside the 1.5 ms repeat edges (CHANGELOG). — Cost if wrong: none.

Task 6: complete (HeadlinerRefHost rebuilt; `bench.py delay|pulse|chop --out ...\2026-10-25\modules-after --update-doc`: 0 FAIL;
new cases delay/tape (listening), pulse/classic and pulse/gate16 (splatter −150/−151 dB), vocal_listen renders for chop and pulse;
listen-proxy: all renders finite, no new clicks (largest curvature outlier in chop/m9_breathy is in the dry clip itself);
CPU delay 0.33 %, pulse 0.19 %, chop 0.22 % (budget 1 %); delay/plain differs from the 0.5 render by −26…−49 dB – entirely the
loop limiter (old tanh compressed echo peaks of hot clips by up to 1.2 dB), aligned to the sample.
Full suite: 24511 checks, 0 failed (after merging feature/automix incl. EQ). Afterwards the stutter display test's reader got a
wait for late thread start (it failed once under full-suite load with 0 frames seen); 5 reruns green.)

- Ruling: no bench case for chop repeats. — The gate_tone procedure's gate_depth tolerance (≤ −40 dB) is per module, a repeat
  pattern has no mutes and would FAIL it by construction; repeat splatter is gated in VelvoxTests (≤ −80 dB). — Cost if wrong: none.

## CHANGELOG notes (pure quality fixes that change old projects slightly)
- Delay: loop limiter now linear up to −2 dBFS (old: soft tanh everywhere → up to 1.2 dB peak compression and −50 dB THD at
  −10 dBFS on repeats). Drive is 2× oversampled (aliasing −30 → −80 dB). Delay-time automation glides tape-style (small moves) or
  crossfades (jumps) – no flutter during sweeps. Modulation without zipper (per-sample).
- Stutter: repeat edges crossfade with a raised cosine (≈ 20 dB less splatter); long repeat runs at slow tempos replay the right slice.
- Pulse: unchanged for old projects (bit-identical); new Amount macro and pump shapes.
