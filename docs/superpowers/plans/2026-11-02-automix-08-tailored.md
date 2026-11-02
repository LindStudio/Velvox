# Auto Mix 0.8 – angepasst auf die Stimme – Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Der Auto Mix leitet jede Modul-Einstellung aus der Stimme, einem gemessenen Zielbild und der Genre/Vibe-Wahl ab; der Start erklärt die Beat-Sidechain und lässt keine vorzeitige Analyse mehr zu.

**Architecture:** Neues Modul `MixTarget` (Genre, Vibe, Zielkurve in Terzbändern, Dynamik-/Dichte-/Raum-Ziele) wird über `MixIntent` vom Controller an Engine, Planner und Objective gereicht. Der EQ wird per gierigem Kurven-Fit gegen die Zielkurve geplant und nach dem Render auf Terzbändern nachgeregelt; der Kompressor wird per Render-Suche auf das Dynamik-Ziel gebracht. Die Party-Zielkurve ist ein generierter Header aus `VelvoxTests --target-curve`.

**Tech Stack:** C++20, JUCE 8.0.9, juce::UnitTest (VelvoxTests), CMake, Python 3.13 (Bench-Skripte).

**Spec:** `docs/superpowers/specs/2026-11-02-automix-08-tailored-design.md`

## Global Constraints

- Keine neuen oder geänderten Parameter-IDs, Plugin-Codes oder EQ-Rollen (`dsp::EqRole` bleibt bei 12 Werten).
- Alle Bericht-/UI-Texte zweisprachig über `txt::both` (Bericht) bzw. `i18n::tr` (UI), deutsch zuerst; Umlaute in UTF-8, Dateien nie über PowerShell Get/Set-Content bearbeiten.
- Engine, Planner, Profiler bleiben reine Funktionen, deterministisch, von jedem Thread aufrufbar.
- Anhebungen über 5 kHz max. 2 dB bei SNR < 28 dB; Absenkungen max. −6 dB (breit), Notches wie bisher −9 dB; breite Anhebungen max. +4 dB.
- EQ 1: max. 8 Auto-Mix-Bänder inkl. Low Cut, max. 3 Notches; Nutzer- und Vocal-Check-Bänder (Rollen none, presence, mud) nie schreiben.
- Zielzeit Zuhören 14 s Gesang (`CaptureConfig::targetVoicedSeconds`), Analysezeit ≤ 25 s auf einem 14-s-Take (Release-Build).
- Standard-Wahl: Genre Party, Vibe Druckvoll.
- Nie zwei VelvoxTests gleichzeitig laufen lassen (gemeinsame Settings-Datei). Voller Lauf: `scripts/build.ps1`.
- Python-Edit-Skripte über das Write-Tool in `.superpowers/sdd/` anlegen, nicht per Bash-Heredoc.

## Review Focus

1. Projekt mit einem Auto-Mix-Bericht aus 0.7 (ohne Genre/Vibe im State) laden → Standard Party/Druckvoll, kein Absturz, alter Bericht bleibt sichtbar. Test in Task 7.
2. Song stoppt mitten im Zuhören (unter 14 s) → Overlay zeigt „Weiter abspielen – noch X s“, kein Analyse-Button, Fortsetzen beim nächsten Abspielen. Test in Task 8.
3. Sehr dunkle verrauschte Aufnahme (SNR 22 dB, wenig Luft) → Kurven-Fit hebt über 5 kHz höchstens 2 dB an. Test in Task 3.
4. Stimme liegt schon auf der Zielkurve → höchstens 1 breiter EQ-Zug (keine Überbearbeitung). Test in Task 3.
5. Kein Host-Tempo (bpm 0) → Hall/Delay nutzen die bisherigen ms-Werte, kein NaN. Test in Task 6.

---

### Task 1: MixTarget – Genre, Vibe, Terzbänder, Zielbild, Generator-Tool

**Files:**
- Create: `Source/AutoMix/MixTarget.h`, `Source/AutoMix/MixTarget.cpp`, `Source/AutoMix/PartyTargetCurve.h` (generiert)
- Create: `Tests/MixTargetTests.cpp`
- Modify: `CMakeLists.txt` (Quelle + Test eintragen), `Tests/TestMain.cpp` (CLI `--target-curve`), `Tests/CheckEval.cpp` (Generator `runTargetCurve` neben `runCheckTemplate`)

