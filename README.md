# Velvox – Vocal Production Suite (Version 0.2.0)

Velvox ist ein VST3-Vocal-Plugin für Windows (JUCE 8.0.9, C++20) für EDM, Hypertechno, Dance und Pop: Lead-Vocals, Adlibs, Backings und kreative Vocal-Effekte.

„Velvox“ und „Lind Audio“ sind Arbeitstitel. Vor einem Verkauf ist eine Markenrecherche nötig.

---

## Überblick

```
          ┌──────────── serielle Kette, Reihenfolge per Drag & Drop ────────────┐
Input ─ Gain ─ EQ ─ Compressor ─ De-esser ─ Saturation ─ Doubler ─ Pitch ─ Chop ─┬───────────────── + ─ Stereo ─ Level-Match ─ Output-Gain ─ Clip Guard ─ Out
                                                                                └─ Delay ─┬─ Reverb ┘   (parallele Sends: nur Effektanteil, trockenes Signal genau einmal)
                                                                                          └ Delay → Reverb (einstellbar)
```

- **Delay und Reverb** laufen wahlweise als **paralleler Send** (Standard) oder als **Insert** an beliebiger Stelle der Kette.
- **Zwei Bedienmodi** greifen auf dieselbe DSP-Engine zu. Der Wechsel ändert nie den Klang.
  - **Beginner:** sechs Makros (Presence, Warmth, Aggression, Width, Space, Density) mit Klartext-Beschreibung und Live-Hinweisen.
  - **Pro:** die vollständige Detailansicht jedes Moduls plus Ausgangsstufe.

## Module

| Modul | Funktion | Pro-Parameter |
|---|---|---|
| **EQ** | Low Cut, Low-Band, Mid-Bell, High-Band, High Cut | Low/High Cut 12/24 dB/Okt, Low/High-Band als Shelf oder Bell mit Q, Mix |
| **Compressor** | Feed-Forward, Soft-Knee, Stereo-Link | Threshold, Ratio, Attack, Release, Knee, Makeup, Mix (parallele Kompression), Detektor Peak/RMS, Sidechain-Hochpass |
| **De-esser** | Split-Band (dynamischer Shelf) oder Breitband | Frequency, Threshold, Range, Mode, Listen |
| **Saturation** | 2× Oversampling, pegelkompensiert | Character Warm/Tape/Tube/Hard, Drive, Output, Mix |
| **Doubler** | zwei modulierte Stimmen, latenzfrei | Mix, Width, Timing, Detune (Cent), Mono safe, Tone |
| **Pitch** | kreativer Pitch-Shifter (Signalsmith Stretch) | ±24 Halbtöne, Fine, Keep formants, Formant, Mix |
| **Chop** | tempo-synchrones 16-Step-Gate mit Stutter | 10 Patterns, Step-Länge 1/8 bis 1/32, Depth, Smooth |
| **Delay** | Ringpuffer, gefilterte, begrenzte Rückkopplung | Time oder Sync-Notenwert, Feedback, Mix, Low/High Cut, Ping-Pong, Delay→Reverb, Ducking |
| **Reverb** | 8-Kanal-FDN, Room/Plate | Mix, Decay, Pre-Delay, Size, Tone, Low Cut, Width, Ducking |
| **Output** | Stereo und Lautheit | Width 0–200 % (nur Seitensignal), Mono below, Level Match (Off / Bypass match / Auto gain), Messfenster, Max. Korrektur, Clip Guard |

**Ducking:** Reverb und Delay haben je Amount, Threshold, Attack und Release und eine eigene Absenkungsanzeige. Als Quelle dient entweder das **Vocal selbst** (intern) oder der **Sidechain-Eingang** des Plugins.

**Level-Matcher:** misst die Lautheit nach ITU-R BS.1770 (LUFS).
- *Bypass match:* Der Bypass spielt genauso laut wie das bearbeitete Signal, für einen fairen A/B-Vergleich.
- *Auto gain:* hält das bearbeitete Signal dauerhaft auf Eingangslautheit, langsam geregelt, begrenzt und bei Stille eingefroren.

**Meter:** Ein- und Ausgang mit Peak, Peak-Hold, Clip-Lampe und LUFS, Phasenkorrelation, Gain-Reduction für Kompressor, De-Esser und beide Ducker. Dazu ein Mono-Check-Schalter, der bewusst nicht im Projekt gespeichert wird.

**Latenz:** 3 Samples (Oversampling der Saturation). Solange **Pitch** eingeschaltet ist, kommen etwa **120 ms** hinzu; das Plugin meldet sie dem Host, und FL Studio gleicht sie automatisch aus. Mehr dazu unter „Bekannte Einschränkungen“.

