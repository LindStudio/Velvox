# EMBER AUTO – Fixes (Branch fix/ember-auto, 2026-11-23)

Gemessen mit `tools/bench/ember_sound` (15 echte Takes, J1–J3 mit Pro-DS-Default), je AUTO auf 3 Ausschnitten
(ab 0 / 25 / 50 s). Alle EmberTests grün (490, mit `EMBER_VOCALS` 15/15 Takes), pluginval Stufe 10 SUCCESS.

## Was geändert ist
1. **Stereo:** Die Aufnahme hält beide Kanäle (gleiche Kanäle → einer, halbe Arbeit), die Analyse rechnet wie der
   Detektor in Stereo (Leistung beider Kanäle addiert, Engine rendert Stereo). Speicher: 16 s je Kanal = so viel wie
   früher 30 s Mono (192 kHz: 23 MB), angelegt erst beim ersten AUTO, nie im Audio-Thread.
2. **Pausen ohne Nähte:** feste 5-ms-Raster ab dem Start statt Host-Blöcken; Pausen über 0,4 s werden auf 0,4 s
   gekürzt (leise an leise). Gleiches Audio in 64/333/512/2048er-Blöcken → bitgleicher Take, gleiches Ergebnis (Test).
   „12 s“ zählt jetzt nur Stimme.
3. **Vokale auch oben geprüft:** Vokale voll (≤ 0,1 dB), Präsenz 2–5 kHz (≤ 0,2 dB), Luft > 10 kHz (≤ 0,5 dB).
   Gemischte Frames (S am Vokal) werden am Vokalteil < 3 kHz (≤ 0,5 dB) und an der Präsenz (≤ 1,5 dB) gemessen statt
   über alles – vorher zählte ihr S-Anteil als „Schaden“.
4. **S-Frames sauberer:** verworfen, wenn das S-Band > 26 dB unter den Vokalen liegt (Atmer, Übersprechen) oder
   oberhalb 12 kHz überwiegt (Hi-Hat, Becken). Schwerpunkt nur über die angenommenen S, jedes gleich gewichtet
   (Energie- oder Wurzel-Gewichtung und eine Mindestlänge von 3 Frames getestet: Frequenz schwankte mehr).
5. **Synthetischer Take:** Er war nicht das Problem, die Messung der gemischten Frames war es (Ziel 4,5 dB, erreicht
   0,5–0,9). Jetzt 4,2–4,6 dB bei 44,1/48 kHz. Bei 96/192 kHz hält die Präsenzgrenze der gemischten Frames AUTO bei
   ~3 dB: die Engine senkt dort pro dB S etwas mehr Präsenz ab (Hinweis an fix-dsp). Der Test prüft „Ziel erreicht oder
   eine Stimmgrenze greift“. Fehlende Takes bei gesetztem `EMBER_VOCALS` sind jetzt ein FAIL.
6. **Schneller:** Suche startet bei einer Schätzung, Regula falsi auf dem 0,5-dB-Raster, Ergebnisse gecacht, früher
   Abbruch nahe am Ziel; Frames, die die Engine bitgenau gelassen hat, brauchen keine FFT; Abbruch in jedem Render-Block.
   3–14 statt bis zu 34 Renderings.
7. **Meldung:** „Schärfe der S-Laute (über 4 kHz) −X dB“ / „S sharpness (above 4 kHz) −X dB“ – das misst
   `expectedReductionDb`. Kommentare 3–12 kHz korrigiert.
8. **Laden während der Analyse:** Projekt, Preset oder A/B verwerfen die laufende Analyse; ein Projekt löscht auch das
   alte Ergebnis (Meldung, Phase idle).
9. **Einstellungen während der Analyse:** Modus, Erkennung, Amount, Attack, Lookahead geändert → AUTO rechnet mit den
   neuen Werten auf demselben Take neu.
10. **Races:** start/stop/prepare/copy über ein lock-freies Handshake (Audio-Thread meldet „in push“), Samplerate atomar;
    neue Samplerate beim Zuhören → hört von vorn zu (statt „zu kurz“).
11. **Abbrechen:** Der AUTO-Knopf heißt auch während der Analyse ABBRECHEN; Schließen während der Analyse: ~6 ms.
12. **Neue Tests:** 192 kHz, Threshold-Boden, Determinismus, Blockgrößen, Stereo-Vorhersage, Hi-Hat/Atmer,
    Schwerpunkt-Stabilität, Zeit + Abbruch, Laden/Ändern/Abbrechen im Prozessor, Raten-Wechsel, Thread-Race.

## Zahlen (vorher → nachher)
| | vorher | nachher | Pro-DS |
|---|---|---|---|
| S-Kern, 15 Takes, 3 Ausschnitte | −3,45 dB | −3,68 dB | |
| S-Kern J1–J3 | −3,88 dB | −4,15 dB | −3,71 dB |
| S 4–12 kHz J1–J3 | −3,73 | −3,97 | −3,62 |
| Luft über dem S, alle | −2,64 | −2,78 | (−3,62 J1–J3) |
| S-Farbe (Schwerpunkt), alle | −85 Hz | −67 Hz | −145 Hz |
| unberührte S | 13,3 % | 12,3 % | 9,5 % |
| Vokale Luft / neben S Präsenz | 0,00 / 0,00 dB | 0,00 / 0,00 dB | 0,00 / 0,01 |
| Ausschnitte > 0,3 dB unter Ziel | 3 (M36 −0,8, M_emerald −1,3, M_allittakes −0,7) | 0 | |
| Frequenz-Streuung über Ausschnitte | Ø 11,0 %, max 27 % | Ø 12,2 %, max 26 % | |

Analysezeit (J1, 75 s Ausschnitt, AUTO hört 12 s): 48 kHz 0,61 → 0,29 s, 192 kHz 3,40 → 1,36 s; 30-s-Take (alt
30 s, neu 16 s Puffer): 48 kHz 1,94 → 0,39 s, 192 kHz 8,90 → 1,39 s. Synthetisch, echtes Stereo: 12 s 0,4 s
(48 kHz) / 2,1 s (192 kHz), 30 s 1,7 / 5,7 s. Abbruch greift nach ~1 ms.

## Offen
- Frequenz-Streuung zwischen Ausschnitten (F_badboy, F01, F22, M42 ~20–26 %) kommt vom Inhalt (mehr „sch“ oder „s“ im
  Ausschnitt), nicht von der Rechnung; keine der getesteten Gewichtungen war klar besser.
- 96/192 kHz: siehe Punkt 5 (Engine).
- Hörurteil Jakob.
