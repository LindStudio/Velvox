# Ledger – overhaul/satpanel (Saturation panel 0.6)

Baseline: feature/automix at dfcb278; full suite fails only the 13 "sat: reaches sat…" checks (new params without UI).

Task 1: complete (SatLink in Source/UI/SatPanel.*, Segments::setTilePainter, VocalChain::getSaturation, effectiveValueOf for
the band drives; SatPanelTests 23 checks)

## Rulings

- Ruling: Link works on gestures, relative to the gesture start: while a user gesture is open on one band drive, the other two are
  set to `dsp::linkedBandDrives (valuesAtGestureStart, band, newDb)` (dragging back returns them exactly; no drift). The dragged band
  itself always follows the user, also when the others are already stopped at a range end — why: writing the dragged parameter back
  from inside its own change callback fights the slider; cost if wrong: at a range end the differences between the bands shrink.
- Ruling: Link is implemented as a parameter listener (SatLink) owned by the panel, not inside the drive controls — why: it catches
  every UI edit (drag, wheel, typed value, reset, paste, the band handles in the strip) because they all run as gestures, while host
  automation (no gesture, possibly audio thread) never links. Cost: edits from a host's generic editor while the panel is open link
  too (they are gestures as well); with the panel closed nothing links.

Task 2: complete (SatView: log strip 20 Hz - 20 kHz, three band areas + two crossover handles as child components with the
shared gesture set, band on/off switches in the band headers, live drive meter, mini transfer curve + H2..H8 + THD; old SatView
removed from ModuleViews.cpp; SatPanelTests 61 checks. TDD note: the view tests were written first and failed to compile; the
first build after the implementation passed.)

- Ruling: the band "drive meter" shows band input RMS + the band's effective drive (dBFS, −42 … +6 dB), i.e. how hard the band
  hits its curve, rising from the band's floor (fast up, slow down) — why: the output level of a level-compensated band barely moves
  with drive, the input + drive is what the user changes. Cost: it is a level, not a measured THD.
- Ruling: the band areas are drag handles for their drive (vertical, 220 px = full range like every knob; click selects) — why:
  spec 2.6 graph handles; the strip is the biggest target for the main edit. Crossover handles stop at high / 1.5 (and low × 1.5)
  instead of pushing the other crossover — why: one gesture never writes a second parameter. Cost: to widen a narrow mid band the
  user moves both handles.
- Ruling: harmonics are shown for a −12 dBFS sine (harmonicProfile), with THD over H2..H8 — a typical vocal peak level; the old
  view used −6 dBFS. Cost: none (display only).

Task 3: complete (SatPanel in SatPanel.cpp, DetailPanels.cpp sat case -> createSatPanel; main row = character tiles (Segments with
tile painter, one group per band context in a stack), Multiband, Link; big Drive (dial 120) in the visual's right column; Mehr: Mix,
Output, Low / Mid, Mid / High; snapshot overlay "set:id=value,…[|overlay]"; help page "sat" describes the bands; SatPanelTests 103
checks; PanelFrameTests: all sat reachability checks green. TDD note: panel tests written first, failed to compile, passed on the
first build after the implementation.)

- Ruling: the big Drive sits in the visual's right column (like the SMASH control), not in the main row — why: the main row is 90 px
  high, a Drive there could not be bigger than every other main knob; the Sat 2 mockup gives Drive half the card. The main row holds
  the character tiles, Multiband and Link (3 controls). Cost: Drive is not in getMainControls() (it is in the visual, always visible).
- Ruling: the tiles read Warm / Tape / Tube / Hard (not "Crunch") — why: the parameter choice names (host automation lanes, hover card,
  typed values) say Hard; one name everywhere. Cost: none.
- Ruling: one tile group and one Drive knob per band context (single, Low, Mid, High), only the selected one visible — why: every
  control stays bound to exactly one parameter (gestures, undo, hover card, macro marker, reachability) instead of re-binding.
  The selected band is panel state only (default Mid, not saved). Link and the crossover knobs are dimmed (still usable) while
  multiband is off.
- Ruling: band drive knobs show macro markers (effectiveValueOf satLow/Mid/HighDrive added in PluginProcessor.cpp).

Snapshots: C:\Users\jakob\dev\velvox-wt\shots-satpanel\ sat-single.png, sat-multiband.png, sat-multiband-more.png,
sat-multiband-min.png (889 px = min scale).

Merge: feature/automix (eq + reverb panels, pitch, …) merged at 466e348; conflicts only in DetailPanels.cpp includes and the
TestMain.cpp snapshot comment (the merged branch has its own plain "set:" overlay; mine strips "set:…[|overlay]" first, so theirs
stays as a no-op fallback). Final full suite: 28697 checks passed, 14 failed — all 14 are "pulse: reaches pulse…" (2) and
"smooth: reaches smoothW…" (12), owned by other panel tracks; every sat check is green, SatPanelTests 103 / 103.
