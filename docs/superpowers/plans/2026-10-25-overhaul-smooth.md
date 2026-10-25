# Smooth (soothe-class) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the 32-band dynamic-bell Smooth with an STFT resonance suppressor that equals or beats oeksound soothe2 on dry vocals
(QUALITY_BASELINE gap #5), with constant reported latency, a weighting curve and a thread-safe UI feed.

**Architecture:** Streaming WOLA STFT (periodic Hann analysis + synthesis, N = 1024 at 44.1/48 kHz scaled by powers of two with the
sample rate, hop N/4, latency exactly N). Per frame: stereo-linked power spectrum -> detection spectrum (1/6-oct smoothing in frequency,
instant attack / 10 ms release in time) -> robust spectral envelope (1-oct smoothing in dB, peaks clipped at envelope + 3 dB, smoothed
again) -> excess over envelope minus threshold (Sensitivity) plus the weighting curve (dB) -> soft-knee reduction x depth (Amount),
capped -> range weight (low/high) -> 1/12-oct frequency smoothing -> per-bin attack/release in dB. The module synthesises only the
*removed* part `X * (1 - g)` and subtracts it from the latency-aligned dry signal: with no reduction the output is the delayed input
bit for bit (transparent, Delta = removed signal). All smoothing filters are O(bins) running-sum box filters (two passes = triangle).

**Tech Stack:** C++17, JUCE 8.0.9 (`juce::dsp::FFT`), VelvoxTests (JUCE UnitTest), Python 3.13 bench (`tools/refhost`).

**Spec:** `docs/superpowers/specs/2026-10-25-headliner-pro-overhaul-design.md` §5.4, §6, §8 (Q6, Q7, Q9, quality fixes); `docs/QUALITY_BASELINE.md` #5.

## Global Constraints

- Never rename/remove parameter IDs; only add. New params default to the old behaviour (flat weighting = no change).
- Real-time safe audio thread: no allocation, locks or logging in `process`. All buffers sized in `prepare`.
- Latency: constant while the instance is in the chain (Sat model: delays by its latency also while switched off); ≤ 1024 samples at 48 kHz;
  included in `VocalChain::getLatencyFor` and the bypass-delay capacity; recomputed on sample-rate change (prepare).
- Amount 0 = transparent (exact null against the delayed input). Delta listen = exactly what is removed.
- Quality gate (Q6): `ltas_reduction_db ≥ soothe2 − 0.5`, `pte_reduction_db ≥ − 0.3`, `flatness_gain_db ≥ − 0.2`, `musical_noise_db ≤ + 0.2`,
  `transient_change_db` 0 ± 0.5, CPU ≤ soothe2 + 1 % and ≤ 2 % real time per instance at 48 kHz; max 3 improvement rounds.
- Do not edit `Source/UI/*`. Keep `numBands`, `bandFrequency`, `bandCutDb`, `getMaxCutDb` (used by the current UI/editor).
- Edit files only with Edit/Write (CRLF files), never PowerShell Set-Content.

## Review Focus

- Sample rates 44.1 / 88.2 / 96 / 192 kHz and tiny/odd block sizes: latency = FFT size, output still aligned -> test "latency and alignment at 96 kHz, block 37".
- Switching on/off while playing: no click, no transient from a stale STFT state -> test "on/off is click-free and off = pure delay".
- Silence / denormals / very loud input: output finite, no NaN, no CPU spike -> test "silence and full-scale input stay finite".
- Two instances in the chain: latency adds up and the bypass delay is large enough -> test in RoutingTests-style processor test.
- Weighting points dragged on top of each other / out of order -> curve stays finite and monotone between points -> test of `weightingDbAt`.

---

### Task 1: STFT engine with constant latency, transparency and alignment

**Files:** Modify `Source/DSP/SmoothModule.h/.cpp`; Test `Tests/NewModuleTests.cpp` (`smoothTests`).

**Interfaces – Produces:**
- `static int SmoothModule::fftSizeFor (double sampleRate) noexcept` – 1024 at 44.1/48 kHz, 2048 at 88.2/96, 4096 at 176.4/192, 512 below 32 kHz.
- `int SmoothModule::getLatencySamples() const noexcept` – = FFT size after `prepare`.
- `SmoothSettings` unchanged meaning; module always delays by the latency (also while off).

- [ ] Tests (fail on the old zero-latency engine): "SMOOTH: constant latency = FFT size (48/96 kHz)"; "SMOOTH: Amount 0 and off are an
  exact delayed copy" (max |out − in(t−L)| < 1e-6); "SMOOTH: reduction keeps the phase" (3 kHz resonance on noise: out ≈ a·in(t−L) at the
  tone, residual of the tone after the fit ≤ −30 dB); "SMOOTH: on/off click-free" (max sample step at the switch ≤ 2× the input's).
- [ ] Implement the WOLA engine (removed-part synthesis), run tests, commit.

### Task 2: Detection, calibration to the old Amount scale, flat-noise transparency

**Files:** `Source/DSP/SmoothModule.*`, `Tests/NewModuleTests.cpp`.

- [ ] Tests: existing three SMOOTH tests stay green; new "SMOOTH: Amount 0 transparent on a vocal-like signal" and "SMOOTH: removes
  peaks not broadband" (synthetic vocal from `SyntheticVocal.h` with a +10 dB 3.2 kHz resonance: resonance reduction ≥ 3 dB while the
  1–10 kHz level away from it (±1/2 oct excluded) changes < 0.5 dB) – fails on the old engine (it dulls broadband).
