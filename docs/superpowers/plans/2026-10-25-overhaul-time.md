# Time Modules (Delay, Pulse, Stutter) DSP-Quality Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Delay without zipper or pitch glitches while its time moves, cleaner drive/modulation characters, verified ping-pong and an
echo-tap feed for the echo map; Pulse "like Kickstart" (Amount macro, 4 curated pump shapes, host-synced rates); Stutter feed for
"cuts on the waveform" and click-free repeats.

**Architecture:** All work stays inside `Source/DSP/{Delay,Pulse,Chop}Module.*`, the parameter table/reader and one new lock-free
display ring (`Source/DSP/DisplayRing.h`). New tests live in one new file `Tests/TimeModuleTests.cpp`, registered in its own block at
the end of `CMakeLists.txt` (merge-friendly, like refhost). UI feeds are plain static functions / lock-free readers the panels
(shell track) call later; `Source/UI/*` is not touched.

**Tech Stack:** C++20, JUCE 8.0.9 (`juce::dsp::FFT` in tests), Python 3.13 bench (`tools/refhost/bench.py`).

**Spec:** `docs/superpowers/specs/2026-10-25-headliner-pro-overhaul-design.md` (§5.11 Delay, §5.13 Stutter, §5.14 Pulse, §4, §7, §8)

## Global Constraints

- Never rename/remove parameter IDs; new IDs only, default = old behaviour (old states load with identical sound).
- Real-time safe audio thread: no allocation, locks or logging in `process()`.
- Delay: "no zipper when changing time (crossfade or tape-style)"; target Timeless 3 / Pro-quality characters.
- Pulse: "one big Amount control, 4 curve shape tiles, rate buttons (1/4 default, 1/2, 1/8, 1/16, 1 bar), sync to host tempo. The
  existing 6-point curve editor and Attack/Release/Smooth/BPM stay in Mehr". Loaded old states = shape Custom, identical sound.