**Interfaces:**
- Produces (namespace `velvox::automix`):
  - `enum class Genre { party = 0, rap, pop, edm, ballad };` `enum class Vibe { punchy = 0, aggressive, floaty, warm, natural };` (als int persistiert)
  - `inline constexpr int numThirds = 24;` `float thirdCentre (int k) noexcept;` (80 Hz · 2^(k/3), k = 0..23 → 80 Hz … 16 kHz)
  - `std::vector<float> thirdsOf (const std::vector<float>& spectrumDb, const std::vector<dsp::EqBand>& bands = {});` – Leistung je Terz (Summe der vier 1/12-Oktav-Bänder um `12 + 4k`, gewichtet wie `autoeq::levelsOf`, inkl. EQ-Modell), in dB relativ zum Kern 300 Hz–2 kHz; Bänder ohne Inhalt = −200.
  - `struct DynamicsTarget { float phraseLo, phraseHi, crestLo, crestHi; };`
  - `struct SpaceTarget { float reverbScale, delayScale, doublerMix; float decayBeats; };` (Skalen relativ zu den heutigen Planner-Werten)
  - `struct MixTarget { Genre genre; Vibe vibe; std::array<float, numThirds> curveDb, spreadDb; DynamicsTarget dynamics; float densityBlend; int satCharacter; float satDrive, satMix; float sibilanceMax; SpaceTarget space; bool estimated; };`
  - `MixTarget targetFor (Genre, Vibe);`
  - `struct MixIntent { Genre genre = Genre::party; Vibe vibe = Vibe::punchy; };`
  - `juce::String genreName (Genre)`, `juce::String vibeName (Vibe)` (beide Sprachen über `txt::both`)
  - Test-CLI: `VelvoxTests --target-curve <out.h> <wav|liste.txt>...` → schreibt `PartyTargetCurve.h` mit `namespace velvox::automix::generated { inline constexpr float partyCurve[24], partySpread[24]; inline constexpr int partyFiles; }`

- [ ] **Step 1: Failing tests in `Tests/MixTargetTests.cpp`** (UnitTest „Headliner mix target“)
  - `thirdCentre (0) == 80`, `thirdCentre (23)` ≈ 16000 (±2 %).
  - `thirdsOf` eines flachen Spektrums (alle 104 Bänder 0 dB): Terz bei 1 kHz ≈ Kernpegel einer Terz (Differenz zwischen Terzen im Kern < 0,5 dB, monoton steigend mit +1 dB/Terz wegen Bandbreite → teste nur: `thirdsOf (flat)[k+3] - thirdsOf (flat)[k]` ≈ 3,0 dB ± 0,3 für k im Kernbereich).
  - Ein EQ-Band +6 dB Bell bei 1 kHz Q 4 hebt die 1-kHz-Terz von `thirdsOf (spec, {band})` gegenüber `thirdsOf (spec)` um 3…6 dB, die 125-Hz-Terz um < 0,2 dB.
  - `targetFor (party, punchy).estimated == false`; `targetFor (rap, punchy).estimated == true`; alle `spreadDb[k] >= 1.5`.
  - Vibe verschiebt: `aggressive` hat Kurve 2,5–4 kHz ≥ punchy + 1,5 dB und `dynamics.crestHi` < punchy; `floaty` hat Kurve ≥ 10 kHz ≥ punchy + 1,5 dB und `space.reverbScale` > punchy; `warm` hat Kurve 160–250 Hz ≥ punchy + 1 dB und ≥ 10 kHz ≤ punchy − 1,5 dB; `natural` hat `densityBlend` < punchy und `satMix` < punchy.
  - Genres verschieben: `ballad.dynamics.phraseHi` > party; `rap.space.reverbScale` < party; `edm.space.reverbScale` > party.
