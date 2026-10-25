# Saturation panel 0.6 (character tiles + big Drive, 3 bands) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The Saturation panel of spec 5.6 ("Sat 2" + Low / Mid / High): a live frequency strip with three bands and two
draggable crossovers, character tiles with icons and a big Drive for the selected band, Multiband and Link switches,
Mix / Output / crossovers in Mehr — and every new sat parameter reachable (the 13 "sat: reaches sat…" checks green).

**Architecture:** A new file pair `Source/UI/SatPanel.{h,cpp}` holds the panel (`SatPanel : DetailPanel`), its visual
(`SatView : ModuleView` with child components for the bands and crossover handles), the Link behaviour (`SatLink`, a
parameter listener that applies `dsp::linkedBandDrives` to user gestures only) and the factories. DetailPanels.cpp and
ModuleViews.cpp only switch their `sat` case to the new factories (the old SatPanel / SatView are deleted). Segments gets an
optional tile painter (additive), so the character tiles keep every gesture of a segment group.

**Tech Stack:** C++20, JUCE 8.0.9, VelvoxTests (juce::UnitTest), `VelvoxTests --snapshot`.

**Spec:** `docs/superpowers/specs/2026-10-25-headliner-pro-overhaul-design.md` §2.4 – 2.6, §5.6, §8; sat DSP ledger
`docs/superpowers/plans/2026-10-25-overhaul-sat.ledger.md`; shell ledger (frame API); `C:\Users\jakob\dev\velvox-wt\AGENT_RULES.md`.

## Global Constraints

- Never rename/remove parameter IDs or plugin codes; no new parameters are needed (all sat IDs exist).
- Link is panel behaviour only: drive edits by the user (gestures) call `dsp::linkedBandDrives`; automation of one band never writes the others.
- Every edit is one undo step via `proc.getUndo()`; host gestures begin/end around drags.
- Graph handles: drag, Shift/Ctrl fine, double-click / Alt+click reset, wheel, right-click menu, hover card after 600 ms.
- No permanent explanatory text; short labels + values only. No allocation in paint; live data ≤ 30 Hz repaints.
- Panel frame: 3 – 5 main controls, everything else in Mehr or the visual; nothing outside / below 14 px; visual ≥ 150 px high.
- Multiband off shows the old single-band parameters (satCharacter, satDrive).
- Don't weaken PanelFrameTests; no edits to other worktrees; no push.

## Review Focus

1. Dragging the low crossover above the high one – expected: the handle stops at high / 1.5 (DSP guard order kept), no jump. Test Task 2.
2. Link on, one band at 24 dB, dragging another up – expected: all bands stop together at the range end, differences kept; automation of one band never moves the others. Test Task 1.
3. Undo after a linked drag – expected: one step restores all three band drives. Test Task 1.
4. Multiband switched on/off from automation while the panel is open – expected: the strip, tiles and Drive follow (single band ↔ selected band) without a stale control. Test Task 3.
5. Second instance (sat_2) – expected: every control and the meters bind to `_2` IDs / instance 1. Test Task 3.

---

### Task 1: Link behaviour, tile painter, chain accessor

**Files:**
- Create: `Source/UI/SatPanel.h`, `Source/UI/SatPanel.cpp` (SatLink first), `Tests/SatPanelTests.cpp`
- Modify: `Source/UI/Segments.h/.cpp` (tile painter), `Source/DSP/VocalChain.h` (`getSaturation`), `Source/PluginProcessor.cpp`
  (`effectiveValueOf` for the band drives), `CMakeLists.txt` (own blocks for the new source and test file)

