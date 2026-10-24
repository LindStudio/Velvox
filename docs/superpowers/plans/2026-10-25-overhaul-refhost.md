# Reference Host + Quality Baseline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Measurement infrastructure for the 0.6 DSP-quality work: a reference host that renders dry vocals through one Headliner
module and through an installed reference VST3 at matched settings, a metrics library per module type, and a measured baseline with
pass/fail tolerances (spec §6, §8 Q6).

**Architecture:** One C++ console target `HeadlinerRefHost` (JUCE, `JUCE_PLUGINHOST_VST3=1` only on this target) renders WAVs
offline through any `juce::AudioProcessor` – a hosted VST3 (incl. WaveShell sub-plugins) or the in-process `VelvoxAudioProcessor`
with a one-module routing. The render core (`tools/refhost/RefHostCore.h`) is format-agnostic and unit-tested inside `VelvoxTests`.
Analysis is Python 3.13 + numpy/scipy (`tools/refhost/qmetrics.py`), driven per module by `tools/refhost/bench.py`, which renders,
level-matches (BS.1770 integrated loudness), measures and writes JSON + a markdown table.

**Tech Stack:** C++20, JUCE 8.0.9 (`juce_audio_processors` VST3 hosting), Python 3.13, numpy 2.5, scipy 1.18, ffmpeg not needed.

**Spec:** `docs/superpowers/specs/2026-10-25-headliner-pro-overhaul-design.md` (§6, §8 Q6, §2.9 null tests)

## Global Constraints

