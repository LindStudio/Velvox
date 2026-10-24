# Dynamics Quality (Compressor, De-Esser, Vocal Smash, Level Ride) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bring the four dynamics modules to the Q6 quality gate against their references (Pro-C 2, Pro-DS, Pro-C 2 Punch,
Waves Vocal Rider) on dry vocals, add the spec 5.2/5.3/5.5/5.7 DSP options, and give every module a lock-free UI data feed for
its 0.6 panel visual.

**Architecture:** DSP changes stay inside the module classes (`Source/DSP/*Module.*`). A new header `Source/DSP/DynamicsFeed.h`
holds two lock-free single-producer/single-consumer queues (`FrameFifo<T, N>` on `juce::AbstractFifo`): `DynamicsHistory`
(scrolling in/out/GR frames for the Pro-C display, Smash GR column) and the Level Ride word-segment queue. New parameters are
appended in one block to `ParameterIDs.h`, `ParameterLayout.cpp` and `SettingsReader.*`; they default to the old behaviour.

**Tech Stack:** C++20, JUCE 8.0.9, VelvoxTests (juce::UnitTest), HeadlinerRefHost + `tools/refhost/bench.py` (Python 3.13).

**Spec:** `docs/superpowers/specs/2026-10-25-headliner-pro-overhaul-design.md` (§5.2, §5.3, §5.5, §5.7, §6, §7, §8 Q6/Q7 and
"Quality fixes vs. old projects"); gaps #6 and #8 of `docs/QUALITY_BASELINE.md`; `C:\Users\jakob\dev\velvox-wt\AGENT_RULES.md`.

## Global Constraints

- Never rename/remove parameter IDs; new IDs only, their default reproduces the old behaviour (Q7).
- Old projects sound identical except pure quality fixes (less distortion from ballistics etc.), listed for the CHANGELOG.
- Real-time safe audio thread: no allocation, locks or logging in `process()`; buffers sized in `prepare()`.
- Compressor lookahead 0 ms (no latency) — spec 5.2. Constant latency while an instance exists (Q9).
- Mix-law handling (`MixLaw.*`, `mixLaws`, `lawMix`, `dryGain`) stays untouched in the modules.
- No edits to `Source/UI/*`.
- Q6 gate: `bench.py comp|deess|smash|ride` = 0 FAIL; ≤ 3 improvement rounds per gap; after-renders to
  `C:\Users\jakob\dev\velvox-bench\2026-10-25\modules-after\<module>\` (`--out`).
- Tolerances (modules.py): comp THD ≤ Pro-C 2 Vocal + 3 dB, curve ≤ 1.0 dB, ratio 4 ± 0.5, threshold −24 ± 2, attack ± 5 ms,
  release ± 40 ms; ride st-loudness ≤ Vocal Rider + 0.3 LU, jitter ≤ ref + 0.05 dB, transients ± 0.5 dB.

## Review Focus

1. Sample rates other than 48 kHz (44.1/96/192 kHz): peak-hold window and feed frame length are in ms, buffers sized in
   `prepare()` for the actual rate. Test: comp THD/hold test also at 96 kHz (Task 2).
2. Host block size 1 / odd sizes: the sliding-window max and the ride segmenter carry state across blocks. Test: block size 1 and
   512 give identical comp output (Task 2) and identical ride segments (Task 7).
3. UI never drains a feed (editor closed): the audio thread must neither block nor allocate; a full FIFO drops new frames.
   Test: push 10× capacity without pulling, then pull ≤ capacity (Task 1).
4. Silence / digital zero / denormals into the detectors (log of 0): no NaN, GR 0. Test: zeros through comp (auto release on) and
   de-esser (relative) → output exactly zero, GR 0 (Tasks 3, 5).
5. Toggling Auto makeup / Auto release / Detection while playing: no click. Test: toggle mid-sine, largest sample step ≤ 1.5 × the
   sine's own (Task 3).

---

### Task 1: Lock-free UI feed queues

**Files:**
- Create: `Source/DSP/DynamicsFeed.h`
- Create: `Tests/DynamicsQualityTests.cpp`; Modify: `CMakeLists.txt` (VelvoxTests sources)

**Interfaces:**
- Produces (namespace `velvox::dsp`):
  - `template <typename T, int Capacity> class FrameFifo { bool push (const T&) noexcept; int pull (T* dest, int max) noexcept; int available() const noexcept; void clear() noexcept; }`
    (push returns false and drops when full; single producer = audio thread, single consumer = UI).
  - `struct DynamicsFrame { float inputDb, outputDb, gainReductionDb; };`
  - `class DynamicsHistory { void prepare (double fs, double frameMs = 5.0); void reset() noexcept; void add (float inAbs, float outAbs, float grDb) noexcept; int pull (DynamicsFrame*, int max) noexcept; double frameSeconds() const noexcept; static constexpr int capacity = 2048; }`
    – `add` per sample keeps the max of |in|, |out| and GR over one frame, then pushes the frame in dB (floor −120).

- [ ] Step 1: tests `"Dynamics feed: frames carry the peak of each frame"` (1 kHz sine at −6 dBFS in, ×0.5 out, GR 6 → frames
  −6 ± 0.1 / −12 ± 0.1 / 6), `"Dynamics feed: never blocks, drops when full"` (10 × capacity adds without pull → pull returns
  exactly `capacity`), `"FrameFifo: order kept"`.
- [ ] Step 2: run, expect compile failure (header missing).
- [ ] Step 3: implement `DynamicsFeed.h`.
- [ ] Step 4: run `-Test "Dynamics feed"` → pass. Commit.

### Task 2: Compressor peak-hold detector (gap #6: THD 100 Hz, curve deviation)

**Files:** Modify `Source/DSP/CompressorModule.h/.cpp`; Test `Tests/DynamicsQualityTests.cpp`.

**Interfaces:**
- Produces: `CompressorModule::holdMs = 10.0` (constexpr), private sliding-window max (monotonic wedge over a ring buffer allocated
  in `prepare()` for `ceil (holdMs * fs / 1000) + 1` samples).

- [ ] Step 1: tests `"Compressor: low ballistics distortion at 100 Hz"` (−10 dBFS 100 Hz sine, thr −24, 4:1, knee 6, 10/120 ms,
  peak detector, last 1 s: THD ≤ −60 dB — the baseline measured −48 dB) at 48 and 96 kHz; `"Compressor: static curve within
  0.3 dB of the ideal knee"` (1 kHz steps −40…0 dBFS, steady-state output vs `computeGainDb`); `"Compressor: block size does not
  change the output"` (1 vs 512, max diff < 1e-6).
