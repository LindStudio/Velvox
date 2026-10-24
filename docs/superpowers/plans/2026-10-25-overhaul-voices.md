# Voices (Doubler engine + Choir quality) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Waves-Doubler-style Doubler engine (2 or 4 voices with per-voice on/delay/detune/gain/pan, smooth random-walk
modulation, low/high cut, dry level, mono-safe) onto which the 0.2 Doubler controls map, and a wider, smoother Choir –
both measured against Waves Doubler2/Doubler4 with the reference host.

**Architecture:** `DoublerModule` is rewritten around one band-limited mono source line, read by up to four voices with a
5th-order Lagrange fractional tap; static detune is a splice pitch shifter (single clean tap most of the time, periodic
correlation-aligned crossfades), modulation a per-voice raised-cosine random walk. A pure `resolveVoices()` turns settings
into per-voice values for both the engine and the UI. A `doublerVoices` choice keeps old projects on "Classic", where the 0.2
Delay/Detune controls drive two voices exactly as before. Choir keeps its engine (dual-tap shifter) but gets wider alternating
pans, independent random-walk drift per voice and uneven voice gains/delays.

**Tech Stack:** C++20, JUCE 8.0.9, VelvoxTests (juce::UnitTest); Python 3.13 + numpy/scipy bench (`tools/refhost`).

