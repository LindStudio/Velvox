# Ledger – overhaul track "reverbpanel"

Baseline (feature/automix ecd6d4c): build ok. PanelFrameTests already fail 15 checks on the base: "sat: reaches satMultiband …"
and "pulse: reaches pulseShape / pulseAmount" (new parameters of the sat / time DSP tracks; owned by the satpanel / timepanels
tracks, not touched here).

Task 1: complete (Source/UI/ReverbPanel.{h,cpp}: reverbedit depth / pointFor / begin-write-end / setDepth / resetDepth;
ReverbPanel moved out of DetailPanels.cpp; Tests/ReverbPanelTests.cpp 170 checks, watched failing first (link errors);
DesignTests "REVERB room list" now checks a dropdown)
- Ruling: the room type is a Choice (dropdown, wheel steps it) instead of 8 segments — why: spec 5.12 "room type dropdown"; the
  8 segment labels were truncated. Removed assertion: DesignTests "REVERB room list" as segments -> replaced by the same check on
  a Choice. Cost: one click more to switch the type.
- Ruling: the depth reads and writes the PARAMETER values (not the macro-effective ones) — why: the dot is a control; with the
  SPACE macro on, the effective mix differs and a drag would otherwise jump. Cost: with SPACE up the dot shows the user's own
  setting, not what the macro adds (the Mix knob shows the macro marker as before).
- Ruling: reset (double-click / Alt+click on the dot) = Mix, Pre-Delay, Size back to their parameter defaults (18 %, 20 ms, 60 %),
  one step — why: the depth has no default of its own; the defaults are what a fresh reverb sounds like. Cost: the reset point lies
  slightly off the path (the dot shows the nearest path depth).
- Ruling: Mehr order: Pre-Delay, Low Cut, High Cut, Damping, Bass Decay, Width / Early, Diffusion, Movement / four ducking knobs, duck
  source segments on top; "Modulation" renamed "Movement" on the knob (the parameter's own name "Reverb Movement").

Task 2: complete (ReverbRoom in Source/UI/ReverbPanel.cpp; createModuleView(reverb) -> createReverbRoom, the 0.5 ReverbView deleted
from ModuleViews.cpp; ReverbPanelTests 231 checks; the room tests were written before the room code and watched failing (link
errors), then passed on the first run)
- Ruling: the drag is relative (260 px = the whole path, Shift/Ctrl 1/10, up = back) and starts on the dot OR anywhere in the room —
  why: one handle in a big picture; the shell's rule "no click-to-position" (relative drag like the knobs) and a large target. The
  room's size changes while dragging (Size is on the path), so an absolute "dot under the mouse" mapping would chase a moving floor.
  Cost: the dot does not stay exactly under the pointer.
- Ruling: settings far from the path (hand-set) are shown at the nearest path depth; the first drag move writes the path values
  (Mix, Pre-Delay, Size jump onto the path inside the same undo step) — why: the dot is the path; the jump is undoable in one step.
  Cost: a drag of 1 px can change Pre-Delay a lot on such settings.
- Ruling: right-click on the room = Depth menu "Vorne / Mitte / Hinten" (depth 0.1 / 0.5 / 0.9), "Standardwerte" + the panel items;
  no "Wert eingeben / kopieren / einfügen" for the depth — why: the depth is not a parameter (no value field, no host value); its
  three parameters have their own controls (Size, Mix main; Pre-Delay in Mehr) with the full value menu. Cost: no typed depth.
- Ruling: room shape table (front width / height of the view at Size 100, back-wall scale): Room .60/.80/.54, Plate .80/.64/.58,
  Hall .82/.92/.34, Large Hall .94/.98/.25, Ambience .54/.70/.60, Atmosphere .96/1/.20 open, Cinematic .92/.90/.21 open,
  Ethereal .96/1/.23 open; Size 0 shrinks the opening to 72 % / 78 % and pulls the back wall in by .22. Open spaces draw ceiling
  and front pillars faintly. Decay hint = predictedRt60 at 24 log frequencies 100 Hz..10 kHz as a faint profile on the back wall
  (left = low), from the macro-effective settings (what is heard); the six octave values are exposed for tests.
- Ruling: live animation — dot glow follows the input level, onsets send floor ripples that live 0.8 x decay (0.4..5 s), quieter
  with less Mix and while ducked; repaint at most 30 Hz while animating, at once on parameter changes; the static room is cached as
  an image at the physical scale (rebuilt only when type / size / decay settings / on / bounds / scale change); paint builds no
  paths or strings (JUCE's own gradient / ellipse internals aside).
- Ruling: the ducking meter (GainReductionMeter, placeMeter) shows only while Ducking > 0 dB; otherwise the room has the full width.
- Ruling: DEPTH n % read-out top-left always; "Mix · Pre · Size" read-out only while dragging; the hover card (600 ms on the dot)
  shows "Depth", one description line and the three values.
- Added: TestMain --snapshot overlay "set:<id>=<value>,..." (parameters in real units) for the screenshots.
- Help page "reverb" rewritten for the 8 rooms, the depth dot and the main row / Mehr split (it still described Room / Plate only).
