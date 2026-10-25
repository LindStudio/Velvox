# Reverb DSP quality (Headliner 0.6, track "reverb") Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reverb at Valhalla VintageVerb / FabFilter Pro-R 2 level on dry vocals for all 8 room types: no modal ringing, T30 = set decay ± 10 %, plus the "Raumtiefe" depth path and a decay-display data feed for the panel.

**Architecture:** Keep the 16-line FDN of `Source/DSP/ReverbModule.*` (Hadamard mixing, per-line Jot absorption from the actual loop length). Quality fixes inside it: (a) a broadband decay compensation computed from the analytic per-frequency RT60 of the loop filters, so the T30 the listener hears (all frequencies, not just the mids) equals the set decay; (b) smooth random-walk delay modulation with different rates per line instead of one sine per line, six instead of four input all-passes with incommensurate lengths, and in-loop density all-passes kept short against their lines so their group-delay peaks do not make single modes outlive their neighbours. New pure helpers (no audio-thread state): `ReverbModule::predictedRt60` (decay display) and `reverbDepthPath` (depth dot → mix / pre-delay / size).

**Tech Stack:** C++17, JUCE 8.0.9 (`juce::dsp::FFT` in tests), VelvoxTests (juce::UnitTest), tools/refhost bench (Python 3.13 + numpy/scipy).

**Spec:** `docs/superpowers/specs/2026-10-25-headliner-pro-overhaul-design.md` (5.12, 6, 7, 8 Q6 + "Quality fixes vs. old projects"); gaps from `docs/QUALITY_BASELINE.md` #4.

## Global Constraints

- Never rename/remove parameter IDs or plugin codes; new params only added, default = old behaviour (no new parameter is needed here).
- Real-time safe audio thread: no allocation, locks or logging in `process()` / `setSettings()`.
- Keep ducking, the mix law (`Source/DSP/MixLaw.*`, `mixLaws`, `lawMix`, `dryGain`) and every existing reverb parameter and its meaning.
- Pure quality fixes may change old projects slightly (list for the CHANGELOG); no character changes.
- Do not edit `Source/UI/*`.
- Q6 gate (tools/refhost/modules.py, never loosen): ringing ≤ VintageVerb + 2 dB; T30 within 10 % of the set decay; curvature ≤ ref + 0.5 dB; density ≥ ref − 0.05; mixing ≤ ref + 20 ms; dry ≤ −60 dB; CPU ≤ ref + 2 %. At most 3 improvement rounds per gap.
- After-renders go to `C:\Users\jakob\dev\velvox-bench\2026-10-25\modules-after\reverb\` (`bench.py reverb --out ...\modules-after`).

## Review Focus

1. Extreme settings (decay 20 s, bass ×2, damping 20 kHz, size 1, modulation 1): the compensated loop gain must stay < 1 at every frequency — covered by the existing "stable at the longest decay" test plus a new loop-gain check with the compensation applied.
2. Automating damping / low cut / high cut while a tail rings: the compensation must glide (smoothed), never step — covered by the existing click test extended with damping and cut sweeps.
3. Very short decays (0.2–0.5 s) with long-line types (Atmosphere, Cinematic): build-up dominates the T30 fit; the compensation must not blow the decay up — test clamps comp to [1, 1.6] and checks 0.5 s for all types.
4. Mono hosts (numChannels 1) and 44.1 / 96 kHz: modulation and diffuser lengths scale with fs — RT60 test at 44.1 and 96 kHz for one type.
5. Depth path inputs outside 0..1 / unknown type index: clamped, never NaN — covered in the depth tests.

---

### Task 1: Reverb quality test harness + decay accuracy (T30 = set ± 10 % for all types)

**Files:**
- Create: `Tests/ReverbQualityTests.cpp` (UnitTest "Headliner reverb quality", category "Headliner"), `Tests/ReverbMetrics.h` (broadband Schroeder T30 −5..−35 dB of the channel energy sum, per-octave T30, modal ringing = port of `qmetrics.modal_ringing_db`)
- Modify: `CMakeLists.txt` (add the test file), `Source/DSP/ReverbModule.h/.cpp`

**Interfaces:**
- Produces: `static float ReverbModule::predictedRt60 (const ReverbSettings& s, float hz, double sampleRate = 48000.0) noexcept` (RT60 in s at `hz` incl. compensation, for the decay display); `float ReverbModule::decayCompensation() const noexcept` (current factor, for tests).

- [ ] Step 1: tests `T30 within 10 % of the set decay` for types 0..7 × decays {0.5, 1.0, 1.8, 2.6, 6.0} at default settings (damping 7000, low cut 200, high cut 12 k, size 0.6) and at the bench hall settings (damping 6500, low cut 150, size 0.7); `predictedRt60 tracks the measured octave T30` (500 Hz, 1 kHz, 4 kHz within 15 %); 44.1 / 96 kHz T30 for Hall.
- [ ] Step 2: run — FAIL (broadband T30 10–30 % short; `predictedRt60` missing).
- [ ] Step 3: implement `computeDecayCompensation()` (24 log-spaced frequencies between low cut and min(high cut, 0.45 fs), white-noise band energy weights through the cut filters, per-frequency RT60 from |gMid · H_jot · H_bass| of the mean line, EDC sum of exponentials, two-point −5/−35 dB slope, comp = set / predicted, clamped [1, 1.6]); only recomputed when type / damping / bass / cuts / size targets change; applied through a multiplicative SmoothedValue to the T60 used in `updateDecayFilters()`.
- [ ] Step 4: run — PASS; full reverb suites green.
- [ ] Step 5: commit.

### Task 2: No modal ringing (random-walk modulation, 6 input all-passes, short in-loop all-passes)

**Files:** Modify `Source/DSP/ReverbModule.h/.cpp`, `Tests/ReverbQualityTests.cpp`

**Interfaces:** Consumes `Tests/ReverbMetrics.h` modal ringing. `numDiffusers` becomes 6.

- [ ] Step 1: test `modal ringing <= 9.0 dB for every type` (1 s of tail from 100 ms, bench settings per type: decay 1.8 plate / 2.6 others, mod 0.3, diffusion 0.7) and `no line's mode outlives the others` (octave-band T30 spread 250 Hz–2 kHz within ±12 % of the 1 kHz value, all types).
- [ ] Step 2: run — FAIL on Plate (≈ 11 dB).
- [ ] Step 3: implement: per-line smooth random walk (deterministic LCG per line, new target each period 1/rate, raised-cosine glide; rates spread 0.35–1.6 × the voicing rate, incommensurate); voicing depths retuned (Plate deeper); diffusers 6 per side with lengths L {4.77, 3.59, 12.73, 9.31, 1.73, 7.13} ms / R {5.21, 3.17, 11.87, 8.63, 2.03, 6.71} ms; density all-pass length ≤ 25 % of its line; plate line lengths retuned if needed. Iterate with the bench (≤ 3 rounds).
- [ ] Step 4: run — PASS; FxQuality click/stability tests green.
- [ ] Step 5: commit.

