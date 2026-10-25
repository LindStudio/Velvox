# Saturation 0.6 (antialiased, multiband) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Saturation at Saturn 2 (HQ) quality: aliasing ≤ −67 dB at equal THD, 4 distinct loudness-compensated characters,
and an optional Low / Mid / High multiband mode with phase-compensated Linkwitz-Riley crossovers, plus a UI data feed.

**Architecture:** One oversampled section per module instance: 4× polyphase-IIR up (two half-band stages, hiir design),
all shapers (first-order ADAA on the static curves, double precision) and the dry/wet mix run at the high rate, then a
half-band down stage and an elliptic low-pass (stop ≥ base Nyquist) before plain decimation. Multiband splits the
oversampled signal with LR4 crossovers (TPT state-variable filters, the low band all-pass compensated), so one
oversampler serves all three shapers. Constant integer latency (dry delay = group delay of the linear chain at 1 kHz).

**Tech Stack:** C++20, JUCE 8.0.9 (no juce::dsp::Oversampling any more in this module), VelvoxTests (juce::UnitTest),
tools/refhost bench (Python 3.13, numpy/scipy) against FabFilter Saturn 2.

**Spec:** `docs/superpowers/specs/2026-10-25-headliner-pro-overhaul-design.md` §5.6, §8 (Q6, quality fixes, Q7, Q9);
`docs/QUALITY_BASELINE.md` gap #1; `C:\Users\jakob\dev\velvox-wt\AGENT_RULES.md`.

## Global Constraints

- Never rename/remove parameter IDs or plugin codes; only add IDs; new params default to the old behaviour (`satMultiband` off).
- Old projects sound identical apart from the pure quality fix (less aliasing, wet path band-limited at ~20 kHz/48 kHz,
  latency per Saturation instance changes once, constant) – CHANGELOG note.
- Real-time safe: no allocation / locks / logging in `process`.
- Mix law code (MixLaw.*, mixLaws, lawMix, dryGain) untouched; Saturation keeps its true crossfade.
- No edits in `Source/UI/*`. No pushing, no other worktrees.
- Q6 gate: `aliasing_db`, `imd_db` ≤ Saturn 2 (HQ) + 3 dB at equal THD (`thd_1k_db` ± 1 dB). At most 3 rounds per gap.
- CPU within `cpu_pct` ≤ Saturn 2 + 1 % (single band) per instance.

## Review Focus

1. Automating Mix to 0 % or switching the module on/off while audio plays – expected: no click (the oversampled path and the
   pure delayed dry differ in HF phase, so the switch must crossfade). Test in Task 1.
2. Partial Mix (auto-mix uses 25–45 %) – expected: no comb filtering between dry and wet. Test in Task 1 (dry mixed inside
   the oversampled domain, ADAA half-sample delay matched).
3. 44.1 / 88.2 / 96 kHz sessions – expected: same quality, constant latency, finite output. Test in Task 1.
4. Crossovers dragged so that High ≤ Low – expected: no instability, the high crossover is kept ≥ 1.5 × low. Test in Task 3.
5. Multiband on with all bands off (or drive tiny) – expected: a flat all-pass sum (no notch at the crossovers). Test in Task 3.

---

### Task 1: Antialiased single-band core (4× IIR oversampling + ADAA, mix at the high rate)

**Files:**
- Create: `Source/DSP/SatOversampler.h`, `Source/DSP/SatOversampler.cpp` (half-band up/down stages, elliptic decimator)
- Modify: `Source/DSP/SaturationModule.h/.cpp`, `CMakeLists.txt` (sources + `Tests/SaturationTests.cpp`)
- Test: `Tests/SaturationTests.cpp`

**Interfaces:**
- Produces: `class SatOversampler { void prepare (double baseRate, int maxBlock); void reset(); int factor() const;
  float* const* upsample (const float* const* in, int numCh, int n); void downsample (float* const* out, int numCh, int n);
  double groupDelayBaseSamples() const; }`
- Produces: `SaturationModule::shape (float, float, int)` and `compensationFor (float, int)` unchanged (UI + tests use them);
  new `static double antiderivative (double x, double driveLin, int character) noexcept`.

- [ ] Step 1: tests (fail on old code): `aliasing at equal THD` (4987 Hz −6 dBFS, drive 12, Tape and Tube ≤ −67 dB; all
  characters at drive 24 ≤ −62 dB), `THD calibration unchanged` (1 kHz −12 dBFS THD of every character within 0.3 dB of
  the static curve's THD computed from `shape` on one period), `linear chain flat` (drive 0, −50 dBFS sweep tones, |gain| ±0.1 dB
  20 Hz–18 kHz at Mix 100 and Mix 50), `constant latency` at 44.1/48/96 kHz, `mix 0 null ≤ −100 dB`, `no click on/off` (> 5 kHz
  splatter of a 200 Hz tone while toggling ≤ −60 dB rel. tone).