- [ ] Step 2: run → THD and curve tests fail on the old detector.
- [ ] Step 3: peak detector = sliding max of the stereo-linked (SC-filtered) |x| over `holdMs`, then the unchanged gain computer
  and log-domain branching attack/release smoother (Giannoulis 2012 "smooth branching", decoupled peak hold). RMS detector
  unchanged.
- [ ] Step 4: tests pass; existing `"Compressor*"`/`"Velvox chain"` tests pass. Bench `comp --no-ref` → thd/curve pass. Commit.

### Task 3: Program-dependent release, auto makeup, transfer curve

**Files:** Modify `CompressorModule.*`, `Source/Parameters/ParameterIDs.h`, `ParameterLayout.cpp`, `SettingsReader.h/.cpp`;
Test `Tests/DynamicsQualityTests.cpp`.

**Interfaces:**
- Produces: params `compAutoRelease` (Bool, default 0, "Comp Auto Release"), `compAutoMakeup` (Bool, default 0,
  "Comp Auto Makeup"); `CompSettings::autoRelease`, `CompSettings::autoMakeup`;
  `static float CompressorModule::autoMakeupDb (const CompSettings&) noexcept` (= static GR at a −18 dBFS detector level);
  `static float CompressorModule::transferDb (const CompSettings&, float inputDb) noexcept` (static curve incl. makeup + auto
  makeup, for the Pro-C curve display).
- Dual release: fast branch (attack A, release 0.4 R) and slow branch (attack 4 R, release 2.5 R), GR = max of both.

- [ ] Step 1: tests `"Compressor auto release: short bursts recover faster than sustained compression"` (30 ms burst vs 2 s tone at
  −6 dBFS, thr −30, 4:1, R 120 ms: time until GR < 3 dB after the end; auto: short ≤ 0.6 × long; off: ratio within 0.8–1.25),
  `"Compressor auto makeup keeps a −18 dBFS tone at its level"` (± 0.3 dB), `"Compressor transfer curve matches the rendered
  steady state"` (± 0.3 dB, makeup 3 dB + auto), `"Compressor: toggles do not click"`, `"Compressor: silence stays silent"`.
- [ ] Step 2: run → compile failure (fields missing).
- [ ] Step 3: implement; parameter specs appended at the end of the table; SettingsReader binds.
- [ ] Step 4: tests pass; `"Factory presets*"`, `"Parameter*"` pass. Commit.

### Task 4: Compressor + Smash UI feed

**Files:** Modify `CompressorModule.*`, `SmashModule.*`; Test `Tests/DynamicsQualityTests.cpp`.