- Stutter: "Scrolling vocal waveform with the cut/muted slices shaded"; internal name stays `chop`.
- Click-free edges: bench `edge_splatter_db` stays ≤ −60 dB (Pulse baseline −151, Stutter −88.5 – must not regress by > 3 dB).
- Keep MixLaw handling (`mixLaws`, `lawMix`, `dryGain`) intact; do not edit `Source/UI/*`.
- After-renders to `C:\Users\jakob\dev\velvox-bench\2026-10-25\modules-after\<module>\` (`bench.py --out`).

## Review Focus

1. Automation that moves the delay time every block (DAW lanes, echo-map drags) – expect a smooth tape-like glide, no 16 Hz
   crossfade flutter, no amplitude dips. Test: "Delay: continuous time sweep glides".
2. Tempo/note-value jumps while echoes ring (300 → 437 ms) – expect a crossfade, no pitch dive. Test: existing FxQuality click test +
   "Delay: a large jump crossfades instead of gliding".
3. A Pulse state saved before 0.6 (no pulseShape/pulseAmount) – must render bit-identical. Test: "Pulse: old state = Custom, identical".
4. Pump shapes at the fastest rate (1/16 at 174 BPM) – the duck edge must not click. Test: "Pulse: every shape is click-free".
5. Stutter repeat runs at slow tempos (4 repeats of 1/8 at 40 BPM = 3 s) – must replay the right slice, not a clamped one.
   Test: "Stutter: long repeat runs at slow tempo replay the slice".

---

### Task 1: Delay time changes – tape-style glide for small moves, crossfade for jumps, per-sample modulation

**Files:** Modify `Source/DSP/DelayModule.h/.cpp`; Create `Tests/TimeModuleTests.cpp`; Modify `CMakeLists.txt` (own block at end).

**Interfaces:** Produces `std::atomic<float> DelayModule::displayTimeMs` (current, glided read time of the left tap, relaxed).

- [ ] Step 1: failing tests in `TimeModuleTests.cpp` ("Velvox time modules", category "Velvox"):
  - "Delay: continuous time sweep glides": 1 kHz sine 0.3, feedback 0, cuts 20 Hz / 20 kHz, time 250 → 450 ms linear over 3 s set
    every 64 samples. In 0.8–3.2 s: 5 ms RMS envelope max − min ≤ 1.0 dB; adjacent-period frequency step (zero crossings) ≤ 3 Hz.
  - "Delay: a large jump crossfades instead of gliding": 300 → 437 ms at 2 s; the output frequency never leaves 1000 ± 15 Hz.
  - "Delay: modulation has no zipper": mod 1.0, static 300 ms, 1 kHz sine: energy above 2.5 kHz ≤ −80 dB re total.
- [ ] Step 2: run, watch the sweep and modulation tests fail (current: 60 ms crossfades restart every 60 ms; modulation stepped every
  32 samples).
- [ ] Step 3: implement. Read position `glide` follows `targetDelay` per sample: one-pole (τ = 50 ms) with slew limit 0.08 samples/sample
  (≈ ±1.4 semitones). When no fade runs and |target − glide| > jump threshold (max(15 ms, 3 × slew-limited lag)), start the 60 ms
  crossfade from the gliding tap to a tap at the target (the next tap then becomes the glide). Modulation evaluated per sample.
- [ ] Step 4: tests pass, plus "Headliner reverb and delay engine" stays green.
- [ ] Step 5: commit.

### Task 2: Delay characters and ping-pong

**Files:** Modify `Source/DSP/DelayModule.cpp`; Test `Tests/TimeModuleTests.cpp`.

- [ ] Step 1: tests:
  - "Delay: ping-pong bounces L, R, L": impulse on L only, fb 0.5, 200 ms: echo 1 only on L (R ≤ −80 dB), echo 2 only on R at
    400 ms with level −6.02 ± 0.1 dB re echo 1, echo 3 only on L; with spread 20 ms echo 2 at 420 ms, echo 3 at 620 ms.
  - "Delay: echoTaps matches the rendered echoes" (Task 3 function) is added in Task 3.
  - "Delay: drive aliasing": 4.1 kHz sine at −6 dBFS, drive 1, cuts open: inharmonic energy (bins not within ±30 Hz of a harmonic
    k·4.1 kHz < 24 kHz) ≤ −60 dB re fundamental.
  - "Delay: modulation is not a plain vibrato": at mod 1 the L and R read offsets differ (decorrelated wow, correlation of the two
    offset traces < 0.9) – the echoes widen like a tape pair instead of moving in lockstep.
- [ ] Step 2: run; record which fail.
- [ ] Step 3: fix what fails (drive: 2× oversampled saturator with polyphase half-band IIR, phase compensated in the read position;
  modulation: per-channel smoothed random-walk wow + flutter). Ruling for each.
- [ ] Step 4/5: green, commit.

### Task 3: Echo map feed

**Files:** Modify `Source/DSP/DelayModule.h/.cpp`; Test `Tests/TimeModuleTests.cpp`.

**Interfaces:** Produces
`struct EchoTap { float timeMs; float levelL; float levelR; };`
`static int DelayModule::echoTaps (const DelaySettings& s, double sampleRate, EchoTap* out, int maxTaps, float floorDb = -60.0f) noexcept`
(level = linear gain of a centred mono impulse incl. ping-pong routing, spread, feedback and the loop filters' gain at 1 kHz; stops at
`floorDb` or `maxTaps`; returns the count). `static float DelayModule::feedbackForSecondTap (float level1, float level2)` for drags.

- [ ] Step 1: test "Delay: echoTaps matches the rendered echoes" for plain, spread and ping-pong: times within 0.1 ms, levels within
  0.3 dB of the impulse response peaks (filters 300 Hz/6 kHz measured as 1 kHz tone burst level).
- [ ] Steps 2–5: fail, implement, pass, commit.

### Task 4: Pulse – Amount macro and 4 pump shapes

**Files:** Modify `Source/DSP/PulseModule.h/.cpp`, `Source/Parameters/ParameterIDs.h`, `ParameterLayout.cpp`, `SettingsReader.h/.cpp`;
Test `Tests/TimeModuleTests.cpp`.

**Interfaces:** New IDs `pulseShape` (choice: Custom, Classic, Punch, Swell, Gate; default 0 = Custom), `pulseAmount` (0–100 %, default
50 %). `PulseSettings::shape` (int), `PulseSettings::amount` (0..1). `static float PulseModule::effectiveDepth (float depth, float amount)`
(amount ≤ 0.5: depth·amount/0.5; above: depth + (1 − depth)(amount − 0.5)/0.5). `static float PulseModule::evaluateShape (const
PulseSettings&, float phase)` (curve of the active shape; Custom = `evaluate`). `static float PulseModule::gainAt (const PulseSettings&,
float phase)` = 1 − effectiveDepth·(1 − evaluateShape) (UI draws exactly this). `static const juce::StringArray& shapeNames()`.
Existing `displayPhase`/`displayGain` stay the live feed.

- [ ] Step 1: tests:
  - "Pulse: old state = Custom, identical": processor with a state that lacks the new IDs renders bit-identical to the same
    settings via `PulseModule` with shape Custom and the old formula; `pulseShape` loads as 0, `pulseAmount` as 50.
  - "Pulse: Amount macro": effectiveDepth (0.6, 0.5) == 0.6, (0.6, 0) == 0, (0.6, 1) == 1, monotonic.
  - "Pulse: every shape is click-free": each shape × rates {1/1, 1/4, 1/16} × 120/174 BPM, depth 1 amount 0.5: energy > 5 kHz with a
    1 kHz −12 dBFS tone ≤ −60 dB; shapes differ (pairwise mean |Δcurve| > 0.05) and all start ducked (curve(0.01) < 0.3) and
    reach 1 before the cycle end.
  - "Pulse: gainAt is what is processed": with smoothing off (attack 0.05, release 0.5, smooth 0) the processed gain follows
    `gainAt` within 0.02 at 1/1.
- [ ] Steps 2–5: fail, implement (shape tables rebuilt on change, each curated shape has a 2.5 % raised-cosine duck ramp at the cycle
  end), pass, commit.

### Task 5: Stutter – waveform/cut feed and click-free repeats

**Files:** Create `Source/DSP/DisplayRing.h`; Modify `Source/DSP/ChopModule.h/.cpp`, `Source/DSP/VocalChain.h` (accessor);
Test `Tests/TimeModuleTests.cpp`.

**Interfaces:** `template <int Capacity> class DisplayRing` – single writer, any reader, frames packed in `std::atomic<uint64_t>`:
`void push (uint64_t)`, `int readLatest (uint64_t* dest, int maxFrames) const` (oldest→newest, drops frames overwritten during the
read), `uint64_t written() const`. `struct ChopModule::DisplayFrame { float peak; float gain; int step; char type; }` with
`static uint64_t pack (const DisplayFrame&)` / `static DisplayFrame unpack (uint64_t)`; `int ChopModule::readDisplay (DisplayFrame* dest,
int maxFrames) const` (frames of 2.5 ms: input peak of the mono sum, the minimum gain applied, the step and its type) and
`static constexpr double displayFrameSeconds = 0.0025`. `VocalChain::getChop()`.

- [ ] Step 1: tests:
  - "Stutter: display frames show input and muted slices": Gate 1/2 1/16 at 120 BPM on a 0.5 sine: frames in muted steps gain < 0.05,
    played steps > 0.95, peak ≈ 0.5 ± 0.01, step indices follow the pattern; a concurrent reader thread never sees torn frames.
  - "Stutter: long repeat runs at slow tempo replay the slice": Stutter end, 1/8, 40 BPM: the 4th repeat equals the slice before the
    run (error < 1e-6).
  - "Stutter: repeat edges are click-free": every pattern with 'r', 1/16 at 128 BPM, smooth 3 ms, 1 kHz −12 dBFS tone: energy > 5 kHz
    ≤ −60 dB.
- [ ] Steps 2–5: fail, implement (history 4 s → long runs fit at ≥ 30 BPM; slices longer than the buffer fall back to the newest
  full slice; raised-cosine repeat crossfade), pass, commit.

### Task 6: Bench after-renders, baseline doc, full suite

- [ ] `bt.ps1 -Target HeadlinerRefHost`; `bench.py delay|pulse|chop --out ...\modules-after --no-ref` (delay with the reference) and
  add bench cases `pulse` shapes (classic) and `chop` repeat (`Stutter 8ths`); `--update-doc` for the three modules.
- [ ] Listen-proxy: dry vocal renders before/after for each module in `modules-after\<module>\listen`; check CPU vs budget.
- [ ] `bt.ps1 -Test "*"` green; ledger complete; commit.
