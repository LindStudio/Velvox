# Headliner 0.6 – final fix pass (plan)

Branch `overhaul/final` from `feature/automix` fb30ccd. Input: the whole-branch review's findings (10 items).
Spec: `docs/superpowers/specs/2026-10-25-headliner-pro-overhaul-design.md`. Ledger: `2026-10-25-overhaul-final.ledger.md`.

## Global Constraints
- No DSP sound change except where an item requires it (item 3: no fade for a silent insert->send flip).
- Never rename/remove parameter IDs. Real-time safe audio thread. One commit per item, TDD (test fails first).
- Full suite 0 failed at the end; Velvox_VST3 builds.

## Review Focus
- UndoHistory coalescing must not merge steps across another step, an undo/redo or a transaction.
- Vorher/Nachher + undo: an edit in Vorher must leave a history whose undo returns exactly to the Nachher state.
- Bridge suspend per APVTS: no cross-instance leakage, still message-thread only.

## Tasks
1. UndoHistory: coalesce single-parameter steps on the same parameter within 400 ms (no other step between). Interface:
   `UndoHistory::setClockForTests (std::function<juce::uint32()>)`. Test: 30 Slider wheel notches -> 1 step; gaps -> 2 steps.
2. Mix law in undo: transaction compare includes `mixLaw`; `switchToTrueMixLaw` runs as one transaction.
3. SPACE insert->send flip: gesture steps also carry routing / mix-law before/after; VocalChain switches a pure insert<->send
   flip of a silent delay/reverb instantly (no routing fade). Test: no dip > 1 dB during a SPACE drag; undo restores routing.
4. Vorher/Nachher: an edit while Vorher is shown ends the compare; the step is rebased onto the Nachher state.
   Interface: `UndoHistory::compareBase` hook; `AutoMixController::endCompareForEdit()`.
5. `eqbands::ScopedBridgeSuspend (apvts)` per instance; `isActive (apvts)`. Test with two processors.
6. `git rm --cached` the refhost `__pycache__` files (+ .gitignore).
7. Combined latency test Sat + Smooth + Ride + Pitch + EQ at 44.1 / 192 kHz.
8. Guards: DynamicsHistory::reset private (prepare only); SeqLockValue single-writer jasserts in the sync functions.
9. UI: tab row follows the view (Advanced: "MODULE" | "VOCAL CHECK"); spectrum labels above the fill with a backing.
10. Docs/version: EQ bench on eqBand* IDs, QUALITY_BASELINE EQ block, VERSION 0.6.0, CHANGELOG, BENCHMARK, HANDOFF, ARCHITECTURE.
