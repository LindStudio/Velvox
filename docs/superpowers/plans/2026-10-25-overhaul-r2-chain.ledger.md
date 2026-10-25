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

Task 4: complete (wheel / shift-wheel / tilt / trackpad, thumb strip under the tiles, edge arrows, strip drag (empty space,
Shift+drag, middle-drag), clicked or selected tiles slide fully into view clear of the arrows; 1091 checks green)

Ruling: "Drag of the strip" = dragging empty strip space (the send divider, space behind the tiles), Shift+drag or a middle-button
drag anywhere, plus the thumb — a plain drag on a tile keeps reordering — why: drag-to-reorder is an established 0.6 gesture and
a full chain has almost no empty space; the hover cards on the arrows / thumb name Shift+drag — cost if wrong: switch the pan
trigger in ChainView::mouseDown.

Ruling: While the row scrolls, tiles are 8 px lower (the tile's vertical layout scales) and the thumb sits in that strip — why:
an overlay thumb across the tile rims looked like a rendering glitch; the 88 px row and the editor layout stay unchanged — cost
if wrong: tiles keep 88 px and the thumb overlays (scrollBarH = 0 + track over the rims).

Ruling: Wheel speeds: a Windows notch (120/256) moves ~one tile (200 px per unit, glide); small steps (< 0.2, precision
touchpads) and smooth trackpad deltas (1:1 pixels, 512 per unit) follow without glide; macOS non-precise wheels 2400 per unit
(notch ~0.04) — why: JUCE's per-platform delta scales — cost if wrong: one constant each.

Ruling: isTileFullyVisible() now counts the edge arrows as covering, and scrollToShow keeps 28 px clear of them — why: a
selected tile under an arrow is not "nothing cut off" — cost if wrong: none (existing tests stay green).

Task 5: complete (help page "Organising modules" gains Solo + "More modules than fit"; full suite 31676 checks, 0 failed
(before the help-text commit; afterwards Chain solo / UX / Help / Design / Shell / Panel frame / UI groups re-run green);
Velvox_VST3 target builds (not installed). Snapshots in C:\Users\jakob\dev\velvox-wt\shots-r2-chain\ (looked at):
before-full-1000/min (0.6.0), full-start-1000, full-mid-min (889 px), full-end-1000, full-end-2000, full-mid-select-1000,
full-solo-sat2-1000, full-solo-min, zoom-*.png (chain row 2x))

Ruling: Hover-only S could not be captured in --snapshot (the hover fade runs on the VBlank, which does not tick offscreen);
its behaviour is covered by the click / tooltip tests — cost if wrong: a cosmetic issue in the hover fade only.