**Interfaces:**
- Produces: `class SatLink : juce::AudioProcessorParameter::Listener { SatLink (APVTS&, int instance); ~SatLink(); }` — while
  `satLink` (of the instance) is on and a user gesture is open on one band drive, every value change of it writes the other two
  drives = `dsp::linkedBandDrives (driveAtGestureStart, band, newDb)` (others only), inside gestures of their own (folded into
  the user's step).
- Produces: `using Segments::TilePainter = std::function<void (juce::Graphics&, int segment, juce::Rectangle<float>, bool selected, bool hover)>;
  void Segments::setTilePainter (TilePainter)` — when set, each segment button is painted by it (gestures unchanged).
- Produces: `const SaturationModule& VocalChain::getSaturation (int instance = 0) const noexcept`.
- Produces: `effectiveValueOf (satLowDrive / satMidDrive / satHighDrive)` = the effective band drives (macro markers).

- [ ] Step 1: tests (SatPanelTests): link on: gesture on satLowDrive 6 → 10 moves mid/high by +4; one undo step restores all three;
  link off: others unchanged; no gesture (automation): others unchanged; clamp: high at 22, drag low +6 → others +2 only;
  instance 2 links `_2` IDs only; a tile painter is called for every segment; `getSaturation(1)` is instance 1's module.
- [ ] Step 2: build, watch them fail (SatLink missing).
- [ ] Step 3: implement; Step 4: pass; commit.

### Task 2: SatView – frequency strip, crossover handles, band areas, mini curve + harmonics

**Files:** Modify `Source/UI/SatPanel.h/.cpp`, `Source/UI/ModuleViews.cpp` (sat case → `createSatView`, old SatView removed); Test `Tests/SatPanelTests.cpp`.

**Interfaces:**
- Produces: `class SatView : ModuleView { SatView (VelvoxAudioProcessor&, int instance); int getSelectedBand() const;
  void setSelectedBand (int); std::function<void (int band)> onSelectBand; bool isMultiband() const;
  juce::Rectangle<float> getBandBounds (int band) const; float xForFrequency (float hz) const; float frequencyForX (float x) const;
  juce::Component* getCrossoverHandle (int which); juce::Component* getBandArea (int band); }`
  band = 0 / 1 / 2 (Low / Mid / High); in single-band mode the only band is 0 and `getSelectedBand()` returns -1.
- Produces: `std::unique_ptr<ModuleView> createSatView (VelvoxAudioProcessor&, int instance)`.
- Crossover handle gestures: drag = frequency (log axis), Shift/Ctrl fine (1/10), double-click / Alt+click = default, wheel = ±1/24 octave
  step, right-click = the parameter menu (gestures::showMenu), hover card (HoverInfo). Low ≤ high / 1.5, high ≥ low × 1.5 while dragging.
- Band area gestures: click = select; vertical drag = that band's drive (with Link through SatLink); double-click = drive default;
  wheel = drive ±0.5 dB; right-click = drive menu; hover card = band name, character, drive.
- Live: per band "drive meter" = band input RMS + drive (from `getMeters()`), the band fill rises with it; ≤ 30 Hz repaint, only on change.
- Mini transfer curve + harmonics bars H2..H8 of the selected band (`SaturationModule::transferCurve`, `harmonicProfile`, recomputed only when character / drive change).

- [ ] Step 1: tests: crossover drag moves `satXoverLow` and is one undo step; drag past high/1.5 stops; double-click resets 250 Hz;
  clicking the high band selects 2 and calls onSelectBand; band vertical drag up raises its drive (and with Link the others); multiband
  off → band 0 spans the strip, handles hidden; xForFrequency/frequencyForX round trip.
- [ ] Step 2: fail; Step 3: implement; Step 4: pass; commit.

### Task 3: SatPanel – main row, big Drive, Mehr, reachability, snapshots

**Files:** Modify `Source/UI/SatPanel.h/.cpp`, `Source/UI/DetailPanels.cpp` (sat case → `createSatPanel`, old class removed),
`Tests/TestMain.cpp` (snapshot overlay `set:id=value,…`), help text if a sat page names controls; Test `Tests/SatPanelTests.cpp`, `Tests/PanelFrameTests.cpp` (unchanged, must pass).

**Interfaces:**
- Produces: `std::unique_ptr<DetailPanel> createSatPanel (VelvoxAudioProcessor&)` (instance from `DetailPanel::constructionInstance`).
- Main row (3): character tiles (Segments with tile painter; one per band context, the selected one visible), Multiband (Toggle satMultiband),
  Link (Toggle satLink, dimmed while multiband is off). Big Drive (dial 120) in the visual's right column: satDrive when multiband is off,
  else the selected band's drive; its caption names the band ("MID DRIVE").
- Band on/off: Toggles satLowOn / satMidOn / satHighOn in the band headers of the strip (multiband only).
- Mehr: Mix, Output, Crossover Low, Crossover High (small knobs).

- [ ] Step 1: tests: all sat params reachable (PanelFrameTests), main row = tiles, Multiband, Link; selecting a band swaps the visible tiles /
  Drive to that band's IDs; multiband off shows satCharacter / satDrive; automation of satMultiband updates the panel in refresh();
  instance 2 binds `_2` IDs; layout fits at 984 × 442 with Mehr closed and open.
- [ ] Step 2: fail; Step 3: implement; Step 4: pass; snapshots (`--snapshot` single / multiband / Mehr, scale 1.0 and 8/9) into
  `C:\Users\jakob\dev\velvox-wt\shots-satpanel\`, look and iterate; full suite green; ledger; commit.