- No changes to `Source/UI/*` or the DSP modules; only new tool/test targets (+ the one test file in the `VelvoxTests` list).
- VST3 hosting enabled only for `HeadlinerRefHost`; the plugin and `VelvoxTests` keep `VELVOX_COMMON_DEFINITIONS` unchanged.
- Sample rate 48 kHz, block-wise offline rendering, reported latency compensated (output aligned to input sample 0).
- Dry vocals: VocalSet val singers only (f7, f8, m8, m9 – never f9, m10, m11) and the MYLK dry chorus. ASCII paths only.
- Rendered WAVs go to `C:\Users\jakob\dev\velvox-bench\2026-10-25\modules\<module>\` (outside git).
- Level matching on integrated loudness (BS.1770-4, gated) before any comparative metric.
- Quality gate (Q6): pass = within or better than the reference on key metrics, tolerances per metric in QUALITY_BASELINE.md.
- Python: `C:\Users\jakob\AppData\Local\Programs\Python\Python313\python.exe -I`.

## Review Focus

1. A reference plugin that needs an authorization popup or crashes on load – the batch must record "not loaded" and continue
   (bench.py: render failure of the reference side -> metrics only for Headliner, marked `ref: n/a`). Test: `test_bench_missing_reference`.
2. Plugins with latency (soothe2, Pro-DS lookahead, Pro-Q linear phase, Smooth STFT) – misalignment ruins null/delta metrics.
   Test: `RefHostCore latency compensation` with a fake processor reporting 333 samples.
3. Parameter text that the plugin cannot parse (`getValueForText` returns garbage) – fall back to bisection on `getText`.
   Test: `RefHostCore parameter set by text with bisection`.
4. Mono VocalSet clips into stereo plugins – the host must duplicate mono into both input channels. Test: render of a 1-channel buffer
   through a 2-in processor yields identical L/R input.
5. Silent/near-silent outputs (e.g. a module that gates everything) – loudness of −inf must not crash level matching.
   Test: `test_loudness_silence`.

---

### Task 1: Format-agnostic render core

**Files:**
- Create: `tools/refhost/RefHostCore.h`
- Create: `Tests/RefHostTests.cpp`; Modify: `CMakeLists.txt` (add to VelvoxTests sources, include `tools/refhost`)

**Interfaces:**
- Produces (namespace `velvox::refhost`):
  - `juce::AudioProcessorParameter* findParameter (juce::AudioProcessor&, const juce::String& key)` – key = index, parameter ID
    (`HostedAudioProcessorParameter::getParameterID` / `AudioProcessorParameterWithID::paramID`) or name (case-insensitive exact,
    then unique prefix).
  - `bool setParameterText (juce::AudioProcessorParameter&, const juce::String& text, juce::String& error)` – numeric text: try
    `getValueForText`, verify via `getText` numeric readback (±0.5 % of range or ±0.01); otherwise bisection over the normalized
    value (monotonic getText); choice text: exact match among the steps.
  - `struct RenderStats { int latency; int blocks; double meanBlockMicros, maxBlockMicros; };`
  - `juce::AudioBuffer<float> renderOffline (juce::AudioProcessor&, const juce::AudioBuffer<float>& in, double fs, int block,
    int tailSamples, RenderStats&)` – prepares with the processor's main bus channel count, mono in -> both channels,
    processes `in + latency + tail` samples, returns the output with the first `latency` samples removed.
  - `juce::String describeParameters (juce::AudioProcessor&)` – one line per parameter: index, ID, name, text, normalized, steps.

- [ ] Step 1: Write `Tests/RefHostTests.cpp` (category "RefHost"): a `FakeDelayFx` (latency 333, delays by 333, one float
  parameter "Gain dB" −24..24 whose `getValueForText` is broken and a choice "Mode" A/B/C); expect output[i] == input[i] for all i,
  length == in + tail; `findParameter` by "1", "gain db", "Gai"; `setParameterText ("6")` -> getText reads 6.0 ± 0.05; "B" -> index 1.
- [ ] Step 2: Build, run `--test RefHost`, expect compile failure (header missing).
- [ ] Step 3: Implement `RefHostCore.h`.
- [ ] Step 4: Run `--test RefHost`, expect PASS. Commit.

### Task 2: HeadlinerRefHost CLI

**Files:** Create `tools/refhost/RefHostMain.cpp`, `tools/refhost/HeadlinerModule.h`; Modify `CMakeLists.txt` (new console target,
`VELVOX_SOURCES`, `JUCE_PLUGINHOST_VST3=1`).

**Interfaces:**
- `HeadlinerRefHost --list --plugin <vst3> [--sub <name>]`
- `HeadlinerRefHost --subs --plugin <vst3>` (WaveShell: all sub-plugin names)
- `HeadlinerRefHost --render --plugin <vst3> [--sub <name>] --in a.wav --out b.wav [--set key=value]... [--setn key=0..1]...
  [--tail seconds] [--block n]` -> writes b.wav (32-bit float, 48 kHz) and `b.wav.json` {latency, blocks, meanBlockUs, maxBlockUs,
  params:{name:text}} – exit 2 = plugin not loadable, 3 = parameter not found/set.
- `HeadlinerRefHost --headliner <moduleKey> [--instance k] ...same render options...` (`--set id=value` in real units on the
  APVTS ID; module routed alone as insert, `safetyClip` 0, `xxxOn` 1) and `--list --headliner <moduleKey>`.
- `HeadlinerModule.h`: `std::unique_ptr<VelvoxAudioProcessor> makeSingleModule (ModuleId, int instance)`.

- [ ] Step 1: Failing check: `RefHostTests` case "single module routing" – `makeSingleModule (eq)` -> routing size 1, eqOn 1,
  safetyClip 0; a sine through it with all EQ gains 0 nulls < −90 dB. (Header in tools/refhost, used by the test.)
- [ ] Step 2..4: implement, build both targets, run; manual smoke: `--list --plugin "FabFilter Pro-Q 4.vst3"`, `--subs` on WaveShell 16.6.
- [ ] Step 5: Commit.

### Task 3: Metrics library

**Files:** Create `tools/refhost/qmetrics.py`, `tools/refhost/tests/test_qmetrics.py`.

**Interfaces (all take float64 arrays shaped (n,) or (n, ch) and fs):** `read_wav`, `write_wav`, `integrated_lufs`,
`match_gain_db(ref, x)`, `thd_db(y, f0)`, `aliasing_db(y, f0)` (energy at non-harmonic bins relative to total), `static_curve
(in_levels_db, outputs)`, `attack_release_ms(env_db, step_index)`, `peak_to_envelope_db(x)` (mean spectral peak above a 1/3-oct
smoothed envelope, 1–10 kHz), `transient_preservation(dry, wet)` (onset-envelope correlation), `delta_energy_db(dry, wet)`,
`musical_noise(dry, wet)` (spectral-gain flux on the gain mask), `freq_response(dry, wet)` (H1 estimate, dB on log grid),
`schroeder_decay(ir)` (EDT, T20, T30 per octave band), `echo_density_profile(ir)` (Abel–Huang NED), `modal_ringing_db(ir)`
(late-tail spectral peak prominence), `delay_track(dry, wet)` (frame cross-correlation lag in ms) and `smoothness` (RMS of its
2nd difference), `pitch_track_cents`, `comb_depth_db(mono_sum)`, `null_db(a, b)`.

- [ ] Steps: write tests with synthetic signals of known answers (tanh sine -> THD known within 0.5 dB, pure tone THD < −100 dB,
  −23 LUFS 1 kHz-sine calibration (997 Hz sine at −23 dBFS -> −23.0 ± 0.1 LUFS... per BS.1770 a full-scale 1 kHz stereo sine
  reads −3.01 LUFS per channel), exponential decay RT60 2.0 s -> T30 2.0 ± 0.1, white noise NED ≈ 1, a 1-kHz comb ->
  modal ringing high, silence -> −inf without exception), watch them fail, implement, pass. Commit.

### Task 4: Per-module bench runner

**Files:** Create `tools/refhost/bench.py`, `tools/refhost/modules.py`, `tools/refhost/signals.py`, test `tools/refhost/tests/test_bench.py`.

**Interfaces:** `python -I tools/refhost/bench.py <module> [--out DIR] [--clips N] [--no-ref]` -> renders into
`<out>/<module>/`, writes `<module>/metrics.json` and prints a markdown table; `python -I tools/refhost/bench.py all`.
`modules.py: MODULES: dict[str, ModuleSpec]` with `headliner` params, `references: list[RefSpec(path, sub, params)]`,
`signals`, `metrics` names. `signals.py: CLIPS` (6 clips) and synthetic generators (sine, log sweep, level steps, bursts, impulse).

- [ ] Steps: tests for clip selection (val singers only), signal generators (lengths, levels), `test_bench_missing_reference`
  (a RefSpec with a non-existent path -> result has `ref_error`, Headliner metrics present); implement; commit.

### Task 5: Baseline + document

**Files:** Create `docs/QUALITY_BASELINE.md`; ledger.

- [ ] Run `bench.py all`, read metrics, write per-module tables (metric, Headliner, reference, gap), the ranked gap list with DSP
  recommendations, the tolerances per module, the plugins that did/did not load, and the one-command re-run per module.
- [ ] Full suite `-Test "*"` green. Commit.
