# Ledger: r2-chain (0.6.1 items 3 + 4)

Task 1: complete (Source/DSP/Solo.h header-only `applySolo` / `canSolo`; Tests/ChainSoloTests.cpp; 936 checks green)

Ruling: Solo keeps the soloed instance's own settings; a soloed module that is switched off stays off (you hear the dry
signal) — why: the spec defines solo as "all other modules bypassed"; forcing a module on would change latency for the pitch
shifter and would make the tile's power switch lie — cost if wrong: one line in applySolo to force it on (except pitch).

Ruling: The pitch shifter, when not soloed, stays on with mix 0 instead of switching off — why: its latency follows its on-switch;
mix 0 gives its latency-aligned dry, so host latency never changes and the switch is a 30 ms mix fade — cost if wrong: pitch CPU
keeps running during a solo (a monitoring state, acceptable).

Ruling: Check cannot be soloed and is never bypassed by a solo — why: it only measures; bypassing it would only stop VOCAL CHECK
from listening — cost if wrong: none audible.

Ruling: Solo.h is header-only — why: no edit to the shared VELVOX_SOURCES list (merge hot-spot) — cost if wrong: none.
