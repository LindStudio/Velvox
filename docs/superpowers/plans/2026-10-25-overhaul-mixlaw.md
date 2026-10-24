# Mix Law and Insert Default Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** "Delay at 100 % wet still passes the dry vocal" is fixed: fresh module instances use a true dry/wet law, old projects keep the
legacy additive law bit-exactly, and Delay/Reverb are inserts in a fresh plugin and when added through the browser.

**Architecture:** A small value type `dsp::MixLaws` (one bit per module instance: legacy or true) travels in `ChainSettings` to the
audio thread like the routing. The processor stores it as the hidden state property `mixLaw` (absent = every instance legacy), owns
the query/switch API for the UI and marks instances that join the chain while switched off (browser add) as true law. The four
modules whose Mix is additive (Delay/Reverb inserts, Doubler, Smash) get a "dry-hold" law under the true flag; every other module
already crossfades truly and is pinned by audit tests.

**Tech Stack:** C++20, JUCE 8.0.9, JUCE UnitTest (`VelvoxTests`).

**Spec:** `docs/superpowers/specs/2026-10-25-headliner-pro-overhaul-design.md` §2.9, §8 "Mix law and old sessions (Q1)", "Insert default (Q2)".

## Global Constraints

- Never rename/remove parameter IDs or plugin codes; the flag is state, not a parameter.
- Old projects sound identical (states without the flag = legacy law, bit-exact with 0.5).
- Real-time safe audio thread: the law set is a trivially copyable value read lock-free (SeqLockValue), no allocation.
- Do not edit `Source/UI/*`. Keep ParameterLayout/SettingsReader untouched if possible.
- Auto-mix planner and the SPACE macro keep sends; auto-mix output for a fixed input unchanged; factory presets not converted.
- Mix 100 % (true law) => dry >= 60 dB down where physically meaningful; Mix 0 % => bit-exact (latency-aligned) dry.

## Review Focus

1. An old project whose delay insert / doubler / smash sits above 50 % must sound exactly as in 0.5 (legacy) — test with a 0.5 state blob.
2. Automation in an old project that switches on a pruned (not-in-chain) Delay/Doubler must stay legacy — test via insertEngagedInstances path.
3. Loading a state must not mark instances "fresh" while the routing is replaced — test: old blob with an off delay insert stays legacy.
4. Moving Mix while playing under the true law must not click (dry gain follows the smoothed mix) — test max sample step during a ramp.
5. SPACE on a fresh instance must still produce the send calibration — test routing + render equality with a legacy-default instance.

---

### Task 1: Reproduce the bug and capture a 0.5 baseline

**Files:** Create `Tests/MixLawTests.cpp`, `Tests/MixLawFixtures.h`; Modify `CMakeLists.txt` (test sources).

- [ ] Step 1: Test "Bug: Delay at 100 % in a fresh instance passes no dry vocal": fresh `Harness`, `neutral()`, delayOn=1, delayMix=100,
      delayFeedback=0, delayTime=300 ms, delaySync=0; input 0.5 s silence + 20 ms noise burst + 0.5 s silence; expect
      rms(out over the burst window) <= rms(in burst) - 60 dB.
- [ ] Step 2: Run: `bt.ps1 -Test "Velvox mix law"` → FAIL (dry at 0 dB: delay is a send).
- [ ] Step 3: With the unchanged build, log a 0.5 state XML (delay insert 100 %, reverb insert 80 %, doubler 80 %, smash blend 90 %) and
      write its render of a fixed signal to the scratchpad (one-off baseline). Paste the XML into `MixLawFixtures.h` as `state05Xml`.
- [ ] Step 4: Commit.

### Task 2: `dsp::MixLaws`

**Files:** Create `Source/DSP/MixLaw.h`, `Source/DSP/MixLaw.cpp`; Modify `CMakeLists.txt` (sources); Test `Tests/MixLawTests.cpp`.

**Interfaces (produces):**
- `enum class MixLaw { legacy = 0, trueDryWet = 1 };`
- `float additiveDryGain (float mix, MixLaw law) noexcept` — legacy 1; true `min (1, 2 (1 - mix))`.
- `struct MixLaws { std::array<std::uint16_t, numModules> legacyBits; static MixLaws allLegacy(); static MixLaws allTrue();
   bool isLegacy (ModuleId, int) const; MixLaw lawOf (ModuleId, int) const; void setLegacy (ModuleId, int, bool);
   juce::String toString() const; static MixLaws fromString (const juce::String&); bool operator== };`
