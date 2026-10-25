# Headliner 0.6 Shell Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rebuild the editor shell of Headliner 0.6: 1000 × 600 window, new header (SIMPEL | ADVANCED, AUTO MIX / VOCAL CHECK),
no A/B and no Vocal Control bar, Simple mode A3, a slim chain row, one common module-panel frame with a "Mehr" drawer, uniform
control gestures, hover-only text, new module browser and preset list, and one global undo history.

**Architecture:** The editor keeps its fixed design canvas (now `ui::baseWidth` 1000 × `ui::baseHeight` 600) scaled by an affine
transform. View state = mode (simple/advanced, stored in `uiMode` as "beginner"/"pro") + tab (automix/check, new key `uiTab`).
Gestures live in the shared widgets (`FineSlider`, `Knob`, new `HSlider`, `Segments`) so every control gets them. Module panels derive
from `DetailPanel`, which becomes the common frame (`header · visual · main row · Mehr drawer`); a panel only creates controls and
names the 3–5 main ones. Undo is a `velvox::UndoHistory` (juce::UndoManager + snapshot actions) owned by the processor.

**Tech Stack:** C++20, JUCE 8.0.9, juce::UnitTest runner `VelvoxTests`.

**Spec:** `docs/superpowers/specs/2026-10-25-headliner-pro-overhaul-design.md` (sections 2.1–2.8, 4, 8 Q3, Q8; window and tooltip
values in section 8).

## Global Constraints

- Window base 1000 × 600 (`ui::baseWidth/baseHeight`), keep the stored `uiScale`; min scale so 9 px text stays ≥ 8 px (8/9).
- Tooltip delay 600 ms.
- Never rename/remove parameter IDs or plugin codes; never edit `Source/DSP/*`, `ParameterLayout.cpp`, `SettingsReader.cpp`.
- Old projects must sound identical; UI state keys only added (`uiTab`), `uiMode` values stay "beginner"/"pro"("check" read).
- German labels: "Mehr", "Vocal analysieren", "SIMPEL", "ADVANCED", "Vorher", "Nachher", "Eigene", "Favoriten", "Als Send",
  "Als Insert"; parameter names stay English.
- Anti-goals: no panel with a grid of 8+ equal knobs; no permanent explanation text; no parameter unreachable.
- Real-time safety: nothing in this plan runs on the audio thread (undo listener only reacts to gestures on the message thread).
- Never edit through PowerShell Get/Set-Content.

## Review Focus

1. A drag in progress while the user presses Ctrl+Z: undo must not apply half a gesture → history records only on gesture end;
   test "undo during an open gesture does nothing harmful" in Task 1.
2. Sessions with ABSlots where slot B was active and slot A is different: the loaded sound is B (= the saved top-level state),
   and the A/B tree is not written again → Task 2 test with a hand-built blob.
3. Typed values with odd input ("2k", "-3 dB", "1/8", "50%", "  ", "abc") must never set NaN or out-of-range → Task 3 parser tests.
4. Tiny window (min scale) and narrow panels: no control of any panel with zero size or outside its panel, Mehr drawer controls
   inside the panel → Task 7 layout test at 984 × panel height.
5. Removing the module whose panel is open, or undoing its insertion, must leave a valid selection → Task 6 test.

---

### Task 1: Global undo history

**Files:**
- Create: `Source/Presets/UndoHistory.h`, `Source/Presets/UndoHistory.cpp`
- Modify: `Source/PluginProcessor.h/.cpp` (own it, listen to gestures), `Source/Presets/PresetManager.*` (load = one step),
  `Source/UI/ChainView.*` (routing edits through the history, remove its private stacks), `CMakeLists.txt`
- Test: `Tests/UndoTests.cpp` (new, in CMake test sources)

