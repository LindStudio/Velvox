# Raumrauschen + Atem-Absenker – Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Two new modules – **Raumrauschen** (key `denoise`, room noise reduction) and **Atem-Absenker** (key `breath`, breath reducer) – each with a user-set strength, in the chain, the browser, the auto mix and the help.

**Architecture:** Both are ordinary chain modules (`dsp::ModuleId::denoise`, `dsp::ModuleId::breath`, one instance each) built like `SmashModule`: a settings struct, a module class with `prepare / reset / process`, parameters bound in `SettingsReader`, a `DetailPanel` with one big strength knob plus a live reduction meter. Denoise works in the frequency domain (STFT, learned noise floor), Breath in the time domain (detector + look-ahead gain). Both report latency; the chain sums the latency of switched-on modules (replacing the pitch-only path removed in plan A).

**Tech Stack:** C++20, JUCE 8.0.9 (`juce::dsp::FFT`), own test runner, pluginval.

**Spec:** Jakob's decisions, 2026-10-27 (chat): „dafür rein: Raumrauschen, Atem-Absenker“ · „Stärke soll man selbst einstellen können bei den neuen Modulen“. Earlier proposal he accepted: denoise lowers room noise; breath reducer lowers breaths between lines; the auto mix sets both.

**Depends on:** `2026-10-27-remove-creative-modules.md` (enum with 11 modules, no latency path).

## Global Constraints

- New parameter IDs (never change once shipped): `denoiseOn`, `denoiseAmount` (0–100 %, default 50), `breathOn`, `breathAmount` (0–100 %, default 50).
- Strength mapping: denoise 100 % = up to 18 dB reduction of the noise floor; breath 100 % = breaths −30 dB (practically gone), 50 % = −12 dB.
- Display names: DE „Raumrauschen“ / EN "Room Noise"; DE „Atem-Absenker“ / EN "Breath Control". Knob label DE „Stärke“ / EN "Amount".
- A clean vocal (no noise, no breaths) passes both modules within ±0.3 dB level and with no audible change (tests below).
- Latency: denoise 1024 samples at any rate (FFT 2048, hop 512, synthesis delay 1024); breath look-ahead 5 ms. Reported to the host; dry/compare paths stay time-aligned.
- Default chain position: `denoise` first, `breath` right after it (before EQ).
- Same gate as everywhere: `scripts\build.ps1` 0 failed, pluginval 10 SUCCESS, commit per task, ask before pushing.

## Review Focus

