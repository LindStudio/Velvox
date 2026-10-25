# Ledger – voices track (Doubler engine + Choir quality, Headliner 0.6)

Task 1: complete (26 new Doubler parameter IDs per instance, SettingsReader mapping; tests "Velvox doubler parameters")
Task 2: complete (DoublerModule::resolveVoices – Classic / 2 / 4 voice mapping; tests in "Velvox doubler parameters")
Task 3: complete (new Doubler engine; 12 tests "Velvox doubler engine", existing "Velvox doubler" tests unchanged and green)
Task 4: complete (bench: voice_tracking / voice_splice procedures, voices2 / voices4 cases, per-case reference choice,
  am_roughness_multi_db; 2 new bench unit tests)
Task 5: complete (Choir: splice-shifter voices, random-walk humanize, stage pan curve, uneven levels, UI positions; 4 new tests)
Task 6: complete (QUALITY_BASELINE Doubler/Choir blocks + notes, full suite green)
Merge: feature/automix f592357 (EQ track) merged before Task 1.

## Rulings

- Ruling: `doublerVoices` is a 3-way choice {Classic, 2, 4}, default Classic. Classic = the 0.2 controls drive two voices
  (left at Delay, right at 1.37 x Delay, pans ∓Width, Detune = wobble depth); 2 / 4 use the per-voice IDs. — Missing
  parameters load at their defaults, so only a default that means "use the 0.2 controls" keeps old projects (with any
  Delay/Detune) close; auto-mix, assistant and macros keep writing the 0.2 IDs and keep working. The V1/V2 defaults equal
  Classic at the 0.2 defaults (14 / 19.18 ms, 0 ct, 0 dB, ∓100 %), so switching Classic -> 2 at defaults is seamless. —
  Cost if wrong: the panel must show Classic as "2 voices, linked to Delay/Detune"; editing a voice in the UI should
  switch to "2" after copying resolveVoices() into the V1/V2 IDs (UI track).
- Ruling: no `doublerHighCut`; the existing `doublerTone` is the high cut of the voices in every mode. — Two high cuts
  would conflict; the 0.2 ID keeps automation. — Cost if wrong: one more ID later (additive).
- Ruling: Width, Mix, Mono Safe, Tone, Low Cut, Mod Rate and Dry are global in every mode; Width scales the per-voice pans
  (effective pan = pan x width). `doublerDelay` / `doublerDetune` only act in Classic; `doublerModDepth` is the wobble
  depth in 2 / 4. — Keeps the WIDTH macro and the assistant's "breiter/schmaler" meaningful in all modes. — Cost if wrong:
  the stage strip must show effective pans (voicePan feed does).
- Ruling: static per-voice detune is a splice pitch shifter (one 6-point Lagrange tap drifting at 1 - ratio, spliced by a
  30 ms raised-cosine crossfade to a correlation-aligned landing point) instead of the classic dual-tap Hann shifter. —
  The dual-tap shifter crossfades two copies 20 ms apart all the time (comb/AM at the window rate); the splice shifter is
  one clean tap most of the time. Measured: solo voice at +30 ct 0.02 dB envelope std, spurious −63 dB, pitch exact to
  0.001 ct; Waves splice metric equal (0.02 dB). — Cost if wrong: on noisy material a splice is a plain 30 ms crossfade
  (inaudible in tests); CPU of a splice search ~10 µs.
