# EQ Band Model (Headliner 0.6, track "eq") Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the fixed five-band EQ with up to 16 user bands per EQ instance (Pro-Q-like analog-matched minimum-phase
biquads, zipper-free), migrate old states sound-identically, and make macros / auto-mix write hidden-role bands. No UI work.

**Architecture:** `EqSettings` gains `bands[16]` + `legacyResponse`; `EqualizerModule` processes bands (matched design, or the
old RBJ bilinear design when `legacyResponse`). New parameter IDs `eqBand{n}{On,Type,Freq,Gain,Q,Slope,Role}` +
`eqLegacyResponse` (banked per EQ instance like every EQ parameter). A new unit `Source/Parameters/EqBands.{h,cpp}` holds the
role helpers, `findOrCreateEqBand`, state/snapshot migration and the legacy-parameter bridge (message thread).

**Tech Stack:** C++20, JUCE 8.0.9, VelvoxTests (juce::UnitTest).

**Spec:** `docs/superpowers/specs/2026-10-25-headliner-pro-overhaul-design.md` §5.1, §7, §8 Q4/Q5.

## Global Constraints

- Never rename/remove existing parameter IDs; only add IDs. New params default to the old behaviour (bands default off).
- Old projects sound identical: migrated states null against the 0.5 EQ (bit-exact or <= -120 dB).
- Real-time safe audio thread: no allocation, locks or logging in `EqualizerModule::process`.
- Max 16 EQ bands; types Bell, Low Shelf, High Shelf, Low Cut, High Cut, Notch, Band Pass, Tilt; cut slopes 6/12/18/24/36/48 dB/oct.
- Every EQ instance (`maxInstances (ModuleId::eq)` = 4) gets the full band bank.
- No edits to `Source/UI/*` (shell track rewrites them).
- Auto-mix objective/calibration unchanged: same frequencies/gains for a fixed input, now in role bands.

## Review Focus

- A 0.5 project with automation lanes on legacy EQ IDs: loads, sounds identical, legacy IDs stay registered (not automatable).
- Fast sweeps of freq/gain/Q and type/slope switches while audio plays: no clicks (zipper test).
- 16 bands at extreme settings (Q 40, +-30 dB, 10 Hz / 30 kHz, all sample rates 44.1-192 kHz): finite, stable output.
- User bands (role none) are never changed by macros, auto-mix or `findOrCreateEqBand`, also when all 16 slots are used.
- Preset files / A/B snapshots from 0.5 (legacy IDs only) apply sound-identically and are not reported as "modified".

---

### Task 1: Matched + legacy band designs (pure functions)

**Files:** Create `Source/DSP/EqDesign.h/.cpp`; Test `Tests/EqTests.cpp` (add to CMake test sources).

**Interfaces – Produces:**
- `enum class EqBandType { bell, lowShelf, highShelf, lowCut, highCut, notch, bandPass, tilt }`
- `enum class EqRole { none, presence, warmth, air, lowCut, mud, autoMix, highCut }`
- `struct EqBand { bool on=false; int type=0; float freq=1000, gain=0, q=1; int slope=3; int role=0; }` (slope index 0..5 = 6..48 dB/oct)
- `struct EqBandDesign { std::array<BiquadCoeffs,4> sections; int numSections; bool isCut; }`
- `EqBandDesign eqdesign::design (const EqBand&, bool legacy, double sampleRate)`
- `double eqdesign::magnitude (const EqBandDesign&, double freq, double sampleRate)`
- `double eqdesign::analogMagnitude (const EqBand&, double freq)` (prototype, for tests)

Matched method (Vicanek 2016): poles by impulse invariance; numerator by magnitude matching at DC, a match point and the
derivative there. Bell matched at f0 (cuts = inverse of the boost), shelves matched at the zero frequency f0*sqrt(A) of the
variant whose poles lie lower (low shelf cut / high shelf boost = inverse), LP/HP/BP with Vicanek's closed forms, Butterworth
cascades for cuts (odd orders: matched first-order section), first-order matched tilt. Legacy = the RBJ formulas of 0.5
(`design::peak/lowShelf/highShelf/highPass/lowPass`, bwQ table for 12/24/48) and bilinear forms for the new types.

