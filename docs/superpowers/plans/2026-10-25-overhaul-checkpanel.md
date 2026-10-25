# Vocal Check view (score + tiles) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The VOCAL CHECK tab of Headliner 0.6 as the user picked it ("Check 1 · score + tiles"): score ring, genre, "Alles anwenden",
one tile per finding with "Ignorieren" / "Anwenden", ignored findings collapsed and restorable; every apply one step of the global undo.

**Architecture:** A new `ui::VocalCheckView` (Source/UI/VocalCheckView.*) is the whole VOCAL CHECK tab body in Simple and Advanced; it
reads only `VelvoxAudioProcessor::getVocalCheck()` (controller of the check track) and the analyzer status. Tiles are child components
in a viewport, rebuilt only when the finding set changes (key string), moved/faded with the `ComponentAnimator`. The controller wraps
`applyResolvedFix` in an `UndoHistory` transaction (Host gets the history), so "Anwenden" and "Alles anwenden" are one step each in
`proc.getUndo()`. The Check module panel in the chain keeps its live picture and shows a compact summary (ring + count + link to the tab).

**Tech Stack:** C++17, JUCE 8.0.9, VelvoxTests (juce::UnitTest), `VelvoxTests --snapshot` for screenshots.

**Spec:** `docs/superpowers/specs/2026-10-25-headliner-pro-overhaul-design.md` §2.5, §2.11, §5.15, §7, §8 (Q8 undo).
Engine API: `docs/superpowers/plans/2026-10-25-overhaul-check.md` Task 6; shell frame: `docs/superpowers/plans/2026-10-25-overhaul-shell.*`.

## Global Constraints

