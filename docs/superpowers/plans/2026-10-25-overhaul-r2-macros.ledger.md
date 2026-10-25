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
Task 4: complete (Source/Parameters/MacroMapper.*: positions -> module writes on the message thread, anchored, gestures for UI moves,
  plain writes for automation; processor: no applyMacros in processBlock, readEffectiveSettings() == readSettings(), effectiveValueOf
  and macroEngaged removed, SPACE insert -> send moved into the mapper; UndoHistory::isApplyingHistory + coalescesWithSideEffects;
  ChainView "engaged only" tiles and DetailPanels value markers removed; tests red (old semantics: reverb stays audible after Reverb
  off, compressor parameters untouched by PUNCH) -> green: user bug (-150 dB tail after switching off), one step per drag + exact
  undo/redo incl. routing, 5 wheel notches = 1 step, up-and-back exact in one drag and across drags for 5 sliders, an Advanced edit in
  between is kept, WIDTH keeps the dry via the additive law and restores the true law, automation from another thread maps with
  0 undo steps, preset / transaction positions are not mapped; adjusted: MacroTests (old offset semantics), DesignTests (old project
  values: macro set first), MixLawTests (law expectation), SaturationTests (applyMacroMove), EqPanelTests (no macro-only bands))
Task 4: Ruling: the six macro parameters stay registered and automatable; an automated macro maps into the module parameters
  (spec recommendation), relative to the last position, on the message thread (async after an audio-thread change), with plain
  setValueNotifyingHost (no gestures -> no undo step, no touch recording on the module lanes). Cost if wrong: not sample-accurate;
  in an offline bounce a fast macro ride can land a few ms late; if the host also automates the module parameter, the two fight.
Task 4: Ruling: the Simple slider shows its LAST SET VALUE (the macro parameter), not a value derived from the modules - a derived
  value is ambiguous for sliders that move 4-10 parameters. Cost if wrong: after an Advanced edit the slider can stand at 60 % while
  its modules were changed by hand.
Task 4: Ruling: "positions only, nothing mapped" while loading, undoing / redoing, in a transaction (presets, auto-mix, Vocal Check,
  assistant), during the auto-mix compare (suppress) and inside any eqbands::ScopedBridgeSuspend (every snapshot apply). Those
  paths restore whole states, the modules already hold the sound. Cost if wrong: code that sets a macro inside a transaction to
  get its sound gets nothing (none left: the assistant no longer suggests macro moves).
Task 4: Ruling: multi-parameter undo coalescing is enabled ONLY for steps whose gesture began on a macro (UndoHistory::
  coalescesWithSideEffects); the panels' multi-parameter wheel steps (reverb depth, graph points) stay one step per wheel event as
  their tests demand. Two quick drags of the same Simple slider within 400 ms become one step, like single knobs already did.
Task 4: Ruling: a module a slider switched on and joined the chain (Doubler, Saturation) stays in the chain when the slider goes back
  down (switched off, inaudible) - same as an Advanced on/off; SPACE's insert -> send switch stays too (0.6 behaviour).
Task 5: complete (stateVersion 4, snapshot format 3; setStateInformation bakes states < 4 before the routing migration (positions
  kept, laws adjusted); ParameterSnapshot::fromValueTree bakes format < 3 (presets, auto-mix Vorher/Nachher of old projects) incl. the
  0.5 EQ migration; null tests in-build: 11 old cases -128.2 ... -400 dBFS max sample difference vs the old macro layer (tolerance
  1e-5 = -100 dBFS), reload bakes nothing, positions kept; one-off vs the 0.6.0 build (VELVOX_MACRO_BASELINE): the same numbers
  (-128.2 ... -400 dBFS); residual case documented)
Task 5: Ruling (deviation from the spec text "zero the macros"): old macro values are KEPT as the sliders' positions after baking -
  they no longer add anything, and keeping them makes host automation of an old macro curve sound as before (it moves the modules
  relative to the saved position; zeroing would apply the saved offset twice when the automation plays) and the Simple slider shows
  where the user left it (zero would show SPACE 0 % while the baked reverb plays - the very confusion of the bug report). Cost if
  wrong: one line (set the six macros to 0 after macrobake::bake in setStateInformation / bakeOldMacros).
Task 5: Ruling: residual - a 0.6.0 project with a true-law Delay / Reverb INSERT whose own Mix is above 50 % plus SPACE cannot be
  expressed with parameters (0.6.0 froze its dry below unity, there is no dry parameter): it gets the additive law, the dry is up to
  +1.9 dB louder (case "Reverb insert 60 % + SPACE 100 %": max sample difference -27.4 dBFS). Only 0.6.0 projects (true law existed
  only there), noted in the CHANGELOG. Cost if wrong: that rare project sounds slightly drier-louder.
Task 6: complete (AutoMixPlanner note text, Assistant without macro suggestions (Presence -> high shelf), hover / help texts,
  ARCHITECTURE "Makros", CHANGELOG 0.6.1 incl. "Alte Projekte")
Task 6: Ruling: the auto-mix keeps resetting the six positions to 0 (its plan) and plans from the module values as they are - a
  Simple move before the analysis is a module edit like an Advanced one. Cost if wrong: an auto-mix result after Simple moves can
  differ from one on an untouched instance in parameters the plan does not set.
