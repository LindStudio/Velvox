# Remove Pitch, Stutter, Pulse, Choir – Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The four creative modules Pitch, Stutter (`chop`), Pulse and Choir are gone from the plugin: no DSP, no parameters, no chain slot, no panel, no preset, no help text, no test.

**Architecture:** `dsp::ModuleId` loses `pitch`, `chop`, `pulse`, `choir`; every table indexed by it shrinks with it (`numModules` 15 → 11). Their parameter IDs disappear from `ParameterIDs.h` / `ParameterLayout.cpp`, so the host sees fewer parameters. The latency path that only existed for Pitch becomes "no latency" (plan B brings a generic one back). Six factory presets are rebuilt without the effects.

**Tech Stack:** C++20, JUCE 8.0.9, CMake, own test runner (`VelvoxTests`, juce::UnitTest), pluginval.

**Spec:** Jakob's decisions, 2026-10-27 (chat; recorded in `docs/design/VOCALYPSE_NAME_UND_MASKOTTCHEN.md` → section "Module 0.7"):
- „Stutter, Choir, Pitch und Pulse raus“ – „ganz gelöscht“.
- „Alte Projekte sollen nicht weiterlaufen, habe nur Testprojekte.“ → no migration, no hidden legacy DSP.
- „Presets neu anpassen ohne diese Module.“

## Global Constraints

- Parameter IDs and plugin codes of the **remaining** modules never change (CLAUDE_HANDOFF.md).
- Old projects that used the removed modules may load with those parts missing; loading must never crash or assert.
- Before "fertig": `scripts\build.ps1` (all tests, log line `==== N checks passed, 0 failed ====`) and pluginval strictness 10 SUCCESS.
- Never edit files through PowerShell `Get-Content`/`Set-Content` (umlauts); Edit tool or Python UTF-8.
- UI texts German + English via `i18n::tr` (docs/I18N.md).
- Commit per task; ask before pushing.

## Review Focus

1. **A project / preset string that still names a removed module** (`routing` property "…|pitch|chop|…", preset XML with `pitchOn`) → loads; unknown slots are dropped, the rest of the chain keeps its order. Test in Task 1.
2. **Vocal Check "mono" finding** suggested Choir as fix → must now point at the Doubler; an "Anwenden" on it must not touch a missing parameter. Test in Task 3.
3. **Host latency** was only non-zero with Pitch on → reported latency is 0 for every chain; switching modules never changes it. Test in Task 2.
4. **Module browser / chain row** with 11 modules: no gap, no tile, no help page for the removed ones; keyboard / solo selection codes stay consistent. Test in Task 4.
5. **Auto mix** no longer mentions "Kreativ-Effekte (Pitch, Stutter, Pulse, Choir)" in its notes. Test in Task 3.

---

### Task 1: Core model without the four modules

**Files:**
- Modify: `Source/DSP/Routing.h` (enum, `numModules`), `Source/DSP/Routing.cpp` (key / name / shortName / defaultOrder tables), `Source/DSP/Instances.h`, `Source/DSP/Solo.h`
- Modify: `Source/Parameters/ParameterIDs.h`, `Source/Parameters/ParameterLayout.cpp`, `Source/Parameters/SettingsReader.h/.cpp`
- Delete: `Source/DSP/PitchModule.*`, `PitchTracker.*`, `FormantCorrector.*`, `SpliceShifter.*`, `ChopModule.*`, `PulseModule.*`, `ChoirModule.*` (keep `VoiceSplice.h`: the Doubler uses it)
- Modify: `Source/DSP/VocalChain.h/.cpp` (members, process switch, `getLatencyFor` → no pitch), `Source/PluginProcessor.cpp:118-127` (engage calls), `CMakeLists.txt` (sources)
- Test: `Tests/RoutingTests.cpp`, `Tests/VelvoxTests.cpp`

**Interfaces:**
- Produces: `enum class ModuleId : std::uint8_t { eq = 0, comp, deess, sat, doubler, delay, reverb, smash, smooth, check, ride };` `inline constexpr int numModules = 11;`
- Produces: `int VocalChain::getLatencySamples() const noexcept` returns 0 (signature `getLatencyFor (const Routing&)` without the pitch flag).

- [ ] **Step 1: Write the failing tests** in `RoutingTests.cpp`:
  `beginTest ("0.7: Pitch, Stutter, Pulse and Choir are gone")` – `expectEquals (dsp::numModules, 11)`; `expect (! dsp::moduleFromKey ("pitch").has_value())` for "pitch", "chop", "pulse", "choir";
  `Routing::fromString ("eq|pitch|comp|chop|pulse|choir|reverb")` yields exactly `eq, comp, reverb` in that order.
  In `VelvoxTests.cpp`: `expect (proc.getState().getParameter ("pitchOn") == nullptr)` for `pitchOn`, `chopOn`, `pulseOn`, `choirOn`.
- [ ] **Step 2: Build** – fails to compile / tests fail (enum still has 15).
- [ ] **Step 3: Remove** the enum values, table entries, parameters, settings fields, DSP members and files listed above; fix every compile error in the files listed (only removals and renumbering, no new behaviour).
- [ ] **Step 4: Run** `VelvoxTests --test "Routing"` and the processor suite – PASS.
- [ ] **Step 5: Commit** `Remove Pitch, Stutter, Pulse, Choir from the model and DSP`.

