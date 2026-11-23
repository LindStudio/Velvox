# EMBER De-Esser – QA-Testbericht (Branch `r2/ember-qa`, 2026-11-23)

Geprüft wurde der Stand `feature/ember-fixes` inklusive Merge von `r2/ember-presets` (8cb8cec: 29 Werks-Presets,
Standard 6 kHz / −34 dB). Getestet über die echte `AudioProcessor`-/Editor-API, nicht nur über die Engine.
Maschine: AMD EPYC 9354 (Cloud-PC, mit anderen Agenten geteilt; CPU-Zahlen = Median aus 3 Läufen, Last schwankte
– ein Lauf unter Last ergab bis zu +25 %).

## Suite
- `Products/Ember/Tests/EmberQaTests.cpp` (+ `EmberQaSys.cpp` für Speicher/Handles/GDI über
  `GetProcessMemoryInfo`, `GetProcessHandleCount`, `GetGuiResources`; gemeinsame Signale in `EmberTestSignals.h`).
- `EmberTests --qa`: schneller Teil (≈ 12 s), läuft im normalen Gesamtlauf mit.
- `EmberTests --qa-full`: zusätzlich ganze Raten×Block-Matrix, CPU, 10 Minuten Audio, Editor-/Instanz-Schleifen,
  echte Takes (`EMBER_VOCALS`), ≈ 75 s.

| Lauf | Ergebnis |
|---|---|
| `EmberTests` (alles, mit `EMBER_VOCALS`, 15 Takes) | **1993 bestanden, 0 fehlgeschlagen**, „15 of 15 takes set up“ |
| davon QA schnell | 381 bestanden |
| `EmberTests --qa-full` (mit `EMBER_VOCALS`) | **438 bestanden, 0 fehlgeschlagen**, „15 of 15 takes clean“ |
| pluginval 1.0.4, Strenge 10, `--validate-in-process`, mit GUI-Tests | **SUCCESS** |

Hinweis: Der bestehende Test `rig: 2x screen under 16 ms a frame` ist eine Zeitmessung und schlug vor meinen Änderungen
einmal unter Maschinenlast fehl; im letzten Lauf grün.

## Testmatrix
- Abtastraten 44,1 / 48 / 88,2 / 96 / 176,4 / 192 kHz; Blockgrößen 1, 7, 32, 64, 128, 256, 512, 1024, 2048, 4096 und
  zufällig wechselnd (1…4096); größere Blöcke als in `prepareToPlay` angekündigt (64 → 8192).
- Mono-Layout, Stereo, Dual-Mono (Mono→Stereo wird abgelehnt, wie vorgesehen).
- Signale: Stille, Rauschen −80 dBFS, Sibilanz −60 dBFS, 0 dBFS, +6 dBFS (Float-Overs), DC 0,5, Sinus-Sweep
  20 Hz–0,45·fs, weißes Rauschen, synthetische Sibilanz stark/dezent, 15 echte Takes.
- Automation aller 16 Parameter pro Block (auch Modus, Erkennung, Abhören, Stereo, Bypass), Preset-Wechsel im Lauf,
  A/B, Undo, State-Wiederherstellung (auch aus anderem Thread während Audio läuft).

## Messwerte

### Latenz (Impuls, gemeldet == gemessen)
| Rate | gemeldet | gemessen (Lookahead 0/1/2,5/5/7,5/10 ms × Split/Breitband/Bypass) |
|---|---|---|
| 44,1 kHz | 441 | 441 (alle 18 Fälle) |
| 48 kHz | 480 | 480 |
| 88,2 kHz | 882 | 882 |
| 96 kHz | 960 | 960 |
| 176,4 kHz | 1764 | 1764 |
| 192 kHz | 1920 | 1920 |

### Null / Verzerrung
- Ohne Absenkung bit-genau (60 Fälle: alle Raten, Split/Breitband; Sweep unter Threshold, −80-dBFS-Rauschen,
  Amount 0 %, Bypass mit +6 dBFS, tiefe Töne mit Smart).
- Nach einem S wieder bit-genau: Smart sofort (Gate zu), Relative/Absolute nach 0,92 s (vorher ≈ 5 s, Fehler 8).
- THD+N unter Absenkung (48 kHz, Absolute, Ton 12 dB über Threshold −30, Range 12):

| Modus | Ton | Absenkung | THD+N (0,5/60 ms) | THD+N (0,1/400 ms) |
|---|---|---|---|---|
| Split | 7 kHz | 8,6 dB | −70,7 dB | −78,2 dB |
| Split | 9 kHz | 8,7 dB | −70,4 dB | −86,3 dB |
| Split | 11 kHz | 7,9 dB | −71,7 dB | −78,4 dB |
| Split | 16 kHz | 5,8 dB | −81,0 dB | −97,4 dB |
| Breitband | 7 kHz | 8,8 dB | −70,7 dB | −78,2 dB |
| Breitband | 9 kHz | 8,8 dB | −70,2 dB | −86,2 dB |
| Breitband | 11 kHz | 8,8 dB | −70,6 dB | −77,2 dB |
| Breitband | 16 kHz | 8,8 dB | −76,2 dB | −92,6 dB |

