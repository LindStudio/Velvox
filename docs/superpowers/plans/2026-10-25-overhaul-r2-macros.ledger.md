# Ledger: overhaul/r2-macros (0.6.1 item 2 - Simple mode sets the modules)

Baseline: full suite on cc26e0b: 30529 checks, 0 failed.
Task 1: complete (Tests/MacroBakeTests.cpp: 12 old cases - 0.5 project, fresh 0.6.0, sends made by SPACE, true-law inserts pushed
  over 50 %, true-law Doubler at 70 %, two factory presets, user-only EQ bands, the residual case, a 0.6.0 and a 0.5 user preset;
  renders of the unchanged 0.6.0 code written to the scratchpad (base060/old*.raw, env VELVOX_MACRO_BASELINE_WRITE), reproducible
  to -400 dBFS on the same build; the 0.6.0 test binary kept as scratchpad/VelvoxTests060.exe)
Task 1: Ruling: the comparison with the 0.6.0 build is env-gated (like the mixlaw track), the committed always-on null test compares
  against an in-build reference of the old semantics (dsp::applyMacros, kept unchanged) - other 0.6.1 tracks change EQ / Sat / auto-mix
  DSP in parallel, committed golden numbers would break at merge. Cost if wrong: a later change of applyMacros itself is not caught.
