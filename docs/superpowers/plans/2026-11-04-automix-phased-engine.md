# Auto Mix 0.9 – Phased Engine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the one-shot auto-mix planner with a phased engine (Clean → Control → Shape → Enhance → Space → Output) in which every one of the 19 stages detects a measured problem, configures itself from its severity and the style, proves a measurable benefit on a render, and otherwise stays off – plus the four missing DSP pieces (Expander/Gate, Dynamic EQ, Multiband compressor, compressor character FET/Opto) and their panels.

**Architecture:** New DSP modules follow the Breath/Denoise pattern of 0.7 (settings struct in `ChainSettings`, module class with `prepare / reset / setSettings / process`, parameters in `ParameterLayout` + `SettingsReader`, appended `ModuleId`). The engine lives in a new folder `Source/AutoMix/Phased/`: a `Stage` interface, a `StageContext` that carries every analysis (A0..A5) and every earlier outcome, a `PhasedEngine` that renders the chain up to each stage with and without it, and one source file per phase. `AutoMixEngine::run` delegates to `PhasedEngine::run`; the old planner, verification loop and candidate search are deleted at the end.

**Tech Stack:** C++20, JUCE 8.0.9, own JUCE `UnitTest` runner (`VocalypseTests`), pluginval 1.0.4, Python 3.13 + numpy for bench tools.

**Spec:** `docs/superpowers/specs/2026-11-04-automix-phased-engine-design.md` (read it first; this plan argues from it).

## Global Constraints