- [ ] **Step 2:** Build + `VelvoxTests` filtern auf „Headliner mix target“ → FAIL (Datei fehlt / Symbole fehlen).
- [ ] **Step 3: Generator** `runTargetCurve (juce::File out, juce::StringArray files)` in `Tests/CheckEval.cpp`: jede Datei laden, wie `--automix` in `CapturedAudio` umwandeln (Mono-Mitte), `VocalProfiler::analyse`, `thirdsOf (profile.spectrumDb)`; je Terz Median über die Dateien = Kurve, halbe Interquartilsbreite = Streuung, Streuung mindestens 1,5 dB, ab 10 kHz mindestens 3 dB (getrennte Stems). Kopfkommentar mit Dateianzahl und Datum.
- [ ] **Step 4: Party-Kurve erzeugen:** `VelvoxTests --target-curve Source/AutoMix/PartyTargetCurve.h` mit allen WAVs aus `%USERPROFILE%\dev\velvox-bench\2026-10-29\train\pro` außer der Taylor-Swift-Datei (kein Party). Plausibilitätscheck im Log: Präsenz 2–5 kHz und Luft 10–16 kHz der Kurve liegen innerhalb ±3 dB der kalibrierten Fenster aus `AutoEq.h` (presence −12…−5, air −22…−13). Liegt ein Wert außerhalb, im Handoff notieren, nicht wegdrücken.
- [ ] **Step 5: `targetFor` implementieren.** Party/punchy = generierte Kurve, Dynamik `{3, 6, 6.5, 10}` (Phrasen-Spreizung, Crest), `densityBlend` 18, Sättigung Tape 5 dB 30 %, `sibilanceMax` −6, Raum `{1, 1, 20, 0.5}`. Genre-Offsets (estimated = true): rap: Präsenz-Terzen 2–4 kHz +1, Raum `{0.5, 0.6, 8, 0.25}`, Dynamik `{2.5, 5, 6, 9}`; pop: ≥ 8 kHz +1, densityBlend 12; edm: ≥ 5 kHz +1,5, Raum `{1.3, 1.2, 25, 0.75}`; ballad: 125–250 Hz +1, Dynamik `{4, 8, 8, 13}`, densityBlend 8, satMix 15, Raum `{1.5, 1.0, 15, 1.0}`. Vibe-Offsets: werte aus Step 1 + aggressive: densityBlend +8, satCharacter Tube, satDrive +2, Raum ×0,7; floaty: 2–4 kHz −1, Crest-Fenster +1, Raum reverb ×1,5 delay ×1,4 doubler +10; warm: satCharacter Tape, ≥ 10 kHz −2; natural: densityBlend 0, satMix 12, Raum ×0,8. Kommentar an jedem Offset: „geschätzt, bis Daten da sind“.
- [ ] **Step 6:** Tests grün.
- [ ] **Step 7: Commit** `Mix target: genres, vibes, third-octave target curve measured on finished party vocals (VelvoxTests --target-curve)`

### Task 2: Profil – Terzbänder, Silbenrate, Konsonanten-Schärfe, Tempo

**Files:**
- Modify: `Source/AutoMix/VocalProfile.h`, `Source/AutoMix/VocalProfiler.cpp`, `Source/AutoMix/CaptureBuffer.h/.cpp` (Feld `hostBpm`), `Source/PluginProcessor.cpp` (BPM an `push` / Capture übergeben)
- Test: `Tests/AutoMixTests.cpp` (neue `beginTest`-Blöcke im Analyse-Teil)

**Interfaces:**
- Produces: `VocalProfile::thirdsDb` (`std::vector<float>`, `thirdsOf (spectrumDb)`), `float onsetRate` (Onsets/s über die Gesangszeit), `float onsetSharpnessDb` (Median: Spitzenpegel der ersten 15 ms nach einem Onset minus RMS der folgenden 100 ms), `bool onsetsValid` (≥ 8 Onsets); `CapturedAudio::hostBpm` (`double`, 0 = unbekannt), `VocalProfile::hostBpm`.
- `CaptureBuffer::push (…, bool hostPlaying, double bpm = 0.0)` – letzter BPM-Wert wird atomar gemerkt und in `CapturedAudio` kopiert.

- [ ] **Step 1: Failing tests** „Analysis: syllable rate and consonant sharpness“: synthetische Vocal (`SyntheticVocal.h`) mit 3 Silben/s ergibt `onsetRate` 2,4…3,6; mit 6 Silben/s 4,8…7,2; eine Variante mit harten Konsonant-Klicks (`VocalSpec` um `consonantDb` erweitern, +12 dB Klick 5 ms) hat `onsetSharpnessDb` mindestens 4 dB über der ohne. „Analysis: third-octave levels“: `profile.thirdsDb.size() == numThirds`, Wert im Kern (Terz 1 kHz) zwischen −3 und +3. „Capture: host tempo travels with the audio“: push mit bpm 128 → `finish…`/Kopie hat `hostBpm == 128`.
- [ ] **Step 2:** FAIL bestätigen.
- [ ] **Step 3:** Onset-Erkennung auf 5-ms-Hüllkurve der Mitte: Anstieg ≥ 9 dB gegenüber dem Minimum der vorigen 60 ms, Mindestabstand 90 ms, nur in Gesangsblöcken. BPM vom Prozessor (`settings.hostBpm`, PluginProcessor.cpp ~Z. 447) an `autoMix.push` durchreichen.
- [ ] **Step 4:** Tests grün, bestehende Analyse-Tests grün.
- [ ] **Step 5: Commit** `Profile: third-octave levels, syllable rate, consonant sharpness, host tempo in the capture`

