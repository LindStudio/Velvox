# Smooth + Pitch panels (Headliner 0.6, track "smoothpanel") Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The Smooth panel of spec 5.4 ("Smooth 1 · soothe-Stil": live spectrum with the removed energy in pink, the weighting
curve with 6 draggable points, draggable low / high limits; Depth, Sharpness, Speed, Delta) and the Pitch panel of spec 5.10
("Pitch 2 · zwei große Regler": big semitone and Formant controls with a live input → output note read-out).

**Architecture:** Two new UI units like the finished `EqPanel.*` / `ReverbPanel.*`: `Source/UI/SmoothPanel.{h,cpp}` (the
`smoothedit::` operations incl. the linked Speed control, the `SmoothGraph` ModuleView, the `SpeedKnob`, the panel) and
`Source/UI/PitchPanel.{h,cpp}` (`PitchStage` ModuleView with the note read-out and the semitone tick ring, the panel with the two
big knobs placed in the visual). `DetailPanels.cpp` / `ModuleViews.cpp` only call the factories; the old `SmoothPanel`,
`PitchPanel`, `SmoothView`, `PitchView` classes are deleted. Pitch gets a minimal lock-free feed of the detected input pitch.

**Tech Stack:** C++17, JUCE 8.0.9, VelvoxTests (juce::UnitTest).

**Spec:** `docs/superpowers/specs/2026-10-25-headliner-pro-overhaul-design.md` (2.4, 2.5, 2.6, 5.4, 5.10, 8); DSP plans / ledgers
`2026-10-25-overhaul-smooth.*` (getSpectrum, weightingDbAt, speedToTimes, smoothW{1..6}{Freq,Gain}) and `2026-10-25-overhaul-pitch.*`.

## Global Constraints

- No parameter IDs renamed / removed / added (Speed is a UI link of smoothAttack / smoothRelease, smooth Ruling 8).
- Smooth: "Live spectrum with the removed energy shown in pink, a weighting curve with draggable points under it, 3 main controls:
  Depth (Amount), Sharpness/Selectivity (Sensitivity), Speed (Attack/Release linked), plus Delta listen. Mehr: separate
  attack/release, mode (soft/hard), output, low/high limits."
- Pitch: "Big semitone control (snaps to semitones) and Formant; Mehr: Cents, Preserve formants, Mix."
- Gestures (2.6): drag, Shift/Ctrl = fine 1/10, double-click and Alt+click = reset, wheel = step, right-click = menu (+ panel
  items), hover card after 600 ms. Every edit one undo step via `proc.getUndo()`; host gestures begin/end around drags.
- No permanent explanatory text (labels + values only). Paint: no allocation (paths / strings built in update()), repaint only on
  change, live data ≤ 30 Hz. `SmoothModule::getSpectrum` is wait-free (SeqLock).
- Smooth instances 1 and 2 (`pidFor`, `getSmooth (instance)`); Pitch has one instance.
- PanelFrameTests "smooth: reaches smoothW…" (12 reds on the base) must pass without weakening the test.
- Do not touch other worktrees; don't push.

## Review Focus

1. Points dragged onto each other / past each other / to the axis ends: curve stays finite and drawn, the grabbed point stays the
   grabbed one (no hand-over to another point mid-drag) — test "drag a point across another: same point, clamped".
2. Attack / Release set by hand off the Speed link: Speed shows their mean, the first Speed move puts both on the link inside the
   same step — test "Speed: off-link values snap on the first move, one step".
3. Instance 2: graph, limits and Speed edit only `_2` IDs and read `getSmooth (1)` — test "instance 2".
4. No signal / module off / sample rate change (binHz changes): the graph shows no stale pink and never reads past numBins —
   test "spectrum off / empty frame: no removed energy shown".
5. Pitch: no voiced input (silence, noise) → read-out shows "–", not a stale or random note; Mix 0 / off dims — test.

---

### Task 1: Smooth operations, Speed link, panel structure

