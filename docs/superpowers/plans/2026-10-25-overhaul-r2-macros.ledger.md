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
Task 3: complete (Source/Parameters/MacroBake.*: touchedParamIds, readSettings/readMacros on an eqbands::Access, writeMacroFields
  (values first, on-switches last), keepDry, bake; OfflineParameters::read no longer applies macros; tests red (header missing) ->
  green: bake on value maps of all 12 old cases == applyMacros within 1e-4 (the % round trip), positions kept; keepDry cases;
  "Headliner macro bake" 142 checks)
Task 3: Ruling: the 0.6.0 macro "lawMix" (a macro adds wet but the dry follows the user's own Mix) is expressed with existing
  parameters: an additive insert whose Mix the macros pushed past the point where the true law starts to fade the dry moves to the
  legacy law (dry at unity, out = dry + wet * mix - exactly what 0.6.0 played); a Doubler already above 50 % also gets its Dry
  level lowered by the dry it had (exact). Delay / Reverb inserts with their own Mix above 50 % have no Dry parameter: legacy law is
  the closest match (residual, see Task 5). The same rule runs for new Simple moves (Task 4), so WIDTH / SPACE never take the
  original away (mixlaw-track ruling kept). Cost if wrong: the panel shows "Alte Mix-Kurve - umstellen" on such an instance.
