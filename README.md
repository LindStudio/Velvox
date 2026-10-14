# Velvox – Vocal Chain (Version 0.1.0)

Velvox ist ein VST3-Vocal-Plugin für Windows (JUCE 8, C++20) mit einer festen, nachvollziehbaren Signalkette:

```
Input ─ EQ ─ Kompressor ─ De-Esser ─ Saturation ─┬───────────────── (dry) ─┬─ Output
                                                 ├─ Delay  (nur Effektanteil) ┤
                                                 └─ Reverb (nur Effektanteil) ┘
```

Delay und Reverb liegen parallel und liefern **nur** ihren Effektanteil zurück. Das trockene Signal ist dadurch genau einmal im Ausgang enthalten; das ist per Test abgesichert.

„Velvox“ und „Lind Audio“ sind Arbeitstitel. Vor einem Verkauf ist eine Markenrecherche nötig.

---

## Funktionen

| Modul | Regler | Umsetzung |
|---|---|---|
| **EQ** | Low Cut (an/aus, 20–600 Hz, 12/24 dB/Okt), Low Shelf (Freq/Gain), Mid Bell (Freq/Gain/Q), High Shelf (Freq/Gain), Mix | Biquads (RBJ-Cookbook), geglättete Koeffizienten, Live-Frequenzkurve aus derselben Filterberechnung |
| **Kompressor** | Threshold, Ratio, Attack, Release, Knee, Makeup, Mix | Feed-Forward, Stereo-Link, Soft-Knee, Gain-Reduction-Anzeige |
| **De-Esser** | Frequency, Threshold, Range (Intensität), Listen | Detektor-Hochpass steuert einen dynamischen High-Shelf: Nur das Band über der Frequenz wird abgesenkt |
| **Saturation** | Drive, Output, Mix | asymmetrische tanh-Kurve, 2× Oversampling, automatischer Pegelausgleich, DC-Blocker |
| **Delay** | Time (1–2000 ms) bzw. Notenwert bei Sync, Feedback (max. 90 %), Mix, Low Cut, High Cut, Ping-Pong | Hermite-Interpolation, Filter und Soft-Limiter in der Rückkopplung; Sync nutzt das Host-Tempo und fällt ohne Tempo auf ms zurück |
| **Reverb** | Room/Plate, Mix, Decay (RT60), Pre-Delay, Size, Tone, Low Cut, Width | eigenes 8-Kanal-Feedback-Delay-Network mit Diffusion und Modulation |
| **Global** | Input Gain, Output Gain, Clip Guard, Bypass, A/B | Ein-/Ausgangsmeter mit Peak-Hold und Clip-Lampe; der Bypass ist latenzkompensiert |

- Jedes Modul lässt sich einzeln ein- und ausschalten (Power-Knopf oder Klick auf den Knoten im Kettenstreifen). Das Umschalten wird überblendet und erzeugt keine Klicks.
- Alle 51 Parameter sind automatisierbar und werden mit dem DAW-Projekt gespeichert.
- **Presets:** Init, Clean Vocal, Modern Pop, Hypertechno Lead, Aggressive Vocal, Wide Vocal. Eigene Presets lassen sich speichern, laden und löschen.
- **A/B:** zwei vollständige Einstellungs-Slots, „Copy A to B“; beide werden im Projekt mitgespeichert.
- **Latenz:** konstant 3 Samples (durch das Oversampling der Saturation), dem Host gemeldet.
- Mono- und Stereo-Spuren; beliebige Blockgrößen und Sample-Rates (getestet: 44,1 kHz bis 192 kHz, Blöcke von 1 bis 4096 Samples).
- Das Fenster ist frei skalierbar (75–160 %, festes Seitenverhältnis); die Größe wird im Projekt gespeichert.
- Tooltips erklären jeden Regler. Doppelklick setzt den Standardwert zurück, ein Klick auf den Wert erlaubt die direkte Eingabe.

---

## Voraussetzungen (Windows)

1. **Visual Studio 2022** mit der Workload **„Desktopentwicklung mit C++“** (enthält MSVC v143, Windows 11 SDK und CMake). Die Community-Edition genügt.
2. **Git**
3. **JUCE 8.0.9** außerhalb des Google-Drive-Ordners:
   ```powershell
   git clone --depth 1 --branch 8.0.9 https://github.com/juce-framework/JUCE.git "$env:USERPROFILE\dev\JUCE-8.0.9"
   ```