### Genauigkeit der Absenkung (statisch, Dauerton, Absolute, Attack 0,1 / Release 400 ms)
768 Fälle (Threshold −50…−10, 6 dB unter bis 20 dB über Threshold, Range 3/6/12/24, Amount 25/50/100 %):
- gemessen vs. Kurve (4:1, Knie 6 dB, Range, Amount, 24-dB-Decke): **≤ 0,035 dB Split** (Ton in Bandmitte, gegen die
  eigene Bandform), **≤ 0,019 dB Breitband**.
- Absenkungs-Anzeige vs. gemessen: ≤ 0,034 dB Split, ≤ 0,002 dB Breitband; S-Pegel-Anzeige vs. Detektorpegel ≤ 0,005 dB;
  Relative-Detektion −30,13 statt −30,15 dB.
- Standardzeiten (0,5/60 ms): 8,78 statt 9,00 dB (Hüllkurve sitzt knapp unter dem Scheitel).
- Mix: Anzeige folgt jetzt der echten Absenkung (Mix 75/50/25/0 %: 5,74/3,38/1,53/0,00 dB, gemessen identisch).

Auszug (Threshold −20 dB):

| Modus | über Thr | Range | Amount | erwartet | gemessen | Anzeige |
|---|---|---|---|---|---|---|
| Split | 0 | 6 | 50 % | 0,56 | 0,56 | 0,56 |
| Split | +3 | 6 | 100 % | 3,39 | 3,37 | 3,36 |
| Split | +12 | 24 | 100 % | 13,47 | 13,43 | 13,47 |
| Breitband | +3 | 24 | 50 % | 2,25 | 2,24 | 2,24 |
| Breitband | +12 | 6 | 100 % | 9,00 | 9,00 | 9,00 |
| Breitband | +12 | 24 | 100 % | 13,50 | 13,48 | 13,49 |

### Klicks (Hochpass 8. Ordnung > 24 kHz bei 96 kHz, lautestes 1-ms-Fenster gegen Signal-RMS; Signal 200 Hz + 7 kHz)
Referenzen: Eingang −101,8 dB, harter 5-dB-Sprung −43,6 dB, Absenkung setzt wie bei einem S ein −71,9 dB.
Grenze: −68,9 dB (Einsatz + 3 dB). Alle 53 Umschaltungen darunter, z. B. Bypass ein/aus −101 dB, Abhören −92…−104 dB,
Split↔Breitband −105,5 dB, Stereo-Modus −93…−97 dB, Stereo-Modus während „S-Bereich“ −89 dB (vorher −27,5 dB, Fehler 5),
Erkennung/Lookahead −106 dB, alle 28 Werks-Preset-Wechsel ≤ −73 dB, lautester Fall Breite 1→4 Okt. −69,9 dB
(26 dB unter hartem Sprung, entspricht einem S-Einsatz).

### Automation / Aussetzer
132 Läufe (6 Raten × 11 Blockvarianten × mit/ohne Abhören), alle Parameter pro Block zufällig: endlich, keine
Denormals, größter Sample-Sprung 0,83 der Grenze (4 × der des Eingangs), Spitze 0,76 von 4 × Eingang, keine Nullfolgen.
Echte Takes mit Preset-Wechsel alle 0,4 s + Amount-Automation: alle 15 sauber (Spitze ≤ 1,05 × Eingang,
Sprung ≤ 1,5 × Eingang), Delta-Identität ≤ 2e-6, deterministisch.

### Matrix / Determinismus
1320 Läufe: **bit-identisch für jede Blockgröße** (auch wechselnd und größer als angekündigt), Stille bleibt Stille,
Ausgangsspitze höchstens 1,10 × Eingang (weißes Rauschen bei 192 kHz; Phasengang der Glocke). Mono = Dual-Mono.
Zweimal derselbe Eingang (mit Automation und Preset-Wechseln) → bit-identisch, auch nach erneutem `prepareToPlay`.
Delta = Eingang − Ausgang (verzögert): Fehler ≤ 3e-8 in 9 Modi (Split, Breitband, Mix 40 %, M/S, Mid, Side, Link 30 %,
Allround, Absolute); mit Output +6 dB gilt Delta = g·Eingang − Ausgang.

### CPU (48 kHz / 512 Stereo, `processBlock`)
| Einstellung | µs pro Block (Median) | % Echtzeit |
|---|---|---|
| Standard (Split, Smart) | 39,2 | 0,37 % |
| Breitband | 38,0 | 0,36 % |
| Link 50 % | 58,0 | 0,54 % |
| Worst: M/S, Link 50, Allround, S-Bereich, Frequenz automatisiert | 90,0 | 0,84 % |
| Bypass | 36,7 | 0,34 % |

Ganzer Prozess mit Echtzeit-Audio: Editor zu 0,4 %, Editor offen (SIMPLE, Animation voll) 31,6 % eines Kerns.

