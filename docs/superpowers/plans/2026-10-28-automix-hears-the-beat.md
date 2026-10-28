# Auto Mix hört den Beat – Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** With the beat on the sidechain, the auto mix measures the vocal against the beat band by band and sets level, EQ, compression and the amount of reverb / delay so the voice sits on top of the beat. Without a beat nothing changes.

**Architecture:** Everything stays in the auto-mix pipeline (`Source/AutoMix/`). `VocalProfiler` already captures the beat (`CapturedAudio::beat`) and measures two ranges; it gets a per-band comparison and a density measure. `AutoEq` turns the per-band comparison into one measured "unmasking" bell (replacing the fixed +1 dB hint at 3 kHz). `AutoMixPlanner` scales reverb / delay by beat density. `AutoMixObjective` narrows the dynamics window over a dense beat. `AutoMixEngine` sets the final output gain relative to the beat loudness. No DSP module, parameter or layout changes.

**Tech Stack:** C++20, JUCE 8.0.9, own test runner, pluginval.

**Spec:** Jakob's decisions, 2026-10-28 (chat): "Auto Mix hört den Beat" (from the options: band-by-band comparison; the auto mix sets level, EQ and effects so the voice sits on top) · Lautstärke: "Zum Beat einpegeln" – with a beat the output is set so the voice sits about 1 LU above the beat; the report states the value.

**Depends on:** `feature/modules-0.7` (Room Noise / Breath Control in the auto mix).

## Global Constraints

- **Without a beat (no sidechain or silent sidechain) every plan, report and render is unchanged.** Existing auto-mix tests and goldens must pass untouched; any new rule is gated on `profile.beatPresent`.
- No new parameters, no parameter ID changes, no state format change, no UI layout change (texts only).
- The beat is analysed, never heard (existing test "Workflow: the beat on the sidechain is analysed but never heard" stays green).
- Every beat decision is reported in both languages (`both (de, en)`), with the measured number, `measured = true`. German texts follow `docs/I18N.md`.
- Level rule constant in one place: `constexpr float leadOverBeatLu = 1.0f;` (AutoMixEngine). The peak limit (≤ −1 dBFS) and the ±24 dB output range still win; then the report says how far the voice stays below the target.
- The report notes once: the level assumes the sidechain carries the beat at the level it plays (send post-fader at 0 dB).
- Same gate as everywhere: `scripts\build.ps1` 0 failed, pluginval 10 SUCCESS, commit per task, ask before pushing.

## Review Focus

1. **No beat = no change:** run the whole AutoMix suite before and after; diff the reports of `--automix` on a test vocal without sidechain (must be identical).
2. **Beat louder than the voice by a lot (e.g. +20 LU):** output gain clamps at +24 dB and the peak limit, no overs; report explains.
3. **Beat only during part of the capture** (e.g. starts after 4 s): measurements use only the frames where both play; a beat present < 3 s of the voiced time counts as no beat.
4. **Mono sidechain / mono vocal:** same results as stereo duplicates.
5. **Unmasking bell never stacks with harshness:** harsh vocal + masking beat → no boost, a note instead.

---

### Task 1: Synthetic beats for the tests

**Files:** Create `Tests/SyntheticBeat.h` (header-only, like `Tests/SyntheticVocal.h`).

