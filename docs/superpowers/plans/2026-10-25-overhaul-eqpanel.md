# Headliner 0.6 EQ Panel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the 0.5 fixed-band EQ panel by a Pro-Q style panel: large curve over the live spectrum, up to 16 user points
with the uniform graph gestures, the selected band's values under the graph, Mix and "Alte EQ-Kurve – umstellen" in Mehr; and
verify (fix if needed) that switching the legacy low-cut slope while playing does not jump.

**Architecture:** A new file pair `Source/UI/EqPanel.{h,cpp}` holds three layers: `eqedit` (band operations on the processor:
add / delete / type / slope / on / reset / materialise, each one undo step, built on `eqbands::Access`), `EqGraph` (a `ModuleView`:
spectrum + band curves + sum curve + points, mouse gestures, hover card) and `EqPanel` (a `DetailPanel` whose main row is five
re-bindable slots showing the selected band). Drawing uses `EqualizerModule::responseCurveDb` on
`proc.readEffectiveSettings().instance (k).eq`, so macro-created bands are drawn too. The old `EqView` (ModuleViews.cpp) and the
old `EqPanel` (DetailPanels.cpp) are deleted; the factories call the new code.

**Tech Stack:** C++20, JUCE 8.0.9, juce::UnitTest runner `VelvoxTests`.

**Spec:** `docs/superpowers/specs/2026-10-25-headliner-pro-overhaul-design.md` §2.5, §2.6, §5.1, §8 (Q4, Q5, Q8, max 16 bands).

## Global Constraints

- Never rename/remove parameter IDs; the 17 legacy fixed-band IDs stay registered, only no longer shown.
- Delete = reset all 7 band params (`On, Type, Freq, Gain, Q, Slope, Role`) to their defaults (EQ track contract, `EqBand::isFree`).
- Types: Bell, Low Shelf, High Shelf, Low Cut, High Cut, Notch, Band Pass, Tilt; cut slopes 6/12/18/24/36/48 dB/oct.
- Max 16 bands; every EQ instance k = 0..3.
- EQ colour #2dd4bf for the sum curve; points in band colour; band curves faint.
- Every edit = one undo step (`proc.getUndo()`), host gestures begin/end around drags.
- Hover card after 600 ms; no permanent explanatory text; paint without allocation; live data ≤ 30 Hz.
- Real-time safety unchanged (no audio-thread code in this plan except a possible EQ slope fix).
- Never edit through PowerShell Get/Set-Content.

## Review Focus

1. A macro (Presence/Warmth) created a role band that exists only in the effective settings: dragging / editing it must not
   double the macro offset → `eqedit::materialise` writes the band with gain 0 (macro adds its share); test in Task 1.
2. All 16 bands in use + double-click on empty space: nothing happens, no crash, no undo step → Task 1/2 test.
3. Deleting the selected band (or a role band while the macro is active): selection moves to a valid band or none → Task 3 test.
4. EQ 2..4 panels edit `..._2` IDs only, EQ 1 untouched → Task 2 test.
5. Parameter values beyond the ±18 dB display (Gain range ±30): the point stays inside the plot (clamped), dragging works → Task 2.

---

### Task 1: Band operations (`eqedit`)

**Files:** Create `Source/UI/EqPanel.h`, `Source/UI/EqPanel.cpp`; modify `CMakeLists.txt`; test `Tests/EqPanelTests.cpp` (new).