**Files:** Create `Source/UI/SmoothPanel.{h,cpp}`, `Tests/SmoothPanelTests.cpp` (UnitTest "Headliner smooth panel", category
"Headliner"); Modify `CMakeLists.txt`, `Source/UI/DetailPanels.cpp` (factory → `createSmoothPanel`, old class removed),
`Source/UI/Widgets.{h,cpp}` (an unbound `Knob` constructor), `Source/UI/ModuleViews.h` (`ModuleView::getEditedParameters`),
`Tests/PanelFrameTests.cpp` (collectParams also asks the visual).

**Interfaces (Produces):**
```cpp
namespace velvox::ui::smoothedit {
    float speed (VelvoxAudioProcessor&, int instance);            // mean of the attack / release positions on the link, 0..1
    void beginSpeedGesture (VelvoxAudioProcessor&, int instance);   // attack, release
    void writeSpeed (VelvoxAudioProcessor&, int instance, float speed01); // SmoothModule::speedToTimes -> both
    void endSpeedGesture (VelvoxAudioProcessor&, int instance);     // release, attack
    void setSpeed (VelvoxAudioProcessor&, int instance, float speed01);   // one step
    juce::String weightFreqId (int instance, int point), weightGainId (int instance, int point);
    void resetPoint (VelvoxAudioProcessor&, int instance, int point);     // freq + gain defaults, one step
    void flattenAll (VelvoxAudioProcessor&, int instance);                // every gain 0 dB, one step
    dsp::SmoothSettings settings (VelvoxAudioProcessor&, int instance);  // the parameters as SmoothSettings
}
class SpeedKnob final : public Knob { ... };   // the linked Speed control (Knob look and gestures, no parameter of its own)
std::unique_ptr<DetailPanel> createSmoothPanel (VelvoxAudioProcessor&);
// ModuleView: virtual juce::StringArray getEditedParameters() const { return {}; }  // graph handles (panel-frame reach test)
```
- [ ] Step 1: tests: `setSpeed` for {0, 0.25, 0.5, 1} writes `speedToTimes` (±0.01 ms) and `speed()` reads back ±0.01; one undo
  step, undo restores both; HostLog sees begin attack, begin release before the first value and both ends after; off-link values
  (attack 50, release 10) → `speed()` = mean of the two positions; the SpeedKnob: drag = one step, double-click = 5 / 80 ms,
  value text "50 %"; instance 2 writes `_2` only; panel main = [Knob smoothAmount, Knob smoothSensitivity, SpeedKnob, Toggle
  smoothDelta]; Mehr = attack, release, mode segments, output, low, high.
- [ ] Step 2: run — FAIL (link). Step 3: implement. Step 4: run — PASS (+ PanelFrameTests minus the graph IDs, DesignTests).
  Step 5: commit.

### Task 2: The graph (`SmoothGraph`) and its gestures

**Files:** Modify `Source/UI/SmoothPanel.{h,cpp}`, `Source/UI/ModuleViews.cpp` (smooth → `createSmoothGraph`, old SmoothView
removed), `Tests/SmoothPanelTests.cpp`.

