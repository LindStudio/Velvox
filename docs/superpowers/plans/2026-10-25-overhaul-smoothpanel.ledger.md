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

Task 2: complete (SmoothGraph in Source/UI/SmoothPanel.cpp; createModuleView(smooth) -> createSmoothGraph, the 0.5 SmoothView deleted
from ModuleViews.cpp; SmoothPanelTests 141 checks, the graph tests written first and watched failing (compile), then 2 fails in the
test itself (dangling menu temporary) fixed; PanelFrameTests "smooth: reaches smoothW…" 12 -> 0)
- Ruling: reachability of graph handles = new virtual `ModuleView::getEditedParameters()` (ModuleViews.h); PanelFrameTests'
  collectParams also collects what the visual reports. The SmoothGraph lists its 12 weighting IDs + low + high, and
  SmoothPanelTests drag every kind of handle and check exactly those IDs change — why: the 12 points are graph handles (spec 2.6),
  twelve extra Mehr knobs would be the "grid of knobs" anti-goal; the test is not weakened (a parameter still counts only if a
  component of the panel claims and edits it; the Pulse / EQ name exemptions are untouched). Cost if wrong: a visual could claim
  a parameter it does not edit — guarded only by its own panel tests.
- Ruling: spectrum display: 240 log columns 20 Hz–20 kHz, per column the max of the bins inside (interpolated below one bin),
  −90…0 dB with a +3 dB/oct display tilt around 1 kHz (vocal highs readable, like soothe2's tilt); input rises fast / falls at 30 %
  per read, the removed part rises at once / falls 35 % per read; read ≤ 30 Hz. Pink = between input and output (input − reduction).
  Off: the module publishes nothing (DSP), so the input fades out by 3 dB per read and no pink is drawn; curve and handles dim
  but stay editable.
- Ruling: weighting axis ±12 dB (the parameter range) over the full plot height (12 px margin), spectrum underneath on its own
  scale (no dB labels for it) — only one labelled axis on screen. Points hollow when flat (0 dB), filled when they weight.
- Ruling: point gestures — drag x = freq, y = weight (relative, Shift/Ctrl 1/10, clamped, no hand-over to another point while
  dragging); double-click / Alt+click = the point's defaults (freq and gain, one step, a drag after the double-click is ignored);
  wheel = ±0.5 dB per notch (Shift 0.05), one step per wheel event (like the reverb dot); right-click = "Punkt n": Zurücksetzen,
  Alle flach + panel items (no typed value for a point: two values). Edges: drag x (clamped to the parameter range 100–8000 /
  1000–18000), double-click / Alt = default, wheel 1/24 oct, right-click = the standard value menu (Standardwert, Wert eingeben …
  with an inline field, copy / paste) + panel items. Hover card 600 ms ("PUNKT 3 · 1.00 kHz +3.0 dB", "LOW LIMIT · 800 Hz"),
  at once while dragging.
- Ruling: read-out top-left "MAX −x dB · f" (deepest current cut) only while something is removed — a value, no explanation.
