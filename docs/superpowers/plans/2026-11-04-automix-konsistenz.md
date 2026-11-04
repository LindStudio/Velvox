# Auto Mix – Reproduzierbarkeit und Stabilität Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the existing auto mix deterministic, level-independent, start-point-independent, state-independent and stable under small measurement changes, prove it with an automated consistency test system, and log every module decision – without new modules and without unnecessary changes to the audio engine or the UI.

**Architecture:** Fix the six causes found in `docs/AUTOMIX_KONSISTENZ_BERICHT.md` inside the existing pipeline (`CaptureBuffer` → `VocalProfiler` → `AutoMixPlanner` → `AutoMixEngine` → `AutoMixController`). A consistency harness (CLI + unit tests) comes first and measures the baseline; every later task tightens one of its checks. Decisions become continuous via one shared soft-threshold helper. The DSP chain (`Source/DSP/*`) is not touched.

**Tech Stack:** C++20, JUCE 8.0.9, own JUCE `UnitTest` runner (`VocalypseTests`), Python 3.13 + numpy for the bench comparison.

**Spec:** Jakob's requirements of 2026-11-04 (chat, two messages: „Auto-Mixing-Engine grundlegend stabilisieren“ and „die vier Ursachen systematisch beheben“) + the report `docs/AUTOMIX_KONSISTENZ_BERICHT.md` (measured baseline, causes U1–U6). The phased-engine spec/plan of the same day (`…-automix-phased-engine*.md`) is **shelved**: no new modules now.

## Global Constraints

- No new DSP modules, no change to `Source/DSP/*` behaviour, no parameter ID or plugin code (`Lnda`/`Vlvx`) change. UI changes only where a task names them (capture hint text).
- Same input + same intent → **bit-identical** plan (`MixPlan::values`, routing, log), any number of runs, any prior plugin state.
- Every auto mix starts from the parameter defaults (`getParameterSpecs()[i].defaultValue`). Kept from the live state (session, not mix): `bypass`, `levelMatchWindow`, `levelMatchRange`, `checkOn`, `checkMode` – nothing else. The plan writes **every** other parameter; the routing is built from scratch.
- Working level: analysis and planning run on the capture scaled so the median voiced RMS is **−18 dBFS** (`automix::workingLevelDb = -18.0f`); the plan sets `inputGain` to exactly that scale (clamped to its −24…+24 dB range).
- Soft thresholds: one helper `automix::soft (x, lo, hi)` (smoothstep, 0 below lo, 1 above hi); no new `if (measurement > constant)` that switches a module or a discrete choice without going through it or `chooseWithMargin`.
- Module activation: a module is on only when its continuous intensity is at least its `minAudible` constant; intensities run continuously to 0 at the activation edge (flips happen only between off and barely audible).
- Order of the continuous rework (Jakob): **EQ, compressor, de-esser first**; saturation, smooth, smash, ride, denoise, breath, reverb, delay, doubler only after the checkpoint (Task 9).
- Consistency tolerances (`Tests/ConsistencyHarness.h`, named constants): repeat / prior state / sequential: exact. Gain ±0.5/±1/±2 dB: no module flip; `inputGain` = original − shift ± 0.05 dB; `outputGain` = original + shift ± 0.3 dB; every other value within 2 % or 0.1 units. Noise −70 dBFS and start offsets 3/6 s: a flip only where the module's intensity is ≤ its `lowIntensity` in the run that has it on; EQ role band gain ≤ 1.5 dB, freq ≤ 1/6 octave; compressor threshold ≤ 2 dB, ratio ≤ 20 %, attack/release ≤ 25 %; de-esser range ≤ 2 dB, freq ≤ 1/6 octave; wet/mix percentages ≤ 6 points; saturation drive ≤ 1.5 dB.
- EQ bands are compared and assigned **by role**, never by slot.
- Fixed takes for the real-vocal suite: `%USERPROFILE%\dev\velvox-bench\2026-10-29\train\dry\{jakob_rec1,jakob_rec2,jakob_vergleich,splice_mylk_dry,vs_f1_caro_straight,vs_f3_row_spoken,vs_m1_row_spoken,vs_m3_caro_vibrato}.wav`, listed in `tools/bench/consistency_takes.txt`; synthetic takes (`Tests/SyntheticVocal.h`) for the unit tests that always run.
- Run single tests with `VocalypseTests --test <substring of the UnitTest name>`; never two `VocalypseTests` at once. Gate per task: its tests green; full `scripts\build.ps1` + pluginval 10 at Tasks 9 and 12; commit per task; ask Jakob before pushing.