## Presets

| Kategorie | Presets |
|---|---|
| Lead | Clean Lead |
| EDM | Hypertechno Lead, Aggressive Party Vocal, Wide Festival Vocal |
| Pop | Bright Pop Vocal |
| Creative | Telephone Vocal, Vocal Chop, Octave Down Layer, Chipmunk Hook |
| Backing & Adlibs | Wide Adlibs |
| Dynamics | Heavy Parallel Compression |
| Classic (v0.1) | Clean Vocal, Modern Pop, Hypertechno Lead v0.1, Aggressive Vocal, Wide Vocal |
| Utility | Init |

- Presets setzen **alle** Module und das Routing vollständig.
- **Input-Gain und Level-Match-Einstellungen bleiben beim Preset-Wechsel unverändert**, weil sie zur Aufnahme bzw. zum Abhören gehören.
- Eigene Presets: *Save* in der Kopfleiste. Sie liegen unter `Dokumente\Lind Audio\Velvox\Presets\*.vvpreset` (XML, Format 2 mit Routing).

---

## Kompatibilität mit v0.1

- **Projekte:** v0.1-Projekte laden unverändert. Alle in v0.2 neuen Parameter werden dabei ausdrücklich auf ihren neutralen Standard gesetzt (Module aus), das Routing auf die v0.1-Reihenfolge. Ein v0.1-Projekt klingt dadurch genauso wie vorher; das ist per Test abgesichert.
- **User-Presets** im Format 1 (v0.1) laden weiterhin. Unbekannte neue Module bleiben aus, und es gilt die Standard-Reihenfolge.
- **Plugin-IDs** (`Lnda`/`Vlvx`) und alle Parameter-IDs sind unverändert, deshalb funktionieren bestehende FL-Automationen weiter.
- **Neu:** ein optionaler Sidechain-Eingang. Er ist standardmäßig deaktiviert; FL Studio bietet ihn in den Routing-Einstellungen des Plugins an.

---

## Voraussetzungen (Windows)

1. **Visual Studio 2022** mit der Workload **„Desktopentwicklung mit C++“** (MSVC v143, Windows SDK, CMake)
2. **Git**
3. **JUCE 8.0.9**:
   ```powershell
   git clone --depth 1 --branch 8.0.9 https://github.com/juce-framework/JUCE.git "$env:USERPROFILE\dev\JUCE-8.0.9"
   ```

Signalsmith Stretch und Signalsmith Linear (beide MIT) liegen bereits im Repository unter `third_party/`. Für den Build ist kein Netzzugriff nötig.

## Quellcode und Sicherung

- **Arbeitskopie (Git):** `C:\Users\jakob\dev\velvox`, Branch `main`, Release-Stände als Tags (`v0.1.0`, …)
- **Sicherung nach Google Drive:**
  ```powershell
  powershell -ExecutionPolicy Bypass -File .\scripts\backup-to-drive.ps1
  ```
  Das Skript schreibt ein vollständiges Git-Bundle mit der gesamten Historie nach `G:\Meine Ablage\MUSIC\Plugin\_git-backup\velvox.bundle` und kopiert die aktuellen Quellen in den Drive-Ordner. Wiederherstellen mit `git clone velvox.bundle velvox`.

## Bauen und testen

```powershell
cd C:\Users\jakob\dev\velvox
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1
```

Das Skript baut **VST3**, **Standalone** und den **Test-Runner** in `build\` (Git-ignoriert) und führt die Tests aus.

- VST3: `build\Velvox_artefacts\Release\VST3\Velvox.vst3`
- Standalone: `build\Velvox_artefacts\Release\Standalone\Velvox.exe`

### Automatisierte Prüfungen

Der Test-Runner `VelvoxTests.exe` umfasst **1850 Prüfungen**: DSP-Messungen mit echten Signalen, State und Migration, Presets, UI-Interaktion mit simulierten Maus-Ereignissen sowie Rechenlast. Auswahl der Messwerte:

| Bereich | Ergebnis |
|---|---|
| v0.1-Kette mit Standard-Routing | bitgenau wie v0.1 |
| Delay als letzter Insert gegen Send | identisch (Differenz 0) |
| Routing-Umbau während der Wiedergabe | klickfrei (größter Sprung 0,009) |
| K-Gewichtung / Kalibrierung | +0,69 dB @ 1 kHz; −20 dBFS-Sinus = −20,00 LUFS |
| Auto Gain / Bypass Match | Ausgang = Eingang ±0,0 LUFS; Bypass = bearbeitet ±0,02 LUFS |
| Width 0–200 % | Monosumme unverändert (< 1e-5) |
| Doubler | Mono safe 100 % → Monosumme bitgenau; Korrelation L/R 0,82 bei 0,01 dB Pegeldifferenz |
| Ducking | Reverb −12,0 dB unter dem Vocal, 0,0 dB in Pausen; externer Sidechain wirkt, Vocal allein nicht |
| Pitch | 1–6 Cent Genauigkeit über ±12 Halbtöne; gemeldete Latenz = gemessene Latenz |
| Chop | Stutter-Wiederholung bitgenau; alle Patterns klickfrei |
| Drag & Drop | Umsortieren, Send/Insert per Ziehen, unzulässige Ziele abgelehnt und erklärt |
| Presets | alle 17 laden, Durchschalten während der Wiedergabe ohne Übersteuerung |
| CPU, alle Module an | 3,7 % eines Kerns @ 48 kHz, 7,7 % @ 96 kHz |

Zusätzlich: **pluginval 1.0.4, Strenge 10**, inklusive Steinberg-VST3-Validator, Editor-Tests, Bus-Layouts und Parameter-Fuzzing. Ergebnis: **SUCCESS**.

UI-Snapshot ohne DAW: `VelvoxTests.exe --snapshot ui.png "Hypertechno Lead" 1280 pro 0` (Modul-ID 0–8, −1 = Output)

## Installation

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\install-vst3.ps1
```