- Never change plugin codes `Lnda` / `Vlvx` (VST3 uid `e632afc1`) or any existing parameter ID. New parameters and `ModuleId`s are **appended** only: `gate = 13`, `dyneq = 14`, `mbcomp = 15` (`numModules = 16`), keys `"gate"`, `"dyneq"`, `"mbcomp"`. One instance each.
- Defaults keep old projects identical: new modules off and not in the chain; `compCharacter` default 0 = Clean (today's compressor, bit-identical).
- Display names DE/EN via `txt::both`: „Expander / Gate“ / "Expander / Gate", „Dynamischer EQ“ / "Dynamic EQ", „Multiband-Kompressor“ / "Multiband Compressor", characters „Clean“, „FET (1176)“, „Opto (LA-2A)“ in both languages.
- Real-time safe: no allocation or locks in `process`; latency reported (`VocalChain::getLatencyFor`), compare/bypass paths stay time-aligned.
- Auto-mix chain order (spec table): inputGain · breath · gate · denoise · ride · eq#1 · dyneq · deess#1 · comp#1(Clean) · smooth · comp#2(FET) · deess#2 · sat · eq#2 · comp#3(Opto) · mbcomp · doubler · smash · delay(send) · reverb(send). Only switched-on modules stay (`pruneInaudible`).
- Every threshold/limit is a **named constant** (style table field or `constexpr` at the stage) – no loose numbers in stage bodies.
- Spec fixed values: input working window −18 … −12 dBFS voice RMS, move to the nearest edge, peaks ≤ −3 dBFS after it; beat hint when vocal − beat < −3 LU; breath edges and gate edges lose ≤ 0.5 dB (±60 ms around onsets/offsets); gate benefit ≥ 6 dB lower gaps; gate active only when gaps < 35 dB under words and no own room; de-esser lisp guard: S/vowel ≤ 4 dB under the style target, vowel highs ≤ 0.5 dB loss; peak comp: spikes 2–4 dB, words ≤ 1 dB (fail > 1.5 dB); smooth: highs loss ≤ 1.5 dB, max 45 %; FET target 3–7 dB (≤ 10 dB exceptional), Opto 1–3 dB, total comp ≤ 10 dB, de-ess range sum ≤ 10 dB; multiband band mean within 1 dB; corrective EQ ≤ 4 static + ≤ 4 dynamic bands, no boosts, static when ≥ 70 % of frames; mono loss < 1 dB; output = input loudness ± 1 LU, peaks < −1 dBFS.
- UI texts DE + EN; fire accent only for "the auto mix did this".
- Run single tests with `VocalypseTests --test <substring of the UnitTest name>`; gate per task: the task's tests green; at the end of every phase task group the full `scripts\build.ps1` run (never two `VocalypseTests` at once), pluginval 10 SUCCESS before claiming done; commit per task; ask Jakob before pushing.
- Panels only after Jakob picked a mockup (Task 6). Mockups: inline SVG in the brainstorm visual companion, 3–5 variants per topic.

## Review Focus

1. **Continuous singing without pauses** (sustained VocalSet takes): no gaps to measure → gate and breath stay off, ride does not pump. Test in Task 10.
2. **Already finished, heavily compressed vocal** (pro stem as input): almost every stage reports „kein Bedarf“, output within ±1 LU of input, no extra compression above 2 dB total. Test in Task 14.
3. **Little material** (< 8 s voiced): conservative – no gate, no multiband, no dynamic EQ, searches limited to 2 renders per stage, report says so. Test in Task 9.
4. **No beat on the sidechain / beat playing < 3 s:** no beat hint, no masking terms, Output Control skips beat checks without error. Test in Task 14.
5. **Cancel during a phase:** `run` returns `ok = false` with „Abgebrochen.“, nothing of the partial plan is applied. Test in Task 9.

---

### Task 1: Model and parameters for the new modules and the compressor character

**Files:** Modify `Source/DSP/Routing.h/.cpp` (enum, keys, names, `legacyFull`/default order unchanged for old modules, new ones placed: gate after breath, dyneq after eq, mbcomp after comp), `Source/DSP/Instances.h` (1 each), `Source/DSP/VocalChain.h/.cpp` (settings fields, stub modules pass-through, latency sum), `Source/DSP/CompressorModule.h` (`int character = 0;` in `CompSettings`), `Source/Parameters/ParameterIDs.h`, `ParameterLayout.cpp`, `SettingsReader.*`, `CMakeLists.txt`. Create stub headers `Source/DSP/ExpanderModule.h/.cpp`, `DynamicEqModule.h/.cpp`, `MultibandCompModule.h/.cpp`. Test: `Tests/RoutingTests.cpp`, `Tests/NewModuleTests.cpp`, `Tests/StateRestoreTests.cpp`.

**Interfaces – Produces:**
```cpp
namespace vocalypse::dsp {
struct ExpanderSettings { bool enabled=false; float thresholdDb=-50, ratio=2, rangeDb=12, attackMs=2, holdMs=40, releaseMs=120, lookaheadMs=5, scHpfHz=80; };
struct DynEqBand { bool on=false; int type=0 /*0 bell,1 low shelf,2 high shelf*/; float freq=1000, q=4, thresholdDb=6 /*band over broadband, relative*/, rangeDb=6, attackMs=5, releaseMs=80; };
struct DynEqSettings { bool enabled=false; std::array<DynEqBand,4> bands{}; };
struct MbBand { bool on=false; float thresholdDb=-24, ratio=2, attackMs=10, releaseMs=120, makeupDb=0; };
struct MbCompSettings { bool enabled=false; float xoverLowHz=300, xoverHighHz=4000; std::array<MbBand,3> bands{}; };
}
// ChainSettings: ExpanderSettings gate; DynEqSettings dyneq; MbCompSettings mbcomp;
// CompSettings::character: 0 Clean, 1 FET, 2 Opto (instances 0..2: compCharacter, compCharacter_2, compCharacter_3)
```
Parameter IDs (ParameterIDs.h, ranges): `gateOn`, `gateThreshold` (−90…0 dB), `gateRatio` (1.2…20, 20 = gate), `gateRange` (0…40 dB), `gateAttack` (0.1…50 ms), `gateHold` (0…500 ms), `gateRelease` (5…1000 ms), `gateLookahead` (0…10 ms), `gateScHpf` (20…500 Hz); `dyneqOn`, per band b=1..4 `dyneq<b>On`, `dyneq<b>Type`, `dyneq<b>Freq` (40…16000), `dyneq<b>Q` (0.3…20), `dyneq<b>Threshold` (−12…24 dB), `dyneq<b>Range` (0…18 dB), `dyneq<b>Attack` (0.5…100 ms), `dyneq<b>Release` (10…1000 ms); `mbcompOn`, `mbcompXoverLow` (60…1000), `mbcompXoverHigh` (1000…12000), per band b=1..3 `mbcomp<b>On`, `Threshold` (−60…0), `Ratio` (1…10), `Attack` (0.1…200), `Release` (10…2000), `Makeup` (−6…12); `compCharacter` choice {Clean, FET, Opto} via `instanceParamId`. Latency: gate = `round(lookaheadMax 10 ms * fs)` constant while in the chain; dyneq and mbcomp 0.

- [ ] Failing tests: keys `gate`/`dyneq`/`mbcomp` round-trip through `Routing::toString/fromString`; a `"v2,…"` string from 0.8 loads with the new modules absent; every new ID exists with the ranges above; a 0.8 state (fixture in `StateRestoreTests`) restores to identical audio (null test −120 dB) with `compCharacter` = 0; latency with gate in chain = `ExpanderModule::latencyFor (fs)`.
- [ ] Implement with pass-through stubs, run `VocalypseTests --test routing`, `--test "new module"`, `--test "state restore"` (`--test <substring of the UnitTest name>`, one run at a time) – PASS.
- [ ] Commit `0.9 model: gate, dynamic EQ, multiband, compressor character (stubs)`.

### Task 2: ExpanderModule (Expander / Gate)

**Files:** `Source/DSP/ExpanderModule.h/.cpp`. Test: create `Tests/ExpanderTests.cpp` (add to CMake).

**Interfaces – Produces:** `class ExpanderModule { void prepare (double fs, int maxBlock); void reset(); void setSettings (const ExpanderSettings&) noexcept; void process (float* const* io, int numChannels, int n) noexcept; static int latencyFor (double fs) noexcept; /* 10 ms */ PeakMeter reductionDb; int openCloseCount() const noexcept; };`

Algorithm (decided): detector = stereo-linked peak of the side-chain high-passed mono sum, look-ahead by delaying the audio `latencyFor` samples and reading the detector `lookaheadMs` early; gain computer below threshold: `gr = min(range, (thr - level) * (ratio - 1))` dB; hysteresis 3 dB (opens at thr, closes at thr − 3); hold timer; attack/release one-pole in dB domain. `openCloseCount` counts state changes for the flutter check.

- [ ] Failing tests: (a) vowel bursts with −55 dBFS noise in the gaps, thr −45, ratio 3, range 18: gap level −≥ 12 dB, vowel RMS ±0.2 dB; (b) word onsets: energy of the first 20 ms of each vowel changes ≤ 0.5 dB (look-ahead works); word ends: last 60 ms ≤ 0.5 dB; (c) signal hovering at threshold ±1 dB for 2 s: `openCloseCount` ≤ 4; (d) block sizes 1/64/2048 identical within 1e-5; off = delayed dry within 1e-6.
- [ ] Implement, run `--only ExpanderTests` – PASS. Commit `Expander/Gate module`.

### Task 3: DynamicEqModule

**Files:** `Source/DSP/DynamicEqModule.h/.cpp`. Test: create `Tests/DynamicEqTests.cpp`.

**Interfaces – Produces:** `class DynamicEqModule { void prepare (double fs, int maxBlock); void reset(); void setSettings (const DynEqSettings&) noexcept; void process (float* const* io, int numChannels, int n) noexcept; float bandReductionDb (int band) const noexcept; };`

Algorithm (decided): per band a detector band-pass (RBJ, freq/q) and a broadband RMS (same time constants); excess = bandDb − broadDb − thresholdDb; gain = −clamp(excess, 0, range) smoothed with attack/release; processing filter = the band's bell/shelf (`eqdesign::design`, matched) recomputed per 32-sample sub-block only when the gain moved > 0.05 dB. Gain 0 → filter is exactly unity (bypass the section).

- [ ] Failing tests: (a) tone 3 kHz bursts 10 dB above a vocal-like spectrum, band at 3 kHz Q 4 thr 3 range 8: bursts reduced 6–8 dB at 3 kHz, 1 kHz and 8 kHz ±0.3 dB; (b) between bursts the band reduces ≤ 0.2 dB; (c) loud and quiet phrase (±12 dB) get the same reduction ±1 dB (relative detector); (d) all bands off = bit-identical; block sizes identical within 1e-5.
- [ ] Implement, run – PASS. Commit `Dynamic EQ module`.

### Task 4: MultibandCompModule

**Files:** `Source/DSP/MultibandCompModule.h/.cpp` (reuse `CompressorModule`'s gain computer: extract `static float CompressorModule::computeGainDb (float levelDb, const CompSettings&)` if not already free). Test: create `Tests/MultibandTests.cpp`.

**Interfaces – Produces:** `class MultibandCompModule { void prepare (double fs, int maxBlock); void reset(); void setSettings (const MbCompSettings&) noexcept; void process (float* const* io, int numChannels, int n) noexcept; float bandReductionDb (int band) const noexcept; };`

Algorithm (decided): Linkwitz-Riley 4th order crossovers (two LR4 splits; the low band gets an all-pass at the high crossover so all three sum flat); per band peak/RMS detector (RMS), gain computer, makeup.

- [ ] Failing tests: (a) all bands on with 0 reduction (threshold 0): magnitude response flat ±0.1 dB 20 Hz–20 kHz; (b) only band 1 on, low-mid tone bursts +10 dB: band 1 reduces, a 6 kHz tone at the same time ±0.2 dB; (c) module off = bit-identical; block sizes identical.
- [ ] Implement, run – PASS. Commit `Multiband compressor module`.

### Task 5: Compressor character FET / Opto

**Files:** `Source/DSP/CompressorModule.h/.cpp`. Test: `Tests/DynamicsQualityTests.cpp`.

Behaviour (decided): Clean = unchanged code path. FET = peak detector in feedback topology, attack mapped 0.02–0.8 ms over the attack knob's 0.1–30 ms range (×0.027), ratio-dependent knee (4:1 → 6 dB, 20:1 → 2 dB), static odd-harmonic shaper `y = x + 0.015·x³` scaled by GR (0 at 0 dB GR, full at 10 dB). Opto = RMS detector, soft knee 10 dB, attack ≥ 10 ms, two-stage release (fast 60 ms for the first 50 % of recovery, slow `releaseMs × 8` for the rest, slow share growing with how long GR > 3 dB lasted, capped 5 s), even-harmonic shaper `y = x + 0.01·x²` (DC-blocked) scaled by GR.

- [ ] Failing tests: (a) Clean renders identical to the 0.8 golden (null −120 dB); (b) FET on a 1 kHz burst with 0.5 ms rise: peak overshoot ≤ 1 dB vs. Clean ≥ 3 dB at the same attack knob; (c) Opto: after a 2 s loud passage the recovery to −1 dB takes ≥ 3× longer than after a 100 ms burst; (d) THD at 10 dB GR: FET 0.2–1 % odd-dominant, Opto 0.2–1 % even-dominant, both < 0.05 % at 0 dB GR.
- [ ] Implement, run – PASS. Commit `Compressor: FET and Opto character`.

### Task 6: Mockups with Jakob (no code)

Visual companion (read `…/skills/brainstorming/visual-companion.md`), inline SVG, 3–5 variants each, in this order: (1) Expander/Gate panel, (2) Dynamic EQ panel, (3) Multiband panel, (4) character switch in the compressor panel, (5) auto-mix report by phases (stage on/off, reason, measured value, „kein Bedarf“ / „kein Nutzen“), (6) beat control field + hint in the Auto Mix view. Existing look: `Source/UI/Theme.cpp`, `docs/design/UI_REDESIGN_DECISIONS.md`.

- [ ] Show each topic, record Jakob's pick and remarks in `docs/design/UI_09_DECISIONS.md`. Commit `Design picks for 0.9 panels`.

### Task 7: Panels, browser, help for the new modules and the character switch

**Files:** Create `Source/UI/GatePanel.h/.cpp`, `DynEqPanel.h/.cpp`, `MultibandPanel.h/.cpp`; modify `DynPanels.cpp` (character segment), `ModuleBrowser.cpp`, `ModuleViews.cpp`, `DetailPanels.cpp`, `ChainView.cpp`, `HelpContent.cpp`, `Text/ParamText.cpp`, `LiveData.h`, `Resources/Icons/{gate,dyneq,mbcomp}.svg`, `CMakeLists.txt`. Test: `Tests/UiTests.cpp`, new `Tests/Panel09Tests.cpp`.

Layout = Jakob's picks from `docs/design/UI_09_DECISIONS.md`.

- [ ] Failing tests: browser lists 15 chain modules with the DE/EN names from Global Constraints; each panel's controls are bound to the Task 1 IDs (attachment test as in `DynPanelTests`); live meters read `reductionDb` / `bandReductionDb`; toggling during playback: no step > −40 dBFS (`StabilityTests` click test).
- [ ] Implement, run – PASS; snapshot each panel (`--snapshot … pro <moduleId>`) and look at it; show Jakob. Commit `Panels: Expander, Dynamic EQ, Multiband, compressor character`.

### Task 8: Analysis additions (VocalProfiler + stage metrics)

**Files:** Modify `Source/AutoMix/VocalProfile.h`, `VocalProfiler.cpp`. Create `Source/AutoMix/Phased/StageMetrics.h/.cpp`. Test: `Tests/AutoMixTests.cpp`, new `Tests/StageMetricsTests.cpp`.

**Interfaces – Produces:**
```cpp
// VocalProfile additions
float breathPeakVsPhraseDb = -100;     // loudest 10 % of breaths against the phrase level
float gapVsWordDb = -100; bool gapsValid = false;  // median pause level right after words vs. word level, >= 6 pauses
float peakExcessShare = 0;             // share of voiced 5 ms peaks > 6 dB above the 60 ms syllable RMS
struct BandActivity { float loHz, hiHz, excessDb, occupancy; float centreHz; }; // vs. target curve; occupancy = share of voiced frames over threshold
std::vector<BandActivity> problemBands; // mud 200-500, boxy 300-800, nasal 800-1500, harsh 2000-5000, lowEnd < 0.8*f0Low
std::array<float,3> bandSwingDb {};    // 10..90 % spread of band-minus-broadband level, bands split at 300 / 4000 Hz
float harmonicDensity = 0;             // energy of harmonics 2-5 vs. fundamental on pitched frames (dB)
// StageMetrics.h
namespace vocalypse::automix::phased {
struct EdgeLevels { float onsetDb, offsetDb; };              // RMS ±60 ms around onsets / offsets of `reference`
EdgeLevels edgeLevels (const CapturedAudio& audio, const VocalProfile& reference);
float levelOnFrames (const CapturedAudio& audio, const std::vector<int>& frames); // dB, analysis frame grid
struct CompGr { float medianDb, peakDb; };                    // per compressor instance, voiced blocks
}
```
`RenderMetrics` gets `std::array<CompGr,3> compGr` and `float thdPct` (held notes) – fill in `AutoMixEngine::render`.

- [ ] Failing tests (synthetic vocal helpers + `SyntheticVocal.h`): each new field moves the right way on a degraded copy – noise in gaps → `gapVsWordDb` rises ≥ 10 dB; loud breaths → `breathPeakVsPhraseDb` rises; a static 3 kHz resonance → harsh band occupancy ≥ 0.7, an intermittent one (30 % of frames) → 0.2–0.4; low-mid level jumping ±6 dB per phrase → `bandSwingDb[0]` ≥ 8; `edgeLevels` identical for identical audio, −6 dB when the first 60 ms are attenuated 6 dB.
- [ ] Implement, run – PASS. Commit `Analysis: gaps, breaths, peaks, problem bands, band swing, harmonics`.

### Task 9: Stage framework, style table and PhasedEngine skeleton

**Files:** Create `Source/AutoMix/Phased/Stage.h`, `StageContext.h`, `PhasedEngine.h/.cpp`, `StyleTable.h/.cpp`. Modify `Source/AutoMix/AutoMixEngine.cpp` (`run` delegates when `phased::enabled` – a `constexpr bool` set true in Task 16), `AutoMixEngine.h` (`AutoMixResult` gets `std::vector<phased::StageOutcome> stages` and `std::vector<VocalProfile> analyses`), `CMakeLists.txt`. Test: create `Tests/PhasedEngineTests.cpp`.

**Interfaces – Produces:**
```cpp
namespace vocalypse::automix::phased {
enum class Phase { clean, control, shape, enhance, space, output };
enum class StageId { inputGain, breath, gate, ride, correctiveEq, deess1, peakComp, resonance, fetComp, deess2,
                     saturation, tonalEq, optoComp, multiband, width, parallel, effects, ducking, output };
enum class OffReason { none, noNeed, noBenefit, harmful, notAllowed };
struct Need { bool active = false; float severity = 0; juce::String reason; };   // severity 0..1
struct Verdict { bool beneficial = false, harmful = false; float benefit = 0; juce::String text; };
struct StageOutcome { StageId id; Phase phase; bool on = false; OffReason off = OffReason::none; float severity = 0;
                      juce::String title, reason, benefitText; int renders = 0; };
struct Measure { CapturedAudio audio; VocalProfile profile; RenderMetrics metrics; EdgeLevels edges; };
struct StageContext {
    const CapturedAudio& input; const std::map<juce::String,float>& current; const StyleTarget& style;
    MixPlan plan;                          // values + routing built so far
    std::vector<VocalProfile> analyses;    // A0 (input) .. after each finished phase
    std::vector<StageOutcome> outcomes;    // every stage so far
    const StageOutcome* outcome (StageId) const;
    bool conservative = false;
    std::function<Measure (const MixPlan&)> render;   // whole chain of `plan`, sends excluded before Phase::space
    std::function<bool()> shouldStop;
};
class Stage {
public:
    virtual ~Stage() = default;
    virtual StageId id() const = 0;
    virtual Phase phase() const = 0;
    virtual Need detect (const StageContext&) const = 0;
    virtual void configure (const Need&, StageContext&) const = 0;     // writes plan values + routing
    virtual Verdict verify (const Measure& without, const Measure& with, const StageContext&) const = 0;
    virtual bool weaken (StageContext&) const { return false; }        // one step softer; false = nothing left
    virtual void disable (StageContext&) const = 0;                    // module off, out of the routing
    virtual int maxRenders (const StageContext& c) const { return c.conservative ? 2 : 5; }
    virtual juce::String title (const StageContext&) const = 0;        // DE/EN via txt::both
};
std::vector<std::unique_ptr<Stage>> allStages();   // spec order; filled by Tasks 10-13
struct StyleTarget : MixTarget {   // StyleTable.h: targetFor() extended to every field below
    float peakSpikeGrLo = 2, peakSpikeGrHi = 4, peakWordGrMax = 1.5f;
    float fetGrLo = 3, fetGrHi = 7, fetGrMax = 10, optoGrLo = 1, optoGrHi = 3, compTotalMax = 10;
    float deessRangeTotalMax = 10, breathKeepDb = -30, gapNeedDb = 35, beatMinLu = -3;
    float smoothMax = 45, duckDb = 6, sideTargetDb = -20; bool satWanted = true;
};
StyleTarget styleFor (Genre, Vibe);
class PhasedEngine { public: static AutoMixResult run (const CapturedAudio&, const std::map<juce::String,float>& current,
    const std::function<bool()>& shouldStop, const AutoMixEngine::Progress&, const MixIntent&); };
}
```
Engine loop (decided): A0 = `VocalProfiler::analyse(input)`; for each phase: for each stage of the phase → `detect`; inactive → outcome `noNeed`; active → `without = render(plan)`, `configure`, `with = render(plan)`, `verify`; while `!beneficial || harmful` and renders < `maxRenders` → `weaken` + re-render; still failing → `disable`, outcome `noBenefit`/`harmful`. After the phase: render, append `VocalProfiler::analyse` of it to `analyses`. Cache `without` = last `with` of the previous kept stage (no double render). Progress text „Phase {n}: {stage}“. Style values: party · punchy = the spec's start values above; other cells scale them (rap/aggressive FET ×1.3 capped 10; ballad/natural ×0.6; floaty Opto ×1.2), all marked `estimated` except party.

- [ ] Failing tests with two fake stages (test-only subclasses): a stage whose detector is inactive is never rendered and reports `noNeed`; a stage whose verify fails 5 times ends `noBenefit` and its module is out of the routing; `analyses.size()` = finished phases + 1; little material (< 8 s voiced) → `conservative`, `maxRenders` = 2; `shouldStop` returning true mid-phase → `ok = false`, error „Abgebrochen.“ / "Canceled.", plan not returned.
- [ ] Implement, run – PASS. Commit `Phased engine: stage framework, style table`.

### Task 10: Phase 1 stages – input gain, breath, gate (+ room noise), ride

**Files:** Create `Source/AutoMix/Phased/StagesClean.cpp` (+ declarations in `Stages.h`). Test: create `Tests/PhaseCleanTests.cpp`.

Detectors/configure/verify exactly per spec section „Phase 1“ with the Global Constraints values. Specific decisions: input gain moves to the nearest window edge in 0.5 dB steps; beat hint stored in `AutoMixResult` as `float beatHintDb` (positive = turn the beat down by that much, 0 = none), computed as `style.beatMinLu − vocalOverBeatLu` when below; breath amount = `clamp(30 + 2·(breathPeakVsPhraseDb − breathKeepDb), 25, 80)` (speech/natural −10); gate threshold = midpoint of noise floor and `voiceRmsLowDb − 6`, range = `min(18, 35 − gapVsWordDb + 6)`; attack from `onsetSharpnessDb` (sharp 0.5 ms, soft 3 ms), hold = `clamp(0.5 / onsetRate s, 20, 150 ms)`; room noise only when `snrDb < 35` and not reverberant (as 0.8); ride target = upper half of the style phrase window, weaken = amount −15 %.

- [ ] Failing tests on synthetic takes: clean take → all four stages `noNeed` (gain window inside); take at −28 dBFS RMS → input gain +10 dB to −18; noise in gaps −50 dBFS → gate on, gaps ≥ 6 dB lower, edges ≤ 0.5 dB; take with reverb tail → gate `notAllowed`; loud breaths → breath on, natural ones (−35 dB) → off; **continuous sustained take without pauses → gate and breath `noNeed`** (Review Focus 1); phrase spread 14 dB → ride on, spread after ride inside the upper half.
- [ ] Implement, run – PASS. Commit `Phase 1: clean`.

### Task 11: Phase 2 stages – corrective EQ + dynamic EQ, de-esser 1, peak control, resonance

**Files:** Create `Source/AutoMix/Phased/StagesControl.cpp`. Reuse `autoeq::` notch and low-cut logic from `AutoEq.cpp` (cuts only, `planCurveMoves` filtered to gain < 0) and `AutoMixPlanner::configureDeEsser` (move to `Phased/DeEss.cpp` as `void configureDeEsser (MixPlan&, int instance, const VocalProfile&, float inputGainDb, float range)`). Test: create `Tests/PhaseControlTests.cpp`.

Decisions: problem bands from `profile.problemBands` with `excessDb` over the style spread; occupancy ≥ 0.7 → static bell cut in EQ 1 (Q from width, resonances use `Resonance::q`), else dyneq band (threshold = band's median excess, range = p90 excess, max 8 dB); at most 4 + 4, sorted by `excessDb × occupancy`. Peak comp: comp#1 Clean, ratio 6, attack 1 ms, release 50 ms, threshold searched so measured `compGr[0].peakDb` lands 2–4 dB and `compGr[0].medianDb` ≤ 1. Smooth: active when after EQ 1 + dyneq a resonance > 7 dB above local spectrum or the harsh band still exceeds the target, ignoring frequencies within 1/12 oct of an EQ 1 / dyneq band; low/high = problem frequency ÷/× 1.5 octaves.

- [ ] Failing tests: static 3.2 kHz resonance → EQ 1 notch, no dyneq band; intermittent 3.2 kHz (30 %) → dyneq band, no static cut; no problem → no bands, no boosts ever in EQ 1; strong S sounds → de-esser 1 on, S/vowel at target, not more than 4 dB under it; spiky consonants → peak comp gr in window; smooth stays `noNeed` on a resonance EQ 1 already cut.
- [ ] Implement, run – PASS. Commit `Phase 2: control`.

### Task 12: Phase 3 stages – FET, de-esser 2, saturation, tonal EQ, Opto, multiband

**Files:** Create `Source/AutoMix/Phased/StagesShape.cpp`. Reuse `AutoEq` curve fit for EQ 2 (instance 1, broad bands only: low shelf, presence bell Q ≤ 1.2, high shelf; limits from `limitsOf`; with a beat the masking lift `autoeq::unmaskOf` goes in, as in 0.8). Test: create `Tests/PhaseShapeTests.cpp`.

Decisions: FET threshold search (bisection, ≤ 5 renders) until measured `compGr[1].medianDb` in `[fetGrLo, fetGrHi]`; above `fetGrHi` allowed up to `fetGrMax` only when phrase spread after Phase 2 > style `phraseHi + 4`; ratio 4 (aggressive 8); de-esser 2 instance 1 when S/vowel after FET > style max or rose ≥ 2 dB vs. A2, same frequency as de-esser 1 when that is on, `range ≤ deessRangeTotalMax − range1`; saturation character: harsh → Warm, thin (body < curve −3) → Tube, aggressive rap → Hard, dark with air missing > 4 dB → multiband sat high band only (Exciter), else the style's character; drive from need, verify `harmonicDensity` up ≥ 1 dB and `thdPct` under style limit; Opto comp#3 when phrase spread > style `phraseHi`, target `[optoGrLo, optoGrHi]`, `compGr[1].median + compGr[2].median ≤ compTotalMax`; multiband per band when `bandSwingDb[b]` > 6 dB, threshold at the band's p75 level, ratio 2–3, verify swing −≥ 2 dB and band mean ±1 dB.

- [ ] Failing tests: a dynamic take → FET measured median GR within 3–7 dB; already dense take → FET `noNeed`; FET lifting S sounds → de-esser 2 on with range sum ≤ 10; harsh voice → Warm, thin voice → Tube; dull dark voice → EQ 2 high shelf boost, bright voice → none; low-mid swinging ±6 dB → only band 1 of mbcomp on; total comp ≤ 10 dB on every test take.
- [ ] Implement, run – PASS. Commit `Phase 3: shape`.

### Task 13: Phases 4 and 5 – width, parallel, effects, ducking

**Files:** Create `Source/AutoMix/Phased/StagesEnhanceSpace.cpp`. Reuse the reverb/delay choices of `AutoMixPlanner.cpp:604-738` (type, tempo times, beat density) moved into this file. Test: create `Tests/PhaseSpaceTests.cpp`.

Decisions: doubler from style width × `pitchedShare` (as 0.8), off when `correlation < 0.8`; verify side level within ±3 dB of `style.sideTargetDb`, mono loss < 1 dB, else weaken mix ×0.7. Smash when quiet words after Phase 3 (`voiceRmsHighDb − voiceRmsLowDb`) above style window or crest > `crestHi`, not when `heavilyCompressed`; verify spread −≥ 1 dB and gap level +≤ 2 dB. Reverb/delay amount searched (≤ 4 renders) until measured tails (0.15–0.4 / 0.4–0.8 / 0.8–1.6 s) within ±3 dB of the style's tail target (party punchy −25/−29/−41). Ducking depth `style.duckDb`, threshold = word level after Phase 4 − 10 dB; verify tail during words ≥ `duckDb − 2` lower than in pauses.

- [ ] Failing tests: rap take → doubler off or ≤ 8 %; wide stereo input → doubler `noNeed`; reverberant take → effects `notAllowed`; dry sung take → tails within ±3 dB of target; ducking measured.
- [ ] Implement, run – PASS. Commit `Phases 4-5: enhance and space`.

### Task 14: Output control and over-processing check

**Files:** Create `Source/AutoMix/Phased/OutputControl.cpp`. Test: create `Tests/OutputControlTests.cpp`.

Decisions: output gain so `lufs = targetLufs(input)` ± 0.5; peaks > −1 dBFS → lower gain; over-processing: if total comp > `compTotalMax`, sat THD > limit, de-ess sum > max, or any EQ band |gain| > its limit → weaken the largest contributor once and re-render the phases from its phase on (once); report mono loss, width, tail level, distance to the target curve, dynamics vs. window; with beat: vocal-over-beat after the mix and `beatHintDb`.

- [ ] Failing tests: output within ±1 LU of input on all test takes; **finished pro vocal as input → ≥ 12 of 19 stages `noNeed`, total added compression ≤ 2 dB** (Review Focus 2); **no beat → no hint, no beat lines, no error** (Review Focus 4); forced over-compression (style limit 4 dB) → FET weakened and report says why.
- [ ] Implement, run – PASS. Commit `Output control`.

### Task 15: Report and Auto Mix view by phases, beat field

**Files:** Modify `Source/AutoMix/ReportText.h`, `AutoMixController.*` (expose `stages`, `beatHintDb`, live vocal/beat levels), `Source/UI/AutoMixView.*`, `AnalysisOverlay.*` per Jakob's picks (Task 6). Test: `Tests/UxTests.cpp`, `Tests/AutoMixTests.cpp`.

- [ ] Failing tests: report groups lines under „Clean · Control · Shape · Enhance · Space · Output“, each of the 19 stages appears once with on/off and its reason; off stages show „kein Bedarf“ / „kein messbarer Nutzen“ (EN "not needed" / "no measurable benefit"); beat field visible only with a beat present, hint text „Beat um ca. {X} dB leiser stellen“ / "Turn the beat down by about {X} dB" when `beatHintDb > 0`.
- [ ] Implement, run – PASS, snapshot, show Jakob. Commit `Auto Mix view: report by phases, beat control`.

### Task 16: Switch over, remove the old planner, goldens, bench

**Files:** `AutoMixEngine.cpp` (`phased::enabled = true`; delete the verification loop, `candidatesFor`, `tuneDynamics`), `AutoMixPlanner.*` (keep only helpers still used: `roleBand`, `setRoleBand`, `sliderPositions`, `eqBand`, `autoEqState`; delete `plan`), `AutoMixObjective.*` (delete if unused), `Check/CheckFixPlanner.cpp` (keep working), tests that pinned 0.8 decisions (re-record goldens with a short reason per changed file), `Tests/VocalypseTests.cpp --bench` report (per stage: on/off, severity, reason, benefit, renders, seconds), `tools/bench/summarise_reports.py` (stage columns).

- [ ] Full `scripts\build.ps1`: 0 failed. pluginval 10 SUCCESS. `VocalypseRefHost --editor --plugin build/Vocalypse_artefacts/Release/VST3/VOCALYPSE.vst3` exits. Commit `Auto mix 0.9: phased engine replaces the planner`.

### Task 17: Calibration on data and listening kit

**Files:** `StyleTable.cpp`, stage constants; `docs/BENCHMARK.md`; `CHANGELOG.md`; `docs/HANDOFF.md` top section.

- [ ] Bench on `velvox-bench\2026-11-03\sonovox\dry` (genre/vibe as numbers), VocalSet val split, Jakob's takes (`train\dry`); check with `summarise_reports.py`: no stage on for > 80 % of takes without a reason in the report; distinct settings per module ≥ 0.8 × takes for on-modules; pro stems: ≥ 12 stages `noNeed`. Tune named constants, re-run.
- [ ] Listening kit `%USERPROFILE%\dev\velvox-bench\2026-11-04\HOERTEST_09\`: per take Original, 0.8, 0.9, pro (where available), all −18 LUFS, plus the reports.
- [ ] Full gate again, install (`scripts/install-vst3.ps1`, FL closed), Drive bundle, handoff. Commit `0.9 calibrated, listening kit`. Ask Jakob before pushing.