- [ ] Depth law `depth = 0.36·A^0.75` (soft) / ×1.3 (hard), cap `3 + 9A` dB (soft) / `6 + 18A` (hard), threshold `10 − 6·sensitivity` dB,
  knee 3 dB; tune on the bench so removed energy at A = 75 % ≈ old (−13.5 dB mean over the six clips).

### Task 3: Weighting curve parameters

**Files:** `Source/Parameters/ParameterIDs.h`, `ParameterLayout.cpp`, `SettingsReader.h/.cpp`, `SmoothModule.*`, Tests.

**Interfaces – Produces:**
- IDs `smoothW{1..6}Freq` (20–20000 Hz, defaults 150, 400, 1000, 2500, 6000, 14000) and `smoothW{1..6}Gain` (−12…+12 dB, default 0).
- `SmoothSettings::weightFreq`, `weightGain` (`std::array<float, 6>`).
- `static float SmoothModule::weightingDbAt (const SmoothSettings&, float hz) noexcept` – curve through the points (sorted by frequency),
  monotone cubic (Fritsch–Carlson) in log2 f, flat beyond the outer points. Positive dB = more reduction (lowers the threshold by that much).
- [ ] Tests: flat default = 0 everywhere; curve passes through each point; finite with duplicate/out-of-order points; a −12 dB weight at
  3 kHz stops the 3 kHz reduction, +12 dB elsewhere increases reduction; parameters exist for instance 2 (`smoothW1Gain_2`).

### Task 4: UI feed

**Interfaces – Produces:** `struct SmoothSpectrum { int numBins; float binHz; std::array<float, maxBins> inputDb, reductionDb; }`
(`maxBins = 2049`), `bool SmoothModule::getSpectrum (SmoothSpectrum&) const noexcept` (SeqLock, wait-free; published every frame),
`static std::pair<float,float> SmoothModule::speedToTimes (float speed01) noexcept` (linked Speed control -> attack, release ms).
- [ ] Tests: after a resonance, `getSpectrum` shows reduction > 3 dB within 1/4 oct of 3 kHz and inputDb peak there; read from another
  thread while processing never tears (numBins consistent); `bandCutDb` still points at the resonance.

### Task 5: Chain latency

**Files:** `Source/DSP/VocalChain.cpp` (`getLatencyFor`, bypass-delay capacity).
- [ ] Tests: processor with smooth in the routing reports FFT-size latency (two instances: 2×), unchanged when smoothOn toggles;
  at 96 kHz doubles; bypass output aligned (global bypass null vs. processed-with-Amount-0).

### Task 6: Bench vs soothe2 at several settings, listening renders, CPU, docs

**Files:** `tools/refhost/modules.py` (cases `light` A 35, `vocal` A 75, `sharp` sens 85, `hard`), `docs/QUALITY_BASELINE.md` (smooth block),
ledger. Renders to `velvox-bench\2026-10-25\modules-after\smooth\`, plus sibilant/harsh/boxy clip renders. Full suite green.
