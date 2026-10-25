# Headliner 0.6.1 chain: horizontal scrolling + module solo — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Items 3 and 4 of the 0.6.1 feedback spec. (3) The Advanced chain row scrolls horizontally with every usual input
(wheel, shift-wheel, trackpad, dragging the strip, a scroll thumb, edge arrows) and the selected tile is always scrolled into
view; nothing is unreachable at any window size. (4) One-click module solo: every other module bypassed, only the soloed
instance plays; a monitoring state (not a parameter, not saved, not an undo step), one solo at a time, click again releases,
click-free, latency unchanged, clearly shown on the chain, discoverable via the hover card; on the tile and in the panel header.

**Architecture:**
- `Source/DSP/Solo.h` (header-only, no CMake source edit): `dsp::applySolo (ChainSettings&, ModuleId, int instance)` — a pure
  settings transform that switches every other module instance off. The pitch shifter is the only module whose latency depends
  on its on-switch: when it is not soloed it stays "on" with mix 0 (its dry is latency-aligned), so the latency never changes.
  Every on/off already fades click-free (gates, module fades, smoothed returns, smoothed pitch mix).
- `VelvoxAudioProcessor`: `std::atomic<int> soloCode` (selection code, -1 none) + `getSolo / setSolo / toggleSolo / isSoloed`;
  `processBlock` applies `applySolo` after the macros. Not in the APVTS, not in the state, not in the undo history. Cleared when
  the soloed instance leaves the routing and when the editor closes.
- `ChainView`: solo "S" button on each tile (shows on hover, stays lit on the soloed tile), Alt+click on a tile, "Solo" in the
  tile menu; while solo is active the other tiles are dimmed and the soloed tile carries a solo-coloured frame + SOLO tag.
  Scrolling: notch-wheel steps of one tile, smooth trackpad deltas 1:1, edge arrow buttons, an overlay scroll thumb along the
  bottom edge (drag), drag on empty strip space and middle-button drag anywhere pan the row.
- `DetailPanel` header: a solo button left of "Presets" bound to the processor's solo state for the panel's instance.

**Tech Stack:** C++20, JUCE 8.0.9, VelvoxTests (juce::UnitTest), `VelvoxTests --snapshot`.

**Spec:** `docs/superpowers/specs/2026-10-25-headliner-0.6.1-feedback-design.md` items 3, 4; 0.6 spec general rules;
`C:\Users\jakob\dev\velvox-wt\AGENT_RULES.md`.

## Global Constraints

- No new parameters, no parameter ID changes; solo is never written into the state or the undo history.
- Real-time safe: the audio thread reads one atomic int and transforms a stack copy of the settings.
- Latency reported to the host and the chain latency are identical with and without solo.
- Old projects sound identical (solo defaults to none; nothing is saved).
- No permanent explanatory text; the hover card (tooltip) explains S / Alt+click.
- Chain row height (88) and the editor layout stay; everything fits in the existing row.

## Review Focus

1. Solo of a module while the pitch shifter is on: latency unchanged, pitch heard dry (mix 0). Test Task 2.
2. Solo toggled mid-stream across several modules: no discontinuity beyond the static states. Test Task 2.
3. Soloed instance removed from the chain / undo removes it: solo ends, nothing stays bypassed. Test Task 2.
4. Solo does not mark the project dirty / create an undo step / survive save+load. Test Task 2.
5. Drag on a tile still reorders; drag on empty strip space pans; clicks on the scroll thumb never hit a tile. Test Task 4.
6. 21-instance chain: every tile reachable with wheel, thumb and arrows; selected tile always fully visible. Test Task 4.

---

### Task 1: `dsp::applySolo`

**Files:** Create `Source/DSP/Solo.h`, `Tests/ChainSoloTests.cpp`; Modify `CMakeLists.txt` (own block for the test file).