- German labels: "Ignorieren", "Anwenden", "Alles anwenden", "x ignoriert"; texts of the tiles come from the engine (`problem`, `fix`).
- No permanent explanatory text (spec 2.5): short labels + values; explanations only as tooltips (600 ms).
- Design tokens only (`colours::*`, `moduleColour (ModuleId::check)` = #38BDF8, fonts `semiBold`/`monoFont`/`font`). Red (`warning`) only
  for real problems.
- Undo (Q8): one global history; "Anwenden"/"Alles anwenden" = one step each, restoring every parameter and the routing.
- No parameter IDs added or changed; old projects sound identical (the view never touches audio unless the user clicks).
- 60 fps-capable: no allocation in paint, rebuild of the tile list at most 10 Hz and only on change.
- Never edit files via PowerShell Get/Set-Content.

## Review Focus

1. Clicking "Anwenden" in continuous mode clears the analysis: the list must fall back to the "collecting" state without a crash
   (tiles deleted while their own click handler runs) — test clicks a tile button and keeps running the editor timer.
2. Ctrl+Z right after "Alles anwenden" must restore all values *and* the routing (EQ inserted) in one step — test in Task 2.
3. Check module off / not in the chain when the tab opens — the view must offer the power switch and show "—" (test in Task 3).
4. A finding without an applicable fix (clipping, "Spurpegel anheben") must show no "Anwenden" and not be counted by "Alles anwenden".
5. Window at minimum scale (8/9): tile texts and both buttons must not overlap (snapshot at the minimum width, checked by eye + bounds test).

---

### Task 1: Headroom text with a sign above 0 dBFS

**Files:** Modify `Source/Check/CheckRules.cpp/.h`; Test `Tests/VocalCheckFixTests.cpp`.

**Interfaces:** Produces `juce::String dbfsText (float dbfs)` in `velvox::check` ("+3", "−0,5", "0"; no unit; half-dB rounding like dbText).

- [ ] Test "Texts: dBFS values carry a sign above 0 dBFS": `dbfsText (3.0f) == "+3"`, `dbfsText (-0.5f) == "−0,5"`, `dbfsText (0.0f) == "0"`;
      `evaluate` on an `AnalysisResult` with `peakDb = 3`, enough material: finding "headroom" problem == "Kaum Headroom (+3 dBFS)".
- [ ] Run, watch it fail (dbfsText missing). Implement; headroom problem and detail use `dbfsText`. Run green. Commit.

### Task 2: Anwenden / Alles anwenden as one step of the global undo

**Files:** Modify `Source/Check/CheckFixPlanner.*`, `Source/Check/VocalCheckController.*`, `Source/PluginProcessor.h`; Test `Tests/VocalCheckFixTests.cpp`.

**Interfaces:**
- `void applyResolvedFix (APVTS&, const ResolvedFix&, UndoHistory* history = nullptr)` — with a history: one `ScopedTransaction (fix.name)`,
  writes with no `apvts.undoManager` involvement; without: the old behaviour (apvts.undoManager transaction, for the stand-alone test).
- `VocalCheckController::Host::history` (`UndoHistory*`, may be null); processor passes `&undoHistory`.
- `bool VocalCheckController::wasApplied (const juce::String& id) const` — applied on the current measurement (tile shows "Angewendet").

- [ ] Test "Controller: Anwenden is one step of the global undo": capture take, `apply ("rumble")`; `proc.getUndo().getNumSteps()` +1,
      undo name starts "Vocal Check"; `undo()` restores every parameter (ParameterSnapshot values equal to before) and the routing string.
- [ ] Test "Controller: Alles anwenden is one step of the global undo": same with `applyAll()` (> 1 fix), one undo restores all, redo re-applies.
- [ ] Watch both fail (no step recorded). Implement. Green. Commit.

### Task 3: VocalCheckView (score ring, genre, Alles anwenden, tiles, ignored row)

**Files:** Create `Source/UI/VocalCheckView.h/.cpp`; Modify `CMakeLists.txt`; Test `Tests/CheckPanelTests.cpp` (new, category "Headliner").

**Interfaces (Produces):**
```
namespace velvox::ui {
  juce::Colour checkScoreColour (int score);            // -1 -> textFaint, >= 80 ok, >= 60 amber, else warning
  juce::Colour checkSeverityColour (check::Severity);   // high warning, medium amber #F59E0B, low check colour
  juce::String checkPreviewText (VelvoxAudioProcessor&, const check::ResolvedFix&);   // "EQ: Band 3 Gain −3,0 dB\n…", "+ EQ in die Kette"
  class VocalCheckView : public juce::Component {
    explicit VocalCheckView (VelvoxAudioProcessor&);
    void refresh();                                    // editor timer; rebuilds at most every 6th call and only on change
    void refreshNow();                                 // immediate (tests, after clicks)
    // test access
    int getNumTiles() const; struct Tile; Tile* getTile (const juce::String& id);
    juce::Button& getApplyAllButton(); juce::Button& getIgnoredToggle(); juce::Button& getRestoreAllButton();
    juce::Button& getGenreButton (check::Genre); juce::String getScoreText() const; juce::String getStatusText() const;
    bool isIgnoredExpanded() const;
  };
  // Tile: getId(), getApplyButton(), getIgnoreButton(), getRestoreButton() (ignored rows), isIgnoredRow()
  // Compact summary for the Check module panel: ring + "n Befunde" + link button
  class CheckSummary : public juce::Component { explicit CheckSummary (VelvoxAudioProcessor&); void refresh();
    std::function<void()> onOpenTab; juce::Button& getOpenButton(); };
}
```
Layout (design units, body 984 × 540): header 104 high: ring Ø 84 left, title "VOCAL CHECK" + power, status line, analysis row
(mode segments Laufend|Capture bound to `checkMode`, Start/Stop, Reset); genre segments right ("Party | Pop | Akustik", caption "GENRE").
List bar: caption "BEFUNDE n", "Alles anwenden (n)" pill right. Tiles 56 high, gap 8: 3-px severity edge, problem (SemiBold 13.5),
fix (12.5, textDim), "Ignorieren" ghost + "Anwenden" filled (check colour) 96 × 28 right; applied → "✓ Angewendet" label.
Ignored: row "n ignoriert ▸" expands into compact rows (problem faint + "Wiederherstellen") + "Alle wiederherstellen".
Empty states centred in the list: off → "VOCAL CHECK ist aus" + "Einschalten"; not enough material → "Singe/spiele ab, um zu analysieren"
+ "x / 6 s"; no findings → "Keine Auffälligkeiten".

- [ ] Tests (Harness + capture take, view 984 × 540): tiles == visibleFindings in order; Anwenden visible iff `canApply`; clicking
      Anwenden applies (one undo step) and the tile shows applied; clicking Ignorieren moves it into the ignored row (count text
      "1 ignoriert"), expanding shows a restore row, restore brings the tile back; "Alles anwenden (n)" with n = applicable count, disabled
      at 0; genre buttons set `getGenre()`; score text "—" + status "Singe/spiele ab, um zu analysieren" without material; check off →
      "Einschalten" switches `checkOn`; tile buttons do not overlap the texts at width 984 and at the minimum scale's size.
- [ ] Watch fail (header missing). Implement. Green. Commit.

### Task 4: Shell integration, Check module panel summary, screenshots

**Files:** Modify `Source/PluginEditor.*`, `Source/UI/DetailPanels.*` (CheckPanel: FindingsList → CheckSummary; `DetailPanel::onShowVocalCheck`),
`Source/UI/HelpContent.cpp` (VOCAL CHECK page), `Tests/TestMain.cpp` (snapshot overlays "checktab", "checktab+ignored", "checktab+empty");
Test `Tests/CheckPanelTests.cpp`.

- [ ] Tests: editor with tab VOCAL CHECK shows `getCheckView()` visible in Simple and Advanced, `getDetail() == nullptr`; Advanced + Check
      selected: the CheckPanel's summary link switches the editor to the VOCAL CHECK tab; Ctrl+Z on the editor after a tile's Anwenden undoes it.
- [ ] Watch fail, implement, green, commit.
- [ ] Snapshots into `C:\Users\jakob\dev\velvox-wt\shots-checkpanel\` at width 1000 and at the minimum width (889), Simple + Advanced,
      with ignored expanded, empty states; look at each and iterate.
- [ ] Full suite `-Test "*"` green; ledger; commit.