**Interfaces (Produces):**
```cpp
namespace velvox {
class UndoHistory : public juce::ChangeBroadcaster {
public:
    explicit UndoHistory (juce::AudioProcessorValueTreeState&);
    void beginTransaction (const juce::String& name);   // nested calls are counted; outermost records one step
    void endTransaction();
    struct ScopedTransaction { ScopedTransaction (UndoHistory&, const juce::String&); ~ScopedTransaction(); };
    template <typename Fn> void perform (const juce::String& name, Fn&& fn);  // begin + fn + end
    bool undo(); bool redo(); bool canUndo() const; bool canRedo() const;
    juce::String getUndoName() const; juce::String getRedoName() const;
    void clear();
    // called by the processor's AudioProcessorListener (message thread)
    void gestureBegan (int parameterIndex); void gestureEnded (int parameterIndex);
};
}
VelvoxAudioProcessor::getUndo() -> velvox::UndoHistory&
```
- Steps (TDD): failing tests in `UndoTests`: knob gesture = one step (undo restores, redo re-applies); 3 gestures = 3 steps;
  routing insert via ChainView = one step incl. values; preset load = one step; beginTransaction/endTransaction groups a module
  add + 5 parameter changes into one step; undo during an open gesture is ignored; applying an undo does not record a new step.
- Implement: actions store `ParameterSnapshot` before/after (full for transactions; single value for gestures), `applyTo` under a
  `restoring` flag; processor implements `juce::AudioProcessorListener::audioProcessorParameterChangeGestureBegin/End`.
- Commit.

### Task 2: Remove A/B, hidden auto-mix compare

**Files:** Modify `PluginProcessor.*` (drop slots/selectSlot/copy…; read and discard `ABSlots`), `AutoMixController` untouched
except wiring (processor stops setting `selectCompare`/`onMixApplied`), `TopBar.*` (A/B UI gone in Task 4).
Tests: replace `Tests/AbCompareTests.cpp` with "Headliner auto-mix compare" tests; remove the A/B blocks of `RoutingTests` and
`VelvoxTests` (Ruling).
- Tests: blob with ABSlots active = 1 loads the top-level (B) values and routing; saved again it contains no ABSlots; after an auto
  mix `showPrevious(true)` restores the before state and `showPrevious(false)` the mix; switching while audio runs never clicks.
- Commit.

### Task 3: Uniform gestures and value entry

**Files:** Create `Source/UI/ValueText.h/.cpp` (parser); modify `Widgets.h/.cpp` (`FineSlider`, `Knob`, new `HSlider`,
`ControlMenu`), `Segments.*`, `VelvoxLookAndFeel.*` (horizontal slider drawing), `Theme.h` (`motion::hoverInfoDelayMs = 600`).
Test: `Tests/GestureTests.cpp`.

**Interfaces:**
```cpp
namespace velvox::ui {
std::optional<float> parseValueText (const juce::String& text, const juce::RangedAudioParameter& p); // real units, clamped
struct ControlActions { static void showMenu (juce::Component& target, juce::RangedAudioParameter&, DetailPanelHooks* panel); };
class HSlider : public juce::Component { HSlider (APVTS&, const char* paramId, const juce::String& label, juce::Colour); FineSlider slider; };
juce::String& valueClipboard();   // copy/paste of values inside the plugin (mirrored to the system clipboard)
}
FineSlider::fineFactor = 10
```
- Tests: parser ("2k" → 2000 on a Hz param, "-3 dB", "1/8" on delayDivision choice, "50 %", "1.5 s" on ms param → 1500, garbage →
  nullopt, clamping); Shift drag = 1/10; Ctrl drag = 1/10; double-click and Alt+click reset; wheel steps; Segments double-click /
  Alt+click reset and wheel step; copy/paste between two knobs; HSlider has all of it.
- Commit.

### Task 4: Window, header, view state

**Files:** `Theme.h` (1000 × 600), `PluginEditor.*` (layout, mode/tab, keyboard undo, remove VocalControlBar),
`TopBar.*` (rewrite), delete `VocalControlBar.*` from CMake. Test: `Tests/ShellTests.cpp`.

**Interfaces:**
```cpp
enum class TopBar::Mode { simple, advanced }; enum class TopBar::Tab { autoMix, vocalCheck };
TopBar::setMode/selectMode, setTab/selectTab, onModeChange, onTabChange, getModeArea(), getTabArea(), getPresetArea()
Editor: isAdvanced(), getTab(), getSimpleView(), getChainView(), getDetail(), keyPressed (Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z)
uiMode: "beginner" -> simple, "pro" -> advanced, "check" -> advanced + vocal check; new key uiTab = "automix" | "check"
```
- Tests: canvas 1000 × 600; min width = ceil(1000 * 8/9); uiMode mapping (3 cases); header has no A/B; SIMPEL/ADVANCED switch
  stores the mode; tab VOCAL CHECK shows the check panel in either mode; Ctrl+Z undoes a gesture.
- Commit.

