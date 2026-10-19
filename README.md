# Velvox – automatischer Vocal-Mixer (Version 0.3.0, Entwicklungsstand)

Velvox ist ein VST3-Plugin für Windows (JUCE 8.0.9, C++20) für EDM, Hypertechno, Hard Dance, Party- und Pop-Vocals. Es erfasst während der Wiedergabe einen begrenzten Abschnitt der Vocal, analysiert ihn, stellt die vorhandenen Module danach ein und prüft das Ergebnis, bevor es angewendet wird. Alle Module bleiben danach frei einstellbar.

„Velvox“ und „Lind Audio“ sind Arbeitstitel. Vor einem Verkauf ist eine Markenrecherche nötig.

> **Ehrlicher Stand:** Die automatische Mischung ist regelbasiert und messwertgestützt, nicht „KI“. Sie wurde mit synthetischen Testsignalen und zehn realen Vocal-Aufnahmen technisch geprüft (siehe unten). Ein **Hörtest durch Menschen** und ein **Test in FL Studio** stehen für 0.3 noch aus. Ein perfekter Mix für jede Aufnahme ist nicht garantiert.

---

## Ablauf

1. Velvox auf den Mixer-Kanal der Vocal laden. Beim **ersten Öffnen einer neuen Instanz** erscheint „Let's mix your vocal.“
2. **Audio analysieren** klicken und die Vocal in der DAW abspielen.
3. Velvox wartet auf Signal, erfasst einen begrenzten Abschnitt (Ziel: 14 s Gesang, höchstens 24 s inklusive kurzer Lücken; Stille wird übersprungen) und stoppt von selbst.
4. Analyse, Planung und Prüfung laufen im Hintergrund. Die Wiedergabe läuft ungestört weiter, auch bei geschlossenem Fenster.
5. Die Einstellungen werden über die normalen Parameter gesetzt (DAW-Automation, Presets, A/B sehen sie). Die Ansicht **Auto Mix** zeigt, was eingestellt wurde und warum.
6. Vergleichen: **Vorher / Velvox Mix** (echter Parameterwechsel), **Original** (Bypass, mit „Fairer Vergleich = Bypass match“ in gleicher Lautheit), **Vorherigen Zustand wiederherstellen**.
7. **Neu analysieren** ist jederzeit verfügbar (Kopfzeile und Auto-Mix-Ansicht).

Instanzverhalten: Das Popup erscheint nur bei einer neuen, noch nicht analysierten Instanz. „Später“ wird gespeichert. Projekte älterer Versionen und analysierte Instanzen fragen nicht erneut. Presets und A/B starten nie eine Analyse.

## Was gemessen wird

| Bereich | Messung |
|---|---|
| Pegel | integrierte Lautheit (ITU-R BS.1770, mit Gating), Lautheitsumfang (EBU Tech 3342), Wortdynamik (400-ms-Pegel, 10.–95. Perzentil), Spitzenpegel, Clipping (≥ 3 Samples auf Vollaussteuerung), Crest-Faktor, Rauschboden und Rauschabstand (nur wenn Pausen erfasst wurden) |
| Tonhöhe | Autokorrelation je Frame: tiefe / mittlere / hohe Töne, gesungene Noten, Anteil tonaler Frames (Gesang vs. Sprechgesang) |
| Spektrum | 1/12-Oktav-Langzeitspektrum der Stimme; Rumpeln, Körper, Tiefmitten, Präsenz, S-Bereich, Luft, Neigung |
| S-Laute | S-Frames gegen Vokal-Frames, Schwerpunktfrequenz, Detektorpegel für die De-Esser-Schwelle |
| Resonanzen | schmale Spitzen über der Umgebung; Teiltöne der gesungenen Noten werden ausgeschlossen |
| Raum | Abklinggeschwindigkeit nach Wörtern (vorhandener Raum oder Effekte) |
| Stereo | Korrelation, Balance, Seiten- zu Mittenanteil |
| Beat-Sidechain (optional) | Lautheit und Überlappung im Präsenzbereich – nur als Hinweis, nie hörbar |

Jede Auffälligkeit trägt eine Sicherheit: *gemessen*, *wahrscheinlich* oder *möglich*. Velvox behauptet keine Stimmtyp-Erkennung.

## Wie entschieden wird (Auszug)