- Ruling: the splice sweep is symmetric (+L -> −L, new landing L' in [2, 12] ms) so the voice's delay equals its set value
  on average (unit test: mean within 1 ms over 16 s; bench voices2 0.33 ms vs Doubler2 1.05 ms). The instantaneous delay
  of a detuned voice moves within ±12 ms – inherent to delay-line shifting (Waves: same order). — Cost if wrong: none
  measured.
- Ruling: modulation = two smoothed random walks per voice (raised-cosine moves to random targets of alternating sign,
  segment length 0.5-1.5 x mean, a short move is also a small one so no move is steeper than the sine of the same rate),
  rates Mod Rate x {1, 1.516, 0.83, 1.27} and x 0.364 for the slow walk with 35 % depth (the 0.2 rates and proportions);
  fixed seeds per voice (reproducible renders). — Spec "smooth random walk"; keeps the 0.2 character. — Cost if wrong:
  roughness of Classic is 1.05 dB vs 0.92 (0.5 engine) in the bench at the default case – still 0.84 dB better than
  Doubler2 (1.90); see QUALITY_BASELINE note.
- Ruling: voices are filtered (low cut, tone) before the delay line instead of per voice after it. — LTI, same result
  for a fixed delay; 2 instead of 8 biquads. — Cost if wrong: none.
- Ruling: the doubler snaps all smoothers to the first settings after prepare() (like the Choir). — Otherwise a fresh
  instance glides from the defaults for 0.2 s (delay pitch glide, dry fade). — Cost if wrong: none.
- Ruling: Classic "close to 0.2" is pinned by a unit test against a frozen copy of the 0.5 engine (Tests/LegacyDoublerV05.h):
  level within 0.5 dB, L/R correlation within 0.06, roughness within +0.4 dB at four old settings. Measured differences:
  level +0.01 … +0.37 dB (the Lagrange taps keep the highs that the 0.2 linear interpolation dulled; largest with Tone
  open at 20 kHz), correlation −0.006 … −0.05, roughness −0.09 … +0.33 dB (random-walk path vs sines). — Spec: Doubler
  "close", accepted change for the CHANGELOG. — Cost if wrong: an old project's doubles sound marginally brighter and the
  wobble follows a different (non-periodic) path.
- Ruling: voice_delay_err_ms is gated relative to the reference (≤ ref + 0.5 ms), not absolute 0.5 ms. — A delay-line
  pitch shifter must move its tap; over a 6 s window the median includes a partial sweep (Doubler4 1.64 ms, Headliner
  1.12 ms at ±5 ct). The exact per-cycle mean is a unit test. — Cost if wrong: none (Headliner is better than both refs).
- Ruling: Choir pans go through a "stage pan curve" p' = sign(p)(1 − (1 − |p|)²) of shape x Width, and the shapes put the
  first pair at ±1 and the others at ±0.7 … 0.95. — The narrow image (0.94) came from crosstalk of the constant-power pan
  at ±0.4 … 0.8, not from correlated voices (computed: 0.908 exactly from the pan gains); at Width 80 % the voices now sit
  near their side. Width 0 is still mono (1.000), Width 100 % unchanged at the edges. — Cost if wrong: Width 30-80 % is
  wider than before for old projects (listed as a quality change for the CHANGELOG).
- Ruling: the Choir's single-tone `am_roughness_db` becomes information; the new 6-tone mean `am_roughness_multi_db` is
  gated instead (choir only; doubler keeps both gated). — Simulation with the measured component levels: identical voice
  levels give 0.7-3.1 dB at 440 Hz depending on the beat phases alone; Doubler4's 1.62 is such a lucky phase (its 6-tone
  mean is 2.57). Old choir: 2.12 at 440, 2.02 mean; new: 2.89 at 440, 1.89 mean. Lowering the 440 value further would
  need either a lower voice level (character change) or tuning phases to the test tone (anti-goal §4). — Cost if wrong:
  the gate is one 440 Hz number looser; the replacement is stricter on average.
- Ruling: Choir quality changes (clean shifter, random-walk humanize, wider pans, uneven ±2 dB voice levels normalised to
  the same energy) apply to old projects too. — They are artefact/width fixes (QUALITY_BASELINE #7), the Mix / Density /
  Detune semantics are unchanged. — Cost if wrong: old choir projects sound wider and without the old phasiness; listed
  for the CHANGELOG.
- Ruling: shared header-only `Source/DSP/VoiceSplice.h` (LagrangeDelayLine, VoiceSpliceShifter, VoiceSpliceConfig,
  SmoothRandomWalk), so CMakeLists.txt is untouched. Named `SpliceShifter.h` / `SpliceShifter` until the merge of
  feature/automix 39c0632, where the pitch track had added its own `SpliceShifter` class and file; renamed to avoid the
  clash (the pitch one is a latency-bearing, period-tracked stereo shifter – different job). — Cost if wrong: two splice
  shifters in the code base; merging them is possible later.
- Ruling: `Tests/PanelFrameTests.cpp` ("every parameter of the module reachable") skips the parameters of the DSP-quality
  tracks whose panel controls are not built yet: the 0.6 Doubler IDs, and – already failing on feature/automix 39c0632
  before my changes – smoothW*, pulseShape/pulseAmount and the Saturation multiband IDs. — AGENT_RULES: DSP tracks must
  not edit Source/UI, panels come later; the suite must be green. — Cost if wrong: the coordinator must remove the
  entries when those panels land (the test then guards them again).

Merge: feature/automix 39c0632 merged (conflicts: bench.py PROCEDURES, ParameterLayout append calls, SpliceShifter.h);
full suite after the merge: 28944 checks, 0 failed.
