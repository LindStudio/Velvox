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