### Task 3: EQ-Kurvenfit

**Files:**
- Modify: `Source/AutoMix/AutoEq.h/.cpp`, `Source/AutoMix/AutoMixPlanner.h/.cpp`
- Test: `Tests/AutoEqTests.cpp`

**Interfaces:**
- Consumes: `MixTarget`, `thirdsOf`, `VocalProfile::thirdsDb`.
- Produces: `autoeq::Context` bekommt `const MixTarget* target` und `std::array<float, numThirds> unmaskDb` (Ziel-Anhebung aus Beat-Masking je Terz, 0…2 dB). `std::vector<Move> planCurveMoves (const Context&)` – Notches (bestehende Logik) zuerst, dann breite Züge. `std::vector<Move> correctCurve (const std::vector<float>& renderedThirds, const Context&, const std::vector<Move>& owned)`. `juce::String regionName (float hz, bool cut)` – „Dröhnen“ (150–450 Hz, Schnitt), „Pappig“ (450–900), „Nasal“ (900–1600), „Härte“ (2,5–5 k, Schnitt), „Körper“ (< 300 Hz, Anhebung), „Verständlichkeit“ (1,6–5 k, Anhebung), „Brillanz“ (> 7 k), sonst „Tiefen/Mitten/Höhen“. `titleOf` für breite Rollen nutzt `regionName`: „Dröhnen 340 Hz −3,5 dB“, „Brillanz +3,0 dB (Shelf 10 kHz)“.
- `AutoMixPlanner::plan (const VocalProfile&, const std::map<juce::String, float>& current, const MixIntent& intent = {})`.
- Rollen-Zuordnung (keine neuen Rollen): Zug < 300 Hz → `warmth`, > 7 kHz → `air`, Mitten → `autoMix`, dann `autoTone`, dann freie `autoNotchN`-Rollen (breit, Q < 10). Begrenzung: max. 8 Bänder gesamt (`maxBands` = 8), Notches max. 3.

Algorithmus (vom Test nicht festgelegt):
```
dev[k] = (voiceThirds[k] + modelEQ[k]) - (curve[k] + unmask[k])          // k = Terzen 100 Hz .. 14 kHz
z[k]   = dev[k] / spread[k];  only |z| > 1 counts, residual = sign(dev)*(|dev| - spread)
repeat while budget > 0:
    take the connected run of same-sign residual with the largest sum |residual|
    if max |residual| in run < 1.0 dB: stop
    centre = residual-weighted log-mean of the run; width (octaves) = run length/3
    run touches <=160 Hz end or >=8 kHz end and width >= 1 octave -> shelf (180 Hz / 9..10 kHz), else bell, Q = clamp(1.4/width, 0.7, 4)
    gain = -0.7 * mean residual of run; clamp to [-6, +4]; boosts > 5 kHz clamp 2 if noisy; no boost within 1/3 oct of a measured resonance; |gain| < 1 -> skip run
    add band, recompute model, budget--
```

- [ ] **Step 1: Failing tests** (neue `beginTest` in AutoEqTests):
  - „Curve fit: a mud bump gets a cut where it is“: Spektrum = Party-Kurve + 5 dB Glocke bei 350 Hz (1/2 Oktave breit) → ein Zug mit Freq 280…440 Hz, Gain −2…−5, Titel beginnt mit „Dröhnen“/„Boomy“.
  - „Curve fit: missing presence and air are lifted where they lack“: Party-Kurve −5 dB ab 2,5 kHz → Anhebung(en) mit Freq ≥ 2 kHz, Summe der Anhebung bei 4 kHz im Modell 2…4 dB.
  - „Curve fit: a voice on the target is left alone“ (Review Focus 4): Party-Kurve ± 0,5 dB Rauschen → höchstens ein breiter Zug, |Gain| ≤ 1,5.
  - „Curve fit: noisy dark take boosts at most 2 dB above 5 kHz“ (Review Focus 3): Party-Kurve −8 dB ab 5 kHz, `noisy = true` → `responseDb` bei 8 und 12 kHz ≤ 2,0.
  - „Curve fit: different voices get different EQs“: drei synthetische Vocals (dunkel/tief, hell/hoch, nasal 1,1 kHz) → die drei Zug-Listen unterscheiden sich in Frequenz (mind. ein Zug > 1/3 Oktave entfernt) – kein festes Muster.
  - „Curve fit: vibe moves the result“: dieselbe Stimme, `warm` vs `aggressive` → Gain bei 3 kHz (Modell) unterscheidet sich ≥ 1 dB.
  - Limits: alle Bänder `withinLimits`, ≤ 8 Bänder, ≤ 3 Notches.