- [ ] Tests (fail first): bell 16 kHz @ 48 kHz (Q 0.5/1/4, +-6/+-12/+-18 dB) within 0.5 dB of analog up to 20 kHz; shelves and
      cuts within 1 dB up to 0.45 fs for Q <= 2; every design over a grid (10 Hz..30 kHz, Q 0.025..40, +-30 dB, 6 rates) has
      poles inside the unit circle and minimum-phase zeros; legacy design equals `design::*` coefficients exactly.
- [ ] Implement, run `-Test "Velvox EQ"`, commit.

### Task 2: Band processing in EqualizerModule

**Files:** Modify `Source/DSP/EqualizerModule.h/.cpp`; Tests in `Tests/EqTests.cpp`; Create `Tests/LegacyEqV05.h` (verbatim copy of the 0.5 module, renamed).

**Interfaces – Produces:** `EqSettings::bands`, `EqSettings::legacyResponse`, `int EqSettings::findRole (EqRole) const`,
`int EqSettings::findFree() const`, `static double EqualizerModule::responseDb (const EqSettings&, double f, double sr)` (now bands),
`static double EqualizerModule::bandResponseDb (const EqBand&, bool legacy, double f, double sr)`,
`static void EqualizerModule::responseCurveDb (const EqSettings&, double sr, const float* freqs, float* outDb, int n, int band = -1)`.

Per band: multiplicative smoothing of freq and Q, linear gain (50 ms), redesign every 16 samples while smoothing; on/off and
topology changes (type, slope, legacy flag) through a 30 ms amount crossfade (cut types `x += (y-x)*amt`, others direct at amt 1
- exactly the 0.5 arithmetic); first settings after prepare()/reset() are adopted without a sweep.

- [ ] Tests (fail first): all bands off = bit-exact pass-through; legacy-mode bands 1-5 null against `LegacyEqV05` (<= -120 dB,
      expected bit-exact) for several settings and 44.1/48/96 kHz; measured sine response equals `responseDb`; zipper test
      (fast freq/gain/Q sweeps, steps and type/slope switches on a 200 Hz + 3 kHz signal: no second-difference spikes);
      extreme-settings stability; CPU of 16 active bands vs the 0.5 EQ printed and below 1.5 % of real time per instance.
- [ ] Implement, run, commit.

### Task 3: Parameters, reader, legacy IDs hidden

**Files:** Modify `Source/Parameters/ParameterIDs.h`, `ParameterLayout.h/.cpp` (spec fields `automatable`, `hidden`),
`SettingsReader.h/.cpp`; Tests in `Tests/EqTests.cpp`.

**Interfaces – Produces:** `pid::eqBandId (int band1to16, const char* field)` -> `juce::String` ("eqBand3Freq"),
fields `pid::eqBandOn/Type/Freq/Gain/Q/Slope/Role`, `pid::eqLegacyResponse`; ranges: Freq 10..30000 Hz (default 1000),
Gain -30..30 dB, Q 0.025..40 (default 1), Type choice (8), Slope choice (6, default 24 dB/oct), Role choice (8, not automatable,
hidden), eqLegacyResponse bool (not automatable, hidden). The 17 fixed-band legacy IDs: hidden, not automatable.

- [ ] Tests (fail first): IDs exist for all 4 instances (`eqBand16Role_4`), defaults off, legacy IDs `isAutomatable()==false`,
      `SettingsReader` maps band params of every instance into `ChainSettings` (instance 0 and `extra[k]`).
- [ ] Implement, run, commit.

### Task 4: Migration, snapshots, bridge, findOrCreateEqBand

**Files:** Create `Source/Parameters/EqBands.h/.cpp`; Modify `Source/PluginProcessor.cpp/.h` (call migration in
`setStateInformation`, own the bridge), `Source/Presets/ParameterSnapshot.cpp` (migrate legacy-only snapshots in `applyTo`/`differsFrom`),
`Source/Presets/FactoryPresets.cpp` (factory presets converted to bands, matched response).