### Speicher (Private Bytes / Handles / GDI / USER)
| Phase | vorher | nachher |
|---|---|---|
| 10 min Audio (Presets, Automation, AUTO an/aus) | Minute 1: 114,6 MB | Minute 10: 116,7 MB (einmalig +3 MB bei Minute 4 = AUTO-Puffer, danach flach) |
| Editor 120× öffnen/schließen (Fenster) | **vor dem Fix: 148 → 398 MB (2,3 MB je Öffnen)** | nach dem Fix: 114,6 → 101,1 MB, Handles 577/576, GDI 51/51, USER 12/12 |
| Editor 60× ohne Fenster | 102,8 MB | 102,7 MB |
| 150 Instanzen anlegen/zerstören | 102,7 MB, 576 Handles | 102,7 MB, 576 Handles |

### Bedienung (Teil 4)
- Jeder Parameter kommt nach einem Block in der Engine an (60 Zufallsbelegungen + Extreme).
- Presets: alle Werks-Presets laden und erreichen die Engine; eigene Presets inkl. Umlaut-Namen; leere, binäre,
  abgeschnittene Dateien und falsches Wurzel-Element werden abgelehnt ohne Änderung; Werte außerhalb des Bereichs
  werden geklemmt, unbekannte IDs ignoriert; schreibgeschützte Datei / unmöglicher Ordner melden Fehler.
- Undo/Redo: 30 zufällige Änderungen vor und zurück exakt.
- State: 25 Zufallssituationen (Werte, Preset-Name, A/B) in frische Instanzen (unvorbereitet, 96 kHz/64, 192 kHz/4096):
  alle Werte, A/B, Name, „*“ identisch; 50 Speicher-/Lade-Zyklen ohne Drift; 11 kaputte Projektdaten (Zufallsbytes,
  abgeschnitten, NaN, inf, 1000 dB, Text, 500 A/B-Slots) ohne Folgen; 121 Restores aus einem Thread bei laufendem Audio
  und Message-Thread ohne Absturz.
- Info-Karten: SIMPLE 19, ADVANCED 30 Bedienelemente, alle mit Titel und Text in DE und EN, nichts unübersetzt.
- Parameter-Text: jede Anzeige gibt eingetippt den Wert zurück (DE und EN), inkl. „6,5 kHz“, „−35,5 dB“, „2,25 Okt.“.

## Gefundene und behobene Fehler
| # | Fehler | Datei | Fix |
|---|---|---|---|
| 1 | Host-Texteingabe: „6,20 kHz“ → 2 kHz, „−35,5 dB“ → 0, deutsche Nachkommastellen verloren | `EmberParams.cpp` (`parseTyped`) | Komma, typogr. Minus, k = ×1000 |
| 2 | `nan`/`inf` im Projekt → Parameter NaN im Host/UI | `EmberProcessor.cpp` setStateInformation | Standardwert |
| 3 | Preset-Blättern blieb an einer kaputten Datei hängen | `EmberProcessor.cpp` stepPreset | überspringt |
| 4 | „*“ (geändert) ging beim Projekt-Laden verloren | `EmberProcessor.cpp` write/readMeta | Preset-Werte im EmberAB-Block |
| 5 | Klick (−27,5 dB) beim Stereo-Modus-Wechsel im „S-Bereich“-Abhören | `EmberEngine.cpp` | Band mit `wet` geblendet |
| 6 | Bypass nicht bit-genau (Rundung) | `EmberEngine.cpp` | bei byp = 1 exakt der Eingang |
| 7 | Absenkungs-Anzeige ignorierte Mix | `EmberEngine.cpp` | Anzeige nach Mix |
| 8 | `fast::exp2` rundete knapp unter 0: Gain 0,99999986 noch ≈ 5 s nach einem S | `EmberEngine.h` | Rundungsfall abgefangen |
| 9 | Einheit „oct“ im deutschen UI | `EmberParams.cpp` | „Okt.“ |
| 10 | **Speicherleck 2,3 MB je Öffnen des Editors** (Fonts neu an JUCE übergeben, JUCE 8 behält jede Kopie) | `Source/UI/Theme.cpp` | Fonts einmal pro Prozess (DeletedAtShutdown-Singleton) |

Test-Hooks (ohne Wirkung im Plugin): `HoverInfo`-Getter + Liste (Widgets), `setUserPresetFolderForTests` (EmberPresets).

## Offen / Beobachtungen
- Split bei tiefen Absenkungen: Die Bandmitte verliert bis 1,27 dB weniger als die nominelle Tiefe (24 dB → ≈ 22,7 dB),
  weil die Mischung über die feste −12-dB-Glocke hinaus extrapoliert. Die Anzeige zeigt die nominelle Tiefe. Für das
  DSP-Team (bewusst nicht geändert).
- GUI-CPU mit offenem Editor ≈ 32 % eines Kerns (bekannt: Figur-Zeichnen, siehe EMBER.md).
- Ausgangsspitze kann auf Breitband-Rauschen durch den Phasengang des Bands bis +0,8 dB steigen (physikalisch normal).
- Delta enthält den Output-Gain (Delta = g·Eingang − Ausgang) – Designentscheidung, dokumentiert.
