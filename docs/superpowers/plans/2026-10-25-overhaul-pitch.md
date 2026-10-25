# Pitch (DSP quality) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Clean ±12 st on dry vocals at Graillon 3 / Little AlterBoy class: exact ratio, ≤ 30 ms constant latency, natural formants, no phasiness on sustained notes, CPU ≤ 1.5 %.

**Architecture:** Replace the Signalsmith phase vocoder (120 ms, bin-grid dependent ±7 ct error) by a time-domain engine:
a pitch-synchronous two-head resampling splicer (read heads move at the ratio ρ, jumps by a whole number of detected periods,
refined by normalized cross-correlation, raised-cosine crossfade) – exact frequency ratio, no phase vocoder, no phasiness – plus an
LPC formant corrector (cepstrally smoothed envelope of the input, warped by ρ and the formant factor, two Levinson fits, lattice
filters A_y/A_t with per-sample interpolated reflection coefficients). A decimated YIN tracker supplies the period. Constant
latency L = ⌈fs/70⌉ + 16 samples (≈ 14.6 ms at 48 kHz), independent of the settings.

**Tech Stack:** C++20, JUCE 8.0.9 (juce::dsp::FFT), VelvoxTests (juce::UnitTest), tools/refhost bench (Python 3.13 + numpy/scipy).

**Spec:** `docs/superpowers/specs/2026-10-25-headliner-pro-overhaul-design.md` §5.10, §7, §8 (Q6, Q9, quality fixes); `docs/QUALITY_BASELINE.md` gap #3.

## Global Constraints

- Never rename/remove parameter IDs; new params default to the old behaviour (none planned).
- Real-time safe audio thread: everything allocated in `prepare()`; no locks/logging/allocation in `process()`.
- Latency constant while the instance exists; reported via `VocalChain::getLatencyFor` (pitch on = L, off = 0) as today.
- Mix stays the existing true crossfade with the latency-aligned dry (no MixLaw involvement in Pitch).
- Do not edit `Source/UI/*`. Files edited only via Edit/Write tools.
- Gates (modules.py): tone error ≤ 2 ct, jitter ≤ 1 ct, spurious ≤ −60 dB, vocal error ≤ Graillon + 1 ct, latency ≤ Graillon + 10 ms, CPU ≤ ref + 2 %; new: formant shift ≤ 1/3 oct (F1/F2, VocalSet long vowels, preserve on, ±12 st).

## Review Focus

1. Ratio/formant parameter automation while playing – heads may be outside their band: must clamp, never read outside the buffer, no click > tolerance.
2. Silence / denormals / NaN input – tracker and Levinson must not divide by zero; output stays finite and silent.
3. Sample rates 44.1 / 96 kHz and host blocks of 1..2048 – all lengths scale with fs; process is per-sample, block size independent.
4. Unvoiced / polyphonic / noise input – no period: fixed 8 ms splices with best-correlation alignment, equal-power fade, no level pumping > 1.5 dB.
5. Pure tones (the bench sine) with preserve on – corrector gain clamped to ±24 dB, output stays a clean sine (spurious ≤ −60 dB).

---

### Task 1: Failing quality tests (root cause pinned)

**Files:** Modify `Tests/PitchTests.cpp`, `Tests/PitchDiagnostics.cpp` (root-cause scan, env-gated).

- [ ] Diagnostic scan of the 0.5 engine: sine 220/233/311/440 Hz × {+12,+7,+1,−5,−12} st, comp on/off → error varies −4…+7 ct with the input frequency at fixed shift ⇒ not semitone→ratio, not sample rate: engine-internal peak mapping on the FFT bin grid.
- [ ] Tests (fail on the old engine): `tone accuracy` (220, 233, 311, 440 Hz; +12, +7, +1, −5, −12; both preserve settings; |err| ≤ 1 ct);
  `latency ≤ 30 ms and constant across settings`; `formants within 1/3 oct at ±12 st` (synthetic vowel buzz, F1/F2 from LPC envelope);
  `sustained note: no amplitude roughness` (harmonic tone 180 Hz, +7 st, envelope std of 10 ms RMS ≤ 0.5 dB); `spurious on a sine ≤ −60 dB`;
  `silence/NaN robustness`; `44.1/96 kHz accuracy`.
- [ ] Run: `VelvoxTests.exe --test "Velvox pitch"` → FAIL on accuracy/latency/formant. Commit.

### Task 2: PitchTracker (decimated YIN)

**Files:** Create `Source/DSP/PitchTracker.h/.cpp`; add to `VELVOX_SOURCES`. Test in `Tests/PitchTests.cpp`.
**Produces:** `void prepare (double fs); void reset(); void push (float mono) noexcept; float getPeriod() const noexcept` (full-rate samples, 0 = unvoiced); `float getAperiodicity() const noexcept`.
- [ ] Test: sines 80/220/600 Hz → period within 0.1 %; noise → unvoiced. Implement (decimate by round(fs/12000) after 4th-order LP, W = 21 ms, fmin 65 Hz, hop 5 ms, threshold 0.15, parabolic). Commit.

### Task 3: FormantCorrector

**Files:** Create `Source/DSP/FormantCorrector.h/.cpp`. **Produces:** `prepare (double fs, int latency)`, `setWarp (float rho, float target, bool active)`, `void analyse (const float* monoHistory…)` driven internally by `pushInput (float mono, float period)`; `void process (float* const* ch, int nCh, int n)`.
- [ ] Test: lattice A/1/A round trip = identity (fixed k, −120 dB); warp 1/1 → identity filter; buzz vowel warped ρ=2 then corrected → F1/F2 back within 1/3 oct.
- [ ] Implement: frame 1024@48k (scaled), hop 5 ms, centre = L behind the newest input; cepstral lifter 0.6·P (24…300 @48k, 72 unvoiced); P_y(f)=E(f/ρ)², P_t(f)=E(f/target)², log ratio clamped ±24 dB, floor −70 dB; order 32 Levinson; reflection coefficients interpolated per sample. Commit.

### Task 4: SpliceShifter + PitchModule integration

**Files:** Create `Source/DSP/SpliceShifter.h/.cpp`; rewrite `Source/DSP/PitchModule.h/.cpp` internals (public API unchanged); update `pitchOn` description in `Source/Parameters/ParameterLayout.cpp` (latency text).
- [ ] Implement: buffer pow2 ≥ L+J_max+16; ρ>1 anti-alias 12th-order Butterworth at 0.40·fs/ρ; heads trigger at L∓J/2±|ρ−1|C/2; J = k·P ≥ 4 ms ≤ J_max; C = min(0.95·J/|ρ−1|, max(J, 10 ms)); NCC refine ±(0.1P+2) (unvoiced: J = 8 ms, ±J/3 coarse); Hermite cubic read.
- [ ] Task 1 tests pass; full `"Velvox pitch"` + MixLaw pitch audits green. Commit.

### Task 5: Bench, listening proxies, docs

**Files:** `tools/refhost/bench.py` (new `pitch_formant` procedure: VocalSet f7/m8 long vowels a/i/u; LPC envelope F1/F2 shift in octaves; `sustain_roughness_db`), `tools/refhost/modules.py` (tolerances), `docs/QUALITY_BASELINE.md` pitch block (`--update-doc`, after renders to `…\2026-10-25\modules-after\pitch`).
- [ ] Run bench with refs; ≤ 3 rounds; document remaining gaps; full suite `-Test "*"` green; ledger. Commit.