1. **Sibilants vs. breaths:** an "s" right after a vowel must not be lowered by the breath reducer (it is noisy and quiet-ish too). Test in Task 3.
2. **Noise floor learned from a vocal that never pauses:** the denoiser must not eat the voice when it never sees pure noise (minimum statistics over 1.5 s, never above the signal's own floor). Test in Task 2.
3. **Sample rate 44.1 / 48 / 96 kHz and block sizes 1…2048:** same reduction, no clicks at block borders. Test in Task 2 and 3.
4. **Switching on/off while playing / bypass compare:** no click, no time jump (latency stays reported while the module is in the chain). Test in Task 4.
5. **Auto mix on a clean studio vocal** switches neither module on. Test in Task 5.

---

### Task 1: Model, parameters, latency sum

**Files:** Modify `Source/DSP/Routing.h/.cpp` (append `denoise, breath` → `numModules = 13`, keys "denoise" / "breath", default order puts them first), `Source/DSP/Instances.h` (1 instance each), `Source/Parameters/ParameterIDs.h`, `ParameterLayout.cpp`, `SettingsReader.*`, `Source/DSP/VocalChain.*` (`int getLatencyFor (const Routing&, const ChainSettings&) const noexcept` = sum of `latencySamples()` of switched-on modules in the routing). Test: `Tests/RoutingTests.cpp`, `Tests/VelvoxTests.cpp`.

**Interfaces – Produces:** `struct DenoiseSettings { bool enabled = false; float amount = 50.0f; };` `struct BreathSettings { bool enabled = false; float amount = 50.0f; };` in `ChainSettings` as `denoise`, `breath`.

- [ ] Failing tests: keys round-trip; the four parameters exist with the ranges above; latency is 0 with both off and equals `DenoiseModule::latency + BreathModule::latencyFor (fs)` with both on.
- [ ] Implement (modules as pass-through stubs with their fixed latency), run – PASS. Commit `Denoise and Breath: model, parameters, latency`.

### Task 2: DenoiseModule (Raumrauschen)

**Files:** Create `Source/DSP/DenoiseModule.h/.cpp`. Test: create `Tests/DenoiseTests.cpp`.

**Interfaces – Produces:** `class DenoiseModule { void prepare (double fs, int maxBlock); void reset(); void process (juce::AudioBuffer<float>&, const DenoiseSettings&); static constexpr int latency = 1024; float getReductionDb() const noexcept; };`

Algorithm (decided here): STFT, Hann window, FFT 2048, hop 512 (75 % overlap), per-channel. Noise floor per bin = minimum of the bin's smoothed power over the last 1.5 s (minimum statistics, smoothing 0.9), times 1.5 bias. Gain per bin = `max (1 − α·N/P, floor)` with α = 1 + 2·amount, floor = dB(−18·amount); gains smoothed over time (attack 5 ms, release 60 ms) and across 3 neighbouring bins (musical-noise control). `getReductionDb` = mean reduction for the meter.

- [ ] Failing tests (`DenoiseTests`): (a) synthetic vocal (`SyntheticVocal.h`) + pink noise at −45 dBFS, amount 100: noise in the gaps drops ≥ 12 dB, voiced frames change ≤ 1 dB RMS; (b) clean vocal, amount 100: output = input delayed by 1024 within −60 dB error; (c) continuous vocal without gaps, 10 s: voiced level loss ≤ 1 dB; (d) 44.1/48/96 kHz and block sizes 1, 64, 2048 give the same output within 1e-4.
- [ ] Implement, run – PASS. Commit `Raumrauschen: STFT noise reduction`.

### Task 3: BreathModule (Atem-Absenker)

**Files:** Create `Source/DSP/BreathModule.h/.cpp`. Test: create `Tests/BreathTests.cpp`.

**Interfaces – Produces:** `class BreathModule { void prepare (double fs, int maxBlock); void reset(); void process (juce::AudioBuffer<float>&, const BreathSettings&); static int latencyFor (double fs) noexcept; /* 5 ms */ float getReductionDb() const noexcept; };`

Detector (decided here), on a mono sum, 10 ms frames: a frame is breath when all hold – level 12–45 dB below the running phrase level (peak hold 2 s), spectral flatness of 300 Hz–8 kHz ≥ 0.35, energy centroid 500 Hz–5 kHz (sibilants sit higher), no voicing (normalised autocorrelation peak < 0.4), and the noisy stretch lasts ≥ 80 ms. Gain on breath frames, with a = amount / 100: `a ≤ 0.5 ? −24·a dB : −(12 + 36·(a − 0.5)) dB` (50 % → −12 dB, 100 % → −30 dB), ramped over the 5 ms look-ahead (attack) and 30 ms (release).

- [ ] Failing tests: (a) vowel – band-limited noise breath (300 ms, −28 dB below the vowel) – vowel, amount 50: breath level −12 dB ±2, vowels ±0.3 dB; amount 100: −30 dB ±3; (b) vowel directly followed by an "s" (high-passed noise > 4 kHz, 120 ms): the "s" changes ≤ 1 dB; (c) clean vocal: output = delayed input within −60 dB; (d) block sizes 1/64/2048 identical.
- [ ] Implement, run – PASS. Commit `Atem-Absenker: breath detection and gain`.

### Task 4: Panels, browser, help, chain integration

**Files:** Create `Source/UI/CleanupPanels.h/.cpp` (both panels: big strength knob, live reduction meter from `getReductionDb`, one-line description); modify `ModuleBrowser.cpp`, `ModuleViews.cpp`, `DetailPanels.cpp`, `ChainView.cpp`, `HelpContent.cpp`, `Text/ParamText.cpp`, `LiveData.h` (activity), `CMakeLists.txt`; add icons `Resources/Icons/denoise.svg`, `breath.svg` (same line style as `deess.svg`). Test: `Tests/UiTests.cpp`.

- [ ] Failing tests: browser lists 13 modules incl. „Raumrauschen“ / „Atem-Absenker“ (EN: "Room Noise" / "Breath Control"); each panel has one knob bound to its amount; toggling a module on/off during processing produces no sample step > −40 dBFS (click test as in `StabilityTests`).
- [ ] Implement, run – PASS; screenshot both panels (`--snapshot … pro <moduleId>`) and look at them. Commit `Raumrauschen and Atem-Absenker: panels, browser, help`.

### Task 5: Auto mix sets them

**Files:** Modify `Source/AutoMix/VocalProfiler.*` (add `float breathShare` = share of 10 ms frames the Task 3 detector flags; reuse `snrDb`), `Source/AutoMix/AutoMixPlanner.cpp` (decisions + report texts). Test: `Tests/AutoMixTests.cpp`.

Rules: denoise on when `snrValid && snrDb < 40`, amount = jmap (snrDb, 40 → 20 %, 20 → 80 %) clamped 20–80; breath on when `breathShare > 0.02`, amount 50 % (spoken 40 %). Report lines DE/EN in the existing style.

- [ ] Failing tests: noisy synthetic vocal → denoise on with amount in 20–80; vocal with breaths → breath on 50 %; clean vocal → both stay off.
- [ ] Implement, run – PASS. Full gate (`scripts\build.ps1`, pluginval 10). Update `CHANGELOG.md`. Commit `Auto mix: room noise and breath reduction`.
