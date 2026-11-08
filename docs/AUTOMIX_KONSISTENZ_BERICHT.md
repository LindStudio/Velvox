# Auto Mix – Technischer Bericht: Warum die Ergebnisse schwanken

Stand 2026-11-04, Code `69f582a` (Branch `feature/vocalypse-rebrand`, unverändert untersucht auf `feature/automix-phased`).
Auftrag (Jakob): erst analysieren, Ursachen finden, Lösungsplan – keine neuen Module, keine unnötigen Änderungen an
Audio-Engine oder Oberfläche.

## 1. Messung

**Aufbau.** 4 feste Aufnahmen (`velvox-bench\2026-10-29\train\dry`: jakob_rec1, jakob_vergleich, vs_f1_caro_straight,
vs_m1_row_spoken), je 7 Varianten: Original, bitgleiche Kopie, Start 3 s / 6 s später, −2 dB, +2 dB, Rauschen
−70 dBFS (unhörbar) dazu. Auto Mix über `VocalypseTests --bench … 0 0` (Party · Druckvoll), Vergleich der gesetzten
Parameter gegen das Original. Daten: `%USERPROFILE%\dev\velvox-bench\2026-11-04\konsistenz\` (`r1\renders\*_report.txt`,
`r1_compare.txt`).

| Variante | Parameter anders (Summe 4 Takes) | Module an↔aus |
|---|---|---|
| bitgleiche Kopie | **0** | **0** |
| −2 dB | 74 | 3 |
| +2 dB | 80 | 2 |
| Rauschen −70 dBFS | 49 | 4 |
| Start 3 s später | 98 | 3 |
| Start 6 s später | 207 | 10 |

Beispiele:
- vs_f1, **−2 dB**: De-Esser aus statt an, eine Höhen-Anhebung (7,9 kHz) wird zu einem −6-dB-Schnitt bei 640 Hz,
  Smooth 45 → 30 %.
- jakob_rec1, **−2 dB**: Hall 27 → 42 %, Delay 17 → 26 %.
- jakob_rec1, **6 s später**: Delay, Doubler, Ride und Smash fallen weg, zwei EQ-Anhebungen von +6 dB auf 0.
- vs_f1, **Rauschen −70 dBFS**: De-Esser aus, Raumrauschen und Atem-Absenker an, Kompressor-Schwelle +6 dB.
- Die gewählte Variante der Kandidatensuche wechselt bei derselben Aufnahme zwischen „Basisplan“, „Effekte
  trockener“, „Höhen nach der Dynamik“ und „trockener + mehr Raum“.

**Ergebnis:** Die Engine ist **deterministisch** (gleiche Eingabe → bitgleich gleiches Ergebnis, kein Zufall im
Code), aber **extrem empfindlich**: unhörbare oder musikalisch belanglose Unterschiede kippen ganze Entscheidungen.

## 2. Ursachen

### U1 – Der analysierte Ausschnitt hängt vom Play-Zeitpunkt ab
`CaptureBuffer` (Default `CaptureConfig`: 14 s Gesang, max. 24 s) nimmt ab dem ersten Block mit Signal auf, also ab
dort, wo der Nutzer Play drückt, und hört nach 14 s Gesang auf. Ein anderer Startpunkt = anderes Material = andere
Analyse. Die Messung „Start 6 s später“ zeigt die größten Abweichungen (207 Parameter, 10 Module).

### U2 – Harte Schwellen schalten ganze Entscheidungen
Planer und Analyse arbeiten mit Ja/Nein-Merkmalen und Stufen. Beispiele (`AutoMixPlanner.cpp`, `VocalProfiler.cpp`):
- `harsh` = Präsenz > −2 dB → Sättigung Warm statt Tube, Smooth 30/45 %, Smash-Ton −10, Hall-Dämpfung, keine
  Höhenanhebung.
- `reverberant` = Ausklang > 0,6 s → **kein Hall und kein Delay** (`roomy`), sonst bis 42 %.
- `spoken` = Tonhöhenanteil < 0,45 → Hall 20 statt 42 %, andere Delay-Teilung, Doubler ≤ 8 %.
- `heavilyCompressed` = Dynamik < 6 dB und Crest < 10 dB → Ratio 1,8, kein Smash, kein Ride.
- De-Esser an ab S/Vokal > −9 dB, Range in Stufen 3 / 5 / 6 / 8 dB; Smooth 20 / 30 / 45 %; Ratio-Raster 1,8 / 2,5 /
  3,5 / 5; `conservative` bei < 8 s Gesang; Rauschen < 28 / 35 dB; Atem > 1 %; Doubler aus unter 5 %.
- Über 20 solche Kipppunkte. Eine Messung knapp über/unter der Grenze erzeugt einen anderen Mix.

### U3 – Messungen hängen vom absoluten Pegel ab
Mehrere Analyse-Schwellen sind absolut in dBFS (`VocalProfiler.cpp`: Stimm-Gate `max(−50, laut − 35)`, Frame-Gate,
Rauschboden, De-Esser-Detektorpegel), und der Planer rechnet Schwellen aus dem absoluten Stimmpegel. ±2 dB ändern
deshalb, welche Frames als Stimme/S-Laut/Pause zählen – und damit über U2 ganze Entscheidungen.

### U4 – Variantenwahl ohne Mindestabstand
`AutoMixEngine::run` (Kandidatensuche, Zeilen 817–875) wählt die Variante mit der kleinsten Bewertung, sobald sie den
Basisplan auch nur minimal schlägt, und kombiniert die zwei besten. Keine Marge, keine Kosten für zusätzliche
Bearbeitung. Die Bewertung selbst springt stark (jakob_rec1: Basisplan 21,7 / 63,7 / 2,9 bei −0 / +2 / −2 dB).
Ebenso `tuneDynamics` (gierige Suche auf festen Rastern) und die 4-Runden-Prüfschleife (reihenfolgeabhängige
Korrekturen).

### U5 – Der Mix hängt vom vorherigen Zustand ab
Der Planer überlagert nur die Parameter, die er setzt, auf die **aktuellen** Werte (`AutoMixController::currentValues`).
Alles andere (z. B. De-Esser-Modus, Sättigungs-Bänder, Doubler-Modus, Hall-Details, Nutzer- und Vocal-Check-EQ-Bänder,
EQ-Slots früherer Läufe) bleibt aus dem vorherigen Zustand stehen und fließt in Render und Bewertung ein. Ein zweiter
Auto Mix startet vom Ergebnis des ersten. (Im Bench nicht sichtbar, weil er immer von Defaults startet.)

### U6 – Bericht ohne vollständige Begründung
`Decision` enthält Titel und Text, aber nicht strukturiert: erkanntes Problem, Messwert, berechnete Parameter,
Stärke, erzieltes Ergebnis, Grund fürs Überspringen. Analysen werden nicht gespeichert – ein Ergebnis lässt sich
nicht nachträglich erklären oder vergleichen.

## 3. Lösungsplan (Kurzfassung)

Umsetzungsplan: `docs/superpowers/plans/2026-11-04-automix-konsistenz.md`.

1. **Testsystem zuerst** (`--consistency`): feste Takes, 10× derselbe, Pegel ±0,5/1/2 dB, andere Startpunkte,
   Rauschen, mehrere Läufe hintereinander, zufälliger Vorzustand; Vergleich nach Rolle, mit Grenzwerten und
   Exit-Code. Der heutige Stand wird als Ausgangswert gemessen.
2. **Vollständiger State Reset** (U5): Jeder Auto Mix startet von den Defaults; der Plan setzt **jeden**
   Mix-Parameter; nicht benutzte Module explizit aus.
3. **Pegelunabhängigkeit** (U3): Analyse und Planung auf einer auf festen Arbeitspegel normierten Kopie; der
   Auto Mix setzt den Input Gain auf genau diesen Arbeitspegel (Gain Staging aus Jakobs Konzept, Schritt 1).
4. **Deterministische, segmentierte Analyse** (U1): ganze gespielte Vocal (bis 3 min) aufnehmen, in feste Segmente
   teilen (am Songraster, wenn der Host die Position meldet), Segmente einzeln analysieren und robust (Median)
   zusammenfassen. Analyse wird gespeichert (JSON).
5. **Kontinuierliche Entscheidungen** (U2): weiche Schwellen (Smoothstep-Gewichte), Stärken laufen stetig gegen 0 an
   der Einschaltgrenze; Auswahlentscheidungen (Sättigungsart, Hallart, Delay-Teilung) mit Vorzugsmarge. Reihenfolge
   nach Jakob: **erst EQ, Kompressor, De-Esser**, dann Sättigung, Hall, Delay und der Rest.
6. **Stabile Variantenwahl** (U4): Variante nur bei Nutzen ≥ Marge + Komplexitätskosten.
7. **Entscheidungsprotokoll** (U6): pro Modul strukturiert (aktiv/übersprungen, Problem, Messwert, Parameter,
   Stärke, Ergebnis, Grund), im Bericht und im gespeicherten JSON.

## 4. Ausgangswert (Testsystem, Stand vor den Korrekturen)

`VocalypseTests --consistency tools/bench/consistency_takes.txt …\konsistenz\baseline` (Commit `509490d`, Party · Druckvoll,
8 feste Takes, je 22 Prüfungen). Grenzwerte: `Tests/ConsistencyHarness.h`.

| Prüfung | bestanden |
|---|---|
| 10× dieselbe Aufnahme (exakt) | 72 / 72 |
| Pegel −2 / −1 / −0,5 / +0,5 / +1 / +2 dB | 0 / 48 |
| Rauschen −70 dBFS | 5 / 8 |
| Start 3 s / 6 s später | 0 / 16 |
| 2. und 3. Lauf hintereinander (exakt) | 4 / 16 |
| zufälliger Vorzustand (exakt) | 0 / 16 |
| **gesamt** | **81 / 176** |

### 4.1 Checkpoint (Task 9, 2026-11-08)

Gleicher Aufruf auf `8984ed9` (Tasks 1–8 gemergt) → `…\konsistenz\checkpoint`. „Start später“ jetzt mit Songposition gezählt,
ohne Songposition nur zur Information.

| Prüfung | Ausgangswert | Checkpoint |
|---|---|---|
| 10× dieselbe Aufnahme (exakt) | 72 / 72 | 72 / 72 |
| Pegel −2 … +2 dB | 0 / 48 | 43 / 48 |
| Rauschen −70 dBFS | 5 / 8 | 2 / 8 |
| Start 3 s / 6 s später (mit Songposition) | 0 / 16 | 0 / 16 |
| 2. und 3. Lauf hintereinander (exakt) | 4 / 16 | 16 / 16 |
| zufälliger Vorzustand (exakt) | 0 / 16 | 16 / 16 |
| **gesamt** | **81 / 176** | **149 / 176** |

Was noch kippt:

- **Pegel (5 Fehler):** `splice_mylk_dry` bei −2 / −0,5 / +1 / +2 dB: Sättigung und Smash schalten um, mit ihnen
  De-Esser-Range (8 → 10 dB), -Threshold (−3 dB) und ein EQ-Band (0,5 dB) – Sättigung / Smash sind Task 10. Dazu
  `vs_f3_row_spoken` −2 dB: Eingangsgain an der +24-dB-Grenze (gewollt).
- **Rauschen (6 Fehler):** fast nur Breath Control / Room Noise schalten ein (Task 10); einzeln Delay-Mix, ein EQ-Band
  (5,1 → 11,6 kHz, 0 dB) und Attack / Release des Kompressors.
- **Start später (16 Fehler):** EQ-Notches springen (z. B. 6,1 kHz → 1,3 kHz), Kompressor 1,5 → 1,95 : 1 und −3 dB Threshold,
  De-Esser-Range, dazu Delay / Doubler / Smash / Sättigung (Task 10). **Ursache zum Teil im Test:** die 8 Takes sind nur
  12–41 s lang, ein Segment dauert 8 s. 3 bzw. 6 s später fehlen bei einem 12-s-Take bis zur Hälfte des Gesangs – das
  ist weniger Material, nicht nur ein anderer Startpunkt. Instabil bleibt trotzdem die Resonanz-Erkennung (Notch nur,
  wenn in ≥ 50 % der Segmente vorhanden: bei 2–3 Segmenten entscheidet ein einziges).

Offene Fragen an Jakob: (1) Offset-Prüfung auf längeren Takes (≥ 60 s) messen, und die Notch-Regel für wenige Segmente
weicher machen? (2) Der Kompressor landet bei Party · Druckvoll fast immer bei 1,5 : 1 (Ruling 22) – so gewollt?