**Interfaces (Produces):**
```cpp
class SmoothGraph final : public ModuleView {
    SmoothGraph (VelvoxAudioProcessor&, int instance);
    static constexpr float rangeDb = 12.0f;                 // weighting axis +-12 dB
    juce::Rectangle<float> plotArea() const; float xForFreq (float) const; float freqForX (float) const;
    float yForWeight (float db) const; float weightForY (float y) const;
    juce::Point<float> pointPosition (int point) const;  int pointAt (juce::Point<float>) const;   // -1 none
    enum Edge { none = 0, lowEdge, highEdge };  Edge edgeAt (juce::Point<float>) const;
    float shownRemovedDb (float hz) const;                 // what the pink shows there (tests)
    juce::PopupMenu buildPointMenu (int point); juce::PopupMenu buildEdgeMenu (Edge);
    juce::StringArray getEditedParameters() const override;   // 12 weighting IDs + low + high of the instance
};
std::unique_ptr<ModuleView> createSmoothGraph (VelvoxAudioProcessor&, int instance);
```
- Plot x log 20 Hz – 20 kHz; spectrum y −90…0 dB with a +3 dB/oct display tilt around 1 kHz; the input spectrum faint, the output
  (input − reduction) filled in the Smooth colour, the removed part between them pink (#f472b6); the weighting curve
  (`weightingDbAt`) with its 6 points on a ±12 dB axis; outside the low / high limits shaded, the two edges draggable.
- Point drag: x = freq, y = gain, relative, Shift/Ctrl 1/10, both gestures open = one step; double-click / Alt+click = point
  defaults; wheel over a point = gain ±0.5 dB per notch (Shift 0.05), one step per event; right-click = Punkt n: Zurücksetzen,
  Alle flach + panel items. Edge drag: x = freq (clamped to the parameter range), same gestures (wheel = 1/24 oct). Hover card
  600 ms. Spectrum read ≤ 30 Hz.
- [ ] Step 1: tests: geometry round trips; drag a point right 1 octave and up 6 dB → freq ×2, gain +6 (±0.1), one step, gestures;
  Shift 1/10; drag across another point keeps the grabbed point; clamped at ±12 dB / 20 Hz…20 kHz; double-click / Alt = defaults;
  wheel; edges: drag low edge → smoothLow, clamped to 100…8000; menu items + panel items; with a resonant input processed by the
  module, `shownRemovedDb` near 3.2 kHz > 1 dB and ≈ 0 far away; module off / no frame → 0; instance 2 reads its own module and
  writes `_2`; `getEditedParameters` lists the 14 IDs.
- [ ] Step 2: run — FAIL. Step 3: implement. Step 4: run — PASS incl. PanelFrameTests "smooth: reaches …". Step 5: commit.

### Task 3: Pitch panel

**Files:** Create `Source/UI/PitchPanel.{h,cpp}`, `Tests/PitchPanelTests.cpp` ("Headliner pitch panel"); Modify
`Source/DSP/PitchModule.{h,cpp}` (`std::atomic<float> detectedHz`, `getDetectedHz()`), `Source/DSP/VocalChain.h` (`getPitch()`),
`Source/UI/DetailPanels.cpp`, `Source/UI/ModuleViews.cpp` (old PitchPanel / PitchView removed), `CMakeLists.txt`.

**Interfaces (Produces):**
```cpp
float dsp::PitchModule::getDetectedHz() const noexcept;     // last voiced f0 of the input, 0 = unvoiced (relaxed atomic)
const dsp::PitchModule& dsp::VocalChain::getPitch() const noexcept;
namespace velvox::ui::pitchnotes { juce::String noteName (float hz); float centsOff (float hz); }  // "A3", -50..+50
class PitchStage final : public ModuleView { void setDialGeometry (juce::Point<float> centre, float radius);
    juce::String getInputText() const; juce::String getOutputText() const; };
std::unique_ptr<DetailPanel> createPitchPanel (VelvoxAudioProcessor&);
```
- Visual: big Semitones knob (snaps, ±24 st, tick ring per semitone with the octaves marked) left, Formant knob right, the read-out
  IN note → OUT note between them. Main row: Fine (Cents), Mix, Formanten erhalten. Mehr empty (button hidden).
- [ ] Step 1: tests: module feed — a 220 Hz tone through the enabled module → `getDetectedHz` 220 ±1, silence → 0; note names
  (440 → "A4", 261.6 → "C4"), cents; stage read-out with 220 Hz and +3 st → IN "A3", OUT "C4"; unvoiced → "–"; panel: the two big
  knobs placed in the visual (dial ≥ 100), main row [Knob pitchCents, Knob pitchMix, Toggle pitchPreserveFormants].
- [ ] Step 2: FAIL. Step 3: implement. Step 4: PASS (+ PitchTests, PanelFrameTests, DesignTests). Step 5: commit.

### Task 4: Look, help, snapshots, full suite

- [ ] `--snapshot` (new overlay prefix `reso;` = live vocal with a 3.15 kHz resonance) Smooth at 1000 px and 889 px, Mehr open,
  instance 2, off; Pitch at 1000 / 889, +7 st; into `C:\Users\jakob\dev\velvox-wt\shots-smoothpanel\`; look, iterate.
- [ ] Help pages smooth / pitch updated to the new panels.
- [ ] Full suite `-Test "*"` green except the base's sat / pulse "reaches" reds (other tracks); ledger; commit.
