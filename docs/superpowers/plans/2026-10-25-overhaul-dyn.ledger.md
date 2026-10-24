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
