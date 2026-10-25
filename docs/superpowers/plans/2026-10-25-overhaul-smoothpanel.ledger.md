# Ledger – overhaul track "smoothpanel" (Smooth + Pitch panels)

Baseline (feature/automix 39c0632): build ok. PanelFrameTests fail 27 checks on the base: 13 "sat: reaches …", 2 "pulse: reaches …"
(other tracks) and 12 "smooth: reaches smoothW{1..6}{Freq,Gain}" (this track).

Task 1: complete (Source/UI/SmoothPanel.{h,cpp}: smoothedit speed / begin-write-end / setSpeed / resetSpeed / resetPoint /
flattenAll / settings, SpeedKnob, the panel moved out of DetailPanels.cpp; Tests/SmoothPanelTests.cpp 67 checks, watched failing
first (compile error), passed on the first build)
- Ruling: Speed writes exactly `SmoothModule::speedToTimes (s)` into Attack + Release (both on the link); the shown value is the mean
  of where Attack and Release sit on the link (each inverted from speedToTimes' end points). Hand-set times off the link jump onto
  it at the first Speed move, inside the same undo step — why: the brief ("moving it writes smoothAttack+smoothRelease via
  speedToTimes") and the reverb track's identical off-path rule. Cost if wrong: a 1 px Speed nudge after hand-setting Attack and
  Release separately resets their ratio (undoable in one step).
- Ruling: Speed is a `Knob` built with a new unbound constructor (`Knob (label, accent)`, Widgets.h/.cpp) — same look, layout,
  hover card, drag / fine / wheel / typed value as every knob; the knob writes the two parameters itself from the slider's drag
  notifications (every JUCE Slider value path – drag, wheel, typed text – is wrapped in one, so each is one gesture = one step).
  Double-click / Alt+click = the Attack / Release defaults (5 / 80 ms); right-click = Standardwert, Wert eingeben … + panel items
  (no copy / paste: there is no parameter to copy). Cost if wrong: none known.
- Ruling: Speed gestures begin attack then release and end in reverse (UndoHistory folds the nested gesture: one step); the host
  sees one gesture group on both parameters.
- Ruling: DesignTests "every panel binds only existing parameters" skips unbound knobs' empty IDs (they still must carry the module
  colour) — the assertion was written when every knob had a parameter. Removed assertion: none; narrowed: empty IDs not looked up.
- Ruling: Mehr labels: Attack, Release, Mode (segments), Output, Low, High ("Focus Low/High" of 0.5 renamed to the limits the graph
  shows).
