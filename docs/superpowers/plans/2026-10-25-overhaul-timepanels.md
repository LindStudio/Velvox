# Time Panels (Delay echo map, Pulse "like Kickstart", Stutter cuts) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The three time-module panels of Headliner 0.6 on the shell's common frame: Delay "echo map" (draggable echo bars),
Pulse "like Kickstart" (big Amount, 4 shape tiles + Custom, rate buttons, live playhead), Stutter "cuts on the waveform"
(scrolling vocal with muted / repeated slices shaded); the UI name "Stutter" everywhere.

**Architecture:** One new UI unit `Source/UI/TimePanels.{h,cpp}` holds the three panels (subclasses of `DetailPanel`) and their
visuals (`ModuleView`s). `DetailPanels.cpp` / `ModuleViews.cpp` lose their old Delay / Pulse / Chop classes and call the two
factory functions of the new unit (three lines each – small merge hunks). New sources and tests are registered in one own block at
the end of `CMakeLists.txt`; tests live in the new `Tests/TimePanelTests.cpp`. The DSP feeds come from the time track
(`DelayModule::echoTaps / feedbackForRatio / displayTimeMs`, `PulseModule::evaluateShape / gainAt / effectiveDepth / displayPhase /
displayGain`, `ChopModule::readDisplay`, `VocalChain::getDelay (k) / getChop()`), no DSP change.

**Tech Stack:** C++20, JUCE 8.0.9, VelvoxTests (juce::UnitTest), `--snapshot` renders.

**Spec:** `docs/superpowers/specs/2026-10-25-headliner-pro-overhaul-design.md` (§2.4 frame, §2.6 gestures, §5.11 Delay, §5.13 Stutter,
§5.14 Pulse, §8); shell plan/ledger `2026-10-25-overhaul-shell.*`; time ledger `2026-10-25-overhaul-time.ledger.md`.

## Global Constraints

- Frame: 3 – 5 main controls; everything else reachable in Mehr; "Alte Mix-Kurve" comes from the frame; Mix of Delay via `setMixKnob`.
- Graph handles use the gestures of all controls: drag, Shift/Ctrl fine, double-click / Alt+click reset, wheel, right-click menu
  (incl. panel items via `gestures::PanelMenuHost`), hover card after 600 ms; every edit is one undo step (host gestures around drags).
- No permanent explanatory text; short labels + values only. Paint without allocation (member paths reused), live repaint ≤ 30 Hz.
- Never rename/remove parameter IDs; no new parameters needed. Internal name stays `chop`; UI name "Stutter" everywhere.
- Colours: Delay #D946EF, Stutter #A855F7, Pulse #8B5CF6 (`moduleColour`). Canvas 1000 × 600, min scale 8/9.
- Do not weaken tests; the full suite (`-Test "*"`) must end green.

## Review Focus

1. Dragging an echo while sync is on but the host sends no tempo – the delay runs on the ms time, so the drag must edit ms
   (snapping to note values would move nothing). Test: "Delay map: synced without host tempo drags milliseconds".
2. A drag that changes time and feedback together must be one undo step and leave no gesture open. Test: "Delay map: one drag is one
   undo step".
3. Delay instance 2 / 3: the map must read and write the instance's own IDs (`delayTime_2` …) and its own `displayTimeMs`.
   Test: "Delay map: instance 2 edits its own parameters".
4. Pulse custom curve edited, then a curated tile clicked, then Custom again – the custom points must be untouched.
   Test: "Pulse tiles: switching shapes never touches the custom points".
5. Stutter display with the module off or no audio yet (0 frames) – no crash, empty waveform. Test: "Stutter view: empty feed".

---

### Task 1: Delay – echo map panel

**Files:** Create `Source/UI/TimePanels.h/.cpp`, `Tests/TimePanelTests.cpp`; Modify `Source/UI/DetailPanels.cpp` (remove DelayPanel,
factory), `Source/UI/ModuleViews.cpp` (remove DelayView, factory), `CMakeLists.txt` (own block at the end).

**Interfaces (Produces):**
`std::unique_ptr<ModuleView> createTimeModuleView (VelvoxAudioProcessor&, dsp::ModuleId, int instance)` (nullptr for other modules);
`std::unique_ptr<DetailPanel> createTimeModulePanel (VelvoxAudioProcessor&, dsp::ModuleId)` (nullptr for other modules).
`class EchoMapView : public ModuleView` with `int getNumTaps() const`, `dsp::DelayModule::EchoTap getTap (int) const`,
`juce::Rectangle<float> getBarBounds (int tap, bool left) const`, `float xForMs (float) const`, `float getWindowMs() const`,
`static float heightForLevel (float linear)` (dB scale −48 … 0 dB → 0 … 1).

