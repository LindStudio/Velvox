# Ledger: overhaul/dyn

Task 1: complete (Source/DSP/DynamicsFeed.h: FrameFifo<T, N>, DynamicsFrame, DynamicsHistory; tests "Dynamics feed" red (missing
  header) -> green, 24 checks)
Task 1: Ruling: one generic SPSC queue on juce::AbstractFifo (Capacity + 1 slots so exactly Capacity values fit) for every feed, frames
  of 5 ms (200/s; a 60 fps UI drains ~3-4 per paint) holding the frame's peak in/out level and the largest GR — a scrolling Pro-C display
  needs peaks, not averages. A full queue drops the newest frame (audio thread never waits). Cost if wrong: the UI shows a gap after
  10 s without draining; it simply discards the backlog on open.