**Interfaces:**
- Produces: `DynamicsHistory CompressorModule::history` (input |x| before gain, output after gain+makeup, GR dB);
  `std::atomic<float> CompressorModule::currentGainReductionDb`; `DynamicsHistory SmashModule::history` (input, output, GR of the
  squashed path); `std::atomic<float> SmashModule::currentGainReductionDb`.

- [ ] Step 1: tests `"Compressor feed: history shows input, output and gain reduction"` (−6 dBFS sine, thr −30, 4:1, knee 0 →
  last frames in −6 ± 0.3, out −24 ± 0.5, GR 18 ± 0.5; frames ≈ samples / frame length), `"Smash feed: gain reduction column"`.
- [ ] Step 2: run → compile failure.
- [ ] Step 3: implement (`history.prepare` in `prepare()`, `add` per sample).
- [ ] Step 4: pass; Smash bench still pass. Commit.

### Task 5: De-Esser relative detection + UI feed

**Files:** Modify `DeEsserModule.*`, params files; Test `Tests/DynamicsQualityTests.cpp`.

**Interfaces:**
- Produces: param `deessDetection` (Choice {"Absolute", "Relative"}, default 0); `DeEssSettings::relative`;
  relative level = band dB − full-band envelope dB (attack 5 ms, release 250 ms) + `referenceDb = −18`, so the knob means the same
  at a voice level of −18 dBFS. Feed: `SampleTap DeEsserModule::spectrumTap` (input), `std::atomic<float> currentReductionDb`,
  `std::atomic<float> bandLowHz, bandHighHz`.

- [ ] Step 1: tests `"De-esser relative detection: loud and quiet phrases are treated alike"` (vowel tone + S-noise burst, same
  material at 0 and −20 dB: relative reduction differs ≤ 1 dB, absolute differs ≥ 3 dB), `"De-esser split band nulls when
  inactive"` (bit-exact below threshold, regression), `"De-esser feed: reduction and band range"`, silence test.
- [ ] Step 2: run → compile failure.
- [ ] Step 3: implement.
- [ ] Step 4: pass; bench `deess` still pass (default Absolute). Commit.

### Task 6: Level Ride evenness (gap #8)

**Files:** Modify `RideModule.*`; Test `Tests/DynamicsQualityTests.cpp`; bench re-runs.

**Interfaces:** unchanged settings; internal control law only (Rounds ≤ 3: 1 = word-level detector speeds + Amount mapping so the
correction reaches the set share of the distance; 2 = phrase blend/transient freeze tuning; 3 = lookahead only if still failing).

- [ ] Step 1: test `"LEVEL RIDE: word-level evening"` (300 ms "words" alternating −30/−18/−24/−12 dBFS RMS noise with 80 ms gaps,
  defaults: std of the per-word output levels ≤ 0.55 × input std) — must fail on the old law; existing ride tests stay.
- [ ] Step 2: run → fails.
- [ ] Step 3: implement round 1; bench `ride` → st_loudness ≤ 3.22 LU, jitter ≤ ref + 0.05, transients ± 0.5.
- [ ] Step 4: pass; commit; record rounds in the ledger.

### Task 7: Level Ride word segments feed

**Files:** Modify `RideModule.*`; Test `Tests/DynamicsQualityTests.cpp`.

**Interfaces:**
- Produces: `struct RideSegment { std::int64_t startSample; int lengthSamples; float levelBeforeDb, levelAfterDb; };`
  `FrameFifo<RideSegment, 128> RideModule::segments`; `std::atomic<std::int64_t> RideModule::samplePosition`.
  Segmenter (control rate): a segment opens when the voice becomes active, closes when it goes silent, when the syllable level
  stays ≥ 9 dB below the segment's peak for 40 ms (word gap) or after 2 s (sustained note); levels = mean band power before /
  after the ride gain.

- [ ] Step 1: tests `"LEVEL RIDE feed: one segment per word"` (5 words of 300 ms, 150 ms silence → 5 segments, start ± 30 ms,
  before level ± 1 dB of the band level, after − before ≈ applied gain ± 1 dB), `"LEVEL RIDE feed: dip splits words"`, block
  size 1 vs 256 identical segments.
- [ ] Step 2: run → compile failure.
- [ ] Step 3: implement.
- [ ] Step 4: pass. Commit.

### Task 8: Gate, bench, docs

- [ ] bench `comp`, `deess`, `smash`, `ride` with references, `--out ...\modules-after`, `--update-doc`; listen-proxy checks
  (clicks/denormals/CPU in the JSON sidecars); full suite `-Test "*"` green; ledger + report (params, Rulings, CHANGELOG notes,
  hot-spots).
