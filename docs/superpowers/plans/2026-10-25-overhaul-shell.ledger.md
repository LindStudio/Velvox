# Ledger – overhaul/shell

Baseline before any change: full suite 11764 checks passed, 0 failed.

Task 1: complete (UndoHistory in Source/Presets, processor GestureListener, preset load + auto-mix + chain edits as steps;
UndoTests 34 checks; UI/UX/preset/routing/automix suites green)

- Ruling: the global history is a juce::UndoManager holding our own actions (one-parameter value steps for gestures, before/after
  ParameterSnapshot steps for transactions) instead of passing an UndoManager into the APVTS — why: the APVTS writes parameter values
  into its tree from a timer, so gesture boundaries and tree writes do not line up and steps would split or merge unpredictably;
  explicit before/after snapshots make undo exact and testable. Cost if wrong: bypass / input gain / level-match changes made inside
  a transaction are not restored by undoing it (ParameterSnapshot excludes them); their own gestures are still undoable.
- Ruling: undo/redo are refused while a gesture is open (a drag in progress) — why: never apply half a gesture. Cost: Ctrl+Z during
  a drag does nothing.
- Ruling: the auto-mix Vorher/Nachher switch never records a step (UndoHistory::beginSuppress), the auto-mix apply and "restore
  previous" are one step each — why: listening is not editing. Hook: AutoMixController::onApplying (merge hot-spot, small).
- Ruling: loading a project (setStateInformation) clears the history.

Task 2: complete (A/B slots removed from processor, TopBar and AutoMixController hooks; ABSlots of 0.5 read and dropped;
Tests/AbCompareTests.cpp replaced by Tests/CompareTests.cpp, 15 checks)

- Ruling: removed assertion family "A/B slots" (AbCompareTests: B copy of A, copy both directions, slots saved; RoutingTests
  "A/B slots include routing"; VelvoxTests "A/B comparison") — why: spec 2.2 removes the feature and its state. Kept intent:
  render-identity / no-click on switching and persistence now test the auto-mix Vorher/Nachher snapshot (CompareTests).
- Ruling: a 0.5 session with ABSlots loads the saved top-level parameters (that is the slot that was active when saved); the inactive
  slot is discarded and ABSlots is never written again — why: spec Q3 "load the active slot"; cost if wrong: the user loses the
  inactive slot of old projects (it was a compare scratch state).
- Finding for the eq track (not fixed here, DSP): switching hpfSlope while audio runs makes a step of ~0.16 on a 0.3 sine
  (EqualizerModule slope change without crossfade). Seen when Vorher/Nachher swaps a mix whose low-cut slope differs.
  CompareTests pins hpfSlope so the compare-switch test stays about the switch.

Task 3: complete (ValueText.{h,cpp}: parseValueText + gestures:: reset/step/copy/paste/menu; FineSlider binds a parameter:
fine 1/10, Alt+click and double-click default, own right-click menu, typed values with units; HoverInfo helper; HSlider;
Segments with double-click/Alt reset, wheel step, right-click menu, hover card; LookAndFeel::drawLinearSlider; tooltip delay 600.
GestureTests 66 checks)

- Ruling: copy/paste of values uses a plugin-internal clipboard shared by all Headliner instances of the process, not the system
  clipboard — why: copying a knob value must never wipe what the user copied elsewhere. Cost: values cannot be pasted into other
  apps (they are visible in the menu item text instead).
- Ruling: clicking on a horizontal slider's track does not jump the value (relative drag like the knobs) — why: one gesture
  model for all controls, no accidental jumps. Cost: no "click to position".
- Ruling: the hover card shows name, the parameter's short description and the value with unit; the old appended lines
  ("Doppelklick: Standardwert. Shift/Strg: fein." and the auto-mix role) are gone — why: spec 2.5/2.6 "name, short description,
  value"; gestures are explained in the help page "controls".
- Ruling: switches (Toggle) keep click-only behaviour — spec 2.6 lists knobs, sliders, graph handles and segment controls.