**Spec:** `docs/superpowers/specs/2026-10-25-headliner-pro-overhaul-design.md` (§5.8, §5.9, §6, §7, §8 Q1/Q6/Q7 and
"Quality fixes vs. old projects"); `docs/QUALITY_BASELINE.md` (#7 Choir, Doubler row, Q6 tolerances); `AGENT_RULES.md`.

## Global Constraints

- Never rename/remove parameter IDs; only add. New params default to the old behaviour (Q7).
- Doubler: old projects sound *close* (exact identity not required; accepted change for the CHANGELOG); mapping documented
  and measured. Choir: only quality fixes may change old projects (listed for the CHANGELOG).
- Keep the mix-law handling (`MixLaw`, `mixLaw`, `lawMix`, `additiveDryGain`, dry-hold law) intact.
- Real-time safe: no allocation/locks/logging in `process`; zero latency for both modules (unchanged).
- Do not edit `Source/UI/*`. UI feed = public atomics / pure functions on the modules.
- Doubler roughness must stay ≤ baseline 0.92 dB (Waves Doubler2 1.90) on the `default` case.
- Q6 gate (modules.py tolerances): comb ≤ ref + 1 dB, roughness ≤ ref + 0.5 dB, lr_corr ≤ ref + 0.1, mono loss ≤ ref + 1 dB,
  CPU ≤ ref + 1 %. Max 3 improvement rounds per gap.
- After-renders to `C:\Users\jakob\dev\velvox-bench\2026-10-25\modules-after\<module>\` (`bench.py --out`).

## Review Focus

1. Detune sign change or detune 0 ↔ ±30 ct while playing – no click, the splice state must stay valid
   (test: "Doubler: detune sweep while playing stays click-free").
2. Very short voice delay (1 ms) with full modulation and detune – the tap must never read ahead of the write head
   (test: "Doubler: 1 ms delay with 25 ct modulation and 30 ct detune stays causal and finite").
3. Sample rates 44.1 / 96 / 192 kHz – buffer sized from fs, detune/delay accurate (test: per-voice tracking at 96 kHz).
4. Mono host bus (1 channel) and 4-voice mode – mono-safe and gain normalisation still hold (test: "Doubler 4 voices, mono bus").
5. A 0.5 state (no new IDs) loads as Classic with its 0.2 values and the per-voice defaults (test: "Doubler: old state maps to Classic").

---

### Task 1: Doubler parameters and settings mapping

**Files:**
- Modify: `Source/Parameters/ParameterIDs.h`, `Source/Parameters/ParameterLayout.cpp` (new block "Doubler engine (0.6)" at the end
  of the instance-1 table), `Source/Parameters/SettingsReader.h/.cpp`, `Source/DSP/DoublerModule.h` (settings struct only)
- Test: `Tests/DoublerTests.cpp`

**Interfaces:**
- Produces: IDs `doublerVoices` (Choice {"Classic","2","4"}, default 0), `doublerV{1..4}On` (Bool, 1), `doublerV{n}Delay`
  (1–60 ms, defaults 14 / 19.18 / 24 / 31), `doublerV{n}Detune` (−30…+30 ct, defaults 0 / 0 / −5 / +5),
  `doublerV{n}Gain` (−24…+6 dB, defaults 0 / 0 / −3 / −3), `doublerV{n}Pan` (−100…+100 %, defaults −100 / +100 / −50 / +50),
  `doublerModDepth` (0–25 ct, 8), `doublerModRate` (0.05–3 Hz, 0.31), `doublerLowCut` (20–1000 Hz, 120),
  `doublerDry` (−60…+6 dB, 0; −60 = off). `doublerTone` stays the high cut (no `doublerHighCut`).
- `enum class DoublerMode { classic = 0, two = 1, four = 2 }`; `struct DoublerVoiceSettings { bool on; float delayMs,
  detuneCents, gainDb, pan; }`; `DoublerSettings` gains `mode`, `std::array<DoublerVoiceSettings, 4> voices`, `modDepthCents`,
  `modRateHz`, `lowCutHz`, `dryDb` (defaults as above).

- [ ] Step 1: test "Doubler parameters: new IDs exist with old-behaviour defaults" (find every ID via `findParameterSpec`, check
  defaults; `OfflineParameters` read gives mode classic, V1/V2 = 14/19.18 ms, 0 ct, 0 dB, ∓1, lowCut 120, dry 0) and
  "Doubler parameters: instance 2 bank" (`doublerV3Pan_2` exists).
- [ ] Step 2: build, watch it fail (unknown IDs / missing fields).
- [ ] Step 3: add IDs, specs, Raw bindings, `readParams` mapping (pan % → −1..1).
- [ ] Step 4: run, pass. Step 5: commit.

### Task 2: Voice resolution (Classic mapping) – pure function

**Files:** `Source/DSP/DoublerModule.h/.cpp`; Test: `Tests/DoublerTests.cpp`

**Interfaces:**
- Produces: `struct DoublerVoice { bool on; float delayMs, detuneCents, gain, pan; };`
  `struct DoublerVoices { int count; std::array<DoublerVoice, 4> v; float modDepthCents; float normalisation; };`
  `static DoublerVoices DoublerModule::resolveVoices (const DoublerSettings&) noexcept;`
  Classic: 2 voices, delay D and 1.37·D, detune 0, gain 1, pan −width / +width, mod depth = `detuneCents`.
  Two/Four: V1..2 / V1..4 from `voices`, pan = clamp(pan·width), gain = dB→lin (off → 0), mod depth = `modDepthCents`.
  normalisation = sqrt(2 / count).

- [ ] Steps: test "Doubler voices: Classic follows the 0.2 controls" (D = 25, width 0.6 → 25 / 34.25 ms, pans ∓0.6, mod depth = 12 when
  detune 12) and "Doubler voices: 2 and 4 voice modes use the per-voice values" (4 mode: count 4, V3 delay 24, normalisation
  0.7071); fail; implement; pass; commit.

### Task 3: New Doubler engine

**Files:** `Source/DSP/DoublerModule.h/.cpp`, `Source/DSP/VocalChain.h` (`getDoubler (int)` accessor); Test: `Tests/DoublerTests.cpp`

**Interfaces:**
- Consumes: Task 2 `resolveVoices`.
- Produces (UI feed, relaxed atomics, written once per block): `std::array<std::atomic<float>, 4> voicePan, voiceGainDb,
  voiceCents, voiceDelayMs; std::atomic<int> activeVoices;` and `VocalChain::getDoubler (int instance)`.

Algorithm (decided here):
- Source = mono mid → HPF lowCut (RBJ, Q 0.7071) → LPF tone (Q 0.7071), written to a power-of-two line sized from fs
  (60 ms + 2·(2·10 ms) modulation + 20 ms shifter + 8). Filters coefficient-updated every 32 samples while smoothing.
- Tap read: 6-point (5th-order) Lagrange around the fractional position.
- Modulation per voice: two raised-cosine random walks (rates f·k_v and 0.365·f·k_v, k = {1, 1.516, 0.83, 1.27}), targets
  alternate sign with magnitude 0.5–1, segment length 1/(2·rate)·(0.8…1.25), fixed seeds per voice; delay offset
  A1(1+x1) + A2(1+x2) with A = depthForCents(cents, rate) (second component 0.35·cents), each A ≤ 10 ms.
- Detune (|c| > 0.05 ct): splice shifter offset s drifting at (1 − 2^(c/1200)) samples/sample inside [lo, lo + 20 ms],
  lo = max(−10 ms, 1 ms − base); at the edge a second tap starts at s ± J, J ∈ [6, 19] ms chosen by maximum normalised
  correlation of the 768 samples behind both read points (coarse step 4, refine ±4), raised-cosine crossfade over 30 ms.
- Output: linear pan like 0.2 (L = 0.5(1 − p), R = 0.5(1 + p)), mono-safe on the mid of the doubles, wet = mix·norm·voices,
  dry = additiveDryGain(lawMix|mix, law) · dB(dryDb).

- [ ] Steps: tests (each fails on the old engine first):
  "Doubler: per-voice delay is exact" (impulse, one voice, mod 0: peak at set delay ± 0.05 ms, V1 7 ms, V2 33 ms),
  "Doubler: per-voice detune is exact and steady" (440 Hz, 4 voices with −20/+12/−7/+30 ct solo'd: median ± 0.5 ct, frame jitter ≤ 1 ct),
  "Doubler: detuned voice has no splice artefacts" (440 Hz, +30 ct, solo: 20 ms energy std ≤ 0.3 dB, spurious ≤ −50 dB),
  "Doubler: modulation is a smooth random walk" (pitch track of a solo voice: deviation reaches ≥ 50 % of depth, never > 140 %,
  no periodicity: autocorrelation of the track below 0.6 beyond one nominal period),
  "Doubler: Classic is close to 0.2" (default + 3 old settings: comb/roughness/corr within 0.3 dB / 0.3 dB / 0.05 of the
  0.2 engine copied into `Tests/LegacyDoublerV05.h`; level within 0.5 dB),
  "Doubler: dry level and true law at Mix 100 %" (dry −60 → no dry; Mix 100 % on true law ≥ 60 dB down),
  Review Focus 1, 2, 3, 4; existing tests stay green. Fail; implement; pass; commit.

### Task 4: Bench – per-voice tracking and voice-mode cases

**Files:** `tools/refhost/bench.py`, `tools/refhost/modules.py`, `tools/refhost/tests/test_bench.py`

**Interfaces:** new procedure `voice_tracking` (case info `voices: [(delay_ms, detune_ct)]`, per-voice `solo` param sets for
Headliner and reference), metrics `voice_delay_err_ms` (|median delay track − set|, mean over voices), `voice_detune_err_cents`
(|median pitch − set|), `voice_pitch_jitter_cents` (std of the pitch track), `voice_splice_db` (20 ms energy std of a solo +30 ct
voice). New doubler cases `voices2` (Doubler2: 14/20 ms, ±8 ct) and `voices4` (Doubler4).

- [ ] Steps: unit test for the procedure on a synthetic fake (delay + resample); fail; implement; pass; run
  `bench.py doubler --out ...modules-after`; commit.

### Task 5: Choir – width and roughness

**Files:** `Source/DSP/ChoirModule.h/.cpp`; Test: `Tests/NewModuleTests.cpp` (choir section)

**Interfaces:** `ChoirModule::shapes()` gets per-voice `gain` and the new pans; UI feed `voicePan`, `voiceLevel` atomics and
`static VoicePosition voicePosition (int voice, const ChoirSettings&)` (`pan` −1..1, `depth` 0..1 from timing, `level`).

- [ ] Steps: tests "Choir: wide image" (pink noise, defaults: L/R corr ≤ 0.85) and "Choir: low AM roughness" (440 Hz, defaults:
  20 ms energy std ≤ 1.7 dB) – fail on the old choir; implement alternating pans ±(1, 0.55, 0.8, 0.3 …)·width, independent
  raised-cosine random-walk drift, voice gains 0 / −1 / −2 / −1.5 dB…, uneven delays; pass; bench `choir`; commit.

### Task 6: Docs, full suite, report

- [ ] `bench.py doubler|choir --update-doc --out ...modules-after`; QUALITY_BASELINE Doubler/Choir blocks + mapping note;
  ledger; full suite `-Test "*"` green; commit.