### Task 2: Latency is zero without Pitch

**Files:** Modify `Source/DSP/VocalChain.h/.cpp` (remove `activePitchOn`, the pitch fade switch), `Source/PluginProcessor.cpp` (`updateHostLatency`). Test: `Tests/VelvoxTests.cpp`.

- [ ] **Step 1: Failing test** `beginTest ("0.7: no module adds latency")`: for the legacy default routing and a routing with every module, `expectEquals (proc.getLatencySamples(), 0)` after `prepareToPlay (48000, 512)`.
- [ ] **Step 2–4:** run (fails while pitch latency code remains), remove, run – PASS.
- [ ] **Step 5: Commit** `Latency: nothing left that adds any`.

### Task 3: Auto mix, Vocal Check, macros, presets

**Files:**
- Modify: `Source/AutoMix/AutoMixPlanner.cpp:219-227` (drop the creative block and its note), `Source/Check/CheckFixPlanner.cpp:22-28`, `Source/Check/CheckRules.cpp:510` (mono → `ModuleId::doubler`), `Source/Parameters/MacroBake.cpp` / `Source/DSP/Macros.cpp` only where they name removed fields
- Modify: `Source/Presets/FactoryPresets.cpp` (rebuild 6 presets), `Source/Presets/ModulePresets.cpp` (drop the four modules' presets)
- Test: `Tests/AutoMixTests.cpp`, `Tests/VocalCheckFixTests.cpp`, `Tests/PresetTests.cpp`

Preset rebuild (same names, effect replaced by modules that remain):

| Preset | Was | Now |
|---|---|---|
| Hard Dance Lead | Pulse | Vocal Smash 35 % + Delay 1/8 15 % |
| Festival Choir Hook | Choir | Doubler 45 % + Reverb 30 % + Width macro 40 |
| Pumping Party Vocal | Pulse | Delay 1/4 18 % ducked + Reverb 20 % ducked |
| Vocal Chop | Stutter | **renamed "Tight Rap Vocal"**: Comp 4:1, Smash 25 %, Delay 1/8 dotted 8 % |
| Octave Down Layer | Pitch | **renamed "Deep Double"**: Doubler 50 %, Warmth macro 50, Sat 20 % |
| Chipmunk Hook | Pitch | **renamed "Bright Hook"**: Presence macro 60, Doubler 30 %, Reverb 18 % |

- [ ] **Step 1: Failing tests:** `PresetTests` – no factory or module preset sets any parameter whose ID starts with `pitch`, `chop`, `pulse`, `choir`; the renamed presets exist under their new names; the existing "Factory presets keep the loudness…" test stays green (re-level the 6 presets with `VELVOX_LOUDNESS_VOCALS` as in CLAUDE_HANDOFF.md). `AutoMixTests` – no plan note contains "Kreativ" / "Creative". `VocalCheckFixTests` – the "mono" finding's module is `ModuleId::doubler` and `apply` writes only existing parameters.
- [ ] **Step 2–4:** run (fail), change, run – PASS.
- [ ] **Step 5: Commit** `Presets, auto mix and Vocal Check without the removed modules`.

### Task 4: UI, help, icons

**Files:**
- Delete: `Source/UI/PitchPanel.*`; remove Choir parts of `Source/UI/VoicesPanels.*`, Pulse / Stutter parts of `Source/UI/TimePanels.*`
- Modify: `Source/UI/ModuleBrowser.cpp`, `ModuleViews.cpp`, `DetailPanels.cpp`, `ChainView.cpp`, `HelpContent.cpp`, `Source/Text/ParamText.cpp`; `CMakeLists.txt` (drop `Resources/Icons/pitch.svg stutter.svg pulse.svg choir.svg` and delete the files)
- Delete tests: `Tests/PitchTests.cpp`, `PitchDiagnostics.cpp`, `PitchPanelTests.cpp`, `ChopTests.cpp`; remove Choir cases from `VoicesPanelTests.cpp`, Pulse / Stutter cases from `TimePanelTests.cpp`
- Test: `Tests/UiTests.cpp`

- [ ] **Step 1: Failing test** `beginTest ("0.7: the module browser offers exactly the 11 modules")`: browser entries == `{ eq, comp, deess, sat, doubler, delay, reverb, smash, smooth, check, ride }` (as keys); `help::shortDescription` exists for each and the help index has no page titled Pitch / Stutter / Pulse / Choir.
- [ ] **Step 2–4:** run (fail), remove, run – PASS.
- [ ] **Step 5:** Full gate: `scripts\build.ps1` → `0 failed`; pluginval strictness 10 → SUCCESS; screenshot `VelvoxTests --snapshot out.png Init 1000 pro -1` and look at the chain row and browser.
- [ ] **Step 6: Commit** `UI and help without Pitch, Stutter, Pulse, Choir`; update `CHANGELOG.md` (0.7, "Entfernt") in the same commit.
