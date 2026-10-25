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
