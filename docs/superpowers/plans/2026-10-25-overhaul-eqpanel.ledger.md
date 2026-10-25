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

Task 3: complete (EqPanel: selected-band row of 5 re-bindable slots (Ein, Typ, Freq, Gain, Q / Slope for cuts) + band badge,
Mehr: Mix + 'Alte EQ-Kurve – umstellen' (only while eqLegacyResponse is set, one undo step); old fixed-band EqPanel removed
from DetailPanels.cpp, createDetailPanel(eq) -> createEqPanel; EQ help page rewritten; EqPanelTests 357 checks, PanelFrameTests
1413, DesignTests 941). TDD note: panel tests written first, implementation before the first run (not watched failing).
- Ruling: the main row is five slot components that rebind their control (Knob / Choice / Toggle from Widgets.h, unchanged) when
  the selection or the band's cut/non-cut kind changes; EqPanel lays the row out itself after DetailPanel::resized() — why: the
  frame's controls are bound to one fixed ID and the frame (DetailPanels.*, shared with every panel track) needs no change.
  Cost: the frame's main-knob size check does not see the slot knobs (EqPanelTests checks them instead).
- Ruling: Gain is shown dimmed and disabled for cuts / notch / band pass, Q for tilt (fixed row, no jumping layout).
- Ruling: the panel opens with the band selected last time in that EQ (per instance, this session), else the first band.
- Ruling: selecting a macro-only band in the panel writes it as parameters right away with the undo history suppressed (same
  sound; not an edit), so the row shows the band's real values — cost: the band then stays as a parameter band (0 dB base) when
  the macro goes back to 0.
- Ruling: role bands are not hidden or locked: they show a tag (PRES / WARM / AIR / LC / MUD / AUTO / HC) under the point and in
  the row badge; the role itself is not a control (it is the owner tag the macros use); delete resets it with the other six.
- Removed assertion families (deliberately): DesignTests 'EQ slopes and band types as segments' + 'EQ cut switches'
  (hpfSlope/lowType/highType segments, hpfOn/lpfOn toggles) -> 'EQ Mix' + 'EQ hides' the 0.5 IDs; PanelFrameTests exemption
  'eqBand* belong to the panels track' -> hidden 0.5 fixed-band IDs must NOT be reached, eqBand* / eqLegacyResponse are checked
  band by band in 'Headliner EQ panel'; UiTests EQ wheel test now drives the new graph (eqBand1Q_2 instead of midQ_2).

Task 5: complete (snapshots C:\Users\jakob\dev\velvox-wt\shots-eqpanel\ eq / eqmore / eqcard at 1000 and 889 px; iterated once:
own frequency labels under the plot, quieter spectrum (intensity 0.42), fainter band fills; docs/ARCHITECTURE.md EQ panel line)
Final: full suite 23758 checks passed, 0 failed.
Deferred: display range switch (+-12/24/30 like Pro-Q); per-band solo/listen; dynamic EQ (spec: not required).