- [ ] **Step 2:** FAIL bestätigen.
- [ ] **Step 3:** `planCurveMoves`, `correctCurve`, `regionName`, `titleOf`-Anpassung, `withinLimits` (breit: −6…+4). `maxBands` = 8, `maxBroadCutDb` = 6. Planner: `planMoves` → `planCurveMoves`, Ziel aus `targetFor (intent.genre, intent.vibe)`, Masking pro Terz aus `beatOverVocalDb` (≥ `unmaskFromDb` → `unmaskGainDb`, nur 1–5 kHz). Die Notiz „Keine Höhenanhebung…“ bleibt sinngemäß.
- [ ] **Step 4:** Neue Tests grün. Alte AutoEq-/Planner-Tests, die das alte Muster festschreiben („broad moves follow the target curve“, Goldens in „Velvox EQ auto-mix“), bewusst auf die Zielkurve umstellen; jede Änderung im Commit-Text nennen.
- [ ] **Step 5: Commit** `Auto EQ: curve fit against the genre/vibe target - cuts and lifts where this voice differs, named by region`

### Task 4: Render-Metriken in Terzen, geschlossener Kreis, Objective

**Files:**
- Modify: `Source/AutoMix/AutoMixEngine.h/.cpp`, `Source/AutoMix/AutoMixObjective.h/.cpp`
- Test: `Tests/AutoMixTests.cpp`, `Tests/AutoEqTests.cpp`

**Interfaces:**
- Produces: `RenderMetrics::thirdsDb`; `AutoMixEngine::run (audio, current, shouldStop, progress, const MixIntent& intent = {})`; `evaluate (m, original, profile, targetLufs, isBasePlan, effectsPlanned, const MixTarget& target)`; neues Feld `ObjectiveBreakdown::beat` (in `total()` und `describe()`).
- Tonal-Term: Mittel über Terzen 100 Hz–12 kHz von `max(0, |m.thirds[k] - curve[k]| - spread[k])²`, skaliert so, dass ein Fehler von 3 dB über der Streuung in allen Terzen = 9 ergibt (vergleichbar zur alten Größenordnung). Dynamik-Term nutzt `target.dynamics`. Sibilanz-Term nutzt `target.sibilanceMax` statt fester −6 dB. `beat`-Term: nur mit Beat; Summe über Terzen 1–5 kHz von `max(0, beatOverVocal[k] - leadOverBeatLu - 3)`, Faktor 0,2.
- Closed loop in `run`: statt `autoeq::correct` → `autoeq::correctCurve (m.thirdsDb, …)` (max. 2 Runden für den EQ).

- [ ] **Step 1: Failing tests:** „Objective: the tonal term is the distance to the target curve“ – Metrik mit `thirdsDb == curve` → tonal 0; +4 dB in allen Terzen über 2 kHz (Streuung 1,5) → tonal > 0 und größer als bei +2 dB. „Objective: a beat that covers the words costs“ – Profil mit `beatOverVocalDb` +8 dB bei 2–4 kHz → `beat` > 0, ohne Beat 0. „Engine: the verified EQ ends closer to the target curve than the open-loop plan“ (ersetzt den bisherigen Engine-Test in AutoEqTests, gleiche drei Specs).
- [ ] **Step 2:** FAIL bestätigen.
- [ ] **Step 3:** Implementieren; `measureInto` füllt `thirdsDb` aus dem Render-Profil.
- [ ] **Step 4:** Tests grün; betroffene Optimizer-Goldens bewusst anpassen.
- [ ] **Step 5: Commit** `Auto mix objective and verification measure the target curve in third octaves; a beat that covers the words costs`

### Task 5: Dynamik – Kompressor-Suche, Attack/Release aus der Stimme, Smash nach Dichte, Ride-Ziel

**Files:**
- Modify: `Source/AutoMix/AutoMixPlanner.cpp`, `Source/AutoMix/AutoMixEngine.cpp`
- Test: `Tests/AutoMixTests.cpp`, ggf. `Tests/DynamicsQualityTests.cpp` (nur lesen, nicht ändern)

