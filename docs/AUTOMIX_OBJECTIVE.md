# Auto-Mix 0.4 – Zielfunktion und Kandidatensuche

Code: `Source/AutoMix/AutoMixObjective.*`, `AutoMixEngine::candidatesFor`, Suche in `AutoMixEngine::run`.
Tests: `Tests/AutoMixTests.cpp` → `optimizerTests()`.

## Ablauf

1. **Analyse** der aufgenommenen Vocal (`VocalProfiler`) → **Basisplan** (`AutoMixPlanner`, regelbasiert).
2. Verifikation: rendern, messen, nachregeln (max. 4 Runden), wie in 0.3.
3. **Kandidaten:** `candidatesFor` leitet aus dem Basisplan Varianten ab. Jede ändert *eine*
   zusammenhängende Entscheidung:
   | Variante | Änderung | Wann nicht |
   |---|---|---|
   | Dynamik leichter | Ratio ×0,7, Threshold +2 dB, Makeup −1 dB, Smash aus | Kompressor aus |
   | Dynamik dichter | Ratio ×1,35, Threshold −2 dB, Makeup +1 dB, sanftes Smash | Kompressor aus, Take bereits stark komprimiert |
   | Mit / Ohne Sättigung | Sättigung umschalten (Tape-artig, Drive 4, Mix 25 %) | Verzerrung im Material erkannt |
   | Höhen sanfter | High-Shelf 5 kHz, −1,5 … −4 dB (je heller, desto mehr) – der Planer selbst hebt Höhen nur an | Take nicht heller als −7 dB (über 2 kHz ggü. Kern), Höhenanhebung geplant |
   | Höhen nach der Dynamik (EQ 2) | High-Shelf und Präsenz-Boost wandern in eine zweite EQ-Instanz hinter Comp/Smash/De-Esser/Smooth | kein Boost vorhanden |
   | Effekte trockener | Reverb- und Delay-Mix ×0,65 | keine Effekte geplant |
   | Mehr Raum (bei getragenem Gesang: Large Hall) | Reverb-Mix ×1,35 (max. 28 %); getragen Large Hall, 2,6 s, Pre-Delay 40 ms (der Basisplan nutzt bei getragenem Gesang ohne Eigenraum bereits Large Hall 2,3 s) | Reverb aus, Sprache, Take schon hallig |
   | De-Esser stärker | Threshold −3 dB, Range +2 dB (max. 12) | De-Esser aus, S-Laute nicht messbar oder unauffällig (< −8 dB) |
4. Jeder Kandidat wird **komplett durch die echte DSP-Kette gerendert** und mit `evaluate` bewertet.
5. Die zwei besten Einzeländerungen, die beide besser als der Basisplan sind, werden zusätzlich
   **kombiniert** und bewertet.
6. Gewählt wird der Kandidat mit der kleinsten Gesamtstrafe; der Bericht nennt die Variante
   („Variante gewählt: …“). Danach Ausgangspegel auf Ziel-Lautheit, Peaks unter −1 dBFS.

Die Suche ist begrenzt (Basis + max. 8 Varianten + 1 Kombination) und deterministisch.

## Zielfunktion

Alle Terme sind Strafen (0 = im Zielfenster). `window(v, lo, hi, s)` = ((Abstand außerhalb [lo, hi]) / s)²,
Standard s = 2 dB, d. h. 2 dB außerhalb kosten 1.

| Term | Messung | Fenster / Gewicht |
|---|---|---|
| tonal | Bänder relativ zum Kern (300 Hz – 2 kHz) | Body −9…−1 (0,6), Low-Mids −7…0 (0,8), Top −16…−7 (1,0), Air −32…−16 (0,4, s = 3) |
| dynamics | Phrasen-Variation, Crest-Faktor | 3…6 dB (0,8); Crest ≥ 5,5 dB (1,2, s = 1,5) – kalibriert am Hörtest vom 24.10.: dicht und upfront gewann |
| sibilance | S/Vokal-Verhältnis | ≤ −6 dB (1,2); nicht mehr als 10 dB unter dem Original (0,8, Lispeln) |
| harshness | 2–5 kHz relativ zum Kern | ≤ −4 dB (1,0) |
| stereo | Mono-Verlust, Korrelation | Verlust ≤ 1 dB (2,0, s = 1); Korrelation ≥ 0,5 (1,0, s = 0,25) |
| space | Effektpegel in den Lücken nach Wörtern | Stil-abhängig: Sprache −40…−24, Gesang −34…−17, getragen −30…−13 dB; zu trocken kostet nur, wenn der Plan Effekte vorsieht; ein schon halliger Take behält seinen Raum |
| artifacts | Zuwachs Spitze−Lautheit gegenüber dem Original, Peak bei Ziel-Lautheit | Zuwachs ≤ 3 dB (0,3, s = 2); Peak ≤ −1 dBFS (0,25, s = 2). Beides kostet nur Lautheit (die Ausgangsstufe regelt immer auf −1 dBFS), bei gleicher Lautheit im Hörtest nicht hörbar – deshalb mild. Clipping-Runs im Float-Render werden berichtet, nicht bewertet (im Ergebnis clippt nichts). De-Essing gilt nicht als Artefakt. Fehlgeschlagener Render = 10⁶ |
| change | Abweichung vom Basisplan | fest 0,25 – eine Variante muss sich lohnen |

**Lautheit wird nie belohnt:** Peaks werden auf die Ziel-Lautheit umgerechnet; eine nur lautere
Variante hat dieselbe Strafe (Test „loudness alone is never rewarded“).

Stil (`styleOf`): Anteil gepitchter Frames < 0,45 → Sprache; > 0,75 und Tonumfang > 2 Halbtöne → getragen; sonst Gesang.

## Getestet (`optimizerTests`)

- Alle Kandidaten: gültiges Routing, alle Werte im Parameterbereich (auch Instanz-IDs `_2`),
  kreative Effekte bleiben aus, Namen eindeutig, Anzahl begrenzt, Ergebnis deterministisch.
- Ausgeglichener Render kostet 0; dieselbe Messung als Variante kostet mehr (Basis gewinnt ohne Vorteil);
  Überkompression, Härte, Clipping kosten; Lautheit allein bringt nichts; ungültiger Render verliert immer.
- EQ-2-Variante: zweite EQ-Instanz an, Boost mit gleicher Frequenz verschoben, EQ 1 neutral,
  EQ 2 hinter der Dynamik; ohne Boost keine EQ-2-Variante.
- Kompletter Lauf: alle Kandidaten im Ergebnis, gewählter Kandidat ist der beste, Bericht nennt ihn.

## Offen

- Gewichte sind aus den 0.3-Schwellen abgeleitet, noch nicht gegen Hörtests kalibriert
  (Benchmark-Runner + Hörtest-Kit, siehe `CLAUDE_HANDOFF.md`).
- Tonal-Fenster „Top“ (−16…−7 dB) im Hörtest prüfen: fertig gemischte Profi-Vocals (Stems aus
  `Documents\Image-Line\FL Studio\Audio\Rendered`) messen −1,3 … −8,7 dB. „Höhen sanfter“ wurde
  am 24.10. bei 4 von 5 davon gewählt (Höhen −1 … −1,4 dB) – Absicht oder zu dunkel kalibriert?
- Lautere Stellen außerhalb des Analyse-Abschnitts (GRÜNER SCHEIN: ganze Datei +0,5 dBFS vor dem Clip Guard): bewusst
  keine pauschale Zusatzreserve (würde jeden Mix leiser machen); der Clip Guard (tanh-Knie −1 → 0 dBFS) fängt 1–2 dB weich ab.
