# HEADLINER UI Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rebuild the Headliner editor in the "HEADLINER VOCAL" look from Claude Design (header with tabs, module chain as columns, 1180 × 460 module panel, IN/OUT meters, Vocal Control macro bar) without changing any DSP.

**Architecture:** Evolve the existing editor, do not fork it: the design tokens replace `Theme.h` values, the knob/switch/segment drawing replaces `VelvoxLookAndFeel`, `ChainView` keeps its chain-edit model (insert / duplicate / remove / move / undo) but draws columns with two live knobs, `DetailPanel` keeps one class per module but gets the new header and layouts. The fixed design canvas (`ui::baseWidth × baseHeight`, uniformly scaled) stays, at the new size 1384 × 1040.

**Tech Stack:** C++20, JUCE 8.0.9, CMake (VS 2022), test runner `VelvoxTests` (JUCE UnitTest), snapshots via `VelvoxTests --snapshot`.

**Spec:** `docs/design/HEADLINER_DESIGN_HANDOFF.md` (reference code in `docs/design/reference/`), parameter gaps and decisions in `docs/DESIGN_MAPPING.md`.

## Global Constraints

- No DSP change: nothing under `Source/DSP`, `Source/AutoMix`, `Source/Parameters` changes; parameter IDs and plugin codes stay as they are.
- Only existing parameters are bound. A design control without a parameter is left out (no dead or placeholder controls). Gaps stay listed in `docs/DESIGN_MAPPING.md`.
- All 15 modules stay visible and editable in the new style (EQ, Compressor, De-Esser, Saturation, Doubler, Pitch, Vocal Check get colours and icons in the same family).
- Macros: PRESENCE → `macroPresence`, SPACE → `macroSpace`, WIDTH → `macroWidth`, PUNCH → `macroAggression`, WARMTH → `macroWarmth`, VOCAL LEVEL → `outputGain`. `macroDensity` only in its module context (not in the bar).
- Colours exactly as in the spec (`#05060A`, `#07080D`/`#161925` r12, `#040509`/`#13161E` r8, `#0D0F15` r7, text `#F1F5F9`/`#E8EBF2`/`#5B6478`; module colours of the 8 design modules verbatim).
- Fonts: Montserrat (existing `Resources/Fonts`), add `RobotoMono-Regular.ttf` (OFL, `THIRD_PARTY_LICENSES.md`). Values Roboto Mono 11.5, knob names Montserrat SemiBold 9 `#6B7489`, section labels SemiBold 10 `#5B6478`, module name SemiBold 15 tracking 2.5, uppercase.
- Knob: 57 ticks over 270° (r 40.5–47.5, w 1.3), shadow r37 +7, body r35 `#34373F`→`#121418`, cap r26 `#17191D`→`#2A2D34`, dot r3.4 on r27; double-click = default. Sizes: module 50–56, column 50, Vocal Control 54, SMASH 150.
- Canvas 1384 × 1040: header 56, chain 330, detail 460, Vocal Control = rest. Resizable with fixed aspect ratio.
- Every task ends with the full suite green (`scripts\build.ps1`) and pluginval strictness 10 SUCCESS before commit; UI tests are adapted, never deleted without a replacement that checks the same behaviour.
- German/English mix as today; module names uppercase as in the spec.

## Review Focus

- Small window / HiDPI: at the smallest allowed scale every knob value and name stays readable and nothing overlaps → snapshot at 0.6 scale in Task 9.
- Many instances: a chain with 12+ columns (repeated EQ/Comp) must scroll, not squeeze columns below 104 px → test in Task 4.
- Choice parameters with many steps (`delayDivision` 14, `pulseRate` 10, `reverbType` 8) shown as segments must stay in sync with host automation → round-trip test in Task 3.
- Bypassed module: column and panel at 30 % opacity but still clickable to turn back on → test in Task 4.
- Opening an old project (state from 0.3/0.4) shows the same values in the new UI (no UI-side defaults overwrite parameters) → test in Task 5.

---

### Task 1: Design tokens, fonts and module identity

**Files:**
- Modify: `Source/UI/Theme.h`, `Source/UI/Theme.cpp`, `CMakeLists.txt` (binary data), `THIRD_PARTY_LICENSES.md`
- Create: `Resources/Fonts/RobotoMono-Regular.ttf` (from `G:\Meine Ablage\MUSIC\Plugin\headliner-juce\Resources\fonts\Anton,Montserrat,Roboto_Mono\Roboto_Mono\static`), `Resources/Icons/*.svg` (8 from the package + 7 new: `eq`, `comp`, `deess`, `sat`, `doubler`, `pitch`, `check`, 48 × 48, white stroke 1.6, round caps, same line style)
- Create: `Source/UI/NeonIcon.h` (from `docs/design/reference/NeonIcon.h`, namespace `velvox::ui`)
- Test: `Tests/UiTests.cpp`