### Task 3: Depth path ("Raumtiefe")

**Files:** Create `Source/DSP/ReverbDepth.h/.cpp`, add to `VELVOX_SOURCES`; tests in `Tests/ReverbQualityTests.cpp`.

**Interfaces:**
- Produces: `struct ReverbDepthPoint { float mixPercent, preDelayMs, sizePercent; };`
  `ReverbDepthPoint reverbDepthPath (int type, float depth) noexcept` (depth 0 = vocal in front, 1 = far back; parameter units of `reverbMix`, `reverbPreDelay`, `reverbSize`);
  `float reverbDepthFromSettings (int type, float mixPercent, float preDelayMs, float sizePercent) noexcept` (closest depth on the path, for placing the dot).

- [ ] Step 1: tests: for every type, mix strictly increasing, pre-delay non-increasing, size non-decreasing over 101 steps; end points depth 0 → mix ≤ 6 %, pre-delay ≥ 25 ms; depth 1 → mix ≥ 35 % and ≤ 70 %, size ≥ 80 %; all values inside the parameter ranges; out-of-range depth / type clamped; round trip `reverbDepthFromSettings (path(d)) ≈ d` (± 0.02).
- [ ] Step 2: run — FAIL (does not compile / link).
- [ ] Step 3: implement per-type end-point table + eased curves (mix ∝ d^1.5 between ends, pre-delay ∝ (1 − d)^1.2, size linear-ish smoothstep).
- [ ] Step 4: run — PASS. Step 5: commit.

### Task 4: Bench after-renders, listen checks, docs

**Files:** Modify `docs/QUALITY_BASELINE.md` (reverb block via `--update-doc`, gap text), `tools/refhost/modules.py` only if a tolerance is tightened; ledger.

- [ ] Step 1: build HeadlinerRefHost, `bench.py reverb --out C:\Users\jakob\dev\velvox-bench\2026-10-25\modules-after`; compare with the baseline; every gated metric pass.
- [ ] Step 2: listen proxies on the after-renders: clicks (max |d2| / p99), denormal-free CPU, CPU vs. baseline.
- [ ] Step 3: `--update-doc`; ledger; full suite `-Test "*"` green; commit.