**Interfaces – Produces:**
- `struct eqbands::Access { std::function<float (const juce::String&)> get; std::function<void (const juce::String&, float)> set; }`
- `eqbands::readBand / writeBand (const Access&, int instance, int band0, ...)`, `findBand`, `findFreeBand`
- `int eqbands::findOrCreateBand (const Access&, int instance, EqRole, EqBandType, float freqHz, float gainDb, float q, int slope = -1)`
- `int findOrCreateEqBand (juce::AudioProcessorValueTreeState&, dsp::EqRole, dsp::EqBandType, float freqHz, float gainDb, float q, int instance = 0)` (message thread, gestures)
- `void eqbands::migrateLegacyValues (const Access&, int instance, bool legacyResponse)`; `bool eqbands::hasBandValues (ids)`
- `void eqbands::clearLegacyResponse (juce::AudioProcessorValueTreeState&, int instance)`
- `class eqbands::LegacyBridge` (legacy fixed-band edits on the message thread -> role bands), `eqbands::ScopedBridgeSuspend`.

Migration maps HPF -> band 1 (Low Cut, role lowCut, slope 12/24/48 -> idx 1/3/5), low -> band 2 (Low Shelf/Bell, warmth),
mid -> band 3 (Bell, autoMix), high -> band 4 (High Shelf/Bell, air), LPF -> band 5 (High Cut, highCut), `eqLegacyResponse=1`.
Instance 0 always; instances 2-4 only when their legacy values differ from the bank defaults.

- [ ] Tests (fail first): an old state (legacy IDs only) loads into bands 1-5 with roles + flag and renders null against the 0.5
      chain rendering via `LegacyEqV05`; a 0.6 state round-trips without migration; old preset snapshot applies identically and is not
      "modified"; `findOrCreateEqBand` reuses the role band, creates in a free slot (also after deletion), returns -1 when full,
      never touches role-none bands; `clearLegacyResponse` switches to matched; legacy UI edit (hpfFreq) reaches the lowcut band.
- [ ] Implement, run, commit.

### Task 5: Macros write role bands

**Files:** Modify `Source/DSP/Macros.cpp`; Tests `Tests/EqTests.cpp` (+ update `Tests/MacroTests.cpp` if it reads `eq.highGain`).

Presence: air band +5 dB*p; Warmth: warmth band +3 dB*w and air band -1.5 dB*w; a missing role band is created in a free slot of
the effective settings with the 0.5 defaults (air: High Shelf 10 kHz Q 0.7071; warmth: Low Shelf 150 Hz Q 0.7071); an off role
band counts as 0 dB base; clamp to +-18 dB unless the base is already outside; user bands untouched.

- [ ] Tests (fail first): migrated state + macros = old macro result (same gains on bands 2/4); fresh state creates air/warmth
      bands; a user band at 10 kHz is never changed; 16 user bands -> macros leave the EQ alone.
- [ ] Implement, run, commit.

### Task 6: Auto-mix writes role bands

**Files:** Modify `Source/AutoMix/AutoMixPlanner.cpp`, `AutoMixEngine.h/.cpp`; Tests `Tests/EqTests.cpp`, update `Tests/AutoMixTests.cpp`.

Planner: lowcut/warmth/autoMix/air role bands in instance 0 via `findOrCreateBand` on (current overlaid by plan); bands with
0 dB that do not exist are not created; highCut band switched off if it exists. Engine: candidate "Höhen sanfter" and
"EQ 2" use role bands (instance 1); verification reads/writes air gain and lowcut freq. `candidatesFor` gets `current`.

- [ ] Golden test first: run `AutoMixEngine::run` on fixed synthetic vocals with the 0.5 code, record the EQ decisions (role ->
      type/freq/gain/Q/slope/on, chosen candidate) and assert them on the role bands (fails before the change).
- [ ] Implement, run auto-mix tests, commit.

### Task 7: Full suite, docs

- [ ] Update tests that read legacy EQ values from plans; full suite `-Test "*"` green; document APIs in `docs/ARCHITECTURE.md`
      (EQ section) and the ledger; commit.