## Review Focus

1. **A vocal shorter than one segment or with long silences** (8 s capture, a hook repeated with 20 s instrumental gaps): segmentation still yields a valid profile, no division by zero, `conservative` set, deterministic. Test in Task 5.
2. **Host without song position / tempo** (standalone, some hosts while stopped): capture and segmentation fall back to capture-relative segments without crashing; result deterministic for the same start. Test in Task 5.
3. **Very quiet (−40 dBFS RMS) or very hot (peaks at 0 dBFS, clipped) input:** normalisation to −18 dBFS works, `inputGain` clamps at ±24 dB and the plan says so in the log; clipping is still detected on the unscaled audio. Test in Task 4.
4. **Second auto mix after the user moved parameters or loaded a preset:** identical result to a fresh instance (only the session parameters survive). Test in Task 3.
5. **Two values exactly on a rounding edge** (e.g. a computed attack of 4.5 ms, ratio search on a step): rounding is deterministic and the harness's 2 % tolerance absorbs gain-shift noise; no `std::round` on a value that the gain shift moves. Test in Task 4.

---

### Task 1: Consistency harness and baseline

**Files:** Create `Tests/ConsistencyHarness.h/.cpp` (add to the test target in `CMakeLists.txt`), `Tests/ConsistencyTests.cpp`, `tools/bench/consistency_takes.txt`; modify `Tests/TestMain.cpp` (new CLI mode). Write the baseline into `docs/AUTOMIX_KONSISTENZ_BERICHT.md` § 4.

**Interfaces – Produces:**
```cpp
namespace vocalypse::test {
struct PlanFingerprint {
    std::map<juce::String, float> values;                       // every parameter except EQ band slots
    std::map<std::pair<int, int>, dsp::EqBand> roleBands;       // (instance, EqRole) -> band, roles != none
    std::set<juce::String> modulesOn;                            // "<moduleKey>#<instance>" in the routing and switched on
    juce::String routing;                                        // Routing::toString()
};
PlanFingerprint fingerprint (const automix::MixPlan& plan);
struct Tolerances;                                               // the Global Constraints values, named fields
enum class Check { exact, gainShift, perturbation };
struct PlanDiff { int differing = 0; std::vector<juce::String> flips, overTolerance; bool pass = true; };
PlanDiff compare (const PlanFingerprint& reference, const PlanFingerprint& other, Check check, float gainShiftDb, const Tolerances&);
// Simulates the plugin: plays `file` from `startSeconds` through CaptureBuffer in 512-sample blocks (with song position
// when `withSongPosition`: 120 BPM, ppq = file time * 2) and runs the auto mix from `prior` parameter values
automix::AutoMixResult mixFile (const automix::CapturedAudio& file, double startSeconds, bool withSongPosition,
                                const std::map<juce::String, float>& prior, const automix::MixIntent&);
std::map<juce::String, float> defaultValues();
std::map<juce::String, float> scrambledValues (juce::uint32 seed);   // every parameter at a seeded random legal value
}
```
CLI: `VocalypseTests --consistency <takes.txt|dir> <outDir> [genre vibe]` runs per take: 10× repeat, gain −2/−1/−0.5/+0.5/+1/+2 dB, noise −70 dBFS (seeded), start 3 s / 6 s (with and without song position), 3 sequential runs (each from the previous result), 2 scrambled priors; writes `consistency.csv` (take, variant, check, differing, flips, overTolerance, pass) and `consistency_summary.txt`; exit code 1 when any check fails.

- [ ] Failing tests (`ConsistencyTests`, UnitTest name „Auto mix consistency“): `compare` of a fingerprint with itself passes `exact`; a changed `deessOn` is a flip; an EQ band moved to another slot with the same role is **not** a difference; a 2 dB threshold shift under `gainShift` with shift 2 passes; on a synthetic vocal (`makeVocal ({})`, 16 s) 10 runs of `mixFile` from defaults give identical fingerprints.
- [ ] Implement, run `VocalypseTests --test consistency` – PASS.
- [ ] Run the CLI on `tools/bench/consistency_takes.txt` → `%USERPROFILE%\dev\velvox-bench\2026-11-04\konsistenz\baseline`. Expected: repeat passes, prior/sequential/gain/offset checks fail. Copy the summary table into the report § 4 „Ausgangswert“. Commit `Consistency harness and baseline`.