**Interfaces:**
- Consumes: `VocalProfile::onsetRate/onsetSharpnessDb/onsetsValid`, `MixTarget::dynamics/densityBlend`, `RenderMetrics::dynamicsDb/crestDb`.
- Produces: `static void AutoMixEngine::tuneDynamics (const CapturedAudio&, const std::map<juce::String,float>& current, MixPlan&, const VocalProfile&, const MixTarget&, int maxRenders = 5);` – rendert Kompressor-Paare (Schwelle −2/0/+2/+4 dB um den Planwert × Ratio aus {1,8, 2,5, 3,5, 5}, gieriger Abstieg, ≤ 5 Renders), wählt das Paar mit Phrasen-Spreizung und Crest am nächsten am Ziel; danach Smash-Blend in {0, ½·densityBlend, densityBlend, 1,5·densityBlend} (≤ 2 zusätzliche Renders, Crest-Ziel). Ersetzt die bisherigen Kompressor-Schritte „entschärft/gesenkt“ in der Verifikationsschleife.
- Planner: Attack = 3 ms + (onsetSharpnessDb ≥ 8 ? 0 : 6 ms), aggressive −2 ms, floaty +8 ms, clamp 1…30; Release = clamp(1000/onsetRate × 0,6, 40, 250) ms (ohne gültige Onsets: 110); Ride-Ziel und -Menge über `target.dynamics.phraseHi` (Ride nur, wenn dynamicsDb > phraseHi + 2). Begründungstexte nennen Silbenrate / Konsonanten.

- [ ] **Step 1: Failing tests:** „Dynamics: release follows the syllable rate“ – 3 vs 6 Silben/s → Release der schnellen Stimme < der langsamen um ≥ 25 %. „Dynamics: sharp consonants get a faster attack“. „Dynamics: the compressor search lands in the target window“ – dynamische Stimme (dynamicsDb ≈ 14) → nach `run` liegt `processed.dynamicsDb` in `[phraseLo-1, phraseHi+1]` von party/punchy. „Dynamics: no fixed compressor“ – fünf unterschiedliche synthetische Stimmen ergeben mindestens drei verschiedene (Ratio, Schwelle relativ zur Stimme)-Paare. „Dynamics: Smash blend follows the density target“ – `natural` → Smash aus oder ≤ 9 %, `aggressive` ≥ party. „Dynamics: at most 25 s on a 14 s take“ – Zeitmessung von `run` (Release-Build, Toleranz im Debug ×4).
- [ ] **Step 2:** FAIL bestätigen.
- [ ] **Step 3:** Implementieren.
- [ ] **Step 4:** Tests grün.
- [ ] **Step 5: Commit** `Dynamics tailored: compressor searched into the genre/vibe window, attack from the consonants, release from the syllable rate, Smash from the density target`

### Task 6: Färbung und Raum nach Stimme, Genre, Vibe, Tempo

**Files:**
- Modify: `Source/AutoMix/AutoMixPlanner.cpp`, `Source/AutoMix/AutoMixEngine.cpp` (`candidatesFor`: Varianten aus dem Ziel ableiten)
- Test: `Tests/AutoMixTests.cpp`

**Interfaces:**
- Consumes: `MixTarget::satCharacter/satDrive/satMix/space`, `VocalProfile::hostBpm`, `thirdsDb`.
- Regeln: Sättigung – Charakter aus dem Ziel, aber harsch → Warm, Körper-Terzen (125–250 Hz) > 2 dB unter Ziel → Tube und Mix +10; Drive/Mix aus dem Ziel; aus bei `distortionLikely`. Doubler-Mix = `space.doublerMix` (Rap-Lead mit pitchedShare < 0,45 → höchstens 8), aus bei schon breiten Aufnahmen. Hall-Mix/Delay-Mix = heutige Werte × `space.reverbScale` / `delayScale`; mit `hostBpm > 0`: Hallzeit = clamp(`decayBeats` × 4 × 60/bpm, 0,8, 3,0) s, Pre-Delay = clamp(60000/bpm/16, 15, 60) ms; ohne Tempo heutige Werte. Delay-Teilung: floaty 1/4 punktiert, aggressive 1/16, sonst wie heute.
- `candidatesFor (base, profile, current, const MixTarget& target)`: „Effekte trockener/mehr Raum“ und „Mit/Ohne Sättigung“ nutzen die Ziel-Werte statt fester Zahlen.

