# Reverb panel "Raumtiefe" (Headliner 0.6, track "reverbpanel") Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The Reverb panel of spec 5.12 ("Reverb 3 · Raumtiefe"): a stylised room in perspective with the vocal as a glowing dot;
dragging the dot back / forward sets the depth (Mix, Pre-Delay and Size together, one undo step, one host gesture group); main row
Room (dropdown, 8 types), Size, Decay, Mix; everything else in Mehr.

**Architecture:** New `Source/UI/ReverbPanel.{h,cpp}` (like the eqpanel track's `EqPanel.*`): `reverbedit::` message-thread
operations (read depth from the parameters with `dsp::reverbDepthFromSettings`, write a depth with `dsp::reverbDepthPath`), the
`ReverbRoom` ModuleView (room geometry from type + size, dot, decay-over-frequency hint from `ReverbModule::predictedRt60`, all
gestures), and the `ReverbPanel` (DetailPanel). `DetailPanels.cpp` / `ModuleViews.cpp` only call the two factories; the old
`ReverbView` and the old `ReverbPanel` class are deleted.

**Tech Stack:** C++17, JUCE 8.0.9, VelvoxTests (juce::UnitTest).

**Spec:** `docs/superpowers/specs/2026-10-25-headliner-pro-overhaul-design.md` (2.4, 2.5, 2.6, 5.12, 8); DSP plan
`docs/superpowers/plans/2026-10-25-overhaul-reverb.md` (APIs).

## Global Constraints

- No parameter IDs renamed / removed / added (none needed). Old projects sound identical (UI only).
- "The vocal as a dot in a room drawing; dragging it back = more space (moves Mix, Pre-delay and Size together along a curated path);
  3 sliders Size, Decay, Mix; room type dropdown; Mehr: everything else incl. ducking."
- Gestures (2.6): drag, Shift/Ctrl = fine 1/10, double-click and Alt+click = reset, wheel = step, right-click = menu (+ panel items),
  hover card after 600 ms (name, short description, value). Every edit one undo step via `proc.getUndo()`; host gestures begin/end
  around drags.
- No permanent explanatory text (labels + values only). Paint: no own allocation (paths / strings built in update / resized,
  static room cached as an image), repaint only on change, live data ≤ 30 Hz.
- Instance 2 edits `reverbMix_2` etc. (`pidFor`).
- Do not touch other worktrees; don't push.

## Review Focus

1. Drag across the depth ends (0 / 1) and back: no jump when the mouse returns, values stay on the path, still exactly one step —
   test "drag past the end clamps, one step".
2. Settings far from the path (hand-set Mix 100 %, Pre-Delay 250 ms): dot stays inside the room, a drag starts from that depth
   (snaps the three values onto the path at the first move, inside the same step) — test "off-path settings: dot inside, first drag
   moves onto the path".
3. Undo / Ctrl+Z while the dot is dragged: refused by UndoHistory (open gesture); after mouse-up one step — covered by the step tests.
4. Module off / as a send: the room is dimmed when off; dragging still works (Mix = send level) — test "send: depth writes the send
   level".
5. Minimum editor scale 8/9 and Mehr open (visual narrower): room, dot and read-out still inside the visual — snapshot review +
   PanelFrameTests layout test.

---

### Task 1: Depth operations + panel structure

**Files:**
- Create: `Source/UI/ReverbPanel.h`, `Source/UI/ReverbPanel.cpp`, `Tests/ReverbPanelTests.cpp` (UnitTest "Headliner reverb panel",
  category "Headliner"); Modify: `CMakeLists.txt`, `Source/UI/DetailPanels.cpp` (factory → `createReverbPanel`, old class removed),
  `Tests/DesignTests.cpp` ("REVERB room list" now a dropdown).

**Interfaces (Produces):**
```cpp
namespace velvox::ui::reverbedit {
    float depth (VelvoxAudioProcessor&, int instance);                 // reverbDepthFromSettings of the parameters
    dsp::ReverbDepthPoint pointFor (VelvoxAudioProcessor&, int instance, float depth); // path point of the current type
    void beginDepthGesture (VelvoxAudioProcessor&, int instance);     // host gestures mix, pre-delay, size (in this order)
    void writeDepth (VelvoxAudioProcessor&, int instance, float depth); // values only (inside a gesture)
    void endDepthGesture (VelvoxAudioProcessor&, int instance);
    void setDepth (VelvoxAudioProcessor&, int instance, float depth);  // complete gesture = one undo step
    void resetDepth (VelvoxAudioProcessor&, int instance);             // mix / pre-delay / size to their defaults, one step
}
std::unique_ptr<DetailPanel> createReverbPanel (VelvoxAudioProcessor&);
```
- [ ] Step 1: tests: `setDepth` for every type × depth {0, 0.3, 0.7, 1} writes exactly `reverbDepthPath` (±0.05 in parameter
  units), `depth()` round-trips ±0.02; one undo step, undo restores all three; listener on the three parameters sees begin ×3
  before the first value change and end ×3 after the last; `resetDepth` = defaults (18 %, 20 ms, 60 %), one step; instance 2 writes
  `_2` IDs only; panel main controls = [Choice reverbType (8 items), Knob reverbSize, Knob reverbDecay, Knob reverbMix], Mehr holds
  pre-delay, damping, width, cuts, movement, early, diffusion, bass, four ducking knobs, duck source.
- [ ] Step 2: run — FAIL (link). Step 3: implement. Step 4: run — PASS (+ PanelFrameTests, DesignTests). Step 5: commit.

### Task 2: The room view (`ReverbRoom`) and its gestures

**Files:** Modify `Source/UI/ReverbPanel.{h,cpp}`, `Source/UI/ModuleViews.cpp` (reverb → `createReverbRoom`, old ReverbView
removed), `Tests/ReverbPanelTests.cpp`.

**Interfaces (Produces):**
```cpp
class ReverbRoom final : public ModuleView {
    ReverbRoom (VelvoxAudioProcessor&, int instance);
    struct Geometry { juce::Rectangle<float> front, back; };   // front opening + back wall (screen)
    Geometry getGeometry() const; juce::Point<float> getDotPosition() const; float getShownDepth() const;
    std::array<float, 6> getDecayBands() const;  // predictedRt60 at 125, 250, 500, 1k, 2k, 4k Hz (shown hint)
    juce::String getReadout() const;            // "Mix 18 % · Pre 20 ms · Size 60 %"
    juce::PopupMenu buildMenu();                 // Vorne / Mitte / Hinten / Standard + panel items
};
std::unique_ptr<ModuleView> createReverbRoom (VelvoxAudioProcessor&, int instance);
```
- Geometry per type (Room small/low … Large Hall / Atmosphere big, Plate wide and shallow) scaled by Size; dot depth z = shown depth
  along the floor's centre line (0 = front, 1 = near the back wall); drag: 260 px per full depth, Shift/Ctrl 1/10, relative (no jump),
  drag starts on the dot or anywhere on the floor; wheel 0.02 per notch (one step each); double-click / Alt+click = `resetDepth`.
- [ ] Step 1: tests: depth from settings moves the dot (mix up → dot y smaller); Large Hall back wall smaller (deeper) than Room,
  Size 100 deeper than Size 0; drag up 130 px = +0.5 depth, Shift = +0.05, one step, gestures; drag past the end clamps; double-click /
  Alt+click reset; wheel step; decay bands = `predictedRt60`; menu items; off-path settings keep the dot inside the room.
- [ ] Step 2: run — FAIL. Step 3: implement. Step 4: run — PASS. Step 5: commit.

### Task 3: Look, help, snapshots, full suite

**Files:** `Source/UI/ReverbPanel.cpp` (visual polish), `Source/UI/HelpContent.cpp` (reverb page: 8 rooms, depth dot), ledger.
- [ ] Step 1: `--snapshot` Advanced reverb at 1000 px and at min scale (889 px), with Mehr open, as send, at instance 2 into
  `C:\Users\jakob\dev\velvox-wt\shots-reverbpanel\`; look at them, iterate until clean.
- [ ] Step 2: full suite `-Test "*"` green; ledger; commit.