- [ ] Step 1: tests in `TimePanelTests.cpp` ("Headliner time panels", category "Headliner"):
  - "Delay map: bars are DelayModule::echoTaps": fb 50 %, 300 ms, plain / spread 20 ms / ping-pong: view tap count == echoTaps count
    (in the window), tap times/levels equal; ping-pong: odd taps have only an upper (L) bar, even taps only a lower (R) bar.
  - "Delay map: drag the first echo = time": sync off, drag bar 1 from x(300 ms) to x(450 ms) → delayTime 450 ± 2 ms; with Shift the
    same pixel move gives 1/10 of the change.
  - "Delay map: synced drags snap to note values": sync on, host 120 BPM: dragging to x(760 ms) selects 1/4D (750 ms); with Shift:
    sync off, delayTime ≈ 760 ms.
  - "Delay map: synced without host tempo drags milliseconds" (Review Focus 1).
  - "Delay map: vertical drag = feedback": dragging up 60 px raises feedback to feedbackForRatio(ratio0 + 60/200) ± 0.5 %; down lowers.
  - "Delay map: one drag is one undo step": time + feedback drag → getNumSteps +1, undo restores both (Review Focus 2).
  - "Delay map: double-click resets time and feedback"; "wheel steps feedback".
  - "Delay map: instance 2 edits its own parameters" (Review Focus 3).
  - "Delay panel: note value in the main row while synced, ms knob while free" (main size 5 in both states, the other control in Mehr).
- [ ] Step 2: run `-Test "Headliner time panels"`; expected: fail (no TimePanels unit / old DelayView).
- [ ] Step 3: implement. Window = clamp(600, 8000, time × 6) ms, frozen during a drag. Horizontal drag: grabbed tap k follows the
  pointer, new time = pointer ms / k (free: `delayTime`, fine 1/10 with Shift/Ctrl when free); synced + host tempo: nearest
  division by ms (`delaydiv::beatsForIndex`), Shift = leave sync, set ms. Vertical: ratio = ratio0 + (−dy)/200 (fine /10),
  feedback = `feedbackForRatio`. Begin gestures on delayTime, delayDivision, delaySync, delayFeedback at mouse down. Live: bars
  glow by the input level history at (now − tap time) and are pushed down by ducking; time read-out from `displayTimeMs`.
- [ ] Step 4: tests pass + "Headliner panel frame" green. Step 5: commit.

### Task 2: Pulse – Kickstart panel and shape tiles (fixes "pulse: reaches pulseShape/pulseAmount")

**Files:** Modify `Source/UI/TimePanels.*`, `DetailPanels.cpp` / `ModuleViews.cpp` (remove PulsePanel / PulseView),
`Tests/PanelFrameTests.cpp` (paramOf reads a control's `paramId` property), `Tests/TimePanelTests.cpp`.

**Interfaces:** `class PulseCurveView : public ModuleView` (`bool showsPoints() const`, `juce::Point<float> pointPosition (int) const`,
`juce::Rectangle<float> getPlotArea() const`, `float yForGain (float) const`); `class PulseShapeTiles : public juce::Component`
(`int getNumTiles() const` = 5, `juce::Rectangle<int> getTileBounds (int) const`, `int getSelectedTile() const`, `void chooseTile (int)`;
property `paramId` = the pulseShape ID).

- [ ] Step 1: tests:
  - "Pulse panel: big Amount in the visual, tiles, rate buttons": Amount knob (pulseAmount) dial ≥ 100 inside the visual; 5 tiles inside
    the visual; main = Rate segments (1/1, 1/2, 1/4, 1/8, 1/16 + …), Mix, Smooth; PanelFrameTests reach pulseShape / pulseAmount.
  - "Pulse tiles: click = one undo step, double-click = Custom, wheel = next": chooseTile / mouse on tile 2 → pulseShape 2, steps +1.
  - "Pulse tiles: switching shapes never touches the custom points" (Review Focus 4).
  - "Pulse curve: Custom shows the 6 points, a curated shape hides them; the drawn gain is gainAt": yForGain(gainAt(s, φ)) sampled.
  - "Pulse curve: dragging a point is one undo step" (pulseX/Y of that point).
- [ ] Steps 2–5: fail, implement, pass (+ panel frame, full time panel suite), commit.

### Task 3: Stutter – cuts on the waveform, "Stutter" everywhere

**Files:** Modify `Source/UI/TimePanels.*`, `DetailPanels.cpp` / `ModuleViews.cpp` (remove ChopPanel / ChopView),
`Source/DSP/Routing.cpp` (display / short name), `Source/UI/HelpContent.cpp` (page title / text), `Source/Parameters/ParameterLayout.cpp`
(host-visible names "Stutter …", IDs unchanged), `Tests/TimePanelTests.cpp`.

**Interfaces:** `class StutterWaveView : public ModuleView` (`int getNumFrames() const`, `dsp::ChopModule::DisplayFrame getFrame (int) const`
(oldest first), `int countShadedSlices (char type) const`, `static constexpr double windowSeconds = 4.0`).

- [ ] Step 1: tests:
  - "Stutter view: muted and repeated slices are shaded": Gate 1/2 at 1/16, 120 BPM, 0.5 sine, 2 s processed → frames > 0, at least
    6 muted slices shaded, played frames drawn bright; pattern "Chop & repeat" → repeat slices > 0.
  - "Stutter view: empty feed" (Review Focus 5).
  - "Stutter: the UI says Stutter": moduleDisplayName / moduleShortName (chop) == "Stutter", help page title, parameter names start
    with "Stutter"; panel main = Rate, Depth, Smooth, Pattern.
- [ ] Steps 2–5: fail, implement, pass, commit.

### Task 4: Screenshots, full suite, ledger

- [ ] `VelvoxTests --snapshot` for delay / pulse (a curated shape and Custom) / stutter at 1000 px and 889 px (min scale) into
  `C:\Users\jakob\dev\velvox-wt\shots-timepanels\`; look at each, iterate.
- [ ] `bt.ps1 -Test "*"` green; ledger complete; commit.