**Produces:** `CapturedAudio`-compatible stereo buffers: `makeBeat (BeatKind kind, double fs, double seconds, float lufs)` with `BeatKind { dense, sparse, presenceHeavy, lowMidHeavy }` – kick on every quarter at 128 BPM, hats (high-passed noise, 8ths), a pad (band noise 300 Hz–6 kHz); `dense` keeps the pad on all the time, `sparse` plays the pad only every other bar and drops the hats in half the bars; `presenceHeavy` lifts 2–5 kHz by 9 dB; `lowMidHeavy` lifts 250–500 Hz by 9 dB. Normalised to `lufs` (BS.1770, the profiler's `integratedLufs`). Deterministic (fixed seed).

- [ ] Small self-test in `Tests/AutoMixTests.cpp`: each kind measures within ±0.5 LU of the requested loudness; `dense` has a smaller gap share than `sparse` (once Task 2 exists – add the check there).
- [ ] Commit `Tests: synthetic beats for the auto mix`.

### Task 2: Profiler – band-by-band comparison and density

**Files:** Modify `Source/AutoMix/VocalProfile.h`, `Source/AutoMix/VocalProfiler.cpp` (beat section around line 774). Test: `Tests/AutoMixTests.cpp` (new `beginTest`s, prefix "Beat:").

**Produces (new fields in `VocalProfile`, all only valid when `beatPresent`):**
- `std::vector<float> beatOverVocalDb` – per profiler band (`VocalProfiler::numBands`), beat power minus vocal power over the frames where the voice is voiced **and** the beat plays (beat frame level above −50 dBFS).
- `float beatGapShare` – share of 50 ms blocks of the beat that are more than 12 dB below the beat's 90th-percentile block level (0 = wall of sound, ~0.5 = sparse).
- `float vocalOverBeatLu` – vocal integrated LUFS minus beat integrated LUFS, both over the span where the beat plays.
- `float beatPlayingSeconds`.
- Keep `beatPresenceVsVocalDb`, `beatLowMidVsVocalDb`, `maskingHint` (now derived from `beatOverVocalDb`, same meaning) so the assistant and Vocal Check keep working.
- `beatPresent` additionally requires `beatPlayingSeconds >= 3`.

- [ ] Failing tests: (a) synthetic vocal + `presenceHeavy` beat → `beatOverVocalDb` in the 2–5 kHz bands is ≥ 6 dB above the 300 Hz–1 kHz bands; (b) `dense` vs `sparse` → gap share < 0.15 vs > 0.35; (c) vocal at −18 LUFS, beat at −14 LUFS → `vocalOverBeatLu` = −4 ± 0.5; (d) beat starting after 4 s of a 10 s capture → measured only over the last 6 s (`beatPlayingSeconds` 6 ± 0.2); a 2 s beat → `beatPresent == false`; (e) mono sidechain = duplicated stereo within 0.1 dB.
- [ ] Implement, run – PASS. Commit `Auto mix profile: vocal against the beat band by band, beat density`.

### Task 3: Level – the voice sits on top of the beat

**Files:** Modify `Source/AutoMix/AutoMixEngine.cpp` (final output gain, around line 714), report text. Test: `Tests/AutoMixTests.cpp`.

Rule: with `beatPresent`, the final output gain is chosen so the processed vocal's integrated loudness = beat LUFS + `leadOverBeatLu` (instead of the input loudness). Then the existing peak rule (≤ −1 dBFS) and the ±24 dB clamp apply. Candidate scoring keeps the old `targetLufs (input)` (fair comparison; only the final gain moves). Decision (`outputStage = true`, measured): DE „Output {0} dB: Stimme 1 LU über dem Beat (Beat {1} LUFS)“ / EN "Output {0} dB: voice 1 LU above the beat (beat {1} LUFS)". When the peak limit stops it: „… – die Spitzen erlauben nur {2} LU unter dem Ziel“ / "… – the peaks allow only {2} LU below the target". Plus the one-time note about the sidechain level (Global Constraints).

- [ ] Failing tests: (a) vocal −18 LUFS, beat −12 LUFS → rendered mix (plugin output) at −11 ± 0.5 LUFS; (b) beat −30 LUFS → −29 ± 0.5; (c) beat 20 LU louder → output gain ≤ +24 and peak ≤ −1 dBFS, report contains the "peaks allow" text; (d) no beat → output gain identical to before (compare to a run on the same vocal without sidechain, exact).
- [ ] Implement, run – PASS. Commit `Auto mix: with a beat on the sidechain the voice sits 1 LU above it`.

### Task 4: EQ – measured unmasking instead of the fixed hint

**Files:** Modify `Source/AutoMix/AutoEq.h/.cpp` (the "beat masks the presence range" block around line 354; `Context` gets `const std::vector<float>* beatOverVocal` or reads `profile->beatOverVocalDb`), `Source/AutoMix/AutoMixPlanner.cpp` (line 147 context). Test: `Tests/AutoEqTests.cpp`.

Rule: in 1.5–6 kHz find the band region (3 adjacent bands, smoothed) where `beatOverVocalDb` minus its median over 300 Hz–1 kHz is largest = `excess`. If `excess > 3 dB` and not harsh: one `autoTone` bell there, Q 1.0, gain = clamp (0.3 · (excess − 3), 0.5, 2.5) dB, rounded to 0.5 dB. Harsh: no boost, note DE „Der Beat verdeckt {0} kHz, aber die Stimme ist dort schon scharf – keine Anhebung“ / EN equivalent. Low-mid: if `beatOverVocalDb` at 250–500 Hz exceeds the core by > 6 dB and the existing mud rule already cuts, that cut may go 1 dB deeper (never a new cut from the beat alone). Reason texts name the frequency and the measured dB.

- [ ] Failing tests: (a) `presenceHeavy` beat → one bell between 2 and 5 kHz, +0.5…+2.5 dB, measured; (b) `dense` neutral beat → no unmasking bell; (c) harsh vocal + `presenceHeavy` → no boost, note present; (d) `lowMidHeavy` + muddy vocal → mud cut 1 dB deeper than without beat; clean vocal + `lowMidHeavy` → no low-mid cut; (e) the band budget (6) is respected.
- [ ] Implement, run – PASS. Commit `Auto EQ: lift only where the beat measurably covers the voice`.

### Task 5: Space and dynamics follow the beat density

**Files:** Modify `Source/AutoMix/AutoMixPlanner.cpp` (space section, line ~540), `Source/AutoMix/AutoMixObjective.cpp` (dynamics window), `docs/AUTOMIX_OBJECTIVE.md`. Test: `Tests/AutoMixTests.cpp`.

Rules (only with `beatPresent`): dense beat (`beatGapShare < 0.15`) → reverb mix × 0.7 (rounded), delay mix − 2 %, decision reason adds „dichter Beat: weniger Hall, damit die Stimme vorne bleibt“ / "dense beat: less reverb so the voice stays in front"; sparse beat (`> 0.4`) → reverb mix + 2 % (max 16), reason „Beat mit Lücken: etwas mehr Raum“ / "beat with gaps: a little more room". Objective: over a dense beat the dynamics window is 3–6 dB instead of 4–8 dB (steadier voice against a wall of sound); otherwise unchanged.

- [ ] Failing tests: (a) same sung vocal: dense beat → reverb mix lower than without beat, sparse → higher, no beat → unchanged; (b) objective: dynamicsDb 7 is penalised with dense beat, not without; (c) roomy take stays without reverb whatever the beat.
- [ ] Implement, run – PASS. Commit `Auto mix: reverb, delay and dynamics follow how dense the beat is`.

### Task 6: Assistant, Vocal Check, docs, changelog

**Files:** Modify `Source/Assistant/AssistantPlanner.cpp` (line ~600: replace the "not implemented yet" note with the measured comparison: the band where the beat covers most, in dB), `Source/AutoMix/VocalProfiler.cpp` (masking issue text uses the new region), `CHANGELOG.md`, `docs/HANDOFF.md`, `docs/ARCHITECTURE.md` (one paragraph). Tests: `Tests/AssistantTests.cpp`.

- [ ] Failing test: asking "die Stimme geht im Beat unter" with a beat profile → the note names a frequency and dB, no "noch nicht implementiert".
- [ ] Implement, full gate (`scripts\build.ps1` + pluginval 10), commit `Assistant and Vocal Check: measured masking against the beat; changelog`.
