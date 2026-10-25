# Ledger: r2-audition (0.6.1 items 1, 5, 6)

Baseline (cc26e0b, before any change): full suite 30529 checks passed, 0 failed.

Task 1: complete (dsp::BandAudition in Source/DSP/Audition.h; EqualizerModule::setAuditionBand / processAudition;
VocalChain eq slot runs the audition after the gate from the gate's captured input; getEqualizer / non-const
getSaturation; ModuleGate::dryChannels. Tests "Velvox audition: EQ band solo" 25 checks. Mutation check: a hard switch
(fade 20 us) gives 3.49 x the steady second difference and fails; with the 15 ms fade 0.57 x; threshold 1.5 x.
Level match: noise 0.05 dB, synthetic vocal 0.16 dB off the input level.)

Ruling: the audition filters the EQ's INPUT (pre-EQ), not its output — the spec says "no gain applied"; hearing the input
region is what finding a resonance needs, and a cut band stays audible in solo. — Cost if wrong: one line (feed io instead
of the gate's dry).
Ruling: the audition runs in the chain after the EQ's gate (Mix / on-off blend), so at Mix < 100 % or with the EQ switched
off the solo still plays only the region (with the EQ off, from the unprocessed input). — Cost if wrong: small; the call
moves into the module.
Ruling: regions: bell / notch / band pass = 2nd-order band pass with the band's Q (Q clamped >= 0.2); low shelf / low cut =
24 dB/oct low pass at the band frequency (what the band acts on / removes); high shelf / high cut = 24 dB/oct high pass;
tilt = broad band pass (Q 0.4) at the pivot. — Cost if wrong: a table change in auditionRegionFor.
Ruling: level match = slow RMS follower (300 ms) of input vs. region, gain smoothed (100 ms), limited to [-6, +24] dB,
start value from the region's share of an 8-octave pink spectrum; held while the input is below -90 dBFS. — Cost if
wrong: soloed narrow regions of near-silent bands are lifted up to +24 dB (that is the point of level matching).
Ruling: switching between bands or to a band of another shape fades the solo out (15 ms) and the new one in (15 ms);
frequency / Q of the soloed band glide (20 ms) while dragged. — Cost if wrong: a ~30 ms dip when hopping bands.
Ruling: the solo of an off band is allowed (you hear its region); a free slot is no solo. — Cost if wrong: none.

Task 2: complete (SaturationModule::setSoloBand / getSoloBand / isSoloAudible; the soloed band's dry share + output
through the same Mix / Output law with its own DC blocker; 15 ms fades; "Velvox audition: Saturation band solo" 16 checks.
The three solos sum to the normal output at -147 dB (Mix 100 % and 40 %); switching 0.57 x steady second difference.
Note: the test file was written before the implementation, its first build failed on the missing API (no separate run).
First test tones (100 Hz / 1 kHz / 8 kHz) were wrong: 8 kHz is a harmonic of the driven 1 kHz and the crossover slopes
leak 6.5 kHz into Mid; tones now sit two octaves inside each band: 60 Hz / 1 kHz / 15.5 kHz.)

Ruling: "only that band's processed output" = the band's dry share plus its saturation at the module's Mix and Output
(not the raw saturator, not level-matched) — what the band contributes to the normal sound; the solos add up to it.
— Cost if wrong: a level-match stage like the EQ's would be ~20 lines.
Ruling: the Saturation solo works only in multiband mode (single band: the request is ignored, bit-identical); switching
multiband off fades the solo out; switching the module off ends it. — Cost if wrong: none (single band = whole signal).

Task 3: complete (Source/UI/AuditionButton.h: drawHeadphones, AuditionPress, AuditionButton; EQ panel: headphone at the
right end of the selected-band badge, graph hot spot beside the hovered / soloed point, soloed band highlighted and the
rest dimmed; solo cleared on band delete, panel / graph destruction and when the panel is hidden inside a window.
"Headliner audition panels" 31 checks; "Headliner EQ panel" 357 checks unchanged.)

Ruling: click = latch, press longer than 350 ms = momentary (ends at release), click on an active solo = off - for both
the row button and the graph hot spot. — Cost if wrong: a constant (AuditionPress::holdMs).
Ruling: the graph hot spot is an 18 px headphone 21 px right of the hovered point (left of it near the right edge); it
stays visible while the band is soloed. The badge of the selected-band row grows from 96 to 128 px to hold the headphone.
— Cost if wrong: layout constants.
Ruling: the solo stays with its band when the selection changes; the row button shows the selected band's state, and a
click there moves the solo to the selected band (one solo per EQ instance). — Cost if wrong: small.

Task 4: complete (SatView: AuditionButton per band at the top right, multiband only, accent = band character colour,
others dimmed while one is soloed; solo cleared when multiband goes off, the view is destroyed or hidden in a window.
SatPanel: satMix knob is the 4th main control (MIX), laid out after Link; the existing SatPanelTests main-row test was
updated to the spec (4 main controls, Mix not in Mehr) and failed before the change. "Headliner audition panels" 52 checks,
"Headliner saturation panel" 109 checks.)

Task 5: complete (listen renders, snapshots, docs).
Listen renders (env-gated test "Velvox audition renders", VELVOX_AUDITION_RENDER=dir) of 4 dry vocals (mylk_chorus,
VocalSet val f8_belt, m8_caro, m9_breathy from the bench's EQ inputs): dry, EQ normal, each EQ band solo, EQ switching every
second, Sat normal, each Sat band solo, Sat switching -> C:\Users\jakob\dev\velvox-bench\2026-10-25\modules-after\audition
(+ summary.txt). The first round found two real problems, both fixed with tests that fail on the old code:
  1. Overs: RMS level matching lifted sibilance-rich high regions to +6.6 dBFS peaks (unit test: +22 dB on a synthetic
     sibilant vocal). Fix: peak cap (region peaks <= max(input peak, min(2 x input peak, -1 dBFS))). Now no render peaks
     above -1.0 dBFS.
  2. Hopping from one band to another faded out to the NORMAL output and back in: a ~30 ms blip of the full signal
     (click ratio up to 13 x in the renders; unit test: a tone in neither region at -1.7 dB / -1.2 dB). Fix: two listening
     paths in the EQ (crossfade region to region), per-band solo weights in the Saturation (band to band). Now -21 dB /
     -43 dB, switch click ratios 0.75 - 1.24 (<= 1.5 everywhere; 1.0 = like the surrounding audio).
  Also: the level match starts at half (in dB) of the pink-spectrum estimate and adapts fast for 150 ms (the full
  estimate overshot narrow regions of tonal input by ~10 dB for the first ~200 ms).
Snapshots (looked at): C:\Users\jakob\dev\velvox-wt\shots-r2-audition\ eq-solo-1000.png, eq-hover-1000.png,
eq-solo-889.png, sat-solo-1000.png, sat-solo-889.png, sat-single-1000.png (889 = minimum scale 8/9). Snapshot tokens
"solo:<band>" and "hoverpoint" added to the --snapshot renderer.

Ruling (supersedes the Task 1 ruling on switching): hopping between bands / shapes crossfades region to region (two EQ
paths; Sat per-band weights), never through the normal output. — Cost if wrong: none audible; 2 x filter CPU only during
the 15 ms crossfade.
Ruling: level match peak cap = region peaks at most 6 dB above the input's and never above -1 dBFS unless the input's are.
Level-matched means RMS; peaks are a safety limit. — Cost if wrong: very peaky regions (sibilance) play up to a few dB
quieter than the input RMS.
Ruling: no level match for the Saturation solo (it plays what the band contributes, quieter for small bands — e.g. High on
a dark voice -12 to -34 dB). Spec says "only that band's processed output". — Cost if wrong: add BandAudition-style gain.
Ruling: CHANGELOG gets a [0.6.1] section (other 0.6.1 tracks add to it).
Full suite on the branch (before merging feature/automix): 30636 checks passed, 0 failed.

Merge of feature/automix (ec3a8d5, chain + macros tracks): conflicts in CHANGELOG.md (my 0.6.1 sections appended under the
merged [0.6.1] heading) and Tests/TestMain.cpp (the chain track's "solo:<selection code>" snapshot token kept; my band
solo token renamed "bandsolo:<band>"). Full suite on the merged tree: 32253 checks passed, 0 failed. Snapshots re-rendered.
Ruling: band-solo headphones keep the band's own colour (EQ band colour / Sat character colour), not the module-solo
yellow (SoloButton soloColour) - per-band identity, and the two solos stay distinguishable. — Cost if wrong: one accent call.
Ruling: the EQ band solo still plays while the EQ is switched off - also when module solo (Solo.h applySolo) switches the
EQ off because another module is soloed; both are explicit listening requests and the panel shows the band solo. — Cost if
wrong: in VocalChain's eq case, skip processAudition while the gate target is 0 (one condition).
Not done (optional per coordinator): removing the now-dead `macroOnly` code in EqPanel.cpp (left untouched, avoids churn).