- [ ] **Step 1: Failing tests:** „Colour and space: genre and vibe change the effects“ – dieselbe Stimme: rap → reverbMix < party < edm; floaty → delayMix > punchy. „Colour and space: tempo sets reverb time and pre-delay“ – bpm 90 vs 150 → Hallzeit 90 > 150; bpm 0 (Review Focus 5) → Werte endlich und gleich der Vor-Version. „Colour and space: a thin voice gets Tube for body“. „Colour and space: a rap lead gets little doubling“.
- [ ] **Step 2:** FAIL bestätigen.
- [ ] **Step 3:** Implementieren.
- [ ] **Step 4:** Tests grün.
- [ ] **Step 5: Commit** `Saturation, doubler, reverb and delay follow the voice, genre, vibe and song tempo`

### Task 7: Controller – MixIntent speichern und durchreichen, Bericht nennt Genre/Vibe

**Files:**
- Modify: `Source/AutoMix/AutoMixController.h/.cpp`
- Test: `Tests/AutoMixTests.cpp` (Workflow-Teil), `Tests/StateRestoreTests.cpp`

**Interfaces:**
- Produces: `void AutoMixController::setIntent (MixIntent)`, `MixIntent getIntent() const`; `start()` nutzt den gespeicherten Intent; `MixReport` bekommt `int genre = 0, vibe = 0` (ValueTree-Properties `genre`, `vibe`); `writeState/readState` speichern den Intent als Properties `intentGenre`, `intentVibe` am AutoMix-Tree (fehlend → Party/Druckvoll).

- [ ] **Step 1: Failing tests:** „Workflow: the chosen genre and vibe reach the mix and are saved“ – `setIntent ({rap, aggressive})`, `runSynchronously` → `getReport().genre == rap`; State schreiben/lesen → `getIntent()` gleich. „State: a 0.7 project without intent loads as Party / Druckvoll“ (Review Focus 1) – ValueTree ohne die Properties → Standard, alter Bericht bleibt `valid`.
- [ ] **Step 2:** FAIL bestätigen.
- [ ] **Step 3:** Implementieren; Worker ruft `AutoMixEngine::run (…, intent)`.
- [ ] **Step 4:** Tests grün.
- [ ] **Step 5: Commit** `Auto mix remembers genre and vibe per instance; the report names them`

### Task 8: Start-Overlay – Genre/Vibe-Auswahl, Beat-Sidechain-Hinweis, volle Zuhörzeit

**Files:**
- Modify: `Source/UI/AnalysisOverlay.h/.cpp`, ggf. `Source/UI/AutoMixView.cpp` (Bericht-Kopf: „Party · Druckvoll“)
- Test: `Tests/AutoMixTests.cpp` oder `Tests/UxTests.cpp` (bestehende Overlay-Tests suchen: `grep -n AnalysisOverlay Tests/*.cpp`), `Tests/I18nTests.cpp`

**Interfaces:**
- Consumes: `AutoMixController::setIntent/getIntent`, `Status::capture` (`voicedSeconds`, `targetSeconds`, `transportStopped`, `beatSeen`).
- Produces: `AnalysisOverlay` hat Chip-Gruppen `genreChips` (5) und `vibeChips` (5) als `juce::TextButton` mit Radio-Gruppe; `getGenreChip (Genre)`, `getVibeChip (Vibe)` für Tests. View::intro Primär-Button „Auto Mix starten“ / „Start auto mix“. In `listening`/`capturing` ist `primary` unsichtbar.
- Texte (exakt):
  - Überschrift Genre: „Was mischst du?“ / „What are you mixing?“; Vibe: „Wie soll die Stimme klingen?“ / „How should the vocal sound?“
  - Genres: „Party“, „Deutschrap / Hip-Hop“ / „Rap / Hip-Hop“, „Pop“, „EDM / Techno“, „Ballade / Akustik“ / „Ballad / Acoustic“
  - Vibes: „Druckvoll“ / „Punchy“, „Aggressiv“ / „Aggressive“, „Floaty“, „Warm“, „Klar & natürlich“ / „Clear & natural“
  - Beat-Kasten: „Tipp: Beat als Sidechain (empfohlen)“ / „Tip: beat as sidechain (recommended)“; Text: „Dann passt der Auto Mix die Stimme an den Beat an und stellt sie über den Beat. FL Studio: Beat-Kanal im Mixer auswählen → Rechtsklick auf den Pfeil unter dem Headliner-Kanal → „Sidechain to this track“ → im Plugin-Fenster (Zahnrad) den Sidechain-Eingang wählen.“ / „Then the auto mix fits the vocal to the beat and sets it above the beat. FL Studio: select the beat channel in the mixer → right-click the arrow below the Headliner channel → "Sidechain to this track" → choose the sidechain input in the plugin window (gear).“ Status: „● Beat erkannt“ / „● Beat detected“ bzw. „○ Kein Beat am Sidechain“ / „○ No beat on the sidechain“.
  - Zuhören: „Noch {0} s Gesang“ / „{0} s of singing to go“; gestoppt: „Weiter abspielen – noch {0} s nötig“ / „Keep playing – {0} s still needed“.
