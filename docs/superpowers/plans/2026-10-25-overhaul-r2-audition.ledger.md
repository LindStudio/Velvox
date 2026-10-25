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