- **Low Cut** bei 0,6 × tiefster gesungener Ton (70–150 Hz); bei Rumpeln steiler und etwas höher.
- **EQ:** Höhen nur bei dunklen Aufnahmen (begrenzt bei Rauschen/S-Lauten), nie bei scharfen; Tiefmitten- oder Resonanzschnitt nur bei klarem Befund; Wärme nur bei tiefen Stimmen.
- **Level Ride** bei großer Wortdynamik, Anhebung begrenzt bei Rauschen; nicht bei komprimiertem Material.
- **Kompressor** (RMS) mit Schwelle aus dem Stimmpegel, Ziel wenige dB Reduktion; sanft bei bereits komprimierten Aufnahmen.
- **De-Esser** mit Schwelle zwischen Vokal- und S-Pegel; dezent vorsorglich, kräftiger bei ausgeprägten S-Lauten.
- **Smooth** (adaptive Resonanzkontrolle) nur bei Schärfe oder Resonanzen über 1,2 kHz.
- **Sättigung, Vocal Smash, Doubler** dezent; aus bei Clipping, starker Kompression, bereits breiten oder gesprochenen Aufnahmen.
- **Hall und Delay** als geduckte Sends; weniger oder keiner bei Aufnahmen mit Raum; kürzer/trockener bei Sprechgesang.
- **Kreativ-Module** (Pitch, Chop, Pulse, Choir) bleiben aus.

**Prüfung vor dem Anwenden:** Der erfasste Abschnitt wird durch eine private Kopie der kompletten Kette gerechnet (gleiche Parameterabbildung wie im Plugin). Korrigiert wird nur, wo die Messung zuverlässig ist: Kompression zu stark/zu schwach, S-Laute durch die Kette angehoben, Höhen oder Körper verloren, Monoverlust. Danach wird der Ausgang auf die Lautheit des Originals gebracht (−24 bis −14 LUFS) mit 1 dB Reserve. Clip Guard bleibt aktiv.

## Module

EQ, Level Ride, Compressor, Vocal Smash, De-esser, Smooth (adaptive Resonanzkontrolle, 32 Bänder), Saturation, Doubler, Choir, Pitch (Signalsmith Stretch), Delay, Reverb, Chop, Pulse, Vocal Check, dazu Ein-/Ausgangsstufe mit Stereo-Breite, Level-Matcher, Clip Guard, Mono-Check und A/B. Delay und Reverb laufen als Send oder Insert; die Reihenfolge ist per Drag & Drop änderbar.

Grenzen: **Choir** ist eine DSP-Stimmvervielfachung (Pitch-Shift, Timing, Panorama) und kein Chor-Synthesizer. **Pitch** ist ein kreativer Pitch-Shifter, keine Tonhöhenkorrektur.

## Bedienung

- Ansichten **Auto Mix** (Status, eingestellte Schritte, Vergleich, Feinschliff mit sechs Makros) und **Module** (alle Parameter). Ein Klick auf eine Modulkarte öffnet das Modul.
- Modulkarten zeigen An / Aus / Bypass / Auswahl und ein **Auto**-Abzeichen für vom Auto-Mix eingestellte Module.
- Regler: Ziehen, Shift/Strg fein, Doppelklick = Standardwert, Mausrad, Wert anklicken zum Eintippen. Nach kurzem Verweilen zeigt eine Infokarte Name, aktuellen Wert, Wirkung und die Rolle im Auto-Mix.
- Hilfe (offline) mit Seite „Automatic mix“ und Tour.

## Presets und Kompatibilität

- Parameter-IDs und Plugin-Codes (`Lnda`/`Vlvx`) sind unverändert; Automationen bleiben gültig.
- State-Version 3: zusätzlich ein Knoten „AutoMix“ (Bericht, Zustand vor dem Mix, Popup-Status). Ältere Projekte laden unverändert und zeigen kein Analyse-Popup.
- Presets setzen alle Module und das Routing; Input-Gain und Level-Match bleiben unverändert.

---

## Bauen und testen

Voraussetzungen: Visual Studio 2022 (Desktopentwicklung mit C++), Git, JUCE 8.0.9 unter `%USERPROFILE%\dev\JUCE-8.0.9`.

```powershell
cd C:\Users\jakob\dev\velvox
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1
```

