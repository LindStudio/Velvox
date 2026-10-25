# Ledger – overhaul/timepanels (Delay, Pulse, Stutter panels)

Baseline (dfcb278, feature/automix after the time merge): full suite 26546 checks passed, 15 failed – 13 × "sat: reaches satMultiband …
satLink" (the Saturation multiband parameters, owned by the satpanel track) and 2 × "pulse: reaches pulseShape / pulseAmount" (this track).

Task 1: complete (EchoMapView + DelayPanel in Source/UI/TimePanels.*, old DelayView / DelayPanel removed; Tests/TimePanelTests.cpp
"Headliner time panels" 245 checks green; panel frame suite unchanged (only the known sat/pulse reach failures).
TDD note: the tests were written before the implementation, but the first red run was a build failure (no TimePanels.cpp / exe locked by
the baseline run), not a runtime failure; afterwards a mutation run (echo-n divisor removed, feedback direction inverted) made 5 checks
fail as expected.)

## Rulings

- Ruling: all three panels and their views live in one new unit Source/UI/TimePanels.{h,cpp}, registered in an own CMake block at the
  end; DetailPanels.cpp / ModuleViews.cpp only lose the old classes and route the three cases to createTimeModulePanel / View.
  — Several panel tracks edit those two files in parallel; small hunks merge cleanly. — Cost if wrong: none.
- Ruling: any echo can be grabbed (not only the first): it follows the pointer, so the time moves by 1/n of the pointer for echo n.
  Vertical = the height ratio from one repeat to the next (200 px = ratio 1.0) → DelayModule::feedbackForRatio. — The spec says
  "echoes … can be dragged"; the ratio is exactly what the bars show. — Cost if wrong: none (first echo behaves as specified).
- Ruling: synced drags snap to the nearest note value only while the host sends a tempo; without host tempo the delay runs on the ms
  time (DSP behaviour), so the drag edits ms. Shift on the grid leaves it (Sync off, free ms; Shift then is not "fine"); in free mode
  Shift/Ctrl = 1/10. — Matches what is heard. — Cost if wrong: Shift has two meanings depending on the mode.
- Ruling: bar height = level on a dB scale (−48 … 0 dB), not linear. — Linear makes everything after the third repeat invisible at
  usual feedback. — Cost if wrong: taste.
- Ruling: live part = each echo lights up while the vocal of one echo time ago was loud (input level history), ducking pushes the bars
  down, the read-out shows the gliding time (displayTimeMs) + feedback, a playhead loops through the window. — "live playhead/time".
- Ruling: main row = note value (segments, Sync on) or Time ms (knob, Sync off) + Sync, Feedback, Mix, Ping-Pong; the inactive one
  waits in Mehr (small). Note segments 1/4, 1/8D, 1/8, 1/16 + "…" (all 14 values). — Spec "note value (sync) / ms". 
  — Cost: with Mehr open and Sync on, "Ping-Pong" is clipped by the frame's main-row shrink (frame layout, shell-owned).
- Ruling: snapshot overlay "set:<id>=<value>,…[,more]" in Tests/TestMain.cpp to render panels with modules on. — Needed for screenshots.

Task 2: complete (PulseCurveView, PulseShapeTiles, PulsePanel; old PulseView / PulsePanel removed; "Headliner time panels" 341 checks
green; panel frame: "pulse: reaches pulseShape / pulseAmount" fixed – it failed for pulseShape until the reach detection below, only the
13 sat reach failures (satpanel track) remain. First run red: the drawn-gain check failed at 100 % amount (0.012 > 0.01, table 240 →
512 points).)

- Ruling: Pulse layout = big Amount (dial 112) in the visual's left column, the gain curve beside it, the 5 tiles (Custom, Classic,
  Punch, Swell, Gate) in a row under the curve; main row = Rate (1/1, 1/2, 1/4, 1/8, 1/16 + "…" for triplets/dotted), Smooth, Mix;
  Mehr = Depth, Attack, Release, Tension, Interpolation, Manual BPM. — Spec: "one big Amount, 4 tiles, rate buttons"; the frame needs
  3 – 5 main controls, so Smooth (the anti-click control) and Mix (Kickstart has one) came up from the spec's Mehr list; the big
  Amount sits in the visual like SMASH. — Cost if wrong: two knobs to move back to Mehr.
- Ruling: Custom reveals the 6-point editor in the visual itself (points drawn and draggable only while Custom is selected), not in
  Mehr. — The curve is the visual; a second curve in the drawer would duplicate it. Curated shapes ignore points, so dragging them
  would do nothing audible. — Cost if wrong: none.
- Ruling: the curve view draws what is processed (PulseModule::gainAt: shape, amount, depth – filled), plus the raw shape as a
  fine grey line on which the Custom points sit; playhead at displayPhase with a dot at the applied gain (displayGain).
- Ruling: PanelFrameTests' reach detection also accepts a control's "paramId" component property (PulseShapeTiles sets it). —
  The tiles are a graph control, not a Knob/Segments/Toggle/Choice; the property names the edited parameter. Nothing excluded,
  the tile behaviour itself is pinned in TimePanelTests. — Cost if wrong: a control could claim a parameter it does not edit (review).