### Task 2: Soft-threshold helpers and the decision log

**Files:** Create `Source/AutoMix/Soft.h`, `Source/AutoMix/DecisionLog.h/.cpp`; modify `AutoMixPlanner.h` (`MixPlan::log`), `ReportText.h`, `Tests/TestMain.cpp` (bench report prints the log), `CMakeLists.txt`. Test: create `Tests/SoftDecisionTests.cpp`.

**Interfaces – Produces:**
```cpp
namespace vocalypse::automix {
float soft (float x, float lo, float hi) noexcept;               // smoothstep; lo > hi = falling
float blend (float w, float a, float b) noexcept;                 // a + w * (b - a)
// Index of the option with the best score; the current favourite `preferred` keeps the choice unless another one
// beats it by more than `margin` (scores: higher = better)
int chooseWithMargin (const std::vector<float>& scores, int preferred, float margin) noexcept;
struct Measured { juce::String key; float value; juce::String unit; };
struct ModuleLog {
    dsp::ModuleId module; int instance = 0;
    bool globalStage = false;             // input / output gain entries (no module)
    bool active = false;
    juce::String problem;                 // stable key ("sibilance", "phraseSpread", ...) or "" when none
    std::vector<Measured> measurements;   // what was measured for this decision
    float need = 0;                       // 0..1 soft weight of the problem
    float intensity = 0;                  // 0..1 share of the module's allowed maximum
    std::vector<Measured> parameters;     // every value this module received
    std::vector<Measured> result;         // after the render (e.g. measured GR, S/vowel after)
    juce::String skipReason;              // DE/EN via txt::both when !active
};
juce::var toVar (const ModuleLog&);       // JSON
}
// MixPlan: std::vector<ModuleLog> log;  ModuleLog& logFor (dsp::ModuleId, int instance = 0);
```
- [ ] Failing tests: `soft` is 0 / 0.5 / 1 at lo / mid / hi and monotonic; `chooseWithMargin` keeps `preferred` when another option is better by less than the margin and switches above it; `toVar` round-trips every field through `juce::JSON::toString` / `parse`.
- [ ] Implement; planner fills `log` for every module it touches (active or skipped, existing reasons). Run – PASS. Commit `Soft thresholds and decision log`.

### Task 3: Full state reset (U5)