> Der Quellordner liegt in Google Drive. Build-Verzeichnis und JUCE liegen deshalb unter `%USERPROFILE%\dev\`, damit Drive keine Tausende von Build-Dateien synchronisiert oder sperrt.

## Bauen

```powershell
cd "G:\Meine Ablage\MUSIC\Plugin"
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1
```

Das Skript konfiguriert CMake (Visual Studio 2022, x64), baut **VST3**, **Standalone** und den **Test-Runner** und führt die Tests aus.

Ergebnis:
- VST3: `%USERPROFILE%\dev\velvox-build\Velvox_artefacts\Release\VST3\Velvox.vst3`
- Standalone: `%USERPROFILE%\dev\velvox-build\Velvox_artefacts\Release\Standalone\Velvox.exe`

Manuell geht es auch so:
```powershell
cmake -S . -B $env:USERPROFILE\dev\velvox-build -G "Visual Studio 17 2022" -A x64 -DJUCE_DIR=C:/Users/<du>/dev/JUCE-8.0.9
cmake --build $env:USERPROFILE\dev\velvox-build --config Release --parallel
```

## Tests

`VelvoxTests.exe` prüft die echte Signalverarbeitung ohne DAW (243 Prüfungen). Gemessene Ergebnisse des letzten Laufs:

| Prüfung | Ergebnis |
|---|---|
| Neutralstellung = exakte Durchleitung (verzögert um die Latenz) | Fehler < 1e-5 |
| EQ-Bell +12 dB @ 1 kHz | 12,00 dB |
| Low Cut 24 dB/Okt @ 200 Hz, Ton bei 50 Hz | −48,2 dB |
| Kompressor, −6 dBFS Sinus, Thr −30 dB, 4:1 | −23,8 dBFS (Theorie −24) |
| De-Esser: 8 kHz / 200 Hz | −10,4 dB / 0,00 dB |
| Saturation: Obertöne H2+H3 bei 18 dB Drive | −11,3 dB rel. Grundton |
| Delay 100 ms: Echo an richtiger Stelle, Dry genau einfach | ✔ |
| Delay mit 90 % Feedback: Ausklingen nach 8 s | ✔ |
| Reverb RT60 2 s: Abfall über 1 s | 33,9 dB (ideal 30) |
| NaN/Inf am Eingang, Mono, 5 Sample-Rates × 5 Blockgrößen | ✔ |
| Presets, User-Presets (Speichern/Laden/Löschen), Projekt-State, A/B | ✔ |

Zusätzlich wurde das VST3 mit **pluginval 1.0.4 auf Strenge 10** geprüft, einschließlich Steinbergs VST3-Validator, Editor-Tests, Parameter-Thread-Safety und Fuzzing. Ergebnis: **SUCCESS**.

```powershell
pluginval.exe --strictness-level 10 --validate-in-process --validate "$env:USERPROFILE\dev\velvox-build\Velvox_artefacts\Release\VST3\Velvox.vst3"
```

UI-Snapshot ohne DAW: `VelvoxTests.exe --snapshot ui.png "Hypertechno Lead" 1240`

## Installation

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\install-vst3.ps1
```

Das Skript kopiert `Velvox.vst3` nach `C:\Program Files\Common Files\VST3\` (UAC-Bestätigung nötig).
Zum Deinstallieren den Ordner `C:\Program Files\Common Files\VST3\Velvox.vst3` löschen.

## Test in FL Studio

1. **Plugin finden:** *Options → Manage plugins* → sicherstellen, dass `C:\Program Files\Common Files\VST3` in den Suchpfaden steht → **Find installed plugins**. „Velvox“ (Hersteller „Lind Audio“) erscheint unter *Effects*.
2. **Einfügen:** Eine Vocal-Spur auf einen Mixer-Insert routen und Velvox in einen freien Effekt-Slot laden.
3. **Funktionstest:**
   - Preset „Hypertechno Lead“ wählen: Der Klang wird heller und dichter, Delay und Reverb sind hörbar.
   - **Bypass** oben rechts zum Vergleich mit dem Original.
   - Einzelne Module per Power-Knopf oder per Klick im Kettenstreifen schalten.
   - De-Esser: **Listen** einschalten, dann *Frequency* auf die S-Laute stimmen und Listen wieder ausschalten.
   - Delay **Sync**: Im Panel steht die resultierende Zeit („… ms at host tempo“). Bei Tempoänderung in FL passt sich die Zeit an.
4. **Automation:** Einen Regler bewegen → *Tools → Last tweaked → Create automation clip* (oder Rechtsklick auf den Regler im Wrapper). Der Clip steuert den Parameter.
5. **Speichern:** Einstellungen verändern, Projekt speichern, FL Studio schließen, Projekt wieder öffnen. Alle Werte, das aktive Preset, A/B-Slots und die Fenstergröße sind wiederhergestellt.
6. **User-Presets:** *Save* → Namen eingeben. Gespeichert wird unter `Dokumente\Lind Audio\Velvox\Presets\*.vvpreset`.

Für die Pegelmessung: Die Meter rechts zeigen den Ausgang. Die rote Lampe zeigt Übersteuerung über 0 dBFS (vor dem Clip Guard); ein Klick darauf setzt sie zurück.

---

## Projektstruktur

```
Source/
  PluginProcessor.*        AudioProcessor: APVTS, State, A/B, Bypass-Parameter, Tempo vom Host
  PluginEditor.*           Editor: skalierbare Leinwand, 30-Hz-Timer für Meter
  ProductInfo.h            Produktname/Version
  Parameters/              Parameter-IDs + tabellengesteuerte Definition (Bereich, Einheit, Beschreibung)
  DSP/                     Reine Signalverarbeitung, keine Parameter- oder UI-Abhängigkeit
    Filters, DspUtilities  Biquads, Modul-Überblendung, Latenzausgleich, lock-freie Meter
    *Module                EQ, Kompressor, De-Esser, Saturation, Delay, Reverb
    VocalChain             Reihenfolge, parallele Returns, Gains, Clip Guard, Bypass
  Presets/                 ParameterSnapshot (gemeinsames Format), Factory-Presets, PresetManager
  Mixing/MixingEngine.h    Schnittstelle für eine spätere Smart-Mixing-Funktion (nicht implementiert)
  UI/                      LookAndFeel, Regler/Meter/Kurven, Modul-Panels, Kopfleiste, Kettenstreifen