Baut VST3, Standalone und den Test-Runner (`build\`) und führt alle Prüfungen aus.

Nützliche Aufrufe des Test-Runners (`build\VelvoxTests_artefacts\Release\VelvoxTests.exe`):

| Aufruf | Zweck |
|---|---|
| `--test "automatic mix"` | nur eine Testgruppe |
| `--automix <in.wav> <ordner>` | Auto-Mix auf eine echte Aufnahme anwenden: Abschnitt wie im Plugin erfassen, analysieren, ganze Datei rendern; schreibt `<name>_velvox.wav` und `<name>_report.txt` |
| `--snapshot out.png <preset> 1440 beginner 0 intro` | UI ohne DAW rendern (Overlays: `intro`, `listening`, `capturing`, `mixed`, `mixed+done`, `help:<seite>` …). In PowerShell keinen leeren String als Presetnamen übergeben. |

Zusätzlich geprüft mit **pluginval 1.0.4, Strenge 10**: SUCCESS.

## Installation

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\install-vst3.ps1
```

Kopiert `Velvox.vst3` nach `C:\Program Files\Common Files\VST3\` (UAC-Bestätigung nötig).

## Test in FL Studio (Checkliste 0.3)

1. Velvox auf einen Mixer-Insert mit einer Vocal laden → Popup „Let's mix your vocal.“
2. *Audio analysieren*, dann Wiedergabe starten. Fortschritt läuft; bei gestoppter Wiedergabe erscheint ein Hinweis.
3. Nach der Analyse: hörbare Veränderung, Liste der Schritte, Auto-Abzeichen auf den Modulkarten.
4. *Vorher / Velvox Mix* und *Original* umschalten – gleich laut?
5. Ein Modul öffnen und ändern; Feinschliff-Makros bewegen.
6. Projekt speichern, FL schließen, neu öffnen: kein Popup, Bericht und Vergleich sind noch da.
7. Zweite Instanz auf einem anderen Kanal: Popup erscheint dort wieder.
8. Optional: Beat per Sidechain zuordnen, neu analysieren – Status „Beat-Sidechain analysiert“, Beat nicht im Ausgang hörbar.
9. Eine Preset-Auswahl darf keine Analyse starten.

## Projektstruktur

```
Source/
  PluginProcessor.*      Parameter, State v3, Routing, Latenz, Sidechain, A/B, Erfassungs-Abgriff
  PluginEditor.*         Leinwand, Overlays, Ansichten
  Parameters/            IDs, Parameterdefinitionen, SettingsReader (Parameter -> DSP, auch offline)
  AutoMix/               CaptureBuffer, VocalProfiler, AutoMixPlanner, AutoMixEngine, AutoMixController
  DSP/                   VocalChain und alle Module
  Presets/               Snapshots, Factory-/User-/Modul-Presets
  UI/                    Theme, LookAndFeel, Widgets, ChainView, AutoMixView, AnalysisOverlay, DetailPanels, Hilfe
Tests/                   Offline-Tests (JUCE UnitTest), SyntheticVocal.h, UI-Snapshots
third_party/             Signalsmith Stretch 1.4.0 + Linear 0.6.4 (MIT)
docs/ARCHITECTURE.md     Architekturentscheidungen
```

## Echtzeit-Regeln

- Der Audio-Thread liest Parameter über Atomics und kopiert während einer Erfassung nur Samples in einen vorab reservierten, begrenzten Puffer (keine Allokation, keine Locks, keine Datei- oder Netzzugriffe).
- Analyse, Planung und Prüf-Renderings laufen auf einem eigenen Thread mit niedriger Priorität; Ergebnisse werden auf dem Message-Thread angewendet.
- Keine Netzwerkzugriffe, keine Cloud, keine externen KI-Dienste.

## Bekannte Einschränkungen (0.3.0)

- **Nicht in FL Studio getestet** und **nicht von Menschen gegengehört** – nur kompiliert, automatisch geprüft und mit pluginval validiert.
- Analysiert wird nur der erfasste Abschnitt, nicht die ganze Spur.
- Raum-/Hallerkennung braucht erkennbare Pausen; eine durchgehend verhallte Aufnahme ohne Pausen wird nicht immer erkannt.
- Rauschabstand ist nur messbar, wenn Pausen mit Rauschen erfasst wurden (digitale Stille wird nicht gespeichert).
- Resonanzerkennung ist eine Langzeit-Spektralmessung; bei sehr wenig Tonhöhenwechsel können Teiltöne verbleiben.
- Die Beat-Sidechain liefert nur Hinweise (Pegel, Überlappung im Präsenzbereich), keinen Maskierungsnachweis.
- Oberfläche: Auto-Mix-Ablauf deutsch, Modultexte und Hilfe englisch.
- Die Dateien des früheren Text-Assistenten (`Source/Assistant/*`, `Source/UI/AssistantView.*`, `Source/UI/BeginnerView.*`, `Tests/AssistantTests.cpp`) liegen noch im Repository, werden aber nicht mehr gebaut.
- Nur Windows x64 VST3 (plus Standalone), kein signierter Installer. Für einen closed-source Verkauf ist eine JUCE-Lizenz nötig ([THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md)).
