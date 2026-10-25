# Voices Panels (Doubler "like Waves Doubler", Choir half-circle stage) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The Doubler and Choir panels of Headliner 0.6 on the shell's common frame: Doubler with voice-mode segments
(Classic / 2 / 4), a draggable stage strip (x = pan, y = gain, live wobble) and compact per-voice value rows; Choir with the
voices on a half circle around the lead.

**Architecture:** One new UI unit `Source/UI/VoicesPanels.{h,cpp}` holds `doubleredit` (the voice edit operations incl. the
Classic -> 2 conversion, each one undo step), `DoublerStage` (strip + rows, a `ModuleView`), `ChoirStage` (half circle, a
`ModuleView`) and the two panels (subclasses of `DetailPanel`). `DetailPanels.cpp` / `ModuleViews.cpp` lose their old Doubler /
Choir classes and call `createVoicesModulePanel` / `createVoicesModuleView` (small hunks). New sources/tests are registered in one
own block at the end of `CMakeLists.txt`; tests in `Tests/VoicesPanelTests.cpp` ("Headliner voices panels"). DSP feeds from the
voices track: `DoublerModule::resolveVoices`, `voicePan / voiceGainDb / voiceCents / voiceDelayMs / voiceCount`,
`ChoirModule::voicePosition / stagePan / shapes`, `voicePan / voiceLevel / voiceCents`, `VocalChain::getDoubler (k) / getChoir (k)`.
No DSP change.

**Tech Stack:** C++20, JUCE 8.0.9, VelvoxTests (juce::UnitTest), `--snapshot` renders.

**Spec:** `docs/superpowers/specs/2026-10-25-headliner-pro-overhaul-design.md` (§2.4 frame, §2.6 gestures, §5.8 Doubler, §5.9 Choir,
§8); voices ledger `2026-10-25-overhaul-voices.ledger.md` (Classic ruling: editing a voice copies resolveVoices() into V1/V2 and
switches to "2"); shell plan/ledger `2026-10-25-overhaul-shell.*`.

## Global Constraints

- Frame: 3 – 5 main controls; everything else reachable in Mehr; "Alte Mix-Kurve" comes from the frame; Mix via `setMixKnob`.
- Graph handles: drag, Shift/Ctrl fine, double-click / Alt+click reset, wheel, right-click menu (incl. panel items via
  `gestures::PanelMenuHost`), hover card after 600 ms; every edit is one undo step (host gestures around drags).
- No permanent explanatory text; short labels + values only. Paint without allocation, live repaint ≤ 30 Hz.
- Never rename/remove parameter IDs; no new parameters. Colours: Doubler #818cf8, Choir #6366ff (`moduleColour`).
- Canvas 1000 × 600, min scale 8/9 (889 px). Do not weaken tests; full suite must end green.
- PanelFrameTests: remove the doubler `pendingPanel` entries (keep `smoothW`).

## Review Focus

1. Classic project, user drags voice 2: the result must sound the same as before the drag except for the dragged value – V1/V2
   get the Classic delays (D, 1.37·D), 0 ct, 0 dB, pans ∓100 %, Mod Depth = the Classic Detune, mode "2"; all in ONE undo step,
   and undo returns to Classic with the old V values. Test: "Doubler edit: a voice edit in Classic converts to 2 in one step".
2. Doubler instance 2: the stage, rows and edits must use `doublerV1Pan_2` … and `getDoubler (1)`. Test: "Doubler edit: instance 2".
3. A drag that is only a click (no movement) must not record an undo step or leave a gesture open. Test: "Doubler stage: click
   without drag records nothing".
4. 4-voice mode then back to 2: V3/V4 keep their values (only hidden). Test: "Doubler stage: 2 / 4 voice rows".
5. Choir at Width 0 / 8 voices: no NaN positions, all voices inside the stage. Test: "Choir stage: positions on the half circle".

---

### Task 1: Doubler voice edits (`doubleredit`)

**Files:** Create `Source/UI/VoicesPanels.h/.cpp`; Modify `CMakeLists.txt` (own block); Test: `Tests/VoicesPanelTests.cpp`

**Interfaces (Produces):**
```cpp
namespace velvox::ui::doubleredit {
  enum class Field { on, delay, detune, gain, pan };
  struct VoiceValues { bool on; float delayMs, detuneCents, gainDb, panPercent; };
  int mode (VelvoxAudioProcessor&, int instance);            // 0 Classic, 1 "2", 2 "4"
  int voiceCount (VelvoxAudioProcessor&, int instance);      // 2 or 4
  VoiceValues voice (VelvoxAudioProcessor&, int instance, int v);   // Classic: the linked values
  juce::String paramId (int instance, int v, Field);
  void leaveClassic (VelvoxAudioProcessor&, int instance);  // inside a transaction: copy + mode 2
  class Edit { public: Edit (proc, instance, v, std::initializer_list<Field>, const juce::String& name);
               void set (Field, float real); ~Edit(); };     // one undo step, host gestures, Classic -> 2 first
  void setValue (proc, instance, v, Field, float);  void toggleOn (proc, instance, v);
  void resetVoice (proc, instance, v);  void resetPosition (proc, instance, v);   // pan + gain
}
```
- [ ] Tests: Classic `voice()` follows Delay (25 ms -> 25 / 34.25), Classic edit converts in one step + undo, 2-mode edit is one
  step and leaves the mode, resetVoice / resetPosition, instance 2. Fail (no unit); implement; pass; commit.

### Task 2: Doubler stage + panel

**Interfaces:** `class DoublerStage : public ModuleView` (strip + rows); `voicePoint (v)`, `getVoiceHandle (v)`, `getRow (v)`,
`numShownVoices()`, `isClassicShown()`; per-voice `VoiceField` (paramId component property for the reach test).
Panel: main = Voices segments, Mix (setMixKnob), Wobble (Classic: doublerDetune, 2/4: doublerModDepth), Dry; Mehr = Width,
Mono Safe, Tone, Low Cut, Mod Rate, Delay (Classic only, disabled otherwise) + the inactive wobble knob.
- [ ] Tests: dot positions follow pan / gain, drag = one step (pan + gain), Shift fine, double-click reset, wheel = detune, menu
  on/off, rows 2 / 4 / Classic, field drag / typed value, click without drag records nothing, panel main row by mode, every doubler
  ID reachable (PanelFrameTests exemption removed). Fail; implement; old DoublerView / DoublerPanel removed; pass; commit.

### Task 3: Choir stage + panel

**Interfaces:** `class ChoirStage : public ModuleView`; `voicePoint (v)`, `numShownVoices()`; drag = Width (one step),
double-click / Alt = Width default, wheel = Width, right-click = panel menu, hover card per voice.
Panel: main = Voices segments, Detune, Width, Mix; Mehr = Timing, Humanize, Density.
- [ ] Tests: positions on the half circle (x = heard pan, depth outward), width 0 collapses, count follows segments, drag width one
  step; fail; implement; old ChoirView / ChoirPanel removed; pass; commit.

### Task 4: Help, screenshots, full suite

- [ ] Help pages (doubler / choir) describe the new controls; snapshots in `C:\Users\jakob\dev\velvox-wt\shots-voicespanel\` at
  1000 and 889 px (Classic, 4 voices, Mehr open, choir 8 voices) – look at each and iterate; full suite green; ledger; commit.
