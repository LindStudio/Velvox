# Ledger: overhaul/r2-macros (0.6.1 item 2 - Simple mode sets the modules)

Baseline: full suite on cc26e0b: 30529 checks, 0 failed.
Task 1: complete (Tests/MacroBakeTests.cpp: 12 old cases - 0.5 project, fresh 0.6.0, sends made by SPACE, true-law inserts pushed
  over 50 %, true-law Doubler at 70 %, two factory presets, user-only EQ bands, the residual case, a 0.6.0 and a 0.5 user preset;
  renders of the unchanged 0.6.0 code written to the scratchpad (base060/old*.raw, env VELVOX_MACRO_BASELINE_WRITE), reproducible
  to -400 dBFS on the same build; the 0.6.0 test binary kept as scratchpad/VelvoxTests060.exe)
Task 1: Ruling: the comparison with the 0.6.0 build is env-gated (like the mixlaw track), the committed always-on null test compares
  against an in-build reference of the old semantics (dsp::applyMacros, kept unchanged) - other 0.6.1 tracks change EQ / Sat / auto-mix
  DSP in parallel, committed golden numbers would break at merge. Cost if wrong: a later change of applyMacros itself is not caught.
Task 2: complete (dsp::MacroId, MacroValues::get/set, dsp::applyMacroMove; tests red (MacroId / applyMacroMove missing) -> green:
  from 0 bit-identical to applyMacros of that macro alone for 4 module states x 6 macros x 3 positions; to == from is the identity;
  down moves; user bands untouched; "Velvox macros" 1116 checks)
Task 2: Ruling: a Simple move is RELATIVE (delta to - from on the module state where the move began), not absolute (module = f(slider))
  - only a relative move can coexist with Advanced edits (the user's own reverb Mix stays, SPACE adds / removes its share) and gives
  0.6.0's mapping exactly from 0. Cost if wrong: two users' mental models; a slider position and the module state can drift apart
  after Advanced edits (by design: the slider shows its last set value, spec allows "or their last set value").
Task 2: Ruling: down moves only reduce modules that are ON and never create bands; a mix that reaches 0 switches its module off (Mix
  stays at 0). Up moves switch on what they need, like 0.6.0. Cost if wrong: after SPACE up/down across separate gestures a reverb that
  was off with Mix 18 % ends off with Mix 0 % (inaudible either way; the gesture-anchored mapper of Task 4 restores the 18 % whenever
  nothing else changed in between).
