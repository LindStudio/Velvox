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