- `AutoMixController::analyseNow()` bleibt für Tests, ist aber aus der UI nicht mehr erreichbar.

- [ ] **Step 1: Failing tests:** „Overlay: intro asks genre and vibe, preselects the saved choice“ – Intent rap/floaty gesetzt → `showIntro()` → die Chips rap und floaty sind `getToggleState()`; Klick auf „Pop“-Chip + Start → `getIntent().genre == pop`. „Overlay: no early analysis“ – `start` → View listening/capturing → `getPrimaryButton().isVisible() == false`. „Overlay: a stopped song asks to keep playing“ (Review Focus 2) – Status mit `transportStopped`, voiced 6 von 14 → Text enthält „noch 8 s“ / „8 s still“. „Overlay: beat status“ – `beatSeen` true/false ändert den Status-Text. I18n-Test: alle neuen Texte haben beide Sprachen. Snapshot `VelvoxTests --snapshot` der Intro-Karte zum Ansehen (kein Golden).
- [ ] **Step 2:** FAIL bestätigen.
- [ ] **Step 3:** Implementieren; Karte bei Bedarf höher (`cardH`), Liquid-Glass-Stil wie die übrigen Overlay-Elemente.
- [ ] **Step 4:** Tests grün; Snapshot ansehen (Text passt, nichts abgeschnitten).
- [ ] **Step 5: Commit** `Auto mix start: asks genre and vibe, explains the beat sidechain, listens the full time`

### Task 9: Bench, Abschluss-Gate, Renders, Übergabe

**Files:**
- Modify: `tools/bench/summarise_reports.py` (Vielfalt + Zielkurven-Abstand), `Tests/TestMain.cpp` (`--automix`/`--bench` optional `--genre <n> --vibe <n>`), `docs/HANDOFF.md`, `CHANGELOG.md`
- Output: `%USERPROFILE%\dev\velvox-bench\2026-11-02\r08_dry\`, Renders für Jakob

- [ ] **Step 1:** `--genre`/`--vibe` (Zahl 0–4) für `--automix` und `--bench` einbauen; Bench schreibt pro Datei `thirds_after` und die Modul-Hauptwerte in den Bericht.
- [ ] **Step 2:** `summarise_reports.py` gibt aus: Median-Abstand zur Party-Kurve nach dem Mix (Terzen 100 Hz–12 kHz) und je Modul die Anzahl verschiedener Hauptwerte über alle Dateien (Kompressor Ratio+Schwelle relativ, Release, Smash-Blend, Sat-Charakter+Mix, Doubler-Mix, Hall-Mix, EQ-Frequenzen).
- [ ] **Step 3:** Bench auf `train\dry` (23 Dateien) mit Stand 8c1fd93 (`base` vorhanden: `2026-10-29\train\r2_dry`) und neu vergleichen. Erfolg laut Spec: Abstand kleiner; kein Modul mit identischen Werten auf allen Stimmen (außer begründet); keine DUMB-Markierung; keine Anhebung > 2 dB über 5 kHz bei SNR < 28 dB. Verfehlt → Ursache beheben, nicht Schwellen verschieben.
- [ ] **Step 4:** Voller Lauf `scripts/build.ps1` → `0 failed`; pluginval Strictness 10 → SUCCESS.
- [ ] **Step 5:** Renders für Jakob: `jakob_vergleich.wav` in Party/Druckvoll, Party/Aggressiv, Party/Floaty nach `velvox-bench\2026-11-02\jakob_vergleich_08\` (plus alter Stand zum Vergleich).
- [ ] **Step 6:** CHANGELOG (0.8), HANDOFF-Kopf aktualisieren, commit, Drive-Backup (`scripts/backup-to-drive.ps1`), installieren nur wenn FL zu ist (UAC). Nicht pushen ohne Rückfrage.
- [ ] **Step 7: Commit** `0.8: bench, changelog and handoff`