**Interfaces (Produces):**
```cpp
namespace velvox::ui::eqedit {
  struct Shown { dsp::EqBand band; bool used = false; bool macroOnly = false; float gainOffset = 0; };
  std::array<Shown, dsp::maxEqBands> shownBands (VelvoxAudioProcessor&, int instance);  // effective settings
  int  addBand (VelvoxAudioProcessor&, int instance, float freqHz, float gainDb);       // Bell Q 1 on; -1 when full
  void deleteBand (VelvoxAudioProcessor&, int instance, int band);
  void setType (..., int band, dsp::EqBandType); void setSlope (..., int band, int slopeIndex);
  void toggleOn (..., int band);  void resetBand (..., int band);   // gain 0 dB, Q 1 (Alt+click)
  bool materialise (VelvoxAudioProcessor&, int instance, int band); // macro-only band -> parameters
}
```
- [ ] Tests: add → band free→used, 1 undo step, undo frees it; 16 used → add returns -1, no step; delete resets all 7 to defaults
      (role band too), 1 step; setType/slope/toggle/reset each 1 step on instance 1 (`_2` IDs) only; materialise with Presence 100 %
      on a fresh state: effective EQ unchanged (response equal within 0.01 dB), band now has parameters (role air).
- [ ] Run → FAIL (no such file); implement; run → PASS; commit.

### Task 2: `EqGraph` view with gestures

**Files:** `Source/UI/EqPanel.{h,cpp}`, `Source/UI/ModuleViews.cpp` (eq case → `createEqGraph`, delete `EqView`), `Tests/EqPanelTests.cpp`,
`Tests/UiTests.cpp` (wheel test rewritten for the new view).

**Interfaces (Produces):** `class EqGraph : public ModuleView` with `plotArea()`, `pointFor (int band)`, `bandAt (Point)`, `getSelected()`,
`setSelected (int)`, `std::function<void (int)> onSelect`, `isHoverCardShown()`, `buildPointMenu (int band)`;
`std::unique_ptr<ModuleView> createEqGraph (VelvoxAudioProcessor&, const SpectrumSource*, int instance)`.
- [ ] Tests: double-click empty → Bell at that freq/gain (±2 %), selected; drag point → freq/gain follow, Shift drag moves 1/10, one undo
      step; wheel over a point → Q up/down, elsewhere nothing; Alt+click → gain 0; double-click on a point → on/off toggled; menu
      contains the 8 types, slopes only for cuts, on/off, delete; hover card only after 600 ms; EQ 2 only `_2` IDs.
- [ ] FAIL → implement → PASS → commit.

### Task 3: `EqPanel` (selected band row, Mehr) and old panel removal

**Files:** `Source/UI/EqPanel.{h,cpp}`, `Source/UI/DetailPanels.cpp` (old EqPanel out, factory → `createEqPanel`),
`Tests/PanelFrameTests.cpp`, `Tests/DesignTests.cpp`, `Source/UI/HelpContent.cpp` (EQ page).

**Interfaces:** `EqPanel::selectBand (int)`, `getSelectedBand()`, slot children bound to `eqBand{n}{On,Type,Freq,Gain,Q|Slope}`.
- [ ] Tests: selecting band n binds the five slots to band n's IDs (Slope instead of Q for cuts); no legacy fixed-band ID reachable;
      every eqBand On/Type/Freq/Gain/Q/Slope reachable through selection; Mix in Mehr; legacy button only while eqLegacyResponse
      is set, click clears it (one step); deleting the selected band selects another used band or none.
- [ ] FAIL → implement → PASS → commit.

### Task 4: Slope switch while playing (shell finding)

**Files:** `Tests/CompareTests.cpp` (unpin, new test), `Source/DSP/EqualizerModule.*` only if the test fails.
- [ ] Test: 0.3 sine 120 Hz through the plugin, legacy `hpfSlope` (bridge) and `eqBand1Slope` switched every 0.25 s → largest
      sample step ≤ max(0.12, 1.6 × steady). Run; fix if red; unpin the Vorher/Nachher test; commit.

### Task 5: Snapshots, full suite, ledger
- [ ] `--snapshot` overlay `eq` (a few user bands + a macro band) → `C:\Users\jakob\dev\velvox-wt\shots-eqpanel\` at 1000 and 889 px; look, iterate.
- [ ] Full suite green; ledger; commit.
