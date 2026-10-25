# Auto-mix EQ: resonance notches and broad tonal moves – Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The automatic mix sets individual EQ boosts/cuts – narrow notches (Q 10-16) on sustained resonances and broad
tonal moves towards the objective's target curve – instead of (in practice) only a low cut, and scores better on the
VocalSet val split and the dry/real test vocals than 0.6.0.

**Architecture:** A new pure unit `Source/AutoMix/AutoEq.*` holds the limits, a tonal model (band levels of the
profiler's long-term spectrum after a set of EQ bands, analog magnitude) and the move planner (resonance notches +
broad moves solved against the objective windows). `AutoMixPlanner::plan` calls it with the bands the auto-mix does
not own (user and Vocal Check bands, plus its own low cut) and writes the moves into auto-mix role bands of EQ 1
(new roles `autoTone`, `autoNotch1..3` appended to `dsp::EqRole`). The profiler marks resonances as sustained (present
in >= 3 of 4 time segments, narrow). The engine's verification loop nudges the broad auto bands when the rendered
tonal levels are still outside the windows.

**Tech Stack:** C++20, JUCE 8.0.9, VelvoxTests (juce::UnitTest), bench CLI (`VelvoxTests --bench`).

**Spec:** `docs/superpowers/specs/2026-10-25-headliner-0.6.1-feedback-design.md` item 7 (+ 0.6 spec
`2026-10-25-headliner-pro-overhaul-design.md`, §8 Q5 role bands).

## Global Constraints

- Limits: max ~6 bands used by the auto-mix; cuts up to about −9 dB narrow / −4 dB broad; boosts up to about +4 dB broad; never boost narrow; narrow = Q ~10-16.
- Uses the role-band mechanism so user bands (role none) are never written; Vocal Check bands (roles presence, mud) are not written either.
- Never rename/remove parameter IDs; only add. Role choice list is only appended to (old indices keep their meaning).
- Pure planner/profiler (any thread); nothing in processBlock paths changes.
- Must improve the mean objective score (bench "chosen") on VocalSet val (f7, f8, m8, m9 excerpts) and the dry/real vocals vs. 0.6.0; never use the held-out singers (f9, m10, m11).
- Keep the change in the planner's EQ decisions; do not refactor Macros (the macros track rewrites them).
- No Source/UI edits except the role tag of the new roles in `EqPanel.cpp` (one switch).

## Review Focus

- A re-run of the auto-mix on a state that already holds auto bands from an earlier run: stale notches must be switched off, not stacked or kept (test in Task 4).
- A Vocal Check fix (presence/mud bell) or a user band already cutting at a resonance: no second notch on top (test in Task 4).
- EQ 1 with all 16 slots used by the user: the plan writes no band, keeps the user bands, stays valid (test in Task 4).
- Harmonics of held notes / a singer's formant must not be read as resonances (sustained + narrow test in Task 2).
- A band-limited (lo-fi) take: a 9 kHz shelf does nothing – the model must choose where a boost acts (test in Task 3).

---

### Task 1: Bench columns + baseline

**Files:** Modify `Tests/TestMain.cpp` (BenchRow, CSV: append `lowmid_in,lowmid_out,air_in,air_out`).

- [ ] Add the columns (appended, old columns unchanged), build, run the 0.6.0 baseline on val + real into the scratchpad.
- [ ] Commit `Bench: low-mid and air columns`.

### Task 2: Sustained, narrow resonances in the profiler

**Files:** Modify `Source/AutoMix/VocalProfile.h` (Resonance), `Source/AutoMix/VocalProfiler.cpp` (resonance block);
Test: create `Tests/AutoEqTests.cpp` (UnitTest "Velvox auto EQ", category "Velvox"), add to CMakeLists as its own `target_sources` line.

**Interfaces:**
- Produces: `struct Resonance { float hz, excessDb; bool strong; float persistence = 0; bool sustained = false; float q = 12; }`
  persistence = share of 4 time segments (voiced frames in order) whose own long-term spectrum shows the peak
  (excess > 3.5 dB within ±1 band); sustained = persistence >= 0.75 and the peak is narrow (>= 3 dB above both
  bands ±2); q in [10, 16] from the width at the peak (16 single band, 12 one neighbour within 4 dB, 10 both).
  The list is sorted sustained first, then by excess, max 3 (as before).

- [ ] Test "Analysis: a fixed resonance is sustained and narrow; a peak in one part of the take is not":
  makeVocal(resonanceHz 1800, resonanceDb 16, 14 s) → a resonance within 1/6 oct of 1800 Hz with sustained, q in [10,16];
  concatenation of 4 s with a +22 dB resonance at 1800 Hz and 12 s without → no sustained resonance within 1/6 oct.
- [ ] Run, watch it fail (no `sustained` member).
- [ ] Implement; run; commit.

### Task 3: Auto EQ roles, tonal model and move planner

**Files:** Modify `Source/DSP/EqDesign.h` (append roles `autoTone, autoNotch1, autoNotch2, autoNotch3`; numEqRoles 12),
`Source/Parameters/ParameterLayout.cpp` (role choices "Auto Tone", "Auto Notch 1..3"), `Source/UI/EqPanel.cpp` (roleTag:
AUTO for the new roles); create `Source/AutoMix/AutoEq.h/.cpp` (+ CMake VELVOX_SOURCES); test in `Tests/AutoEqTests.cpp`.

