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
