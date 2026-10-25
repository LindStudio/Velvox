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

Task 2: complete (processor solo state: getSolo / setSolo / toggleSolo / isSoloed / isSoloActive, applied in processBlock after
the macros; 969 checks green. Click test: solo switching is exactly as smooth as making the same changes by hand
(max curvature 0.00079 with pitch on / 0.00026 without, identical to the by-hand runs); latency and structure switches unchanged)

Ruling: Solo ends when the soloed instance leaves the routing (checked in syncRoutingFromState) and when a state is loaded
(setStateInformation) — why: nothing may stay silently bypassed; a loaded project must sound as saved — cost if wrong: user
re-clicks S after an undo that removed and re-added the module.

Ruling: Solo is stored as a selection code in a std::atomic<int> on the processor (like monoMonitor) — why: one source for
tile, panel header and audio thread, lock-free (the editor-close rule is in Task 3) — cost if wrong: none.

Task 3: complete (tile S + Alt+click + "Solo (Alt+Klick)" in the tile menu + hover-card texts; soloed tile framed in solo
yellow, every other tile dimmed, OUTPUT shows a SOLO tag (click on it ends the solo); panel header S next to the power switch;
editor destructor ends the solo; snapshot tokens solo:/scroll:/hover: in --snapshot; 999 checks green)

Ruling: The tile's S sits bottom right on the power switch's line and appears on hover (always shown while soloed); Alt+click
anywhere on the tile solos too — why: the tile has a fixed 88 px layout (name / icon / power); bottom right is the only free
spot that is not the name, and Alt+click is the DAW-familiar quick gesture — cost if wrong: move one rectangle.

Ruling: Closing the editor ends the solo — why: spec item 1 (same monitoring rules) says closing the plugin ends it; a hidden
solo would leave the mix silently bypassed — cost if wrong: one line in the editor destructor.

Ruling: Solo colour is a new lemon yellow (0xffffd84d) defined in Source/UI/SoloButton.h (not Theme.h) — why: classic solo
colour, distinct from bypass amber; a new header keeps Theme.h (shared by every track) untouched — cost if wrong: move the
constant into Theme.h at merge time (the audition track's band solo may want the same colour).

Ruling: Panel header order is power · S · icon · NAME — why: on/off and solo belong together like a channel strip; the right
side holds Presets / Mehr / ? — cost if wrong: header layout lines in DetailPanel::resized/paint.