**Interfaces:**
- Produces: `colours::*` with the spec values (existing names `colours::text`, `textDim`, `panel`, … keep their names, new values; new names `windowBg`, `panelInset`, `control`, `border`, `borderSoft`, `tickOff`, `textStrong`, `textMuted`, `textFaint`); `juce::Colour moduleColour (dsp::ModuleId)`; `juce::String moduleIconSvg (dsp::ModuleId)` (SVG text from binary data); `juce::Font monoFont (float height)`; `juce::Font semiBold (float height)`.
- Module colours: Ride `#00D1FF`, Smooth `#22B8FF`, Smash `#3B82F6`, Choir `#6366FF`, Pulse `#8B5CF6`, Chop (STUTTER) `#A855F7`, Delay `#D946EF`, Reverb `#F472B6`; new: EQ `#2DD4BF`, Check `#38BDF8`, De-Esser `#0EA5E9`, Compressor `#2563EB`, Doubler `#818CF8`, Pitch `#C084FC`, Saturation `#EC4899`.

- [ ] **Step 1: Failing test** `"Design: every module has its colour and a neon icon"`: for all `dsp::numModules` ids `moduleIconSvg(id)` is non-empty and parses with `juce::Drawable::createFromImageData`; `moduleColour (ModuleId::ride) == Colour (0xff00d1ff)` and `moduleColour (ModuleId::reverb) == Colour (0xfff472b6)`; all 15 colours distinct.
- [ ] **Step 2:** Run `VelvoxTests --test "Velvox UI"` → FAIL (`moduleIconSvg` undefined).
- [ ] **Step 3:** Implement: tokens in `Theme.h`, binary data entries, `moduleIconSvg`, `monoFont`/`semiBold` via `EmbeddedFonts`, `NeonIcon` (recolours `#FFFFFF`, glow 6 px, `setDimmed`).
- [ ] **Step 4:** Run the suite → PASS; snapshot `--snapshot out.png x 0 pro 0 none` renders (colours change, layout still old).
- [ ] **Step 5:** Commit `UI: HEADLINER VOCAL design tokens, Roboto Mono, neon module icons`.

### Task 2: Knob, switch and segment drawing

**Files:**
- Modify: `Source/UI/VelvoxLookAndFeel.cpp/.h`, `Source/UI/Widgets.cpp/.h` (`Knob`, `Toggle`)
- Create: `Source/UI/Segments.h/.cpp`
- Test: `Tests/UiTests.cpp`

**Interfaces:**
- Consumes: Task 1 tokens and fonts.
- Produces: `drawRotarySlider` per spec (reference `docs/design/reference/HeadlinerLookAndFeel.cpp`, 270° = start `1.25π`, end `2.75π`); `Knob` draws value (mono 11.5, `#E8EBF2`) and name (SemiBold 9, `#6B7489`) under the dial, `Knob::setDialSize (int px)` replaces `setProminence`/`setHero` sizing (old calls map to 50/56/150); `Toggle` draws the 26 × 14 switch with label right, accent via `ToggleButton::tickColourId`; `class Segments : public juce::Component` bound to a choice or bool parameter: `Segments (APVTS&, const char* paramId, juce::StringArray labels, juce::Colour accent)`, `int getSelectedIndex() const`.

- [ ] **Step 1: Failing tests** `"Design: segments follow the parameter both ways"` (set `delayDivision` via `setValueNotifyingHost` → selected index equals choice index after the async update; clicking segment 3 sets the parameter to index 3); `"Design: knob double-click returns the default"` (`compRatio` knob: set 8, double-click → 3).
- [ ] **Step 2:** Run → FAIL (`Segments` undefined).
- [ ] **Step 3:** Implement drawing and `Segments` (radio TextButtons on a `#0D0F15` r7 background, active = accent 16 % fill, text accent→white 55 %).
- [ ] **Step 4:** Suite green; snapshot shows the new knobs everywhere.
- [ ] **Step 5:** Commit `UI: design knob, switch and segment controls`.

### Task 3: Canvas size and header