Das Skript kopiert `Velvox.vst3` nach `C:\Program Files\Common Files\VST3\` (UAC-Bestätigung nötig) und ersetzt dabei v0.1.

## Test in FL Studio (Checkliste v0.2)

1. FL Studio starten. Ein bestehendes Projekt mit Velvox 0.1 öffnen: Es muss **gleich klingen** wie vorher.
2. Velvox in einen Mixer-Insert einer Vocal-Spur laden. Das Plugin startet im **Beginner**-Modus.
3. **Presets:** Den Preset-Namen anklicken, um das Menü nach Kategorien zu öffnen. „Hypertechno Lead“ und „Wide Festival Vocal“ ausprobieren.
4. **Makros:** Presence, Width und Space bewegen; der Klang muss sich hörbar ändern. In **Pro** zeigt ein weißer Punkt am Regler, wohin das Makro den Wert schiebt.
5. **Fair compare:** Auf *Bypass match* stellen und Bypass umschalten. Beide Varianten sind gleich laut.
6. **Routing:** In der Kette „Saturation“ vor „EQ“ ziehen. Delay auf die obere Reihe ziehen (wird Insert) und wieder zurück auf die untere Spur.
7. **Ducking:** Im Pro-Modus Reverb wählen, Amount auf 6–10 dB stellen. Der Hall tritt zurück, solange gesungen wird.
   - *Externer Sidechain:* In FL eine Kick-Spur per „Sidechain to this track“ auf den Mixer-Kanal mit Velvox routen, in den Wrapper-Einstellungen des Plugins (Zahnrad-Symbol, Bereich *Processing*) den Sidechain-Eingang von Velvox zuordnen und in Velvox *Duck source = Sidechain input* wählen. Ohne Zuordnung zeigt Velvox „No sidechain connected“ und folgt dem Vocal. Die genaue Bezeichnung im FL-Wrapper habe ich nicht selbst geprüft; bitte im Test bestätigen.
8. **Pitch:** Preset „Octave Down Layer“. FL meldet die neue Latenz; das Vocal bleibt im Timing zur Musik.
9. **Chop:** Preset „Vocal Chop“, Wiedergabe starten. Der Chop folgt dem Song-Tempo und der Position.
10. **Mono check:** Rechts unten einschalten. Der Klang soll nicht hohl oder leise werden; die Korrelationsanzeige bleibt im grünen Bereich.
11. **Speichern:** Projekt speichern, FL schließen, neu öffnen. Werte, Routing, A/B, Modus und Fenstergröße sind wiederhergestellt.
12. **Automation:** Einen Makro-Regler automatisieren (*Tools → Last tweaked → Create automation clip*).

---

## Projektstruktur

```
Source/
  PluginProcessor.*          Parameter, State v2 + Migration, Routing, Latenz, Sidechain-Bus, A/B
  PluginEditor.*             skalierbare Leinwand, Beginner/Pro, Hinweise, Meter
  StateKeys.h                Nicht-Parameter-Zustand (stateVersion, routing, uiMode)
  Parameters/                IDs + tabellengesteuerte Definitionen (Bereich, Einheit, Beschreibung)
  DSP/
    VocalChain               Routing-Reihenfolge, Sends/Inserts, Ausgangsstufe, Bypass, Meter
    Routing                  Modulreihenfolge + Send/Insert, lock-freie Übergabe
    Macros                   Makros als Offset-Schicht (reine Funktion)
    Equalizer/Compressor/DeEsser/Saturation/Doubler/Pitch/Chop/Delay/Reverb-Module
    Ducker, Loudness (BS.1770 + LevelMatcher), StereoStage (+ Korrelation)
  Presets/                   ParameterSnapshot (Werte + Routing), Factory-Presets, PresetManager
  Mixing/                    Schnittstelle Smart-Mixing (ohne KI) + ProposalSession
  UI/                        Theme, LookAndFeel, Widgets, ChainView, DetailPanels, BeginnerView, TopBar
