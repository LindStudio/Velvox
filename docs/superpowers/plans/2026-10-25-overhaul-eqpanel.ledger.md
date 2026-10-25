# Ledger – overhaul track "eqpanel"

Baseline: build of overhaul/eqpanel at 63bdf92 ok.

Task 4: complete (shell finding "hpfSlope switch jumps"): CompareTests "Switching the low-cut slope while playing never jumps
(legacy hpfSlope and band slope)" — 0.3 sine 130 Hz, slope switched 11 times: largest step 0.0051 / 0.0052 against a steady
0.0048 / 0.0051, i.e. no jump. The 0.6 EqualizerModule already crossfades topology changes (30 ms out / switch / in); the finding
was made on the 0.5 EQ before the EQ merge. No DSP change. The test was not watched failing on the current code (the fix
pre-existed); mutation check instead: adopting a slope change instantly makes it fail with steps 0.51 / 0.49. The hpfSlope pin
in "Switching Vorher / Nachher while playing never clicks" is removed (still green: 0.0102 vs steady 0.0072).

Task 1: complete (Source/UI/EqPanel.{h,cpp} eqedit: shownBands / materialise / add / delete / type / slope / on / reset, one undo
step each; Tests/EqPanelTests.cpp 87 checks, watched failing (link errors) first)
- Ruling: a band a macro created only in the effective settings (Presence/Warmth on a state without that role band) is shown as a
  normal point; before the first edit it is written as parameters with gain 0 dB, on, its role, type/freq/Q of the macro default
  (materialise, inside the same undo step) — why: the macro then adds its share to the 0 dB base exactly as before, so the sound
  does not change and the offset is never doubled. Cost if wrong: after the edit the band exists as parameters also when the
  macro goes back to 0 (a flat 0 dB shelf, inaudible).
- Ruling: a new point goes into the first slot that is free in the parameters AND in the effective settings, so a macro band keeps
  its slot (and colour); only when no such slot is left it takes a macro band's slot.
- Ruling: Alt+click reset = gain 0 dB, Q 1 and slope 24 dB/oct (the parameter defaults); type, frequency and on stay.

Task 2: complete (EqGraph in Source/UI/EqPanel.cpp; createModuleView(eq) -> createEqGraph, the 0.5 EqView deleted from
ModuleViews.cpp; EqPanelTests now 163 checks; UiTests wheel test rewritten for the graph on EQ 2 (eqBand1Q_2), 199 checks)
TDD note: the graph tests were written before the graph code, but the code was written before the first run, so they were not
watched failing (the build would only have failed to link).
- Ruling (spec 2.6 vs Pro-Q): double-click on a point = band on/off (Pro-Q's double-click on a band), Alt+click on a point = reset
  (gain 0 dB, Q 1, slope 24) — why: the brief asks to follow Pro-Q for points, spec 2.6 names Alt+click = reset; a point has no
  single 'default' (its frequency is the reason it exists), so the double-click does what Pro-Q users expect. Cost if wrong: a
  user expecting double-click = reset finds the band switched off (it stays visible and comes back with another double-click).
- Ruling: cut, notch and band-pass points sit on the 0 dB line and drag in frequency only (they have no gain; cut Q is ignored by
  the DSP); tilt points sit at their gain. The wheel over a cut steps the slope (one step per notch, accumulated for trackpads);
  over tilt it scrolls the parent.
- Ruling: shown range +-18 dB fixed (0.5 scale); bands beyond it are drawn at the edge and keep their value until dragged (the
  value field takes up to +-30 dB). Cost: no display-range switch like Pro-Q.
- Ruling: a click on empty space deselects (Pro-Q); right-click on empty space opens the panel menu (module reset / presets).
- Ruling: the hover card is drawn by the graph itself (one card per point, values built in update(), not in paint) after
  motion::hoverInfoDelayMs = 600 ms; while dragging it shows at once as a read-out.
- Ruling: the sum curve shows the EQ Mix as the magnitude approximation |1 + (H - 1) * mix| like the 0.5 curve (phase ignored).
