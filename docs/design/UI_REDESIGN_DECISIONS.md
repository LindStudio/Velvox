# UI-Redesign – Protokoll der Umsetzung (Ledger)

Kopie des Arbeitsprotokolls aus der Umsetzung des Plans docs/superpowers/plans/2026-10-24-headliner-ui-redesign.md.
Zeilen mit "Ruling:" sind Entscheidungen, die bei der Umsetzung getroffen wurden; "minor (deferred)" sind bewusst offen gelassene Kleinigkeiten.

```
# SDD ledger — plan: docs/superpowers/plans/2026-10-24-headliner-ui-redesign.md
Setup: Ruling: work directly on branch feature/automix (no extra worktree) — the branch is already a feature branch, the user works on it on two PCs via the Drive bundle — cost if wrong: none, history is linear and revertable.
Pre-flight: T1→T2,T4,T5,T7 tokens/fonts/NeonIcon/moduleColour consistent; T2→T4,T5,T6,T7 Knob::setDialSize + Segments consistent; T3 View enum → T8 consistent; T4 columnParams self-contained. No conflicts.
Task 1: Ruling: design tests in new Tests/DesignTests.cpp instead of UiTests.cpp — keeps the redesign's checks together — cost if wrong: none (one more file)
Task 1: Ruling: existing colour names kept (textDim = design textMuted #8A93A6, textFaint = design textDim #5B6478) — ~100 call sites keep their meaning — cost if wrong: some labels a shade off, fixed per panel in Task 5/6
Task 1: complete (commits c2ecbd3..e34fb82, tests: build/VelvoxTests_artefacts/Release/VelvoxTests.exe --test 'Headliner design' → ==== 41 checks passed, 0 failed ====)
Task 2: Ruling: knob block (dial+value+name) stays vertically centred in its cell; test uses a cell of exactly 84 px — matches HeadlinerKnob::preferredSize behaviour — cost if wrong: none
Task 2: Ruling: Segments map labels onto choices by name (fallback position) and show no selection for values without a segment — lets a 6-segment group sit on the 14-choice delayDivision — cost if wrong: an unusual division looks unselected (by design)
Task 2: complete (commits e34fb82..1d8baf7, tests: build/VelvoxTests_artefacts/Release/VelvoxTests.exe --test 'Headliner design' → ==== 59 checks passed, 0 failed ====)
Task 3: IN PROGRESS (uncommitted): RED test 'Design: 1384 x 1040 canvas, header tabs switch the three views' written in Tests/DesignTests.cpp; TopBar.h already has View enum/selectView/setView/tabs/help '?'; TopBar.cpp constructor, setView, paint (logo HEADLINER + VOCAL #A78BFA), resized (preset 290x36 left, tabs right) and PluginEditor (baseWidth 1384/baseHeight 1040 in Theme.h, uiMode beginner->autoMix, pro->chain, check->vocalCheck; remove setHasMix/onAnalyse/onModeChange users; AutoMixTests:982 use getAutoMixView().getAnalyseButton(); UxTests:184 use selectView(autoMix)) still to do.
Task 3: Ruling: uiMode values kept ('beginner' = AUTO MIX, 'pro' = CHAIN) plus new 'check' — old projects open in the view they had — cost if wrong: none
Task 3: Ruling: AUTO MIX and VOCAL CHECK use the whole chain+detail+meter area; meters/gains only in CHAIN — design shows meters next to the module panel only — cost if wrong: no level meter while in AUTO MIX
Task 3: Ruling: tests use isVisible instead of isShowing (no desktop peer in tests) — cost if wrong: none
Task 3: complete (commits 1d8baf7..9a295a9, tests: build/VelvoxTests_artefacts/Release/VelvoxTests.exe --test 'Headliner design' → ==== 71 checks passed, 0 failed ====)
Task 4: Ruling: sends stay in the same row behind a dashed divider (send zone = old 'lane', at least one column wide) — keeps send/insert drag and all lane tests — cost if wrong: slightly different from the design, which shows no sends
Task 4: Ruling: maxCardW 280 instead of 196 so few columns fill the width (design: equal widths over the row) — cost if wrong: very wide columns with 2-3 modules
Task 4: Ruling: undo/redo/chain menu sit under the round '+' in the add column — design has no place for them — cost if wrong: less discoverable
Task 4: Fixed flaky 'Help buttons open the right page' (waited 5 ms for an async click; now waits until the help is open) — the heavier editor made it fail under full-suite load
Task 4: complete (commits 9a295a9..25e0b33, tests: build/VelvoxTests_artefacts/Release/VelvoxTests.exe --test 'Headliner design' → ==== 132 checks passed, 0 failed ====)
Task 5: Ruling: controls the old panels had but the design omits (delay/reverb ducking, early, diffusion, bass decay, drive, into-reverb, smooth release/output, ride amount/phrase/transients, choir density, pulse attack/release/BPM) stay as extra knobs in the grid — 'only existing params' + nothing lost — cost if wrong: panels busier than the canvas
Task 5: Ruling: LEVEL RIDE Range = two knobs Max Boost / Max Cut (one design knob maps to two parameters) — cost if wrong: none
Task 5: Ruling: STUTTER pattern stays a dropdown (many patterns), rate as segments — cost if wrong: none
Task 5: Ruling: panel layout = visualizer top left, side controls right, knob grid below (canvas image not available, derived from the spec text) — cost if wrong: layout differs from the canvas
Task 5: complete (commits 25e0b33..b896bec, tests: build/VelvoxTests_artefacts/Release/VelvoxTests.exe --test 'Headliner design' → ==== 401 checks passed, 0 failed ====)
Task 6: Ruling: input gain moved into the OUTPUT panel as well (spec lists it there); the meter column keeps its gain knobs until Task 7 decides — cost if wrong: duplicate control
Task 6: complete (commits b896bec..d752402, tests: build/VelvoxTests_artefacts/Release/VelvoxTests.exe --test 'Headliner design' → ==== 448 checks passed, 0 failed ====)
Task 7: Ruling: input/output gain knobs removed from the meter column (output = VOCAL LEVEL in the bar, input gain in the OUTPUT panel) — design shows only meters there — cost if wrong: one more click to reach input gain
Task 7: Ruling: Vocal Control visible in all three views (it sits below the switching area) — cost if wrong: duplicates the AUTO MIX view's Feinschliff macros until Task 8
Task 7: complete (commits d752402..8ef4385, tests: build/VelvoxTests_artefacts/Release/VelvoxTests.exe --test 'Headliner design' → ==== 458 checks passed, 0 failed ====)
Task 8: Ruling: AUTO MIX keeps only Density + fair compare as fine-tuning (the five other macros are in VOCAL CONTROL); Zielstil/Tempo/Tonart/Trefferquote not built (no backend, listed in DESIGN_MAPPING) — cost if wrong: none
Task 8: Ruling: analyse button text 'Vocal analysieren' (design wording) — cost if wrong: none
Task 8: complete (commits 8ef4385..31cf6cb, tests: build/VelvoxTests_artefacts/Release/VelvoxTests.exe --test 'Headliner design' → ==== 466 checks passed, 0 failed ====)
Task 9: Ruling: minimum window scale 0.7 instead of the plan's 0.6 — at 0.6 the design's 9 px knob names are ~5 px and unreadable (Review Focus #1) — cost if wrong: the window cannot get as small as planned
Task 9: complete (commits 31cf6cb..9d938c3, tests: build/VelvoxTests_artefacts/Release/VelvoxTests.exe --test 'Headliner design' → ==== 818 checks passed, 0 failed ====)
Final review: fresh reviewer (opus) — verdict "With fixes": 1 Critical, 6 Important, 10 Minor.
Final: Ruling: Task 2 ruling "unusual divisions look unselected" reversed — reviewer right, plan required a "…" menu; it hid 8 delay divisions and 6 pulse rates — cost if wrong: none
Final: fixed hidden delay/pulse values — "Review: every delay division and pulse rate stays reachable (overflow segment)" RED(compile)→GREEN, suite 11764/11764
Final: fixed old projects opening taller than the screen — "Review: window size is stored as a scale..." RED→GREEN, suite 11764/11764
Final: fixed knobs covering the scroll arrows — "Review: column knobs never cover the scroll arrows..." RED→GREEN, suite 11764/11764
Final: fixed wheel changing values over the chain — same test (wheel) RED→GREEN, suite 11764/11764
Final: fixed delay status line with zero height — "Review: no control is laid out with zero size or outside its panel" RED→GREEN, suite 11764/11764
Final: fixed garbled VOCAL CHECK column text — "Review: VOCAL CHECK column text is proper UTF-8" RED→GREEN, suite 11764/11764
Final: fixed text too small at the minimum scale — min scale 0.8 (test asserts it) RED→GREEN, suite 11764/11764
Final: minor (deferred): bypassed panel dims to 55 % instead of 30 % (header icon 40 %)
Final: minor (deferred): help mentions a FIX button per finding; the findings list has "Open <module>" links
Final: minor (deferred): ChainView tooltip still says "Drag Delay or Reverb onto the lower lane"
Final: minor (deferred): ChainView repaints fully on every activity change and mouse move (redraw cost)
Final: minor (deferred): partly scrolled-in column shows empty knob slots; hint toast and drag ghost drawn under the knobs
Final: minor (deferred): long names cut at 104 px columns; column says CHOP, panel says Stutter
Final: minor (deferred): Source/UI/NeonIcon.h unused (columns draw icons directly)
Final: minor (deferred): OUTPUT panel side area 4 px too short (Clip Guard switch 22 px high)
Final: minor (deferred): old-project test uses a state saved by the current build, not a real 0.4 blob
Final: minor (deferred): VOCAL CONTROL gain reduction reads only compressor instance 1
```