**Interfaces:**
- `namespace velvox::automix::autoeq`
  - limits: `maxBands = 6` (all auto-mix bands of EQ 1 incl. the low cut), `maxNotches = 3`, `maxNarrowCutDb = 9`,
    `maxBroadCutDb = 4`, `maxBroadBoostDb = 4`, `minNarrowQ = 10`, `maxNarrowQ = 16`
  - `bool ownedRole (dsp::EqRole)` – lowCut, warmth, autoMix, air, highCut, autoTone, autoNotch1..3
  - `struct Levels { float body, lowMid, presence, top, air; }` (dB relative to the 300 Hz-2 kHz core, like VocalProfile)
  - `Levels levelsOf (const std::vector<float>& spectrumDb, const std::vector<dsp::EqBand>& bands)` (bands that are on; analog magnitude)
  - `struct Move { dsp::EqRole role; dsp::EqBand band; juce::String title, reason; }`
  - `struct Context { const VocalProfile* profile; std::vector<dsp::EqBand> fixed; bool conservative; bool noisy; bool sibilant; bool harsh; bool masking; int budget; }`
  - `std::vector<Move> planMoves (const Context&)`
- Rules (windows = objective's tonal windows, act only > 0.5 dB outside, aim 1-1.5 dB inside):
  body > −1 (or the 0.6.0 rule body > −3 → −4.5) low-shelf cut 200 Hz ≥ −4 (role warmth); body < −9, not a high voice (f0Low ≤ 250), pitch valid, not noisy → low-shelf boost 170 Hz ≤ +4 (+2 conservative);
  low-mids > 0 (or "mud") → bell Q 1.2 at lowMidPeakHz ≥ −4 (autoMix); presence > −4 → bell Q 1.0 at the strongest 2.5-5 kHz point ≥ −4 (autoTone);
  top > −7 → high shelf 5 kHz ≥ −4 (air); top < −16 → high shelf (9 kHz when there is air content, else 4.5 kHz) ≤ +4 (+2 noisy/sibilant), the rest as autoTone bell 3 kHz Q 0.8 ≤ +3 when not harsh/sibilant;
  air < −30 (0.6.0 rule) → +1.5 dB shelf 11 kHz unless harsh/sibilant/noisy; masking hint → autoTone +1 dB 3 kHz.
  Notches: sustained resonances 250 Hz-8 kHz, excess (after the fixed bands at that frequency) ≥ 8 dB → gain −clamp(0.6·excess, 3, 9) (×0.6 conservative), Q from the profile, roles autoNotch1..3.
  Budget: drop the smallest |gain| moves beyond `budget`.
- [ ] Tests: levelsOf (flat spectrum + 3 dB shelf at 2 kHz raises top by ~3 dB; a 9 kHz shelf on a spectrum without content above 8 kHz moves top < 0.3 dB);
  planMoves limits (narrow: Q in [10,16], gain in [−9, −3], never > 0; broad: gain in [−4, +4]; count ≤ budget); resonance → notch near its frequency;
  muddy / dark / bright synthetic takes → the matching broad move; a fixed band already cutting −8 dB at the resonance → no notch.
- [ ] Run, fail; implement; run; commit.

### Task 4: Planner integration

**Files:** Modify `Source/AutoMix/AutoMixPlanner.cpp` (EQ section, SMOOTH rule); tests in `Tests/AutoEqTests.cpp`, golden updates in `Tests/AutoMixTests.cpp` where justified.

**Interfaces:** consumes `autoeq::planMoves`, `ownedRole`, `setRoleBand/roleBand`.
- Fixed bands = enabled bands of EQ 1 with a role the auto-mix does not own (user, presence, mud), when EQ 1 is on, plus the planned low cut.
- Owned roles without a move: warmth/autoMix/air flattened as before (gain 0); autoTone/autoNotch* switched off.
- SMOOTH: on for harshness, or a high resonance the EQ did not notch.
- [ ] Tests: resonance take → notch band (role autoNotch1, Q 10-16, ≤ −3 dB) and decision text; re-run on the plan's own values without the resonance → notch off;
  Vocal Check presence band at the resonance in `current` → no auto notch and the presence band unchanged; user band IDs never in plan.values;
  16 user bands → no auto band written, plan valid; total auto bands ≤ 6 on every engine test case.
- [ ] Run, fail; implement; run the automix suites; commit.

### Task 5: Engine – tonal verification and variants

**Files:** Modify `Source/AutoMix/AutoMixEngine.cpp`.
- Verification round: rendered body/lowMid/presence/top outside the window by > 0.5 dB → move the owning broad band (warmth/autoMix/autoTone/air) towards it, within the limits, creating it when free (budget respected).
- "Höhen sanfter" only when the base plan does not already cut the highs; EQ 2 variant also moves an autoTone boost.
- [ ] Tests (AutoEqTests): engine run on a dark synthetic take ends with top closer to the window than 0.6.0's rule-only plan; all bands within limits after the run.
- [ ] Commit.

### Task 6: Bench, tuning, listening files, docs

- [ ] Bench val + real (≤ 3 tuning rounds), compare with the 0.6.0 baseline, render before/after listening files to
  `C:\Users\jakob\dev\velvox-bench\2026-10-25\autoeq\`.
- [ ] `docs/BENCHMARK.md` (numbers), `docs/AUTOMIX_OBJECTIVE.md` (variants table), CHANGELOG note, ARCHITECTURE line.
- [ ] Full suite green; ledger; commit.
