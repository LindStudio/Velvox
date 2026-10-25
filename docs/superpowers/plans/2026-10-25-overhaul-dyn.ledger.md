# Ledger: overhaul/dyn

Task 1: complete (Source/DSP/DynamicsFeed.h: FrameFifo<T, N>, DynamicsFrame, DynamicsHistory; tests "Dynamics feed" red (missing
  header) -> green, 24 checks)
Task 1: Ruling: one generic SPSC queue on juce::AbstractFifo (Capacity + 1 slots so exactly Capacity values fit) for every feed, frames
  of 5 ms (200/s; a 60 fps UI drains ~3-4 per paint) holding the frame's peak in/out level and the largest GR — a scrolling Pro-C display
  needs peaks, not averages. A full queue drops the newest frame (audio thread never waits). Cost if wrong: the UI shows a gap after
  10 s without draining; it simply discards the backlog on open.
Task 2: complete (peak detector = 10 ms sliding-window max (PeakHold, monotonic wedge) before the unchanged gain computer and log-domain
  branching smoother; tests "Compressor quality" red (THD -48.0 dB at 48/96 kHz, curve 1.13 dB) -> green (THD -167 dB, curve 0.00 dB,
  block size 1 == 512); bench comp --no-ref: thd_100hz -122 dB, curve 0.00 dB, ratio 4.00, threshold -24.0, attack 10.5 ms, release 130 ms)
Task 2: Ruling: the peak hold applies to the existing (default) Peak detector, not behind a new option — spec §8 lists "less distortion
  from ballistics" as a pure quality fix. CHANGELOG: old projects with the compressor on Peak compress slightly more steadily (GR on
  steady tones reaches the static curve; attack reads its set value instead of ~16 ms; release +10 ms hold). RMS detector unchanged
  (auto-mix uses it; calibration untouched). Cost if wrong: an old mix sounds a touch more compressed on low notes.
Task 2: Ruling: bench attack_ms is judged against the set 10 ms ± 5 (absolute), no longer against Pro-C 2 Vocal ± 5 — Pro-C 2 reads 18 ms
  (Vocal) and 6.5 ms (Clean) at the same knob, so its knob defines no single target; ratio/threshold were already judged against the set
  values for the same reason. Headliner was 16.0 ms only because ripple slowed the measured attack. Not a loosening of the window
  (still ± 5 ms). Cost if wrong: our attack feels faster than Pro-C Vocal at equal numbers.
Task 2: Ruling: ProExtensionTests "Compressor RMS detector and sidechain filter" asserted "RMS ripples less than Peak on noise" — that was
  the ballistics ripple this task removes (peak now 0.00 dB, RMS 0.04 dB). Re-pointed to "both detectors ripple < 0.1 dB on steady noise".
Task 3: complete (params compAutoRelease / compAutoMakeup (Bool, default off), CompSettings::autoRelease/autoMakeup,
  CompressorModule::autoMakeupDb / transferDb / getGainReductionDb; tests "Compressor options" red (compile) -> green: recovery to 3 dB GR
  after 30 ms / 2 s: fixed 223 / 225 ms, auto 95 / 543 ms; auto makeup 4.5 dB keeps -18 dBFS at -18.00; transfer curve error 0.000 dB;
  toggles click-free (2nd difference 0.003 / 0.001 vs 0.012 limit); silence stays silent)
Task 3: Ruling: "Style" of the spec Mehr list = the release behaviour (Auto release on/off), not a new detector choice — adding choices
  to the existing compDetector would change its normalised automation values. Dual release: fast branch release 0.4 R, slow branch
  attack 4 R / release 2.5 R, GR = max; crossfaded over 50 ms when toggled; with Auto off the code path is bit-identical to Task 2.
  Cost if wrong: the factors are constants in CompressorModule.h.
Task 3: Ruling: Auto makeup = static GR of a -18 dBFS (detector level) tone, added to the Makeup knob (Pro-C style "auto gain" for a
  typical vocal level, not full 0 dBFS compensation which over-lifts). Cost if wrong: one constant (autoMakeupLevelDb).
