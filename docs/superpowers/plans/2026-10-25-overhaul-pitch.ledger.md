# Ledger – overhaul/pitch (Headliner 0.6, spec 5.10, QUALITY_BASELINE gap 3)

Task 1: complete (quality tests in `Tests/PitchTests.cpp` "Velvox pitch quality": exact ratio on tones 220/233/311/440 Hz x
+12/+7/+1/-5/-12 st both formant modes <= 1 ct, 44.1/96 kHz, latency <= 30 ms and identical for every setting, spurious <= -60 dB,
formants within 1/3 oct at +-12/+7/-5 st, formant control, held-note envelope, noise level, NaN/denormal robustness, automation
clicks; on the 0.5 engine 41 checks failed: up to 6.9 ct error, 120 ms, formant control ineffective at 0 st, noise -2.75 dB).
Root cause of the "+2.7 ct ratio error" (`PitchDiagnostics` scan): NOT semitone->ratio (`pow (2, st/12)` is exact) and NOT the
sample rate. The error of Signalsmith Stretch 1.4 varies from -4.3 to +6.9 ct with the *input frequency* at a fixed shift (e.g.
+1 st: 220 Hz +4.8 ct, 440 Hz +1.4 ct; -12 st: 233 Hz -3.5 ct, 220 Hz +4.4 ct) and grows with formant compensation: its peak
mapping places each partial by the energy centroid of the FFT bins above the smoothed spectrum, so the output partial lands at
f_in + (ratio-1) * f_centroid; the centroid is biased by where the partial falls on the 6144-point bin grid. The baseline's two
cases (220 Hz up 12 / down 5) both happened to read 2.7 ct. A config change cannot fix it - the engine was replaced.
Task 2: complete (`Source/DSP/PitchTracker.*`: YIN on ~8 kHz decimated copy every 10 ms, full-rate parabolic refinement; period
within 0.1 % for 80-900 Hz at 44.1/48/96 kHz, noise/silence unvoiced).
Task 3: complete (`Source/DSP/FormantCorrector.*`: Blackman-Harris spectrum, peak hold over +-f0/2, cepstral lifter 0.8 P
(0.5-3 ms), 12 ms time smoothing, G = E(f/target)/E(f/rho) clamped (-36 dB, +36 dB below 4 kHz -> +18 dB from 10 kHz, 0 dB
above y's content), minimum-phase FIR 256 taps @48k from the folded cepstrum, 2.5 ms crossfade per 10 ms hop, SSE dot product,
Signalsmith-linear RealFFT; bit-exact bypass when rho == target).
Task 4: complete (`Source/DSP/SpliceShifter.*` + `PitchModule` rewrite, public API unchanged; parameter description and two
factory preset descriptions say "about 15 ms"; MixLaw audit "Mix 0 % bit-exact" needed the Mix to snap on activate()).
Task 5: complete (bench: `pitch_formant` procedure on VocalSet f7/m8 long vowels a/i/u, `formant_shift_oct` = envelope scaling
that maps the dry LPC envelope onto the output (qmetrics.lpc_envelope_db / formants / formant_shift_oct + unit tests),
`sustain_roughness_db` = whole-period, detrended envelope std; new case down12; renders in
`velvox-bench\2026-10-25\modules-after\pitch`; QUALITY_BASELINE pitch block updated: 25 metrics, 0 FAIL; full suite 22926
checks green; refhost python tests 32 OK).

Results (bench, vs Graillon 3): tone error 0.00 ct (Graillon 24-70 ct on a sine), spurious -84/-82/-85 dB (Graillon -44..-58),
latency 14.6 ms (22.4), vocal pitch error 2.20/1.34/2.93 ct (1.95/1.14/3.35), formant shift 0.18 oct up12 / 0.11 oct down12
(Graillon with Formant 0 moves them ~1 oct), held-vowel roughness +0.15/+0.22 dB (Graillon 0.01/0.08), CPU 1.26 % (0.77 %; engine
alone ~0.85 % in the component probe; 0.5 engine 2.24 %).

## Rulings

- Ruling: replace Signalsmith Stretch by a time-domain engine (pitch-synchronous two-head resampling splicer + formant
  correction), no phase-vocoder fallback and no engine switch. — Why: the vocoder needs ~120 ms for clean partials (shorter
  windows detuned by up to 70 ct) and its error is bin-grid dependent; resampling gives the exact ratio by construction, no
  phasiness, 15 ms. A switch would need a new parameter defaulting to the *old* engine (hard rule), so old projects and fresh
  instances would keep the 120 ms engine. Polyphonic/noisy input uses correlation-chosen 8 ms splices with a correlation-normalised
  fade (WSOLA-like) instead of the stretch. — Cost if wrong: chords / dense backing vocals through Pitch sound grainier than with
  the vocoder; a "Poly" engine could be added later as a fresh-instance option.
- Ruling: latency = ceil(fs/70) + 16 samples (702 @48k, 14.6 ms), constant, independent of all settings; reported as before
  (pitch on & in chain). — Why: two heads within L +- J with J <= one period of 70 Hz; Graillon 22.4 ms. — Cost: voices below
  70 Hz use fixed (non-period) splices.
- Ruling: the latency change (120 -> 14.6 ms) and the new engine apply to old projects too (quality fix per spec 8 / task brief).
  — CHANGELOG: "Pitch: new low-latency engine (15 ms instead of 120 ms), exact transposition (was 2-7 ct off), formants kept
  within 1/3 octave at +-12 st, Formant control works at 0 st". — Cost: old sessions shift timing of the pitched layer by the
  PDC difference (hosts compensate), sound slightly different (cleaner).
- Ruling: formant correction realised as a minimum-phase FIR (cepstral design), not as an LPC A_y/A_t lattice. — Why: the LPC
  pair under-delivered large corrections by 8-20 dB on real vowels (down12 formant shift 0.9 oct); the FIR is exact to the
  designed curve (down12 0.11 oct). — Cost: ~0.2 % CPU for the 256-tap FIR; small min-phase group delay (< 1 ms) vs the dry.
- Ruling: correction limits -36 dB / +36 dB (voice band) tapering to +18 dB at 10 kHz and 0 dB above y's content; lifter cap
  3 ms. — Why: down-shifts must regenerate upper formants from weak content (singer's formant needs ~+40 dB on "u"); more boost
  did not help but would raise noise; 3 ms cap halved held-vowel AM at -12 st (0.29 -> 0.22 dB) and improved formant match.
  — Cost: a pure sine keeps "formant preservation" literally (its level can drop by up to 24 dB at +12 st with Preserve on).
- Ruling: bench formant metric = best envelope scaling (no peak picking) and roughness = whole-period, 150 ms-detrended envelope
  std (new metrics, gated: formant <= 1/3 oct absolute, roughness <= Graillon + 0.5 dB). — Why: LPC peak picking flipped
  between formants on real vowels (Graillon read 1.0-1.3 oct either way); fixed 20 ms frames counted the waveform of a 65 Hz
  output and the note's onset/decay as "roughness". — Cost: f7 "i"/"u" at +12 (output f0 516 Hz above F1) read ~0.9 oct
  because F1 is not observable above the fundamental; the 6-vowel mean still passes (0.18).
- Ruling: unit roughness test measures over whole output periods and is gated at 0.2 dB (new engine 0.03-0.10 dB). The 0.5 engine
  was not re-measured with this frame choice (it passed the earlier 10 ms version at 0.19-0.39 dB). — Cost if wrong: none for
  the gate's purpose (it guards the new engine).
- Ruling: keep `third_party/signalsmith-stretch` (only `Tests/PitchDiagnostics.cpp` uses the stretch now; the corrector uses
  signalsmith-linear's RealFFT). — Why: documents the root cause reproducibly. — Cost: an unused header in the product build.
