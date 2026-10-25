# Vocal Check (algorithm + apply model) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A more accurate Vocal Check: findings with id, category, severity, confidence, short German problem/fix texts and a concrete,
idempotent fix action (one undo step), a 0–100 score, persistent "Ignorieren" – validated on real dry vocals with injected problems.

**Architecture:** `VocalAnalyzer` (message thread, fed lock-free by `CheckModule`) gains the missing measurements (pitch, 1/3-octave
deviation from a data-derived voice template, loudness/LRA, plosives, sibilance centre, mono loss). A pure rule engine
(`Source/Check/CheckRules`) turns the measurements plus genre/voice targets (`CheckTargets`, from the auto-mix objective windows) into
findings and a score. A pure fix planner (`CheckFixPlanner`) turns a finding's abstract `FixAction` (parameter amounts, module
insertions, abstract `EqBandRequest`s) into absolute parameter values through an `EqBandResolver`; `applyResolvedFix` writes them into
the APVTS as one undo transaction. `VocalCheckController` (owned by the processor) holds ignore state (persisted on the state tree),
the applied-generation guard and the public API for the UI.

**Tech Stack:** C++17, JUCE 8.0.9, VelvoxTests (juce::UnitTest), Python 3.13 + numpy only for exploration.

**Spec:** `docs/superpowers/specs/2026-10-25-headliner-pro-overhaul-design.md` §2.11, §5.15, §7, §8 (Q5 roles, Q8 undo).

## Global Constraints

- Never rename/remove parameter IDs or plugin codes; only add. No new parameters are needed for this track (state properties only).
- Old projects must sound identical: Vocal Check never changes audio; new state keys are optional (missing = empty/default).
- Real-time safe: `CheckModule::process` stays the only audio-thread part (no allocation/locks/logging). All analysis on the message thread.
- Do not edit `Source/UI/*` (shell track). Existing `dsp::Finding` fields used by the UI stay (title, detail, advice, module, certainty,
  severity, why, confidence, handling, handlingText); new fields are additive.
- German UI texts ("Ignorieren", "Anwenden", "Alles anwenden"); finding texts short, e.g. "Mulm um 300 Hz (+4 dB)" / "→ EQ −3 dB bei 300 Hz".
- EQ roles mirror spec Q5: none/presence/warmth/air/lowcut/mud/auto. Macros/auto-mix own their roles; user bands (none) are never touched.
- Undo: one global history (Q8); "Anwenden"/"Alles anwenden" are one step each. Use `apvts.undoManager` (the shell track supplies it).
- Data: VocalSet val singers f7, f8, m8, m9 (excerpts) + MYLK dry chorus (`velvox-bench/2026-10-24/hoertest_original_vs_0.4/08_A.wav`,
  08_B = Headliner 0.4 mix). Template only from VocalSet train singers (f1–f6, m1–m7). Never the held-out singers f9, m10, m11.
- Never edit files through PowerShell Get/Set-Content.

## Review Focus