Task 4: complete (canvas 1000 x 600, min scale 8/9, max 2.0; TopBar rewritten: logo, ‹ name ▾ ›, SIMPEL | ADVANCED, AUTO MIX |
VOCAL CHECK, Bypass, ?; editor rewritten around mode + tab; Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z; VocalControlBar deleted)
Task 5: complete (SimpleView A3: dashed-before / filled-after spectrum, "Vocal analysieren", Vorher | Nachher after an analysis,
"Details" opens the auto-mix report overlay with Density + fair compare; six HSliders; CompactMeters IN/OUT + LUFS)
Task 6: complete (ChainView rewritten: tiles name → icon → on/off, no knobs, drag reorder, click select, menu via buildTileMenu /
performTileAction, "+" tile and "+" on gaps, SEND badge + send zone, OUTPUT tile; IN/OUT meters right of the chain)
Tests: ShellTests (59), DesignTests / UiTests / UxTests / AutoMixTests updated.
TDD note: the tests of tasks 4–6 were written after the restructure (the old tests could not compile against the new layout);
each new expectation was checked against the running code, not watched failing first.

- Ruling: view state = mode + tab. uiMode keeps its 0.5 values ("beginner" = SIMPEL, "pro" = ADVANCED), a new key uiTab
  ("automix"/"check") holds the tab; a 0.5 "check" loads as ADVANCED + VOCAL CHECK and is stored back in the 0.6 form — why: older
  Headliner builds still read uiMode; cost: none for audio (view only).
- Ruling: VOCAL CHECK shows the check panel over the whole body in both modes; AUTO MIX shows Simple or Advanced — why: the spec
  names two tabs plus a mode switch; this is the only combination where every view is reachable from both modes.
- Ruling: the old AUTO MIX overview (report steps, notes, Density, fair compare, restore) lives in a "Details" overlay opened from
  Simple mode (button next to "Vocal analysieren"); a step opens its module in Advanced — why: spec 2.3 gives Simple mode no room
  for it but Density must stay reachable; Density is also nowhere else. Cost: one extra click for the report.
- Ruling: removed AutoMixView's Vorher/Mix/Original buttons — Vorher/Nachher moved to Simple mode (spec Q3), "Original" was the
  global bypass that the header already has. Test family "compare buttons as segments" + "Original toggles bypass" removed.
- Ruling: IN/OUT meters in Advanced sit at the right end of the chain row (CompactMeters, stacked), so the module panel keeps the full
  width; the old big LevelMeters, clip-guard switch, mono button and correlation bar of the meter column are gone from the shell
  (clip guard is in the OUTPUT panel; mono and correlation move into the OUTPUT panel in Task 7).