### Task 5: Simple mode A3

**Files:** Create `Source/UI/SimpleView.h/.cpp`; modify `Spectrum.*` (dashed pre curve option), `AutoMixView.*` (becomes the
"Mix-Bericht" overlay content: steps, Density, fairer Vergleich, restore; no compare buttons), `PluginEditor.*`.
- Tests: six HSliders bound to macroPresence, macroSpace, macroWidth, macroAggression, macroWarmth, outputGain in this order;
  "Vocal analysieren" calls the analysis; Vorher/Nachher toggle hidden before an analysis, visible after, toggles
  `AutoMix::isShowingPrevious`; Density and level match reachable through the "Bericht" overlay; meters present.
- Commit.

### Task 6: Chain row

**Files:** rewrite `ChainView.*` (tiles name → icon → on/off, drag, click, right-click menu, `+` tile, SEND badge, IN/OUT meters
at the right end are placed by the editor). Tests: update `UiTests`, `DesignTests` chain parts.
- Tests: tile layout order (name above icon above power); no Knob children; drag reorders; menu actions callable through
  `performTileAction (code, TileAction)`; "Als Send"/"Als Insert" only for delay/reverb; undo/redo in menu use `UndoHistory`;
  removing the selected instance selects a neighbour.
- Commit.

### Task 7: Common module panel frame

**Files:** `DetailPanels.h/.cpp` (frame + all panels), `ModuleViews.cpp` untouched except where a view must fit.
**Interfaces (for later tracks):**
```cpp
class DetailPanel {
protected:
    Knob& knob (const char* id, const juce::String& label);         // created in the Mehr drawer by default
    Toggle& toggle (...); Segments& segments (...); Choice& choice (...);
    void setMainControls (std::initializer_list<juce::Component*>); // 3–5, shown in the main row (large)
    virtual void layoutVisual (juce::Rectangle<int> visualArea);     // default: visual fills it; override to add meters etc.
    juce::Component& addVisualOverlay (std::unique_ptr<juce::Component>); // extra components inside the visual area
public:
    bool isMoreOpen() const; void setMoreOpen (bool);
    juce::Button& getMoreButton(); juce::Rectangle<int> getVisualArea() const; juce::Rectangle<int> getMainRowArea() const;
    std::vector<juce::Component*> getMainControls() const; std::vector<juce::Component*> getMoreControls() const;
    bool isSend() const;   // delay/reverb running as a send: header "SEND", Mix label "SEND"
};
```
- Tests: every panel has 3–5 main controls, every other bound control sits in Mehr, union = every parameter the old panel had
  (reachability list kept from DesignTests); no grid of ≥ 8 equal knobs in the main row; layout inside bounds with the drawer open
  and closed; delay as send → header text and Mix label "SEND".
- Commit.

### Task 8: Module browser

**Files:** `ModuleBrowser.*`. Categories: Clean-up (EQ, De-Esser, Smooth), Dynamics (Compressor, Vocal Smash, Level Ride),
Colour (Saturation), Width/Voices (Doubler, Choir, Pitch), Space (Delay, Reverb), Rhythm (Stutter, Pulse), Analysis (Vocal Check).
- Tests: grouping and order; search; Esc closes; Enter adds highlighted; arrow keys move the highlight; full module greyed with a
  reason and not choosable; hover sets description text.
- Commit.

### Task 9: Preset list

**Files:** Create `Source/UI/PresetBrowser.h/.cpp`; modify `PresetManager.*` (`Entry::description`, `renameUserPreset`),
`UserSettings.h` (`presetFavourites`), `TopBar.*` (`‹ name ▾ ›` opens it).
- Tests: categories = factory categories + "Eigene" + "Favoriten"; search filters; star persists in UserSettings; double-click /
  Enter loads (one undo step); save, save as, rename, delete user presets (temp preset dir).
- Commit.

### Task 10: Text, help, docs, screenshots, full suite

- Remove permanent hint lines (panel subtitles, Pitch/Stutter/Delay notes, Auto Mix intro texts → tooltips/help).
- Update `HelpContent.cpp`, `TutorialOverlay.cpp` (targets: presets, modes, tabs, help, chain, body), `docs/STYLEGUIDE.md`.
- `--snapshot` renders Simple mode and 3 Advanced panels into `C:\Users\jakob\dev\velvox-wt\shell-shots\`.
- Full suite green; ledger complete.