Tests/                     Offline-Test-Runner (JUCE UnitTest) + UI-Snapshot
scripts/                   build.ps1, install-vst3.ps1
```

### Thread-Sicherheit
- Der Audio-Thread liest Parameter ausschließlich über die atomaren Werte der APVTS (`getRawParameterValue`) und baut daraus pro Block ein `ChainSettings`-Objekt.
- Im Audio-Callback gibt es keine Allokationen: Alle Puffer werden in `prepareToPlay` angelegt, Host-Blöcke beliebiger Größe werden intern in 256-Sample-Stücken verarbeitet.
- Meter laufen lock-frei (Atomics, compare-exchange); die UI liest sie per Timer.
- Presets und A/B laufen auf dem Message-Thread mit `begin/endChangeGesture`, damit der Host Änderungen sauber mitbekommt.
- Ungültige Werte (NaN/Inf) werden am Ein- und Ausgang ersetzt. Ein Modul mit ungültigem Filterzustand setzt sich selbst zurück.

### Vorbereitung auf die spätere KI-Funktion
Version 1 enthält **keine** KI, keine Sprachsteuerung und keine Audioanalyse. Vorbereitet ist Folgendes:
- `createParameterSchema()` liefert alle Parameter mit Bereich, Einheit und Beschreibung als JSON. Das ist der Vertrag, den eine Engine kennen muss.
- `ParameterSnapshot` ist das gemeinsame Austauschformat für Presets, A/B und zukünftige Vorschläge. Ein Vorschlag wird genauso angewendet wie ein Preset (automationsfähig, per A/B vergleichbar).
- `Mixing/MixingEngine.h` definiert `SuggestionProvider` (asynchron, nie im Audio-Thread) mit Anfrage (Text, Genre, aktuelle Einstellungen) und Antwort (Snapshot und Begründung).

---

## Bekannte Einschränkungen (Version 0.1.0)

- **Nicht selbst in FL Studio getestet.** Der Build, die Offline-Tests und pluginval (inkl. Steinberg-Validator) sind bestanden; den manuellen Test in FL Studio (Abschnitt oben) muss ein Mensch durchführen.
- **Bypass gleicht keinen Pegel an.** Ein lauteres Preset klingt im Vergleich „besser“; für faire Vergleiche Output Gain anpassen.
- Der **Clip Guard** ist ein Soft-Clipper ohne Lookahead und ohne Oversampling. Er schützt vor Übersteuerung, ist aber kein Mastering-Limiter; bei starken Übersteuerungen entsteht Aliasing.
- **Reverb und Delay** sind eigene Algorithmen. Die Klangqualität ist technisch geprüft, aber noch nicht im Hörvergleich mit kommerziellen Plugins abgestimmt.
- Eine Änderung der **Delay-Zeit** während der Wiedergabe erzeugt einen kurzen Tonhöhen-Effekt (Tape-artig). Ein Wechsel des **Reverb-Typs** startet den Hall neu.
- **Tempo-Sync** braucht einen Host, der das Tempo meldet; sonst wird die ms-Zeit verwendet (im Panel angezeigt).
- Es gibt keine Anzeige „Preset verändert“, kein Undo/Redo und keine Preset-Kategorien oder -Suche.
- `setStateInformation` wird auf dem Message-Thread erwartet (so verhält sich FL Studio). Hosts, die es aus anderen Threads aufrufen, sind nicht gezielt getestet.
- Nur Windows x64 und nur VST3 (plus Standalone für Entwicklungszwecke). AU/macOS, AAX und CLAP sind nicht gebaut.
- Es fehlen ein signierter Installer und ein Lizenz- bzw. Kopierschutzsystem.
- Lizenzen: Für einen closed-source Verkauf ist eine JUCE-Lizenz erforderlich, siehe [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).
