# Audition (0.6.1 items 1, 5, 6) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** EQ band solo (Pro-Q-style headphone), Saturation per-band solo in multiband mode, and the Saturation Mix knob in the panel's main row.

**Architecture:** Monitoring state lives in the DSP modules as one `std::atomic<int>` each (requested band, -1 = none),
written by the UI on the message thread and read once per block by the audio thread. Nothing is a parameter, nothing is
saved, nothing goes through the undo manager. EQ: a header-only `dsp::BandAudition` (TPT state-variable band-pass /
low-pass / high-pass of the band's region, adaptive level match, 15 ms crossfade) runs after the EQ's gate on the EQ's
input. Saturation: inside the multiband loop the soloed band's own dry + processed signal (with its own DC blocker) is
crossfaded against the normal output. UI: a shared header-only `ui::AuditionButton` (headphones; click = latch, press and
hold > 350 ms = momentary) in the EQ selected-band row, a headphone hot spot next to the hovered EQ point (momentary
while held), one per band in the Saturation strip.

**Tech Stack:** C++20, JUCE 8.0.9, juce::UnitTest (VelvoxTests).

**Spec:** `docs/superpowers/specs/2026-10-25-headliner-0.6.1-feedback-design.md` (items 1, 5, 6), general rules from
`docs/superpowers/specs/2026-10-25-headliner-pro-overhaul-design.md`.

## Global Constraints

- "it is a monitoring state: not a parameter, not saved, not automatable, not an undo step; turning it off or closing the panel/plugin ends it."
- "Clicks-free switch (short crossfade)."
- EQ: "a band-pass around the band's frequency with the band's Q (for shelves/cuts: the region the band acts on), level-matched, no gain applied."
- Sat: "In multiband mode each band (low/mid/high) gets a solo (headphone) button: only that band's processed output is heard."
- "`satMix` (exists) moves into the main row ("MIX"), not only in Mehr."
- Never rename/remove parameter IDs; old projects sound identical (no solo requested = bit-identical output).
- Real-time safe: no allocation, locks or logging on the audio thread; UI -> audio handoff via atomics only.
- No permanent explanatory text in the panels; short labels + values; hover cards for discoverability.

## Review Focus

- Soloed band deleted / slot freed while soloed: the solo must end (no stuck silence, no solo of a default bell).
- Band type changed (bell -> low cut) or dragged while soloed: the region follows without clicks.
- EQ switched off (power) or Mix < 100 % while soloed: the solo still plays only the region (not blended with the full signal).
- Saturation multiband switched off while a band is soloed: solo ends, normal single-band sound returns click-free.
- Panel closed / other module selected / editor closed while soloed: the solo ends.

---

### Task 1: `dsp::BandAudition` + EQ module audition

**Files:**
- Create: `Source/DSP/Audition.h` (header-only: region mapping, SVF, level match, fade)
- Modify: `Source/DSP/EqualizerModule.h/.cpp` (atomic request, `processAudition`)
- Modify: `Source/DSP/DspUtilities.h` (`ModuleGate::dryChannels()`), `Source/DSP/VocalChain.h/.cpp` (eq case calls
  `processAudition`; `getEqualizer (k)`; non-const `getSaturation (k)`)
- Test: `Tests/AuditionTests.cpp` (new, own CMake block at the end of CMakeLists.txt)

**Interfaces:**
- Produces: `dsp::AuditionRegion dsp::auditionRegionFor (const EqBand&) noexcept` (shape bandPass / lowPass / highPass, freq, q);
  `EqualizerModule::setAuditionBand (int band) noexcept` (any thread, -1 = off), `int getAuditionBand() const noexcept`,
  `void processAudition (const float* const* moduleInput, float* const* io, int numChannels, int numSamples) noexcept`
  (audio thread, after process and the gate; `moduleInput` may alias `io`), `bool isAuditionAudible() const noexcept`;
  `VocalChain::getEqualizer (int instance)`.

- [ ] Step 1: tests (fail first): region of a bell at 1 kHz Q 4 (200 Hz and 5 kHz >= 20 dB below 1 kHz); gain ignored
  (bell +12 dB vs 0 dB: same audition output); low shelf / low cut = below, high shelf / high cut = above; level match
  (noise and synthetic vocal: output RMS within 3 dB of input RMS after 1 s); never requested = bit-identical; switching
  on / off / to another band click-free (max second difference bounded vs. the signal's own); free slot -> no audition;
  chain: EQ Mix 50 % and EQ off still give only the region.
- [ ] Step 2: run, see them fail (compile error = missing API).
- [ ] Step 3: implement.
- [ ] Step 4: run `-Test "Velvox audition"` + EQ suites green. Commit.

### Task 2: Saturation band solo

**Files:** Modify `Source/DSP/SaturationModule.h/.cpp`; Test `Tests/AuditionTests.cpp`.

**Interfaces:** Produces `SaturationModule::setSoloBand (int band) noexcept` (-1 = off), `int getSoloBand() const noexcept`,
`bool isSoloAudible() const noexcept`.

- [ ] Step 1: tests: solo low / mid / high on a 100 Hz + 1 kHz + 8 kHz mix (other bands >= 30 dB down); the three solos sum
  to the normal output (linearity, <= -60 dB); multiband off -> solo has no effect (bit-identical); never requested =
  bit-identical; switching click-free.
- [ ] Step 2: fail. Step 3: implement. Step 4: green + SaturationTests. Commit.

### Task 3: AuditionButton + EQ panel UI

**Files:** Create `Source/UI/AuditionButton.h`; modify `Source/UI/EqPanel.h/.cpp`; tests in `Tests/AuditionTests.cpp`.

**Interfaces:** `ui::AuditionButton` (`std::function<void (bool on)> onChange`, `setActive (bool)`, `isActive()`,
`static constexpr int holdMs = 350`). `EqPanel::getAuditionButton()`, `EqGraph::auditionIconArea (int band)`,
`EqGraph::getAuditionBand()`.

- [ ] Step 1: tests: row button click latches solo of the selected band (module atomic = band), second click ends it; no
  parameter changed, no undo step, state not in getStateInformation; press-hold-release = momentary; hovering a point shows
  the hot spot, press on it solos while held; deleting the band ends it; destroying the panel ends it.
- [ ] Step 2-4 as above. Commit.

### Task 4: Saturation panel UI (solo buttons, Mix in the main row)

**Files:** Modify `Source/UI/SatPanel.h/.cpp`; tests in `Tests/AuditionTests.cpp`.

- [ ] Step 1: tests: three solo buttons visible only in multiband; click = solo of that band (only one at a time);
  multiband off clears it; panel destruction clears it; satMix knob is a main control and not in Mehr.
- [ ] Step 2-4. Commit.

### Task 5: Snapshots, listen renders, docs, full suite

- [ ] Snapshot overlay tokens `eqsolo` / `satsolo` in `Tests/TestMain.cpp`; render into `C:\Users\jakob\dev\velvox-wt\shots-r2-audition\`, look, iterate.
- [ ] Listen renders (env `VELVOX_AUDITION_RENDER=<dir>` test) of dry vocals: dry, EQ normal vs. soloed bands, Sat normal vs.
  each solo, into `C:\Users\jakob\dev\velvox-bench\2026-10-25\modules-after\audition\`; check clicks (second difference), levels.
- [ ] CHANGELOG / HELP lines; ledger; full suite green; commit.