**Files:**
- Modify: `Source/UI/Theme.h` (`baseWidth = 1384`, `baseHeight = 1040`), `Source/PluginEditor.cpp/.h`, `Source/UI/TopBar.cpp/.h`
- Test: `Tests/UxTests.cpp`, `Tests/UiTests.cpp`

**Interfaces:**
- Produces: `TopBar` layout (height 56): left preset box 290 × 36 with ‹ › and the existing save / menu icons; centre logo (existing `drawBrand`, wordmark "HEADLINER" + "VOCAL" `#A78BFA` + "BY LIND AUDIO"); right tabs `CHAIN | AUTO MIX | VOCAL CHECK` (active = 2 px underline in accent), A/B, A→B copy, bypass and help as small icon buttons. `enum class View { chain, autoMix, vocalCheck }`, `std::function<void (View)> onViewChange`, `void setView (View)`. "Analysieren" moves into the AUTO MIX view (Task 7). No Dry/Wet knob (no parameter).
- Existing test hooks keep working: `getHelpButton()`, `getAnalyseButton()` (now returns the AUTO MIX view's analyse button via the editor), `onModeChange` replaced by `onViewChange` (tests updated: `onModeChange (true)` → `setView (View::chain)`, `false` → `View::autoMix`).

- [ ] **Step 1: Failing test** `"Design: header tabs switch the three views"`: `setView (autoMix)` → `getAutoMixView().isVisible()`; `setView (vocalCheck)` → the Check panel is visible; `setView (chain)` → `getChainView().isVisible()`.
- [ ] **Step 2:** Run → FAIL.
- [ ] **Step 3:** Implement header and view switching; editor keeps the scaled canvas with `setResizable (true, true)` and a fixed-aspect constrainer, min scale 0.6.
- [ ] **Step 4:** Suite green; snapshot header.
- [ ] **Step 5:** Commit `UI: 1384x1040 canvas, header with CHAIN / AUTO MIX / VOCAL CHECK tabs`.

### Task 4: Chain as columns

**Files:**
- Modify: `Source/UI/ChainView.cpp/.h`
- Test: `Tests/UiTests.cpp`, `Tests/UxTests.cpp`

**Interfaces:**
- Consumes: `NeonIcon`, `Knob::setDialSize`, `moduleColour`.
- Produces: same public API as today (`insertModule`, `duplicateInstance`, `removeInstance`, `moveInstance`, `setSendMode`, undo/redo, `getTileBounds`, `getPowerBounds`, `getRemoveBounds`, `getDuplicateBounds`, `getAddCardBounds`, `onSelect`, `onAddModule`, `setSelected`). New: `static std::pair<const char*, const char*> columnParams (dsp::ModuleId)`, `getMoveLeftBounds/getMoveRightBounds (id, instance)`.
- Column (height 330, equal width, min 104 px, horizontal scroll beyond): power + name (44) · NeonIcon 64 (96) · two knobs (50 px) · footer ‹ duplicate remove › (34); divider `#13151E`; selected = gradient module colour 14 % → 2 %; bypassed = content 30 % alpha (power stays 100 % and clickable); delay/reverb columns keep the send/insert switch in the name row; round dashed "+" at the end opens the module browser. Input/output ports become slim end caps.
- Column knobs: Ride `rideTarget`/`rideResponse`; Smooth `smoothAmount`/`smoothSensitivity`; Smash `smashAmount`/`smashBlend`; Choir `choirMix`/`choirWidth`; Pulse `pulseDepth`/`pulseRate`; Chop `chopDepth`/`chopRate`; Delay `delayFeedback`/`delayMix`; Reverb `reverbDecay`/`reverbMix`; EQ `hpfFreq`/`highGain`; Compressor `compThreshold`/`compRatio`; De-Esser `deessThreshold`/`deessRange`; Saturation `satDrive`/`satMix`; Doubler `doublerWidth`/`doublerMix`; Pitch `pitchSemitones`/`pitchMix`; Check: none (shows the last Vocal Score instead). Instance parameters via `dsp::instanceParamId`.

- [ ] **Step 1: Failing tests** `"Design: a column carries its two knobs on the instance's parameters"` (insert EQ 2 → its column knobs are bound to `hpfFreq_2` / `highGain_2`); `"Design: footer arrows reorder"` (click right arrow of the first column → routing index +1, undo restores); `"Design: bypassed column dims but stays switchable"`; `"Design: 14 columns scroll instead of shrinking"` (`getTileBounds(...).getWidth() >= 104`).
- [ ] **Step 2:** Run → FAIL.
- [ ] **Step 3:** Implement; drag-to-reorder may stay as an extra but the arrows are the primary path.
- [ ] **Step 4:** Suite green (existing chain-edit tests adapted to the new bounds getters only).
- [ ] **Step 5:** Commit `UI: module chain as design columns with live knobs`.

### Task 5: Module panel frame and the 8 design panels

**Files:**
- Modify: `Source/UI/DetailPanels.cpp/.h`, `Source/UI/ModuleViews.cpp` (colours only)
- Test: `Tests/UiTests.cpp`

**Interfaces:**
- Consumes: `Segments`, `Knob`, `Toggle`, `NeonIcon`.
- Produces: `DetailPanel` frame 1180 × 460, padding 22/28: header = NeonIcon 30 + name (SemiBold 15, tracking 2.5) + slot info ("Slot 3 · Insert"), below it the four functions as grey 11 px text, switches / live values top right; visualizer box `#040509` r8. Helpers: `Segments& segments (const char* paramId, juce::StringArray labels)`.
- Panels (existing parameters only, ranges as the parameters define them):
  - LEVEL RIDE: knobs Target, Range→`rideMaxBoost` + `rideMaxCut` as two knobs "BOOST"/"CUT", Speed `rideResponse`, Amount, Phrase, Transients, Gate Floor `rideSilence`.
  - SMOOTH: segments Charakter → `smoothMode` (its 2 labels), switch Delta; knobs Amount, Sharpness `smoothSensitivity`, Focus Low/High, Speed `smoothAttack`, Release, Output.
  - VOCAL SMASH: big SMASH 150 px `smashAmount`, GR meter; knobs Attack, Release, Tone, Output, Mix `smashBlend`.
  - CHOIR: segments Stimmen `choirVoices`; knobs (2 columns, name left of value) Detune, Timing, Humanize, Width, Density, Mix.
  - PULSE: segments Rate `pulseRate`, Kurve `pulseInterp`; knobs Depth, Shape `pulseTension`, Attack, Release, Smooth, BPM, Mix.
  - STUTTER (CHOP): segments Rate `chopRate`; knobs Gate `chopDepth`, Smooth.
  - DELAY: segments Time `delayDivision` (6 most used as segments + "…" menu for the rest), switches Sync, Ping-Pong; knobs Time, Feedback, Offset `delaySpread`, Low Cut, High Cut, Ducking `delayDuckAmount`, Wobble `delayMod`, Drive, Diffusion, Mix.
  - REVERB: room list = `reverbType` (8 entries), knobs Size, Decay, Pre-Delay, Damping, Width, Low Cut, High Cut, Modulation `reverbMod`, Early, Mix.
- Existing visualizers (`ModuleViews`) stay; only colours and frame change.

- [ ] **Step 1: Failing test** `"Design: every panel binds only existing parameters"`: for each module open the panel, collect all `Knob::getParamId()` and `Segments`/`Toggle` ids, each resolves via `apvts.getParameter`; and the LEVEL RIDE panel contains `rideTarget`, `rideMaxBoost`, `rideMaxCut`, `rideSilence`.
- [ ] **Step 2: Failing test** `"Design: an old project shows the same values"`: load a state saved by 0.4 (fixture written by `getStateInformation` before the redesign in the test itself: set 20 parameters, save, rebuild editor, load) → all 20 unchanged after the panels are opened.
- [ ] **Step 3:** Run → FAIL on the new panel ids.
- [ ] **Step 4:** Implement the frame and the 8 panels.
- [ ] **Step 5:** Suite green; snapshots of all 8 panels reviewed against the spec images.
- [ ] **Step 6:** Commit `UI: design module panels for the 8 design modules`.

### Task 6: The 7 further modules in the same style

**Files:**
- Modify: `Source/UI/DetailPanels.cpp`
- Test: `Tests/UiTests.cpp` (the "only existing parameters" test from Task 5 covers them)

**Interfaces:**
- EQ (visual = EQ curve, knobs per band, segments for slopes/band types), COMPRESSOR (segments Detector, knobs Threshold, Ratio, Attack, Release, Knee, Makeup, SC Filter, Mix + GR meter), DE-ESSER (segments Mode, switch Listen, knobs Freq, Threshold, Range), SATURATION (segments Charakter `satCharacter` 4, knobs Drive, Output, Mix), DOUBLER (knobs Width, Delay, Detune, Mono Safe, Tone, Mix), PITCH (switch Keep Formants, knobs Semitones, Cents, Formant, Mix), OUTPUT (Input/Output gain, Safety Clip, Level Match segments, Stereo Width, Mono Below).

- [ ] **Step 1:** Extend the Task 5 test list with these 7 + output (fails until done).
- [ ] **Step 2:** Implement in the Task 5 frame.
- [ ] **Step 3:** Suite green; snapshots reviewed.
- [ ] **Step 4:** Commit `UI: remaining modules and output stage in the design style`.

### Task 7: Meters, Vocal Control bar

**Files:**
- Modify: `Source/UI/Widgets.cpp` (`LevelMeter`), `Source/PluginEditor.cpp`
- Create: `Source/UI/VocalControlBar.h/.cpp`
- Test: `Tests/UiTests.cpp`

**Interfaces:**
- `LevelMeter`: two 5 px bars per meter, gradient cyan `#00D1FF` → violet `#8B5CF6` → pink `#F472B6`, value below in mono; right of the detail area.
- `class VocalControlBar : public juce::Component` — 6 knobs (54 px) in the macro colours of the spec (`#00D1FF`, `#3B82F6`, `#6366FF`, `#8B5CF6`, `#D946EF`, `#F472B6`) bound per Global Constraints, plus a GR read-out (compressor GR from `LiveData`).

- [ ] **Step 1: Failing test** `"Design: Vocal Control maps PUNCH and VOCAL LEVEL"`: the bar's knob ids in order are `macroPresence, macroSpace, macroWidth, macroAggression, macroWarmth, outputGain`.
- [ ] **Step 2:** Run → FAIL.
- [ ] **Step 3:** Implement and place (Vocal Control below the detail area, height = rest).
- [ ] **Step 4:** Suite green.
- [ ] **Step 5:** Commit `UI: IN/OUT meters and Vocal Control macro bar`.

### Task 8: AUTO MIX and VOCAL CHECK views in the new style

**Files:**
- Modify: `Source/UI/AutoMixView.cpp/.h`, `Source/UI/AnalysisOverlay.cpp`, `Source/UI/DetailPanels.cpp` (`CheckPanel` full-size for the tab)
- Test: `Tests/AutoMixTests.cpp` (workflow tests), `Tests/UxTests.cpp`

**Interfaces:**
- AUTO MIX (tab, spans chain + detail area): analyse button ("Vocal analysieren"), progress, measured values that exist in `VocalProfile` (Lautheit, Dynamik, Sibilanz, Vocal-Typ = spoken/sung/sustained), the chosen steps (existing list, module chips in `moduleColour`), Vorher / Mix / Original comparison, "Vorherigen Zustand wiederherstellen". Not built (no backend): Zielstil, Tempo, Tonart, Trefferquote, Intensität — listed in `docs/DESIGN_MAPPING.md`.
- VOCAL CHECK (tab): existing findings (score ring, OK / Hinweis / Problem, FIX buttons) in the new style.

- [ ] **Step 1:** Adapt the existing workflow/UI tests to the tab (they must still find `getMixButton`, `getPreviousButton`, `getOriginalButton`, `getNumStepRows`).
- [ ] **Step 2:** Implement the restyle.
- [ ] **Step 3:** Suite green.
- [ ] **Step 4:** Commit `UI: AUTO MIX and VOCAL CHECK tabs in the design style`.

### Task 9: Help, tutorial, snapshots, verification

**Files:**
- Modify: `Source/UI/TutorialOverlay.cpp`, `Source/UI/HelpContent.cpp`, `Source/PluginEditor.cpp` (`tutorialTarget` keys), `docs/STYLEGUIDE.md`, `CHANGELOG.md`, `docs/HANDOFF.md`
- Test: `Tests/UxTests.cpp`

- [ ] **Step 1:** Tutorial targets and help texts point at the new places (tabs, column footer arrows, Vocal Control); test `"Design: every tutorial step has a visible target"`.
- [ ] **Step 2:** Snapshots at scale 1.0 and 0.6 for chain, each panel, AUTO MIX, VOCAL CHECK into `%USERPROFILE%\dev\velvox-bench\ui-<date>`; review for overlaps.
- [ ] **Step 3:** Full suite + pluginval strictness 10 (with GUI tests) → SUCCESS.
- [ ] **Step 4:** `docs/STYLEGUIDE.md` rewritten for the new system; CHANGELOG; handoff.
- [ ] **Step 5:** Commit and Drive backup; user tests in FL Studio (`scripts\install-vst3.ps1`).