**Files:** Modify `AutoMixPlanner.cpp` (start from `defaultValues()` instead of `current`; every parameter written; routing from scratch), `AutoMixEngine.cpp` (`run` / `render` take the plan's complete values; `current` used only for the five session parameters), `AutoMixController.cpp` (`applyResult` writes every parameter, also those equal to the default), `Check/CheckFixPlanner.cpp` only if it reads the planner's `current` handling. Test: `Tests/ConsistencyTests.cpp`, `Tests/AutoMixTests.cpp`.

**Interfaces – Produces:** `std::set<juce::String> automix::sessionParameters();` = { `bypass`, `levelMatchWindow`, `levelMatchRange`, `checkOn`, `checkMode` }. `MixPlan::values` contains every parameter ID of `getParameterSpecs()` except those.

- [ ] Failing tests: plan from `scrambledValues (1)` and `scrambledValues (2)` equals the plan from `defaultValues()` (`exact`); 3 sequential `mixFile` runs (each from the previous result) are identical to the first; after `AutoMixController::applyResult` every non-session APVTS value equals the plan (including EQ user bands → default, macros → 0); a module not in the routing has its On parameter 0. **Review Focus 4:** a preset loaded before the second run changes nothing.
- [ ] Implement, run `--test consistency`, `--test "auto mix"` – PASS (re-record goldens that pinned carried-over values, one-line reason each). Commit `Auto mix starts from defaults and sets every parameter`.

### Task 4: Level independence (U3)

**Files:** Modify `AutoMixEngine.cpp` (`run`: measure the median voiced RMS on the capture, scale a copy to `workingLevelDb`, analyse and plan on it, plan sets `inputGain`; renders use the original capture with that `inputGain`), `AutoMixPlanner.cpp` (all level-derived values relative to the working level), `VocalProfiler.cpp` (clipping / peak detection stays on the unscaled audio: pass `float scaleDb` so `clipRuns`, `peakDb`, `hotInput` describe the original). Test: `Tests/ConsistencyTests.cpp`.

**Interfaces – Produces:** `inline constexpr float workingLevelDb = -18.0f;` in `AutoMixEngine.h`; `float AutoMixEngine::workingGainDb (const CapturedAudio&)` (deterministic: median of 50 ms voiced-block RMS, same gate as the profiler, computed on the unscaled audio). Logged as a `ModuleLog` with `globalStage = true`, problem `"level"`, parameter `inputGain`.

- [ ] Failing tests on a synthetic vocal at −18, −20, −22, −16 dBFS RMS and at −40 / clipped (+6 dB into clip): `gainShift` check passes for ±0.5/1/2 dB; −40 dBFS → `inputGain` +22 dB; a take needing more than +24 dB clamps and logs it; the clipped take still reports `clipRuns > 0`. **Review Focus 5:** shifts of ±0.5 dB produce no rounding flip in any value.
- [ ] Implement, run – PASS; CLI on the takes: all gain checks pass. Commit `Auto mix analyses at a fixed working level`.

### Task 5: Deterministic segmented capture and analysis, stored (U1)

**Files:** Modify `CaptureBuffer.h/.cpp` (record song position per block; `CaptureConfig` defaults below; beat stored mono), `AutoMixController.cpp` (pass `hostPpq`; finish when the transport stops with ≥ `minVoicedSeconds`), `PluginProcessor.cpp` (forward ppq to `autoMix.push` – the only processor change), `VocalProfiler.h/.cpp` (segment analysis + aggregation), `AutoMixEngine.cpp` (render excerpt), `UI/AnalysisOverlay.cpp` (hint text only); create `Source/AutoMix/AnalysisStore.h/.cpp`. Test: create `Tests/SegmentTests.cpp`; `Tests/ConsistencyTests.cpp`.

Decisions: `CaptureConfig { targetVoicedSeconds = 60, maxSeconds = 120, minVoicedSeconds = 5 }`; capture ends at 60 s voice, 120 s signal or transport stop. Segments: with song position and tempo = 4 bars aligned to multiples of 16 quarter notes (clamped 6–12 s, else 2 or 8 bars), without = 8 s from the first stored sample; segments with < 2 s voice are skipped. Aggregate: scalar features = voiced-time-weighted median over segments; long-term spectrum and third octaves = power mean over all voiced frames; resonances kept when present in ≥ 50 % of the segments (persistence = that share); S/vowel frames from all segments. Render excerpt for the engine: segments ranked by voiced seconds (ties: earlier position), taken until 30 s, played in song order with 20 ms crossfades. Hint text DE „Spiel die Vocal einmal ganz ab – VOCALYPSE hört bis zu 2 Minuten zu.“ / EN "Play the whole vocal once – VOCALYPSE listens for up to 2 minutes."

**Interfaces – Produces:**
```cpp
struct Segment { int startSample, numSamples; double startPpq = -1; float voicedSeconds; };
std::vector<Segment> VocalProfiler::segmentsOf (const CapturedAudio&);   // CapturedAudio gets std::vector<double> blockPpq (per 512 block, -1 unknown)
VocalProfile VocalProfiler::analyseSegmented (const CapturedAudio&, const std::function<bool()>& shouldStop);   // + std::vector<VocalProfile> segmentProfiles out-param
CapturedAudio AutoMixEngine::excerptFor (const CapturedAudio&, const std::vector<Segment>&);
// AnalysisStore: writes userApplicationDataDirectory/Lind Audio/VOCALYPSE/AutoMix/<yyyyMMdd_HHmmss>_<fnv1a of capture>.json
// { version, intent, capture { seconds, voicedSeconds, hash, ppqStart }, segments [profile...], profile, log [...], values {...}, routing }
// keeps the newest 50 files; the bench writes the same JSON next to each report
juce::File AnalysisStore::write (const AutoMixResult&, const CapturedAudio&);
```
- [ ] Failing tests: the same synthetic 60 s vocal played from 0 s and from 6 s **with** song position gives the same segment boundaries for the overlap and passes the `perturbation` check; **Review Focus 2:** without song position, playing from 0 s twice is `exact`; **Review Focus 1:** an 8 s capture and a capture with 20 s gaps give valid, deterministic profiles with `conservative`; `analyseSegmented` of the same audio 10× is identical; the JSON contains every segment and every `ModuleLog`; memory of an armed buffer at 48 kHz stereo ≤ 70 MB.
- [ ] Implement, run – PASS; CLI offset checks with song position pass for all takes (record without-position results in the report). Commit `Segmented, start-independent analysis, stored as JSON`.

### Task 6: Continuous de-esser and compressor

**Files:** Modify `AutoMixPlanner.cpp` (de-esser and compressor sections, `configureDeEsser`), `AutoMixEngine.cpp` (`tuneDynamics`; the de-esser corrections in the verification loop). Test: `Tests/SoftDecisionTests.cpp`, `Tests/AutoMixTests.cpp`.

Decisions: de-esser need = `soft (sibilanceVsVowelDb, style.sibilanceMax - 6, style.sibilanceMax + 3)`; range = `blend (need, 0, 8)` dB, on when range ≥ `minAudible` 1.5 dB (`lowIntensity` 2.5 dB); threshold continuous between vowel p90 and S median (existing measurement). Compressor: ratio continuous `1.5 + 3.5 * soft (spread / mid, 1.0, 2.5)` (no grid), threshold from crest continuously, `compressed` replaced by `w = soft (dynamicsDb, 8, 5) * soft (crestDb, 12, 9)` blending ratio towards 1.5 and depth towards 1 dB; attack/release already continuous – keep, remove the `rideWanted` jump (blend by the ride's intensity). `tuneDynamics`: search starts from the continuous plan, candidates compared with `chooseWithMargin` (margin 0.15 of the dynamics-window cost), steps symmetric, never on a grid that a gain shift can move.

- [ ] Failing tests: sweeping a synthetic vocal's sibilance 0.1 → 0.9 in 9 steps gives monotonic de-esser range with no step > 1.5 dB; sweeping `phraseRangeDb` 4 → 16 dB gives monotonic ratio and threshold, max step between neighbours ≤ 0.4 ratio / 1.5 dB; compressed-material blend: no jump > 1 ratio across `dynamicsDb` 5.5 → 6.5. Log entries show need, intensity, parameters, measured GR.
- [ ] Implement, run – PASS; CLI: compressor/de-esser checks pass on all takes. Commit `Continuous de-esser and compressor decisions`.

### Task 7: Continuous EQ, stable role slots

**Files:** Modify `AutoEq.cpp/.h` (window edges, `rangeOf`, `limitsOf`, notch selection), `AutoMixPlanner.cpp` (EQ section, `harsh`/`dull` flags in EQ use), `AutoMixEngine.cpp` (EQ correction in the verification loop), `Parameters/EqBands.cpp` only for a fixed slot per role. Test: `Tests/AutoEqTests.cpp`, `Tests/SoftDecisionTests.cpp`.

Decisions: every move's gain = target deviation × `soft` weight of its window edge (no gain from 0 to ±x at an edge); low-cut frequency already continuous – remove the `rumble` slope jump by blending 12 → 24 dB/oct only as a choice with margin; notches only for resonances with persistence ≥ 0.5 (Task 5) and excess weighted by `soft (excessDb, 5, 9)`, depth continuous, at most 3 sorted by `excess × persistence` with a 1 dB margin before a later one replaces an earlier one; each role always uses the same slot (low cut 1, warmth 2, autoMix/mud 3, presence 4, air 5, notches 6–8) when free; the `harsh` / `dull` booleans in EQ decisions replaced by their soft weights.

- [ ] Failing tests: tilt sweep −3 → +3 dB/oct in 13 steps: air/presence gains monotonic, neighbour step ≤ 1 dB; resonance level sweep 4 → 12 dB: notch depth monotonic from 0, no notch below 5 dB excess; a band never changes slot between two runs of slightly different takes; CLI EQ checks pass on all takes.
- [ ] Implement, run – PASS. Commit `Continuous EQ decisions, one slot per role`.

### Task 8: Stable variant choice (U4)

**Files:** Modify `AutoMixEngine.cpp` (candidate search, combination of two variants, verification loop order), `AutoMixObjective.h/.cpp` (complexity cost). Test: `Tests/AutoMixTests.cpp`, `Tests/ConsistencyTests.cpp`.

Decisions: a variant is taken only when `base − variant ≥ max (0.5, 0.08 × base) + complexity (variant) − complexity (base)`; complexity = 0.3 per switched-on module instance + 0.05 per dB of total EQ gain magnitude + 0.1 per dB of median compression; the combination of two needs the same margin over the better single variant; ties (equal within 1e-6) keep the earlier candidate; the verification loop applies its corrections in a fixed order and at most once per kind.

- [ ] Failing tests: a variant 0.3 better than a base of 10 is **not** taken; 1.5 better is; a variant with one more module needs 0.3 more; perturbation checks of the CLI: no variant flip on any take for gain shifts.
- [ ] Implement, run – PASS. Commit `Variant choice with margin and complexity cost`.

### Task 9: Checkpoint – EQ, compressor, de-esser reliable

- [ ] Full `scripts\build.ps1`: 0 failed; pluginval 10 SUCCESS.
- [ ] CLI on all takes → `…\konsistenz\checkpoint`; update report § 4 with baseline vs checkpoint. Expected: repeat, prior, sequential, gain: all pass; offsets/noise: no EQ/compressor/de-esser flip outside `lowIntensity`.
- [ ] Listening renders for Jakob (`…\konsistenz\checkpoint\listening`: per take original, baseline, checkpoint, −18 LUFS). Commit `Checkpoint: EQ, compressor, de-esser stable`. **Stop and report to Jakob; continue with Task 10 only after his OK** (his rule: „erst wenn diese zuverlässig funktionieren, erweitern wir“).

### Task 10: Continuous saturation, smooth, smash, ride, room noise, breath

**Files:** `AutoMixPlanner.cpp` (those sections), `AutoMixEngine.cpp` (`tuneDynamics` Smash blend, smooth correction). Test: `Tests/SoftDecisionTests.cpp`.

Decisions: saturation character by `chooseWithMargin` over scores from the soft `harsh` / `thin` weights and the style (margin 0.2, preferred = style character); mix/drive continuous in need; smooth amount `blend (need, 0, 45)` with need from soft harshness/resonance (no 20 % „Grundpflege“ floor – on only when ≥ `minAudible` 10 %); smash blend continuous in the compressed weight and the style; ride amount continuous in `soft (dynamicsDb, phraseHi, phraseHi + 4)`; room noise amount continuous in `soft (snrDb, 38, 28)` times `1 − reverberant weight`; breath amount continuous in breath share, on from `minAudible` 25 %.

- [ ] Failing tests: sweeps of presence (harshness), SNR, breath share, phrase range: every amount monotonic, neighbour step ≤ 8 points; saturation character changes at most once across a harshness sweep and not back.
- [ ] Implement, run – PASS. Commit `Continuous saturation and clean-up decisions`.

### Task 11: Continuous reverb, delay, doubler

**Files:** `AutoMixPlanner.cpp` (space section), `AutoMixEngine.cpp` (space candidates). Test: `Tests/SoftDecisionTests.cpp`.

Decisions: `roomy` / `someRoom` replaced by `room = soft (tailSeconds, 0.4, 1.2)`; reverb mix = style amount × (1 − room) (+ the existing sustained/spoken terms as `soft (pitchedShare, 0.35, 0.55)` and `soft (pitchedShare, 0.7, 0.85)` blends); reverb type and delay division by `chooseWithMargin` (preferred = the style's); decay, pre-delay, damping, size continuous blends; delay mix × (1 − room); doubler mix continuous in pitched share and correlation (`soft (correlation, 0.75, 0.9)`), on from `minAudible` 5 %.

- [ ] Failing tests: tail sweep 0.2 → 1.6 s: reverb and delay mix fall monotonically to 0, no jump > 6 points; pitched-share sweep: reverb mix and doubler mix monotonic; type/division change at most once per sweep.
- [ ] Implement, run – PASS. Commit `Continuous space decisions`.

### Task 12: Final gate and report

**Files:** `docs/AUTOMIX_KONSISTENZ_BERICHT.md` (§ 5 „Umsetzung“: fixed errors, changed functions, how reproducibility is ensured, tests run, open problems), `CHANGELOG.md`, `docs/HANDOFF.md` top section.

- [ ] Full `scripts\build.ps1`: 0 failed; pluginval 10 SUCCESS; `VocalypseRefHost --editor --plugin build/Vocalypse_artefacts/Release/VST3/VOCALYPSE.vst3` exits.
- [ ] CLI on all takes → `…\konsistenz\final`, all checks pass (list any remaining failures as open problems with numbers). Listening kit as in Task 9.
- [ ] Write the report § 5, install (FL closed), Drive bundle. Commit `Auto mix reproducible and stable: report`. Ask Jakob before pushing.