- [ ] Step 2: run, watch them fail (aliasing −36 dB, Mix 50 comb, click …).
- [ ] Step 3: implement `SatOversampler` (hiir coefficients: up 48→96 7 coefs tbw 0.0833, 96→192 3 coefs tbw 0.25; down
  192→96 the 3-coef stage; 96→48 an order-11 elliptic, wp 0.4167, ws 0.5 of the 96 kHz Nyquist, Rp 0.05, Rs 95, then decimate;
  base ≥ 88.2 kHz: 2× with only the steep stage). ADAA in double with fallback `shape (mid)` when |Δx| < 1e-7.
  Dry inside: `dryOS = ½ (u[n] + u[n−1])`; `out = dryOS + a · (g · DC (wet) − dryOS)`; DC blocker 10 Hz at the high rate;
  outer crossfade (20 ms) to the integer-delayed dry when the module is fully off.
- [ ] Step 4: run tests, then `-Test "*"`; commit.

### Task 2: Characters, loudness compensation, UI feed

**Files:** Modify `Source/DSP/SaturationModule.h/.cpp`; Test `Tests/SaturationTests.cpp`.

**Interfaces:**
- Produces: `static void transferCurve (int character, float driveDb, const float* x, float* y, int n) noexcept` (incl. compensation),
  `static std::array<float, 8> harmonicProfile (int character, float driveDb, float levelDb)` (H1..H8 dB rel. H1, message thread),
  `struct SatMeters { std::atomic<float> inDb[3], outDb[3]; }` via `const SatMeters& getMeters() const`.
- [ ] Step 1: tests: four characters pairwise different (harmonic signature distance ≥ 6 dB), loudness across drive 0–24 dB
  on a vocal-like signal within ±1.5 dB, `transferCurve` == shape·comp, `harmonicProfile` matches the rendered H2/H3 within 1 dB.
- [ ] Step 2: fail; Step 3: implement; Step 4: pass; commit.

### Task 3: Multiband (parameters, crossovers, per-band shapers, Link helper, macros)

**Files:** Modify `Source/Parameters/ParameterIDs.h`, `ParameterLayout.cpp`, `SettingsReader.h/.cpp`, `Source/DSP/SaturationModule.*`,
`Source/DSP/Macros.cpp`; Test `Tests/SaturationTests.cpp`.

**Interfaces:**
- Produces IDs: `satMultiband` (bool, 0), `satXoverLow` (60–1000 Hz, 250), `satXoverHigh` (1000–12000 Hz, 4000),
  `satLowOn/satMidOn/satHighOn` (bool, 1), `satLowDrive/satMidDrive/satHighDrive` (0–24 dB, 6), `satLowChar/satMidChar/satHighChar`
  (choice of the 4 characters; 0/1/1), `satLink` (bool, 1).
- Produces: `SatSettings::multiband`, `SatSettings::Band bands[3] { bool on; float driveDb; int character; }`, `xoverLowHz`, `xoverHighHz`;
  `std::array<float, 3> linkedBandDrives (std::array<float, 3> current, int band, float newDb) noexcept` (SaturationModule.h).
- [ ] Step 1: tests: multiband off == single band (bit-exact vs a module without the new params set), bands off → output nulls
  against the input through the LR4 all-pass pair (≤ −80 dB), band isolation (drive only the high band: 200 Hz THD unchanged,
  8 kHz distorted), crossover order guard, aliasing in multiband ≤ −67 dB, macros move all bands, link helper keeps differences/clamps,
  instance banks exist (`satLowDrive_2`).
- [ ] Step 2: fail; Step 3: implement (LR4 = two cascaded TPT SVF Butterworth sections at the high rate, low band through the
  fc2 all-pass, per-band ADAA shaper + compensation, sum, shared DC/mix/down path); Step 4: pass; commit.

### Task 4: Bench vs Saturn 2 (single and 3-band), docs, ledger

**Files:** Modify `tools/refhost/modules.py` (multiband case), `docs/QUALITY_BASELINE.md` (sat block via `--update-doc`), ledger.
- [ ] Run `bench.py sat --out ...\modules-after` (with refs), check FAIL count 0, listen-proxy (clicks, CPU), update doc, full suite green, commit.