**Interfaces:**
- Produces: `void dsp::applySolo (ChainSettings& s, ModuleId id, int instance) noexcept` — every module instance other than
  (id, instance) switched off (eq/comp/deess/delay/reverb on flags, `enabled` of sat/doubler/smash/choir/smooth/ride/chop/pulse);
  pitch not soloed: `pitch.mix = 0` (enabled unchanged); Check untouched (read-only); the soloed instance unchanged.
- Produces: `bool dsp::canSolo (ModuleId)` — false for Check (it does not change the sound).

- [ ] Step 1: tests — each other instance off, the soloed one untouched, pitch keeps `enabled` with mix 0, check untouched,
  instance k of a duplicated module solos only k.
- [ ] Step 2: run, watch fail (no header). Step 3: implement. Step 4: green. Commit.

### Task 2: Processor solo state

**Files:** Modify `Source/PluginProcessor.h/.cpp`, `Tests/ChainSoloTests.cpp`.

**Interfaces:**
- Produces: `int getSolo() const noexcept` (selection code or -1), `void setSolo (int code)` (message thread; -1 ends;
  ignored for codes not in the routing or Check), `bool toggleSolo (int code)` (returns the new solo state of that code),
  `bool isSoloed (int code) const noexcept`.
- Consumes: `dsp::applySolo` in `processBlock`.

- [ ] Step 1: tests — solo comp: output equals a chain where only comp is on (same input); latency equal with pitch on;
  solo switching mid-stream click-free; undo history unchanged and state XML has no solo; removing the soloed instance
  ends solo; toggle twice releases; a second solo replaces the first.
- [ ] Step 2: fail. Step 3: implement. Step 4: green. Commit.

### Task 3: Solo on the chain tiles and in the panel header

**Files:** Modify `Source/UI/ChainView.h/.cpp`, `Source/UI/DetailPanels.h/.cpp`, `Source/UI/Theme.h` (`colours::solo`),
`Source/PluginEditor.cpp` (destructor ends solo), tests in `Tests/ChainSoloTests.cpp`.

**Interfaces:**
- Produces: `juce::Rectangle<float> ChainView::getSoloBounds (ModuleId, int instance = 0)`; `TileAction::solo`;
  Alt+click on a tile toggles solo; click on the S toggles solo (no selection change, no drag).
- Produces: `juce::Button* DetailPanel::getSoloButton()` (nullptr for Output / Check).

- [ ] Step 1: tests — click on S solos, again releases; Alt+click on the tile body solos; tile menu item; tooltip mentions Solo;
  S hit area does not overlap the power switch; panel solo button follows/controls the processor state; editor destruction ends solo.
- [ ] Step 2: fail. Step 3: implement (draw: S on hover / when soloed, others dimmed, solo frame + tag). Step 4: green. Commit.

### Task 4: Chain scrolling

**Files:** Modify `Source/UI/ChainView.h/.cpp`, tests in `Tests/ChainSoloTests.cpp`.

**Interfaces:**
- Produces: `juce::Rectangle<float> getScrollThumbBounds()`, `getScrollTrackBounds()`, `getScrollArrowBounds (bool right)`
  (empty when not needed), `float scrollStep() const` (one tile).

- [ ] Step 1: tests — 21-instance chain: notch wheel (deltaY 0.117) moves about one tile; shift-wheel (deltaX) and trackpad
  (smooth) scroll; drag on the thumb scrolls proportionally; drag on empty strip space pans and does not reorder; drag on a tile
  still reorders; arrow click pages; selecting via the editor scrolls the tile fully into view at min and max editor size;
  at max scroll the add card and the last send tile are fully visible.
- [ ] Step 2: fail. Step 3: implement. Step 4: green. Commit.

### Task 5: Snapshots, full suite, ledger

- [ ] Snapshots (`shots-r2-chain`): full chain at 1000 and min width, scrolled to the end, solo active, hover S. Look at them.
- [ ] Full suite green; ledger complete; commit.