Task 4: complete (CompressorModule::history / currentGainReductionDb, SmashModule::history / currentGainReductionDb; tests "Dynamics module
  feeds" red (compile) -> green: comp last frame in -6.02 / out -24.00 / GR 17.98 dB; smash in -6.02 / out = rendered / GR 28.1 dB)
Task 4: Ruling: the comp feed's output is the module output before the Mix/On gate (ModuleGate in VocalChain) — the Pro-C display shows
  the compressor's own action; the chain's IN/OUT meters show the gated result. Smash GR in the feed = compression of the squashed path
  only (same as its existing meter, not the breath expander). Cost if wrong: one line each.
Task 5: complete (param deessDetection (Choice Absolute|Relative, default Absolute), DeEssSettings::relative, feed spectrumTap /
  currentReductionDb / bandLowHz / bandHighHz; tests "De-esser quality" red (compile) -> green: S reduction loud / quiet (20 dB apart)
  absolute 7.69 / 0.23 dB, relative 3.73 / 3.73 dB; split band bit-exact when inactive in both modes; silence stays silent)
Task 5: Ruling: relative detection is a new option, default Absolute — it changes what the threshold means, i.e. character, so old
  projects and the current bench case keep absolute. The relative level is band - full-band envelope (instant attack, 250 ms release)
  + referenceDb -18, so the knob means the same as absolute for a voice peaking at -18 dBFS; the full-band level is floored at -60 dBFS
  so silence and room noise are never de-essed. Recommendation for the panel/coordinator: fresh instances could start on Relative
  (spec 5.3 wants it) via the browser's fresh-instance path — not done here (Source/UI is off limits). Cost if wrong: users must pick
  Relative in Mehr to get Pro-DS "Single Vocal" behaviour.
Task 5: Ruling: the full-band envelope first used a 5 ms attack; the onset of a tone then read the HP-filter transient ~40 dB "too loud"
  (floored full level) and the split band no longer nulled bit-exactly (1.5e-5). Instant attack (like the band detector) fixes it.
Task 6: complete (Level Ride: 80 ms lookahead (constant latency 3840 samples at 48 kHz, counted by VocalChain::getLatencyFor whenever a
  ride instance is in the routing, on or off - like Saturation), word-level control law (syllable 80 ms, gaps < 250 ms hold the gain,
  no transient freeze in the first 60 ms of a word, log Response mapping down 150->10 ms / up 200->10 ms, Amount curve
  1-(1-a)^2), 10 ms fade-in after an emptied delay line. Tests: "LEVEL RIDE: word-level evening" red on the old code (4.99 vs 4.93 dB
  spread, i.e. no evening at all) -> green (3.28 dB, limit 0.7 x); latency test; bench ride --no-ref: st_loudness 2.96 LU
  (old 3.30, Vocal Rider 2.92, limit 3.22), gain jitter 0.07 dB (= old, limit ref + 0.05 = 0.10), transients -0.36 dB (limit +-0.5)
Task 6: rounds (Q6, 3 used): 1 = zero-latency faster speeds + gap hold + Amount curve -> 3.17 LU but jitter 0.11 and transients -0.84
  (FAIL: a zero-latency rider that moves within a word lifts the sustain after each onset); 2 = 50 ms lookahead -> 3.07 / 0.08 / -0.70
  (FAIL transients); 3 = 80 ms lookahead, faster speeds, syllable 80 ms -> 2.96 / 0.07 / -0.36 (pass). Syllable 45 / 60 ms were tried
  in round 3 (2.88 / 0.11 FAIL jitter, 2.92 / 0.09 marginal).
Task 6: Ruling: the lookahead is fixed (not an option) and present whenever Ride is in the chain — spec Q9 forbids latency that changes
  with a parameter, and an option defaulting to off would leave the gap open for every user. CHANGELOG: projects that use Level Ride get
  80 ms plugin latency (host-compensated) and a stronger, word-level ride at the same settings (quality fix: Amount now delivers the set
  share of the correction; the old law did not even the words of a sung phrase at all). Cost if wrong: 80 ms latency is noticeable when
  monitoring through the plugin while recording; reducing lookaheadMs to 50 costs the transient metric (round 2).
Task 6: Ruling: NewModuleTests "LEVEL RIDE: ... amount 0 is transparent" re-pointed to "amount 0 = the input delayed by the latency"
  (after the 10 ms fade-in); not weakened (still bit-exact). The 0.3 toggle-click test first failed (0.26 step: an emptied delay line
  released silence, then the voice with a hard edge) — fixed in the module (warm-up fade), test unchanged.
Task 7: complete (RideSegment, RideModule::segments (FrameFifo<RideSegment, 128>), RideModule::samplePosition; segmenter in the output
  domain (band power delayed by the lookahead x applied gain); tests "Level Ride feed" red (compile) -> green: 5 words -> 5 segments,
  start 0.0 ms off (output position = input + latency), length = word +- 20 ms, before levels follow the set levels, after - before =
  rendered gain +- 1 dB; 100 ms dip 16 dB down splits two words; 5 s note -> 3 segments (2 s split); block size 1 == 256)
Task 7: Ruling: segment levels are the mean voice-band power (100 Hz-8 kHz, the rider's own detector band) before / after the ride gain,
  in dB; positions are output-domain sample counts (RideModule::samplePosition is the count at the end of the last block), so the UI can
  place bars against its scrolling time axis. Bars end where the word was last at its level (the decay into a gap is not drawn).
Task 7: the segment feed exposed two ride defects, fixed here (not a new metric round): (1) the phrase level started at the onset's
  threshold crossing (-42 dB) and the 150 ms gaps between words (still "voiced", the 80 ms syllable level decays slowly) dragged it
  down, so loud words were lifted by +7 dB; now it starts at the first word's level (100 ms warm-up) and ignores short dips; (2) in
  those gaps the gain rose towards Max boost (breaths lifted; trace: +6.8 -> +7.9 dB per gap). Now a dip 9 dB under the phrase level
  that is shorter than 250 ms holds the gain (longer = a quieter passage, ridden normally). New test "breaths in the gaps between words
  are not lifted" (0.00 dB). Bench ride --no-ref after: st_loudness 2.35 LU (Vocal Rider 2.92), jitter 0.09 dB (limit 0.10),
  transients -0.33 dB. Tried for jitter margin: fast speeds 15 ms (2.38 / 0.09 / -0.40), syllable 100 ms (2.40 / 0.09 / -0.44): kept 10 / 80.
Task 7: Ruling: a word onset right after a gap restarts the 60 ms onset grace (no transient freeze), so the rider follows word onsets
  and keeps freezing on consonant peaks inside words.
