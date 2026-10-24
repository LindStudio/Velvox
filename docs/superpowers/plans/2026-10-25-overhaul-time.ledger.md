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