- Ruling: modules added through the browser are always inserts (also delay/reverb, spec Q2; merged with mixlaw's identical line).
- Ruling: removed assertion families (DesignTests): 1384 × 1040 canvas + 3 tabs incl. CHAIN, chain column knobs (EQ "hpfFreq,highGain"
  etc.), column footer arrows, knobs vs. scroll arrows, VOCAL CHECK column UTF-8 note, Vocal Control bar macros/colours, min 80 %.
  Replaced by: 1000 × 600 + mode/tab, tile name→icon→power order and no knobs, tile menu actions, Simple-mode faders with the same
  ids/order/colours, min scale 8/9. UiTests: mode pill click → "Als Send/Als Insert" actions; hover remove button → menu action.
- Ruling: chain tile width 84–96 px (old min 104), row height 88 — why: 1000 px canvas.
- Ruling: the header logo shows icon + HEADLINER only ("VOCAL" and "BY LIND AUDIO" dropped) — why: 8 px text is below the 9 px
  minimum at 1000 px width.
- Fix found: the stored uiScale was overwritten before it was read (setResizeLimits on a 0 × 0 editor calls resized()); now read first.

Merge: feature/automix (mixlaw, c46dba8) merged after task 6 (conflicts: CMakeLists test list, StateKeys, PluginEditor
browser.onChoose comment, UiTests reverb insert/send — both intents kept). Coordinator items: duplicate copies the source mix law
(ChainView::duplicateInstance, ShellTests); browser inserts keep "defaults, off, insert, on" (ShellTests checks the true law);
"Alte Mix-Kurve – umstellen" in Mehr + panel menu (Task 7); wrong mix tooltips replaced in the UI (Task 7).

Task 7: complete (DetailPanel = common frame: header icon · NAME n · SEND/AUS badge · Presets · Mehr · ?; visual fills; one main row
of 3–5 controls; Mehr drawer from the right with small knobs, segments and switches; placeInVisual / layoutVisual / placeMeter for
extras; setMixKnob → "SEND"; PanelMenuHost items: Modul zurücksetzen, Modul-Preset speichern …, Alte Mix-Kurve – umstellen;
user module presets in UserSettings "userModulePresets". All 15 module panels + OUTPUT moved in. PanelFrameTests 1351 checks.)

- Ruling (main controls per panel, from spec §5 where the parameter exists today): EQ hpfFreq, lowGain, midFreq, midGain, highGain;
  Comp Threshold, Ratio, Attack, Release; De-Esser Frequency, Threshold, Range, Mode, Listen; Smooth Depth, Sharpness, Speed, Delta;
  Ride Ausgleich (rideAmount), Target, Speed; Sat Character, Drive, Mix (Output in Mehr — spec lists 2 main, 3 needed); Smash: big
  SMASH in the visual + Mix, Tone, Output; Doubler Width, Detune, Timing, Mix; Choir Voices, Detune, Width, Mix; Pitch Pitch, Formant,
  Fine (spec has 2; Fine added to reach 3); Delay note value, Sync, Feedback, Mix, Ping-Pong (Time knob in Mehr, enabled while not
  synced); Reverb Room, Size, Decay, Mix; Stutter Rate, Gate, Smooth, Pattern; Pulse Depth, Rate, Curve; Output Input Gain, Vocal
  Level (outputGain), Width, Level Match; Vocal Check mode, Start/Stop, Reset (check track redoes it). Everything else in Mehr.
- Ruling: OUTPUT now holds VOCAL LEVEL (outputGain) as a main control and the Mono-listen button + clip guard in Mehr — why: with the
  Vocal Control bar gone, Advanced mode would otherwise have no output level; mono/correlation lost their meter column.
- Ruling: Pulse curve points (pulseX1..6 / pulseY1..6) count as reachable through the curve editor in the visual (unchanged).
- Ruling: permanent hint lines removed from panels (subtitle line, Pitch latency note → Pitch knob hover card, Stutter note,
  Delay tempo/sidechain status line); the panel's one-line description is the panel tooltip.
- Ruling: module presets: factory + "Eigene" in the Presets menu; saving asks for a name through the editor's dialog
  (DetailPanel::askForName) and stores the values of the module's instance-1 IDs, so a preset applies to any instance.
- Ruling: mix tooltips are set by the panel (DetailPanel::updateSendState) instead of editing ParameterLayout.cpp: send =
  "Send-Pegel", true law insert = "Dry/Wet: 0 % nur Original, 100 % nur Effekt", legacy = "Alte Mix-Kurve"; Doubler Mix = "Anteil
  der Dopplungen". The ParameterLayout texts stay (they are also host-visible; owner: mixlaw/coordinator).
- Removed assertion family: DesignTests "Review: no control is laid out with zero size or outside its panel" moved into
  PanelFrameTests (now checked with the Mehr drawer closed and open, at the editor's panel size 984 × 442).

Task 8: complete (ModuleBrowser rewritten: 760 × 500 popup, search field, cards 108 × 76 grouped by Clean-up / Dynamics / Colour /
Width-Voices / Space / Rhythm / Analysis in a wrapping flow, hover/highlight description line, full modules greyed with
"voll" + reason, Up/Down (Left/Right while the search is empty) move the highlight, Enter adds it, Esc closes. UiTests updated.)
- Ruling: removed assertion family "category chips filter (All / Space / Analysis)" — spec 2.7 shows all groups at once with a
  search field; replaced by "every card sits in its category group, grouped order", hover text, Enter/Esc/highlight tests.

Task 9: complete (PresetBrowser overlay: categories Alle / factory / Eigene / Favoriten, search, star favourites in UserSettings
"presetFavourites" ("f:"/"u:" + name), hover/selection description, double-click / Enter load (one undo step), Esc, Speichern /
Speichern als … / Umbenennen … / Löschen; PresetManager: Entry::description, renameUserPreset. PresetBrowserTests 61 checks.)
TDD note: written test-first in this order, but the first run already passed (implementation was written in the same step).
- Ruling: the preset list takes the PresetManager by reference (editor passes the processor's), so tests use a temporary preset
  folder and never touch the user's Documents folder.
- Ruling: "Speichern" overwrites the loaded user preset; with a factory preset (or none) loaded it asks for a name (= Speichern als).
- Ruling: the old header save / preset-menu icons are gone; everything is in the list (spec 2.8).