1. Check placed *before* the EQ (or EQ not in the chain): "Anwenden" must still fix the problem and a second "Anwenden" must not cut deeper.
2. "Alles anwenden" with two findings that want the same EQ slot (legacy resolver: one mid bell) – result must be deterministic, one undo step.
3. Very little / silent material, mono vs stereo input, 44.1 vs 48 kHz – no finding without enough voice, ids stable across rates.
4. Ignored finding id after reload (state round trip) and after an analysis where the resonance moved by < 1/3 octave – stays ignored.
5. A processed (auto-mixed) vocal must not be told to undo the auto-mix (e.g. "zu hell" right after the mix's air boost).

---

### Task 1: Finding schema and fix-model types

**Files:**
- Create: `Source/Check/CheckTypes.h`
- Modify: `Source/DSP/VocalAnalyzer.h` (Finding: new fields), `CMakeLists.txt` (new test file)
- Test: `Tests/VocalCheckFixTests.cpp` (new)

**Interfaces:**
- Produces (namespace `velvox::check`):
  - `enum class Category { level, lowEnd, tonal, resonance, harshness, sibilance, dynamics, stereo, noise, space }`
  - `enum class Severity { low, medium, high }`
  - `enum class EqRole { none, presence, warmth, air, lowcut, mud, autoRole }`, `enum class EqShape { bell, lowShelf, highShelf, lowCut, highCut }`
  - `struct EqBandRequest { EqRole role; EqShape shape; float freqHz; float gainDb; float q; int slopeDbPerOct = 12; }`
    (`gainDb` is an *amount*: the change the measurement asks for; the planner makes it absolute)
  - `struct ParamChange { juce::String paramId; float value; enum class Kind { set, amount, atLeast, atMost } kind; float neutral; }`
  - `struct FixAction { std::vector<dsp::ModuleId> modules; std::vector<ParamChange> params; std::vector<EqBandRequest> eq; bool empty() const; }`
  - `struct ResolvedParam { juce::String paramId; float value; }`, `struct ResolvedFix { juce::String name; std::vector<ResolvedParam> params; std::optional<dsp::Routing> routing; bool empty() const; }`
- `dsp::Finding` gains: `juce::String id; check::Category category; check::Severity level; juce::String problem, fix; check::FixAction action; float freqHz = 0;`

- [ ] Step 1: test "Finding schema: every shown finding has id, category, German problem/fix text and confidence ≥ threshold" (synthetic vocal with rumble+resonance) – fails to compile (fields missing).
- [ ] Step 2: add the types + fields; fill them from the existing rules minimally (id per rule) → test passes.
- [ ] Step 3: commit.

### Task 2: Measurements

**Files:** Modify `Source/DSP/VocalAnalyzer.{h,cpp}`; Test `Tests/VocalCheckFixTests.cpp`

**Interfaces (AnalysisResult gains):** `f0MedianHz, f0LowHz, f0HighHz, pitchedShare, pitchSpreadSemitones, pitchValid, std::vector<float> sungNotesHz`,
`loudnessLufs` (BS.1770-style gated, K-weighting applied per 1/12-octave band), `lraLu`/`lraValid` (3 s windows, 10th–95th pct),
`plosiveCount`, `plosivesPerMinute`, `sibilanceHz`, `monoLossDb`, `std::vector<float> deviationDb` (1/3-octave smoothed LTAS minus
voice template on the 1/12-octave grid, median-normalised over 150 Hz–8 kHz; empty = not enough material), `highVoice`, `generation`.
`VocalAnalyzer::getGeneration()` increments on `clear()`.

- [ ] Tests (fail first): synthetic vocal f0 = 220 Hz → f0Median within 3 %; −20 dB RMS sine at 1 kHz for 10 s → loudness −20±1 LUFS (K-weighting ~0 dB at 1 kHz... with the +0.69 offset: −20.0±1); 6 injected 40 ms LF bursts (80 Hz, +12 dB over the voice peak) → plosiveCount 6±1, clean → 0; generation increments on clear().
- [ ] Implement: per-frame autocorrelation f0 (power spectrum → inverse FFT of order 13, peak 70–1000 Hz, clarity > 0.45); per-frame LF level 40–150 Hz; plosive = LF ≥ 12 dB over the voiced-frame median LF, ≥ 6 dB over both neighbours, and LF share ≥ 0.5 of the frame energy, voiced or not (event = run of such frames).
- [ ] Commit.

### Task 3: Voice template and targets

**Files:** Create `Source/Check/CheckTargets.{h,cpp}`, `Source/Check/VoiceTemplate.h` (generated table), `Tests/CheckEval.cpp` (CLI), modify `Tests/TestMain.cpp` (one dispatch line), `CMakeLists.txt`.

**Interfaces:**
- `enum class Genre { party, pop, acoustic }` (party = default = the auto-mix calibration: EDM/party/pop lead).
- `struct Window { float lo, hi; }`; `struct CheckTargets { Window body, lowMid, top, presence, air, phrase; float rumbleMax, sibilanceMax, crestMin, lraMax, loudnessMin, peakMax, correlationMin, snrMin, tailMax, devMud, devBoxy, devHarsh, resonanceMin; static CheckTargets make (Genre, bool highVoice, bool spoken); }`
  Party values = AutoMixObjective windows (body −9..−1, lowMid −7..0, top −16..−7, air −32..−16, presence ≤ −4, phrase 3..6, crest ≥ 5.5, sibilance ≤ −6); pop/acoustic widen dynamics and top windows.
- `const float* voiceTemplate (bool highVoice)` – 104 values (1/12-octave grid from 40 Hz) generated by `VelvoxTests --check-template <out.h> <wav>...`.
- [ ] Test (fail first): `CheckTargets::make (Genre::party, false, false)` equals the objective windows; template is finite and smooth (neighbour step < 3 dB) for both classes.
- [ ] Generate the table from VocalSet train singers (excerpts of f1–f6, m1–m7), commit table + generator.

### Task 4: Rule engine and score

**Files:** Create `Source/Check/CheckRules.{h,cpp}`; modify `VocalAnalyzer::analyse (Genre = Genre::party)` to call it; Test `Tests/VocalCheckFixTests.cpp`, adapt `Tests/VocalCheckTests.cpp` and `Tests/NewModuleTests.cpp` (title lookups → id).

**Interfaces:** `void check::evaluate (dsp::AnalysisResult& r, const CheckTargets& t)` fills `r.findings` (only confidence ≥ `check::showThreshold` = 0.5),
`r.hiddenFindings` (count below threshold), `r.score` (0..100, −1 without enough material).
Finding ids: `clipping, headroom, quiet, rumble, plosives, mud.<hz>, boxy.<hz>, harsh.<hz>, resonance.<hz>, presence, dull, bright, thin, sibilance, dynamics, crest, overcompressed, lra, mono, noise, space` where `<hz>` is the ISO 1/3-octave centre (e.g. `mud.315`, `resonance.3150`).
Fix actions: rumble/plosives → EqBandRequest{lowcut, lowCut, f, 0, 0.7, 12|24}; mud → {mud, bell, f, −min(6, 0.75·dev), Q}; boxy/harsh/resonance → {autoRole, bell, f, …, Q 1.4 / 4–8};
dull/bright → {air, highShelf, 9 kHz}; thin → {warmth, lowShelf, 180 Hz}; presence → Smooth on (amount); sibilance → De-Esser on (freq = 0.78·S centre, threshold from levels, range amount);
dynamics/lra → Ride on (target = voice level, amount); crest → Comp on (threshold from voice level, 3:1); overcompressed → comp ratio/smash blend atMost (only if active), else no action;
quiet/headroom → inputGain amount; mono → width atMost; noise → rideSilence atLeast (only with Ride); space → reverb/delay mix atMost (insert before Check only); clipping → no action.
Score = 100 − Σ (high 22 / medium 12 / low 5) × confidence over shown, non-ignored findings, clamped 0..100.
- [ ] Tests (fail first), on synthetic vocals: +6 dB bell at 300 Hz → `mud.*` with freqHz within ±1/3 oct of 300; 3.5 kHz Q 8 +10 dB → `resonance.*` (category harshness) within ±1/3 oct; rumble −26 dB → `rumble`; 7 kHz +8 dB → `sibilance`; plosive bursts → `plosives`; clean synthetic vocal → none of these and score ≥ 80; problematic vocal score < clean score; confidence grows with the problem size.
- [ ] Commit.

### Task 5: Fix planner, resolvers, one-step apply

**Files:** Create `Source/Check/CheckFixPlanner.{h,cpp}`; Test `Tests/VocalCheckFixTests.cpp`.

**Interfaces:**
- `using ValueLookup = std::function<float (const juce::String& paramId)>` (real units).
- `class EqBandResolver { struct Band { float freqHz, gainDb, q; bool on; }; virtual std::optional<Band> find (const EqBandRequest&, int instance, const ValueLookup&) const = 0; virtual std::vector<ResolvedParam> write (const EqBandRequest& absolute, int instance, const ValueLookup&) const = 0; }`
  Contract: role `autoRole` matches an existing band of that role within ±1/3 octave, every other role matches its single band; `write` creates the band if `find` returns none.
- `class LegacyEqResolver : public EqBandResolver` – today's fixed EQ: lowcut→HPF, bells < 600 Hz → low band as bell, bells 600 Hz–8 kHz → mid bell, highShelf/bells above → high band, lowShelf → low band shelf.
- `struct FixContext { ValueLookup value; dsp::Routing routing; std::function<bool (dsp::ModuleId, int)> audible; const EqBandResolver* eq; }`
- `ResolvedFix planFix (const FixAction&, const juce::String& name, const FixContext&)` and `ResolvedFix planFixes (std::vector<std::pair<FixAction, juce::String>>, const FixContext&)` (merged: later changes to the same parameter keep the more extreme amount).
  Amount rule: module instance in the chain, before Check and audible → target = current + amount (the measurement includes it); otherwise target = the more extreme of current and neutral + amount. Values clamp to the parameter range. Missing modules are switched on and inserted with `Routing::insertDefault`.
- `void applyResolvedFix (juce::AudioProcessorValueTreeState&, const ResolvedFix&)` – one `beginNewTransaction`, gestures per parameter, routing property set with `apvts.undoManager`, then `copyState()` to flush into the same transaction.
- [ ] Tests (fail first): legacy mapping (mud 300 → lowType bell, lowFreq 300, lowGain −3); EQ after Check: planning twice on the result keeps −3 (no doubling), user cut −5 stays −5; EQ before Check: current −2 + amount −3 → −5; missing EQ → eqOn + routing insert; one undo step on an APVTS with an UndoManager restores every parameter and the routing; merge of two requests.
- [ ] Commit.

### Task 6: Controller, persistence, processor API

**Files:** Create `Source/Check/VocalCheckController.{h,cpp}`; modify `Source/PluginProcessor.{h,cpp}` (member, accessor, analyse with genre), `Source/StateKeys.h` (`checkIgnored`, `checkGenre`); Test `Tests/VocalCheckFixTests.cpp`.

**Interfaces (message thread):** `VocalCheckController& VelvoxAudioProcessor::getVocalCheck()`;
`std::vector<const dsp::Finding*> visibleFindings() const; std::vector<const dsp::Finding*> ignoredFindings() const; int score() const;`
`bool canApply (const juce::String& id) const; bool apply (const juce::String& id); int applyAll();`
`void ignore (const juce::String& id); void restore (const juce::String& id); void restoreAll(); bool isIgnored (const juce::String&) const;`
`void setGenre (check::Genre); check::Genre getGenre() const; void setEqResolver (std::unique_ptr<check::EqBandResolver>);`
`ResolvedFix preview (const juce::String& id) const`.
Ignore list = comma-separated ids in `apvts.state[checkIgnored]` (saved/restored with the state, no undo). After apply: continuous mode → analyzer cleared (new generation); a finding id applied in the current generation cannot be applied again (stale measurement).
- [ ] Tests (fail first): ignore survives get/setStateInformation into a new processor; ignored findings excluded from visible list and score; apply on a harness inserts the EQ and sets the band; second apply in the same generation returns false and changes nothing; applyAll = one transaction (APVTS with UndoManager); restore brings the finding back.
- [ ] Commit.

### Task 7: Calibration on real vocals + precision/recall

**Files:** `Tests/CheckEval.cpp` (`--check-eval <outDir> <wav>...`), `Tests/VocalCheckRealDataTests.cpp` (skips when the data is missing), thresholds in `CheckTargets`/`CheckRules`.
Injections per file: mud (+6 dB bell 300 Hz Q 1.4), boxy (+6 dB 700 Hz Q 1.4), sibilance (+8 dB bell 7 kHz Q 1), rumble (25–70 Hz noise at voice RMS −26 dB),
harsh resonance (+10 dB 3.5 kHz Q 8), plosives (8 LF bursts). Plus the auto-mixed render (AutoMixEngine) and 08_B.
Metrics: recall = injected problem found with correct id (and frequency within ±1/3 octave where it has one); precision = correct / all findings of that category over clean + injected versions; processed: findings before vs after the auto-mix.
- [ ] Run, tune onsets until recall ≥ 0.8 with precision ≥ 0.8 on val+MYLK (rulings for every threshold moved), write the numbers into the ledger.
- [ ] Data-backed suite test on 4 val files + MYLK (asserts the injected categories are found, auto-mix reduces the finding count).
- [ ] Full suite green, commit.