- `bool hasAdditiveMix (ModuleId)` — delay, reverb, doubler, smash.
- Format: `"v1"` + `",<key>[#n]"` per legacy instance; anything without the `v1` marker parses as allLegacy.

- [ ] Test "MixLaws: string round trip, unknown keys ignored, missing marker = all legacy"; run (fail: no header); implement; run (pass); commit.

### Task 3: True law in the DSP (Delay/Reverb inserts, Doubler, Smash)

**Files:** Modify `Source/DSP/VocalChain.h/.cpp` (`ChainSettings::mixLaws = MixLaws::allLegacy()`, insert law, pass law to modules),
`Source/DSP/DoublerModule.h/.cpp`, `Source/DSP/SmashModule.h/.cpp` (`MixLaw mixLaw` in the settings struct). Test `Tests/MixLawTests.cpp`.

- Insert: `out = in * additiveDryGain (m, law) + m * duck * wet` (m = smoothed return; ducking acts on the wet only).
- Doubler: `out = in * additiveDryGain (a, law) + a * voices`. Smash: `processed = (dry * hd + b * wet) * norm`, `hd = additiveDryGain (b, law)`,
  `norm = 1 / (1 + 0.6 (hd + b - 1))`; the legacy branch keeps the old expressions literally (bit-exact).
- [ ] Tests (VocalChain level, 48 kHz stereo): delay insert (fb 0, 300 ms), reverb insert (pre-delay 100 ms), doubler (14 ms, monoSafe 0)
      at Mix 100 % true law: burst-window residual <= -60 dB; same settings legacy: output == "send" configuration (delay/reverb) /
      unchanged formula; Mix <= 50 %: true == legacy bit-exact; smash at 100 %: legacy - true == g * dry (fit error <= -60 dB);
      ramp 0 -> 100 % has no step > 0.05 on a sine. Run fail → implement → pass → commit.

### Task 4: Processor state, API, insert default, SPACE sends

**Files:** Modify `Source/StateKeys.h` (`mixLaw`), `Source/PluginProcessor.h/.cpp`, `Source/DSP/Routing.h/.cpp`
(`defaults()` inserts, `legacyDefaults()` sends), `Source/PluginEditor.cpp` (browser adds as insert), `Source/AutoMix/AutoMixPlanner.h`
(default member → legacyDefaults), existing tests that meant the 0.1–0.5 default (`RoutingTests`, `PresetTests`, `AbCompareTests`, `DuckingTests`).

**Interfaces (produces, UI track consumes):**
- `bool VelvoxAudioProcessor::usesLegacyMixLaw (dsp::ModuleId id, int instance = 0) const noexcept;` (only true for additive-mix modules)
- `void VelvoxAudioProcessor::switchToTrueMixLaw (dsp::ModuleId id, int instance = 0);` ("Alte Mix-Kurve – umstellen")
- `void VelvoxAudioProcessor::setLegacyMixLaw (dsp::ModuleId id, int instance, bool legacy);` (e.g. duplicate copies the law)
- `dsp::MixLaws VelvoxAudioProcessor::getMixLaws() const noexcept;`
- [ ] Tests: fresh instance = all true, `Routing::defaults()` has delay/reverb inserts; 0.5 blob → all legacy, renders as the send
      equivalent and as the one-off baseline; round trip keeps the flag; v0.1/v0.3 states legacy; `ChainView::insertModule` of a delay in
      an old project → true law, insert; automation engaging a pruned doubler in an old project stays legacy; switch API flips one instance;
      SPACE on a fresh instance turns the macro-engaged delay/reverb into sends and renders like a legacy-default instance.
      Run fail → implement → pass → commit.

### Task 5: Audit of all 15 modules

**Files:** Test `Tests/MixLawTests.cpp`.
- [ ] Per module: choir (humanize 0) burst residual; pitch +7 st sine residual at the input frequency; EQ/Comp/Sat/Pitch/Choir/Pulse 0 % bit-exact
      (latency-aligned) dry and 100 % = processed; De-Esser/Smooth/Ride/Check/Chop: no Mix crossfade (documented). Log the residuals for the
      audit table. Fix any violation under the true law only. Commit.

### Task 6: Auto-mix regression, docs, full suite

**Files:** Test `Tests/MixLawTests.cpp`; Modify `docs/ARCHITECTURE.md` (law paragraph).
- [ ] Test: the planner never sets an additive mix above 50 % (several synthetic vocals) and the full workflow gives identical parameters,
      routing and render in a true-law and an all-legacy instance. One-off: same workflow digest as the baseline build (ledger).
- [ ] Full suite `-Test "*"` green; ledger; commit.