Tests/                       Offline-Tests (JUCE UnitTest), UI-Snapshots
third_party/                 Signalsmith Stretch 1.4.0 + Linear 0.6.4 (MIT, header-only)
docs/ARCHITECTURE.md         Architekturentscheidungen
```

### Echtzeit- und Thread-Sicherheit
- Der Audio-Thread liest Parameter nur über Atomics. Das Routing wird als gepackter 64-Bit-Wert lock-free übergeben.
- Im Audio-Callback wird nichts alloziert. Alle Puffer und der Pitch-Shifter werden in `prepareToPlay` vorbereitet.
- Strukturänderungen (Reihenfolge, Pitch an/aus) werden unter einer kurzen Ausblendung übernommen.
- Die Latenzmeldung an den Host erfolgt auf dem Message-Thread.
- NaN/Inf am Ein- und Ausgang wird ersetzt. Module mit ungültigem Filterzustand setzen sich zurück.
- Hosts, die weniger Kanäle liefern als ausgehandelt, führen nicht zum Absturz.

### Vorbereitung Smart-Mixing (ohne KI)
Bereits vorhanden und getestet sind folgende Bausteine:
- das Parameter-Schema (`createParameterSchema`), also alle Parameter mit Beschreibung
- `ParameterSnapshot` inklusive Routing als einheitliches Format für Presets, A/B und Vorschläge
- die Makro-Abbildung `applyMacros`
- die `ProposalSession`: Vorschlag anwenden, mit dem Original vergleichen, übernehmen oder exakt verwerfen

Eine KI-Anbindung, Sprachsteuerung oder Audioanalyse gibt es **nicht**.

---

## Bekannte Einschränkungen (0.2.0)

- **In FL Studio noch nicht getestet:** v0.2 ist automatisiert und mit pluginval geprüft, der Test in FL Studio (Checkliste oben) steht aus. Besonders zu prüfen sind der Sidechain-Eingang und die Latenzänderung beim Ein- und Ausschalten von Pitch.
- **Pitch:**
  - Er erzeugt etwa 120 ms Latenz, solange er an ist, und eignet sich daher nicht zum Live-Einsingen mit Monitoring durch das Plugin.
  - Kürzere Fenster wurden gemessen und verworfen: Sie verstimmten um bis zu 70 Cent.
  - Den Pitch-Schalter nicht automatisieren, da jede Latenzänderung den Host neu kompensieren lässt.
  - Es handelt sich um **keine** Tonhöhenkorrektur (Key/Scale/Retune), sondern um einen kreativen Effekt.
- **Formant-Erhalt** kostet etwas Genauigkeit (bis ca. 6 Cent statt 2 Cent).
- **Doubler:** Bei Mono safe < 100 % entsteht in Mono ein leichter, wandernder Kammfilter-Anteil (gemessen und gegenüber 0 % stark reduziert).
- **Level-Matcher:** Die Fenster sind exponentielle Näherungen (0,4 s, 3 s, 10 s) und keine exakte integrierte BS.1770-Messung mit Gating. Auto Gain regelt langsam (6 dB/s) und hält bei Stille an.
- **Chop:** Ohne laufenden Host-Transport läuft er frei (Host-Tempo oder 120 BPM) und ist dann nicht taktgenau ausgerichtet.
- **Routing:** Ein Umbau erzeugt eine kurze Pegelabsenkung (etwa 2 × 8 ms), die bewusst statt eines Klicks gewählt ist. „Delay → Reverb“ wirkt nur, wenn beide als Send laufen.
- **Low Cut und High Cut** bieten 12/24 dB/Okt. 48 dB/Okt sind im DSP vorhanden, werden aber nicht angeboten, weil eine Erweiterung des bestehenden Parameters FL-Automationen verschieben würde.
- Es gibt keine Anzeige „Preset verändert“, kein Undo/Redo und keine Preset-Suche.
- Nur Windows x64 VST3 (plus Standalone für Entwicklung), kein signierter Installer, kein Kopierschutz.
- Lizenzen: Für einen closed-source Verkauf ist eine JUCE-Lizenz nötig, siehe [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).
