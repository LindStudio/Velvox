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

Task 3: complete (Source/UI/PitchPanel.{h,cpp}: pitchnotes, PitchStage, the panel; the 0.5 PitchPanel (DetailPanels.cpp) and PitchView
(ModuleViews.cpp) deleted; PitchModule::getDetectedHz + VocalChain::getPitch; Tests/PitchPanelTests.cpp 30 checks, watched failing
first (compile: no getDetectedHz), passed on the first build)
- Ruling: minimal lock-free feed in PitchModule: `std::atomic<float> detectedHz` (relaxed) = fs / tracker period at the start of
  every processed chunk, 0 when unvoiced and after activate(); `VocalChain::getPitch() const` — why: the read-out needs the tracker
  the pitch track already runs (no second analysis in the UI); one relaxed store per chunk, no allocation, no lock. While Pitch is
  off the module is not processed, so the stage shows "–" whenever pitchOn is off. Cost if wrong: none for the sound (the DSP
  output is untouched; Velvox pitch quality 643 checks still green).
- Ruling: layout = the two big controls IN the visual (like Vocal Smash's SMASH): Semitones (dial 110–168 px, snaps through the
  parameter's 1 st interval, ±24 st = the parameter range, a tick per semitone around it, octaves ±12/±24 longer and labelled, the
  set value lit) left, Formant (dial ~70 % of it) right, the read-out IN note → OUT note (with Hz and cents) between them. Main row =
  Fine (Cents), Mix, Formanten erhalten; Mehr is then empty and its button hidden — why: the frame needs 3–5 main-row controls and
  the brief's "Main row: Semitones, Formant, Mix (+ Cents)" / "Mehr: Cents, Preserve, Mix" cannot both hold without showing a control
  twice; the user's mockup "Pitch 2" shows exactly this (two dials, "FINE · MIX", "◉ Formanten erhalten" under them). Cost if wrong:
  Fine/Mix/Preserve are one level more prominent than the spec's "Mehr" (nothing hidden, nothing duplicated).
- Ruling: no separate ±12 / ±24 range switch for the semitone dial — the dial spans the parameter's ±24 with the ±12 octave marks
  emphasised; drag 220 px = full range (4.6 px per semitone), Shift fine, wheel = 1 st per notch. Cost if wrong: coarse dragging
  for users who only use ±12 (wheel / typed values are exact).
- Ruling: read-out holds the last voiced note 250 ms (consonants / breaths don't flicker it), glides within ±0.04 oct (vibrato)
  and jumps to a new note; OUT = IN × 2^((st + ct/100)/12), dimmed with less Mix.
- Ruling: knob label "Semitones" (was "Pitch"); the latency note stays in its hover card.

Task 4: complete (snapshots in C:\Users\jakob\dev\velvox-wt\shots-smoothpanel\: smooth-1000.png, smooth-889.png (min scale),
smooth-mehr.png, smooth2-off.png (instance 2, off), pitch-1000.png (+7 st), pitch-889.png (-5 st, Formant +2.5), pitch-off.png;
reviewed: fixed the Speed dial not showing its arc (unbound knob now snaps its dial: `Knob::snapToValue`, protected); help pages
smooth / pitch rewritten for the new controls)
- Added: TestMain --snapshot overlay tokens joined with ';': "reso" (live vocal with a 3.15 kHz +14 dB resonance), "more" (Mehr open
  together with other tokens), "route:<routing, '|' for ','>".
Final: full suite 28737 checks passed, 15 failed — all 15 are the base's PanelFrameTests "sat: reaches satMultiband/…" (13) and
"pulse: reaches pulseShape/pulseAmount" (2), owned by the satpanel / timepanels tracks. The 12 "smooth: reaches smoothW…" are green.
Smooth panel suite 141/141, Pitch panel suite 30/30.
