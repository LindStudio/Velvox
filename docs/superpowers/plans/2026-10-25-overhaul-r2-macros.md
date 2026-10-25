# Simple mode sets the modules (0.6.1 item 2) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Simple slider (PRESENCE, SPACE, WIDTH, PUNCH, WARMTH; Density in the auto-mix view) writes the module parameters
directly, one undo step per gesture, so a switched-off module stays silent and the Advanced panels show what you hear; old
projects / presets with macro values load sound-identical (baked once).

**Architecture:** The six macro parameters stay registered but no longer reach the audio path: they hold the slider position.
A message-thread `MacroMapper` (owned by the processor) turns every change of a macro position into module-parameter writes
with the pure function `dsp::applyMacroMove (base, macro, from, to)` (the 0.6.0 mapping made relative and signed), anchored at
the module state where the move began so that returning to the start restores it exactly. UI gestures write nested host
gestures (UndoHistory records them in the slider's step); host automation writes plain values (no undo). States / presets made
before this change (state version < 4, snapshot format < 3) are baked once on load with the unchanged `dsp::applyMacros`.

**Tech Stack:** C++17, JUCE 8.0.9, VelvoxTests (juce::UnitTest).

**Spec:** `docs/superpowers/specs/2026-10-25-headliner-0.6.1-feedback-design.md` item 2; `docs/superpowers/specs/2026-10-25-headliner-pro-overhaul-design.md` (Q2 sends for SPACE, Q5 role bands, §7/§8 old projects).

## Global Constraints

- Never rename/remove parameter IDs; the six `macro*` IDs stay registered and automatable.
- Old projects must sound identical: bake "exactly what `applyMacros` produced" once on load (null test).
- Real-time safe audio thread: no allocation, locks or logging in processBlock; mapping happens on the message thread.
- "one undo step per gesture, works with the existing UndoHistory incl. its 400 ms coalescing and the insert/send routing logic of SPACE".
- "a switched-off module stays silent", "the Advanced panels show exactly what you hear", "never a second offset".
- Role bands: macros find or create their role band (`air`, `warmth`), never touch user bands (spec Q5).
- No permanent explanatory text in the UI; hover texts may change.
- Commit messages end with `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`.

## Review Focus

1. Undo of a Simple drag must restore modules, slider and routing exactly; redo likewise (incl. SPACE insert -> send).
2. Wheel notches on a Simple slider within 400 ms coalesce into one step although each notch moves several parameters.
3. Host automation of an old macro curve keeps working (maps relative to the last position) and never records undo steps.
4. An old project with macros AND the auto-mix "Vorher" snapshot: Vorher/Nachher both sound as in 0.6.0.
5. Moving a slider up then down to its start returns the module state bit-exact (no drift, no module left switched on).

---

### Task 1: Old-state fixtures and 0.6.0 baseline renders

**Files:** Create `Tests/MacroBakeTests.cpp` (CMake test list).
**Interfaces:** Produces `oldState (configure, macros)` (an XML state with `stateVersion` 3 and macro values) and
`renderLoaded (state, input)`; env `VELVOX_MACRO_BASELINE_WRITE=<dir>` writes raw renders, `VELVOX_MACRO_BASELINE=<dir>` compares.

- [ ] Write the env-gated baseline test, build on the unchanged code, write the 0.6.0 renders to the scratchpad. Commit.

### Task 2: `dsp::applyMacroMove`

**Files:** Modify `Source/DSP/Macros.h/.cpp`; Test `Tests/MacroTests.cpp`.
**Interfaces:** `enum class MacroId { presence, warmth, aggression, width, space, density }`;
`ChainSettings applyMacroMove (const ChainSettings& base, MacroId, float from, float to) noexcept` (0..1 positions).

- [ ] Failing tests: from 0 equals `applyMacros` with that macro alone (all audio fields but `*LawMix`); to == from is the
      identity; a move down reduces (mix 0 -> module off); clamps; user bands untouched; a down move never creates a band.
- [ ] Implement (signed delta d = to - from; attack scales by (1-0.6 to)/(1-0.6 from); d > 0 switches modules on). Commit.

### Task 3: Writing settings back, keeping the dry, baking

**Files:** Create `Source/Parameters/MacroBake.h/.cpp`; Test `Tests/MacroBakeTests.cpp`.
**Interfaces:**
- `void macrobake::writeMacroFields (const eqbands::Access&, const dsp::ChainSettings& from, const dsp::ChainSettings& to)`
- `void macrobake::keepDry (const dsp::ChainSettings& before, const dsp::ChainSettings& after, const dsp::Routing&, dsp::MixLaws&, float& doublerDryDb)`
- `bool macrobake::bake (const eqbands::Access&, const dsp::Routing&, dsp::MixLaws* laws)` (true when macros were non-zero).

- [ ] Failing tests on value maps: bake of a map == applyMacros field by field; laws: additive Mix pushed over 50 % with a
      unity dry -> legacy law; doubler with dry < 1 -> legacy + Dry dB. Implement. Commit.

### Task 4: Live mapping in the processor

**Files:** Create `Source/Parameters/MacroMapper.h/.cpp`; Modify `PluginProcessor.*`, `Presets/UndoHistory.*`,
`Parameters/SettingsReader.h`, `Parameters/RoutingMigration.cpp`, `UI/ChainView.*`, `UI/DetailPanels.cpp`; Tests `MacroTests.cpp`, `UndoTests.cpp`.
**Interfaces:** `UndoHistory::isApplyingHistory()`, multi-parameter coalescing; processor `readEffectiveSettings() == readSettings()`.

- [ ] Failing tests: the user bug (reverb off in Advanced + SPACE up -> no reverb tail); slider writes reverbMix etc.; drag =
      one step, undo/redo exact incl. routing; wheel coalescing; automation (audio thread) maps without undo steps.
- [ ] Implement; remove `applyMacros` from processBlock, OfflineParameters, routing migration; remove engaged markers. Commit.

### Task 5: Migration of old states, presets, auto-mix snapshots

**Files:** `StateKeys.h` (version 4), `PluginProcessor.cpp` (setStateInformation), `Presets/ParameterSnapshot.*` (format 3);
Tests `MacroBakeTests.cpp`; `CHANGELOG.md`.

- [ ] Failing null tests (old state renders == in-build reference of the old semantics; old preset; auto-mix Vorher).
- [ ] Implement; one-off compare with the 0.6.0 renders; CHANGELOG "Alte Projekte". Commit.

### Task 6: Consumers and docs

**Files:** `AutoMix/AutoMixPlanner.cpp` (note), `Assistant/*` (no macro actions), `UI/Widgets.cpp` hover texts, docs.
- [ ] Adjust, full suite green, ledger. Commit.
