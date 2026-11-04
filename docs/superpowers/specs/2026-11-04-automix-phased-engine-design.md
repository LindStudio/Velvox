# Auto Mix 0.9 – Engine in Phasen (Design)

> **AUSGESETZT (2026-11-04):** Jakob: erst Reproduzierbarkeit und Stabilität, keine neuen Module. Gültig ist `2026-11-04-automix-konsistenz.md`.

Stand 2026-11-04. Grundlage: Jakobs Konzept „VOCALYPSE – Auto-Mixing Engine, Mixing Philosophy & Processing
Architecture“ (im Chat eingefügt, 19 Schritte in 4 Phasen + 12 Regeln) und seine Antworten vom selben Tag.

## Warum

Jakob: „oft zu viel Kompression, manchmal gar keine, EQ ist random, Gate, De-Ess, Hall, Delay, Saturation usw. alles
random Werte, das Programm hat sich keine Gedanken über die Stimme gemacht.“ „Bau die Engine danach um, es passt
leider gerade gar nicht, wie es ist.“

Leitsatz aus dem Konzept: **Eine professionelle Vocal entsteht nicht dadurch, dass möglichst viele Plugins verwendet
werden, sondern dadurch, dass die richtigen Probleme erkannt und gezielt gelöst werden.**

### Befund im Code (Stand `69f582a`)

- `AutoMixPlanner::plan` entscheidet **alle Module in einem Durchgang** aus der Erstanalyse. Danach prüft eine
  Schleife (max. 4 Runden) nur das Endergebnis und dreht an wenigen Stellen nach. Eine Analyse zwischen den Phasen gibt
  es nicht. Kein Modul weiß, was das Modul davor tatsächlich bewirkt hat.
- Mehrere Module laufen **praktisch immer**, ohne erkanntes Problem: Kompressor (immer an), Smooth (immer bei genug
  Material, „20 % Grundpflege“), Sättigung (immer außer bei Verzerrung), Hall (immer außer bei halliger Aufnahme),
  Smash (immer, wenn das Genre Dichte will). Das widerspricht den Regeln 1, 2 und 9.
- Es gibt **einen** Kompressor ohne Charakter. Peak Control, 1176- und LA-2A-Stufe fehlen. Der Kompressor wird über
  Ratio-Stufen gesucht, die Ziel-Reduktion ist nicht ausgesprochen. Das erklärt „mal zu viel, mal gar nichts“.
- Es fehlen **Gate/Expander**, **dynamischer EQ** und **Multiband-Kompression**.
- Der Eingangspegel (Input Gain) wird nie gesetzt. Ein zu lauter Beat wird erkannt (`vocalOverBeatLu`), aber dem
  Nutzer nicht als Empfehlung gezeigt.

## Ziele

1. **Phasen statt Einmal-Plan:** Clean → Control → Shape → Enhance → Space. Nach **jeder** Phase wird das bisherige
   Ergebnis gerendert und **neu analysiert**. Die nächste Phase entscheidet auf dieser neuen Analyse (Regel 11).
2. **Erkennen vor Bearbeiten:** Jedes Modul hat einen ausgesprochenen **Detektor** (was wurde gemessen, ab welchem
   Wert ist es ein Problem). Schlägt er nicht an, bleibt das Modul aus und der Bericht sagt warum (Regeln 1, 2).
3. **Stärke aus der Messung:** Jedes Modul hat einen eigenen **Aktivierungs- und Intensitätswert** (Regel 3). Die
   Intensität folgt stetig der Schwere des Problems und dem Stil, nicht festen Stufen.
4. **Nutzen-Nachweis:** Jedes eingeschaltete Modul wird mit und ohne gerendert. Bringt es den erwarteten messbaren
   Nutzen nicht (oder schadet es an anderer Stelle), wird es schwächer gestellt oder wieder ausgeschaltet (Regel 9).
5. **Module sprechen miteinander** (Regel 10): Jede Stufe sieht, was die vorherigen gemessen und getan haben (z. B.
   De-Esser 2 kennt De-Esser 1, Smooth lässt Resonanzen in Ruhe, die EQ 1 schon absenkt, LA-2A kennt die Reduktion
   der 1176-Stufe, Multiband sieht, was nach den Kompressoren an Dynamik übrig ist).
6. **Genre und Vibe beeinflussen alles** (Regel 4): Detektor-Schwellen, Intensitäten, Ziel-Reduktionen, Charakter
   von Sättigung, EQ-Ziel, Breite und Raum kommen aus einer Stiltabelle je Genre × Vibe.
7. **Charakter erhalten, keine Überbearbeitung:** feste Obergrenzen pro Modul (Kompression, De-Essing, Sättigung,
   EQ-Hub), dazu eine Gesamtprüfung am Ende.
8. **Im Kontext des Beats beurteilen** (Regel 12): Liegt der Beat am Sidechain, fließen Maskierung und
   Vocal-über-Beat-Pegel in EQ, Raum und die Schlussprüfung ein. Ist der Beat zu laut, zeigt VOCALYPSE eine
   Empfehlung und ein Kontrollfeld für den Beat-Pegel.
9. **Fairer Vergleich:** Der Ausgang wird auf die Lautheit des Eingangs abgeglichen (Schritt 19). Die Verbesserung
   darf nicht nur aus mehr Lautstärke kommen.

### Nicht-Ziele

- Kein maschinelles Lernen und kein Modell-Training. Die Engine bleibt regel- und messbasiert, kalibriert auf den
  vorhandenen Daten (SONOVOX-Paare, VocalSet, Jakobs Aufnahmen, Profi-Stems).
- Keine Tonhöhenkorrektur und keine kreativen Effekte außer den im Konzept genannten (Chorus nur als Option im
  Doubler, siehe unten).
- Kein Umbau des SIMPEL-Modus. Er zeigt weiter, was der Auto Mix eingestellt hat.

### Anti-Ziele (das wäre ein Fehlschlag, auch wenn es technisch läuft)

- Module, die bei fast jeder Stimme an sind, ohne dass ein Problem gemessen wurde.
- Werte, die bei verschiedenen Stimmen gleich sind, ohne dass der Bericht einen Grund nennt.
- Ein Mix, der nur lauter ist als das Original.
- Abgeschnittene Wortanfänge oder Wortenden (Gate, Breath), lispelnde S-Laute (De-Esser), hörbares Pumpen.

## Die Kette (feste Reihenfolge des Auto Mix)

Der Nutzer kann die Kette im PRO-Modus weiter frei umbauen. Der Auto Mix setzt diese Reihenfolge und nimmt nur
Module auf, die er einschaltet (`pruneInaudible` wie bisher).

| # | Phase | Schritt (Konzept) | Modul in VOCALYPSE | Instanz |
|---|-------|-------------------|--------------------|---------|
| 1 | Clean | Input Analysis & Gain Staging | Input Gain (Parameter) | – |
| 2 | Clean | Breath Control | Breath Control (`breath`) | 0 |
| 3 | Clean | Gate / Expander | **neu: Expander** (`gate`); Raumrauschen (`denoise`) bei stetigem Rauschen | 0 |
| 4 | Clean | Vocal Rider | Level Ride (`ride`) | 0 |
| 5 | Control | Corrective EQ | EQ 1 (statisch) + **neu: Dynamic EQ** (`dyneq`) | 0 |
| 6 | Control | De-Esser | De-Esser 1 | 0 |
| 7 | Control | Peak Control | Kompressor 1, Charakter **Clean** | 0 |
| 8 | Control | Resonance Suppression | Smooth (`smooth`) | 0 |
| 9 | Shape | Character Compression (1176) | Kompressor 2, Charakter **FET** | 1 |
| 10 | Shape | Additional De-Essing | De-Esser 2 | 1 |
| 11 | Shape | Harmonic Enhancement | Sättigung (`sat`) | 0 |
| 12 | Shape | Tonal EQ | EQ 2 | 1 |
| 13 | Shape | Leveling Compression (LA-2A) | Kompressor 3, Charakter **Opto** | 2 |
| 14 | Shape | Multiband Compression | **neu: Multiband** (`mbcomp`) | 0 |
| 15 | Enhance | Width / Doubler | Doubler (`doubler`) + Stereo-Stufe | 0 |
| 16 | Enhance | Parallel Compression | Vocal Smash (`smash`) | 0 |
| 17 | Space | Reverb, Delay | Hall + Delay als Sends | 0 |
| 18 | Space | Ducking | Ducker von Hall und Delay | – |
| 19 | Output | Final Output Control | Output Gain, Pegelabgleich, Prüfungen | – |

Die Instanz-Bänke reichen: EQ 4, Kompressor 3, De-Esser 3, Rest wie vorhanden. Die neuen Module bekommen je eine
Instanz.

## Ablauf der Engine

```
capture → Analyse A0 (Eingang + Beat)
  Phase 1 Clean   : Stufen 1–4   → Render R1 → Analyse A1
  Phase 2 Control : Stufen 5–8   → Render R2 → Analyse A2
  Phase 3 Shape   : Stufen 9–14  → Render R3 → Analyse A3
  Phase 4 Enhance : Stufen 15–16 → Render R4 → Analyse A4
  Phase 5 Space   : Stufen 17–18 → Render R5 → Analyse A5
  Output Control  : Stufe 19 (Pegel, Peaks, Mono, Breite, Dry/Wet, Frequenzbalance, Beat-Kontext)
```

Jede **Stufe** ist ein eigenes Objekt mit vier Teilen:

1. **detect (Analyse, Kontext) → Bedarf:** aktiv ja/nein, Schwere 0…1, gemessene Werte und Begründung. Der Kontext
   enthält Stil (Genre × Vibe), die Analysen aller früheren Phasen und die Entscheidungen der früheren Stufen.
2. **configure (Bedarf, Stil) → Einstellungen:** alle Parameter des Moduls, stetig aus Schwere und Messwerten.
3. **verify (vorher, nachher) → Nutzen:** Die Stufe wird auf dem Stand vor ihr gerendert, mit und ohne sich. Die
   Stufe nennt ihre Nutzen-Messung (siehe Tabelle) und ihre Schaden-Messungen. Erfüllt sie das Ziel nicht, darf sie
   eine kleine Suche über ihre Intensität machen (2–5 Renders). Gelingt das nicht, schaltet sie sich aus. Der Bericht
   sagt dann „ausgeschaltet: kein messbarer Nutzen (…)“.
4. **explain → Bericht:** Titel, Begründung mit den gemessenen Zahlen, Phase.

Gerendert wird immer **die ganze Kette bis zu dieser Stufe** auf der aufgenommenen Stelle (nicht das Modul allein),
damit Wechselwirkungen stimmen. Hall und Delay bleiben bis Phase 5 aus der Messung heraus (wie heute `withoutSpace`).

Der heutige Einmal-Planer, die 4-Runden-Prüfschleife und die Kandidatensuche (`candidatesFor`) werden durch diesen
Ablauf ersetzt. Die vorhandenen Bausteine bleiben und werden von den Stufen benutzt: `VocalProfiler` (die Analyse,
jetzt auch auf Zwischen-Renders), `AutoEq` (Kurvenfit, Notches), `MixTarget` (Zielkurve), `configureDeEsser`,
die Beat-Messungen, `pruneInaudible`, der Bericht.

**Rechenzeit:** Jakob: „egal, Qualität zählt“. Es gibt kein festes Zeitlimit. Der Fortschrittsbalken zeigt Phase und
Stufe, Abbrechen bleibt jederzeit möglich. Ziel für die Umsetzung: so schnell wie ohne Qualitätsverlust möglich, die
Laufzeit wird im Bericht und im Benchmark ausgegeben.

## Die Stufen im Einzelnen

Alle Zahlen sind **Startwerte**. Sie werden in der Umsetzung auf den vorhandenen Daten kalibriert (siehe
„Kalibrierung“) und als benannte Konstanten in der Stiltabelle bzw. bei der Stufe geführt, nie als lose Zahl im Code.

### Phase 1 – Clean

**1 Input Analysis & Gain Staging.** Gemessen: Peak, RMS der Stimme (Median stimmhafter Blöcke), LUFS, Dynamik,
Crest, Clipping, Rauschpegel, Pegelunterschiede der Passagen (vorhandene `VocalProfile`-Werte).
- Input Gain nur, wenn der Stimm-RMS außerhalb des Arbeitsfensters **−18 … −12 dBFS** liegt. Ziel ist die nächste
  Fenstergrenze, nicht die Mitte (kleinster Eingriff). Peaks dürfen danach −3 dBFS nicht überschreiten.
- Clipping in der Aufnahme: Hinweis im Bericht (nicht reparierbar), Sättigung später vorsichtiger.
- **Beat:** Liegt der Beat am Sidechain und ist die Stimme mehr als einen Stil-Grenzwert (Startwert: Stimme mehr als
  **3 LU unter** dem Beat) leiser, erscheint eine **Empfehlung**: „Beat um ca. X dB leiser stellen“ (X aus der
  Messung). Dazu ein **Beat-Kontrollfeld** in der Auto-Mix-Ansicht: zeigt live Stimme und Beat und den Abstand mit dem
  Zielbereich des Stils. Die Engine ändert den Beat nicht (sie kann es nicht), sie mischt die Stimme aber für den
  gemessenen Abstand.

**2 Breath Control.** Detektor: Anteil der Atmer (`breathShare`) und ihr Pegel gegen die Phrase. Natürliche Atmer
(leiser als Stil-Grenze, Startwert **−30 dB unter der Phrase**) bleiben. Aktiv nur, wenn laute Atmer da sind.
Intensität aus ihrem Pegelüberschuss. Rap/Speech und Vibe „Natural“ behalten mehr Atem. Nutzen: Atem-Pegel sinkt um
mindestens die Hälfte des Ziels. Schaden: Pegel von Wortanfängen/-enden (±60 ms um Onsets) sinkt um nicht mehr als
**0,5 dB**.

**3 Gate / Expander (neues Modul).** Detektor: Pegel der Pausen zwischen Wörtern gegen den Wortpegel (`gapDb`) und
der Rauschboden. Aktiv nur, wenn die Pausen hörbar sind (Startwert: Pausen weniger als **35 dB** unter den Wörtern)
und die Aufnahme keinen eigenen Raum/Hall hat (sonst schneidet er die Fahne ab). Stetiges, breitbandiges Rauschen
(Lüfter, Rauschen) geht zusätzlich an **Raumrauschen** (`denoise`), wie bisher nur bei SNR < 35 dB.
Einstellungen: **Expander** (Ratio 1,5–3, Range nach Rauschabstand, max. 12–18 dB), Gate-Modus (Ratio ∞) nur bei
sehr lautem Störsignal. Schwelle zwischen Rauschboden und leisesten Wörtern. **Look-ahead** (Startwert 5 ms), Attack
aus der Schärfe der Einsätze, Hold aus der Silbenrate, Release aus den gemessenen Wortenden. Nutzen: Pausenpegel sinkt
um mindestens **6 dB**. Schaden wie bei Breath (Wortanfänge/-enden), dazu kein Flattern (Anzahl Öffnen/Schließen pro
Sekunde begrenzt).

**4 Vocal Rider.** Detektor: Spanne der Phrasenpegel (`dynamicsDb`) über dem Stil-Fenster. Ziel: Spanne in die
**obere Hälfte** des Stil-Fensters bringen. Den Rest machen die Kompressoren. So arbeitet der Rider nicht gegen die
Performance. Intensität stetig aus dem Überschuss, Grenzen bei Rauschen wie heute. Nutzen: Phrasenspanne sinkt
messbar Richtung Ziel. Schaden: Rauschen in den Pausen steigt nicht (Silence-Schwelle aus A1).

→ **Render R1, Analyse A1.**

### Phase 2 – Control (Frequency Cleanup)

**5 Corrective EQ (EQ 1 + Dynamic EQ).** Analysiert auf A1.
- **Low Cut** aus dem tiefsten gesungenen Ton und dem Rumpeln (wie heute), Steilheit nach Rumpelmenge.
- Problemfelder mit eigenem Detektor gegen die Zielkurve und gegen den lokalen Spektralverlauf: **Mud/Tiefmitten**
  (200–500 Hz), **Boxiness** (300–800 Hz), **Nasalität** (800 Hz–1,5 kHz), **Härte** (2–5 kHz), **Resonanzen**
  (schmale Spitzen, `resonances`), **zu viel Tiefbass** (unter dem Grundton).
- **Statisch oder dynamisch:** Für jedes Problem misst die Engine, wie oft es auftritt (Anteil der Frames, in denen
  das Band über der Schwelle liegt; vorhanden als `persistence` bei Resonanzen, für die Bänder neu). Ständig
  (Startwert ≥ 70 % der Frames) → statischer Schnitt in EQ 1 (Notch mit schmalem Q bei Resonanzen). Zeitweise →
  Band im **Dynamic EQ**, das nur absenkt, wenn das Problem auftritt.
- **Keine Anhebungen** in dieser Phase. **So wenige Bänder wie möglich:** nur Probleme über der Schwelle, sortiert
  nach Schwere, höchstens 4 statische und 4 dynamische Bänder.
- Nutzen: Abstand des Problembands zur Zielkurve sinkt. Schaden: kein Band außerhalb des Problems verliert mehr als
  1 dB, kein Verlust an Körper (wie heute beim Low Cut).

**6 De-Esser 1.** Detektor: S-Laute gegen Vokale (`sibilanceVsVowelDb`) über dem Stil-Grenzwert. Frequenz aus der
gemessenen S-Energie, Schwelle zwischen Vokal- und S-Pegel (vorhandenes `configureDeEsser`), Range aus dem Überschuss,
relative Erkennung an. Nutzen: S/Vokal-Abstand sinkt auf den Stil-Zielwert. Schaden („Lispeln“): S/Vokal darf nicht
mehr als **4 dB** unter dem Zielwert landen, Höhen auf Vokalen (Frames ohne S) verlieren nicht mehr als 0,5 dB.

**7 Peak Control (Kompressor 1, Clean).** Detektor: einzelne Spitzen deutlich über dem Wortpegel (Crest über
Stil-Fenster, Anteil von Peaks > 6 dB über dem Silben-RMS). Aktiv nur dann. Schnelle Attack (0,5–3 ms), kurze
Release, Ratio 4–8, Schwelle so, dass nur die Spitzen greifen. **Ziel-Reduktion: Spitzen 2–4 dB, auf normalen Wörtern
≤ 1 dB.** Nutzen: Crest sinkt Richtung Fenster. Schaden: mittlere Reduktion auf Wörtern bleibt unter 1,5 dB.

**8 Resonance Suppression (Smooth).** Detektor: zeitweise auftretende Spitzen und Härte nach EQ 1 und Dynamic EQ
(auf A1 plus dem, was Stufe 5 nicht genommen hat). Resonanzen, die EQ 1 oder der Dynamic EQ bereits behandeln,
zählen nicht. Aktiv **nur bei Befund** (die „Grundpflege“ entfällt). Intensität aus der Schwere, Obergrenze nach Stil
(Startwert 45 %). Bereich (low/high Hz) aus den gemessenen Problemfrequenzen, nicht fest 1–10 kHz. Nutzen: Spitzen im
Problembereich sinken. Schaden: Höhen gesamt verlieren nicht mehr als 1,5 dB („Vocal muss lebendig bleiben“).

→ **Render R2, Analyse A2.**

### Phase 3 – Shape (Tonal Shaping & Character)

**9 Character Compression (Kompressor 2, FET/1176).** Analysiert auf A2. Aktiv, wenn die Stimme nach Phase 2 mehr
Dichte/Kontrolle braucht, als der Stil will (Phrasenspanne und Crest gegen das Stil-Fenster). Bei bereits dichten,
fertig komprimierten Aufnahmen aus oder sehr leicht.
- **Ziel-Reduktion (Median auf Wörtern) aus dem Stil:** Startwerte 3–7 dB, bis 10 dB nur wenn das Material es
  erfordert (Spanne/Crest weit über dem Fenster) und der Stil es erlaubt (z. B. Rap · Aggressiv). Ballade · Natural
  2–4 dB.
- Die Engine sucht die Schwelle, bis die **gemessene** Reduktion im Zielband liegt (gerenderte Messung, nicht
  geschätzt). Ratio aus dem Stil (4:1 Standard, 8:1 aggressiv). Attack aus der Schärfe der Einsätze und dem Stil,
  Release aus der Silbenrate (vorhandene Formeln, auf die FET-Kennlinie umgerechnet).
- Nutzen: Phrasenspanne und Crest landen im Fenster. Schaden: kein hörbares Pumpen (Pegel in Pausen direkt nach
  Wörtern steigt nicht über eine Grenze), Transienten nicht völlig weg (Onset-Schärfe bleibt über einem Stil-Minimum).

**10 Additional De-Essing (De-Esser 2).** Misst S/Vokal **nach** Stufe 9 (Render). Aktiv nur, wenn die S-Laute jetzt
über dem Stil-Grenzwert liegen oder die Kompression sie um mehr als 2 dB angehoben hat. Kennt De-Esser 1: gleiche
Frequenz bevorzugt, Summe der Ranges begrenzt (Startwert 10 dB), sonst Lispeln. Nutzen/Schaden wie Stufe 6.

**11 Harmonic Enhancement (Sättigung).** Detektor: Wenig Dichte/Obertöne (Obertonanteil der Stimme, Körper unter der
Zielkurve, dünne/dunkle Stimme) **oder** der Stil will Charakter (Stiltabelle). Aus bei gemessener Verzerrung oder
Clipping, sehr vorsichtig bei harter, heller Stimme.
- **Charakter aus Stimme und Stil:** Tape (rund, glättet Spitzen; warme/weiche Stile), Tube (2. Harmonische, Körper;
  dünne Stimme), Warm/Analog (dezent; harte Stimme), Hard/Soft Clip (Rap · Aggressiv), Exciter = Multiband-Sättigung
  nur im Höhenband (dunkle Stimme, Air fehlt).
- Drive/Mix stetig aus dem Bedarf. Nutzen: Obertonanteil bzw. Dichte steigt messbar. Schaden: Verzerrung (Klirr auf
  gehaltenen Tönen) unter Stil-Grenze, Härte 2–5 kHz steigt nicht über die Zielkurve.

**12 Tonal EQ (EQ 2).** Erst hier darf geboostet werden. Kurvenfit der Analyse nach Stufe 11 auf die Zielkurve des
Stils (vorhandener `AutoEq`-Fit, aber in EQ 2 und nur mit breiten Pultec/SSL-artigen Bändern): Air (High Shelf),
Präsenz (breite Glocke), Wärme/Körper (Low Shelf/Glocke), Low-End-Shaping. **Nur Bereiche, die gegen die Zielkurve
fehlen oder zu viel sind.** Mit Beat: Maskierung in Präsenz/Tiefmitten fließt ein (vorhandenes `unmaskOf`).
Obergrenzen je Band (Rauschen, S-Laute, wie `limitsOf`). Nutzen: Abstand zur Zielkurve sinkt. Schaden: S-Laute und
Rauschen steigen nicht über Grenzen.

**13 Leveling Compression (Kompressor 3, Opto/LA-2A).** Aktiv, wenn nach Stufe 12 die Phrasenspanne noch über dem
Stil-Fenster liegt. Kennt die Reduktion von Kompressor 2: Die Summe der mittleren Reduktionen bleibt unter einer
Stil-Grenze (Startwert 10 dB). Langsame, programmabhängige Release, weiches Knie, Ziel-Reduktion 1–3 dB. Nutzen:
Spanne sinkt ins Fenster. Schaden wie Stufe 9.

**14 Multiband Compression (neues Modul).** Detektor pro Band, auf der Analyse nach Stufe 13: Schwankung des
Bandpegels gegen die Gesamtstimme über die Zeit (Spanne der relativen Bandpegel). Aktiv **nur für die Bänder**, deren
Schwankung über der Grenze liegt (Beispiele aus dem Konzept: schwankende Tiefmitten, unkontrollierte Höhen, zu
dominante Präsenz). Jedes Band eigene Schwelle/Ratio/Zeiten aus seiner Messung, die anderen Bänder bleiben aus.
Trennfrequenzen aus den gemessenen Problembereichen. Nutzen: Schwankung des Bands sinkt. Schaden: Bandpegel im Mittel
bleibt innerhalb von 1 dB (kein versteckter EQ).

→ **Render R3, Analyse A3.**

### Phase 4 – Enhancement & Stereo

**15 Width / Doubler.** Aus dem Stil (Breite je Genre × Vibe) und der Stimme (gehaltene Töne → mehr, Rap → wenig/kein),
nicht bei bereits breiten Aufnahmen. Modi des Doublers: Doubling, Micro-Pitch (Detune), Modulation (Chorus-artig über
Mod Depth/Rate). Lead bleibt in der Mitte (Mono-Safe). Nutzen: Seitenanteil im Stil-Zielbereich. Schaden:
**Mono-Kompatibilität** (Pegelverlust Mono < 1 dB, Korrelation > Grenze).

**16 Parallel Compression (Smash).** Detektor: Leise Details gehen unter (Abstand leiser zu lauter Wörter nach
Phase 3 noch über Stil-Fenster) oder der Stil will Dichte und die Stimme hat sie noch nicht (Crest über Fenster).
Nicht bei bereits dicht komprimierten Aufnahmen. Blend stetig aus dem Bedarf. Nutzen: leise Wörter näher an den
lauten, Dichte steigt. Schaden: Rauschen/Atem in den Pausen steigt nicht über die Grenze.

→ **Render R4, Analyse A4.**

### Phase 5 – Space

**17 Effects (Hall, Delay, Slapback, Ping-Pong).** Grundlage ist der Stil, angepasst an die Stimme und den Beat:
- Aus, wenn die Aufnahme schon Raum hat (`reverberant`) oder der Stil trocken ist.
- Art aus der Stimme: gehaltene Melodien → längerer Hall, Rap/Speech → kurz/trocken, Slapback bei Rap · Aggressiv.
- Zeiten aus dem Songtempo (Delay-Teilung, Hallzeit in Takten, Pre-Delay), ohne Tempo aus der Silbenrate.
- Menge so, dass die Fahne im Stil-Zielbereich liegt (vorhandene SONOVOX-Kalibrierung: −25/−29/−41 dB für
  Druckvoll), **gemessen** auf dem Render, nicht geschätzt. Dichter Beat → weniger, Beat mit Lücken → mehr.
- Immer als **Sends**.

**18 Ducking.** Für Hall und Delay: Schwelle aus dem Wortpegel nach Phase 4, Tiefe aus dem Stil. Nutzen: Fahne in den
Pausen hörbar, während der Wörter um die Ducking-Tiefe leiser (gemessen).

→ **Render R5, Analyse A5.**

### 19 Final Output Control

Gemessen auf R5 und, wenn vorhanden, mit dem Beat:
- Lautheit wird auf die **Eingangslautheit** abgeglichen (wie heute `targetLufs`), Peaks unter −1 dBFS, kein Clipping.
- Mono-Kompatibilität und Breite, Dry/Wet-Verhältnis (Fahnenpegel), Frequenzbalance gegen die Zielkurve,
  Dynamik im Fenster.
- **Überbearbeitungs-Prüfung:** Summe der Kompression, Sättigung, De-Essing und EQ-Hub gegen Stil-Grenzen. Liegt
  etwas drüber, wird die stärkste Ursache zurückgenommen und die betroffene Phase neu geprüft.
- Mit Beat: Stimme gegen Beat (Pegel, Maskierung). Ergebnis und ggf. Beat-Empfehlung erscheinen im Bericht.

## Stiltabelle (Genre × Vibe)

`MixTarget` wird zur vollständigen Stiltabelle erweitert. Für jede der 25 Kombinationen (Genre: Party, Rap, Pop, EDM,
Ballade; Vibe: Druckvoll, Aggressiv, Floaty, Warm, Natürlich) enthält sie: Zielkurve + Toleranz (vorhanden),
Dynamik-Fenster (vorhanden), **Ziel-Reduktion je Kompressor-Stufe**, Grenze Gesamt-Kompression, S-Grenzwert
(vorhanden), Atem-Grenze, Pausen-Grenze, Sättigungs-Charakter und -Menge (vorhanden), Breite, Raum (vorhanden),
Ducking-Tiefe, Beat-Abstand. Werte, die nicht auf Daten beruhen, bleiben als `estimated` markiert. Genre setzt den
Grundwert, Vibe verschiebt ihn (wie heute).

## Neue Module (DSP)

Alle neuen Parameter werden **angehängt**, bestehende IDs und Plugin-Codes (`Lnda`/`Vlvx`) bleiben. Neue `ModuleId`s
werden ans Enum angehängt (`gate = 13`, `dyneq`, `mbcomp`), Routing-Strings bleiben lesbar. Alte Projekte laden
unverändert (neue Module aus und nicht in der Kette). Echtzeitsicher, keine Allokation im Audio-Thread, Latenz wird
gemeldet. Vorlage für den Einbau aller Teile (DSP, Parameter, Routing, Panel, Hilfe, Browser, Tests): das
Breath/Denoise-Paket von 0.7 (`docs/superpowers/plans/2026-10-27-denoise-breath-modules.md`).

- **Expander/Gate (`gate`):** Schwelle, Ratio (1,2 … ∞), Range, Attack, Hold, Release, Look-ahead (feste Latenz),
  Sidechain-Hochpass, Stereo verkoppelt. Hysterese gegen Flattern.
- **Dynamic EQ (`dyneq`):** 4 Bänder (Glocke, Low/High Shelf), je Frequenz, Q, Schwelle, max. Absenkung (Range),
  Attack, Release, an/aus. Detektor relativ zum Breitbandpegel (wie De-Esser „relative“), damit laute und leise
  Phrasen gleich behandelt werden. Absenkung nur, wenn das Band über der Schwelle liegt.
- **Multiband-Kompressor (`mbcomp`):** 3 Bänder mit Linkwitz-Riley-Weichen (Summe phasenkorrekt, flach bei 0 dB
  Reduktion), je Band an/aus, Schwelle, Ratio, Attack, Release, Makeup. Zwei Trennfrequenzen einstellbar.
- **Kompressor-Charakter (alle 3 Instanzen):** neuer Parameter „Charakter“: **Clean** (heutiges Verhalten, Default),
  **FET** (1176-artig: Peak-Detektor in Rückkopplung, sehr schnelle Attack, Ratio-abhängiges Knie, leichte
  ungerade Obertöne), **Opto** (LA-2A-artig: programmabhängige zweistufige Release, weiches Knie, langsamere Attack,
  leichte gerade Obertöne). Default Clean, damit alte Projekte gleich klingen.

## Oberfläche

Jakob will das **volle Design jetzt**. Er gibt die Richtung über SVG-Mockups im Browser vor (3–5 Varianten pro Thema,
„nicht 1 zu 1“). Themen:
1. Panel **Expander/Gate** (Pegelkurve/Transferkennlinie, Reduktion live).
2. Panel **Dynamic EQ** (Kurve mit statischer Lage und Live-Absenkung je Band, wie der EQ).
3. Panel **Multiband** (3 Bänder nebeneinander, Reduktion je Band, Weichen ziehbar).
4. **Charakter-Schalter** im Kompressor-Panel (Clean / FET / Opto).
5. **Auto-Mix-Bericht nach Phasen** (Clean → Control → Shape → Enhance → Space, je Stufe an/aus mit Begründung
   und Messwert; ausgeschaltete Stufen mit „kein Bedarf“/„kein Nutzen“).
6. **Beat-Kontrollfeld** und Beat-Empfehlung (Stimme vs. Beat live, Zielbereich).

Gebaut wird im bestehenden Stil (Theme, LookAndFeel, Feuer-Akzent = „das hat der Auto Mix gemacht“). Texte deutsch
und englisch wie alle anderen (`txt::both`).

## Kalibrierung und Prüfung

- Startwerte werden auf den vorhandenen Daten kalibriert: SONOVOX-Paare trocken/Profi (`velvox-bench\2026-11-03\
  sonovox`), VocalSet (val-Split, Held-out unberührt), Jakobs Aufnahmen, Profi-Stems. Werkzeuge: `VocalypseTests
  --bench`, `tools/bench/*`. Neu im Bench-Bericht: pro Stufe aktiv/aus, Schwere, Grund, Nutzen-Messung.
- **Prüfkriterien für die Engine (Tests):**
  - Auf einer sauberen, gut aufgenommenen Stimme bleiben Gate, Dynamic EQ, Multiband, Smooth und Peak Control aus.
  - Auf künstlich verschlechterten Fassungen derselben Stimme (Rauschen dazu, Resonanz dazu, S-Laute verstärkt,
    starke Pegelsprünge, Atmer lauter) schaltet genau die zuständige Stufe ein und der Nutzen ist messbar.
  - Kompressions-Reduktion jeder Stufe liegt im Zielband des Stils (gerendert gemessen).
  - Verschiedene Stimmen ergeben verschiedene Einstellungen, ein Wert ist nur gleich, wenn der Bericht es begründet
    (vorhandenes `summarise_reports.py` „distinct settings“).
  - Ausgangslautheit = Eingangslautheit ± 1 LU.
  - Keine abgeschnittenen Wortanfänge/-enden (Onset-Pegel vorher/nachher).
- Die alten Goldens des Planers werden durch neue ersetzt. Voller Testlauf grün, pluginval Strictness 10 SUCCESS,
  FL-Exit ohne Hänger (`VocalypseRefHost --editor`), bevor ich „fertig“ sage.
- Hör-Kit für Jakob: Original, Auto Mix 0.8, Auto Mix 0.9 auf seinen und den SONOVOX-Takes, −18 LUFS.

## Entschieden von Jakob (2026-11-04)

- Engine nach dem Konzept umbauen („fang einfach an“).
- Alle fehlenden Module als **echte neue Module** bauen (Expander, Dynamic EQ, Multiband, Kompressor-Charakter).
- **Volles UI-Design jetzt**, über Mockups.
- Rechenzeit: **egal, Qualität zählt**.

## Meine Entscheidungen (bitte prüfen)

- Raumrauschen (`denoise`) bleibt für stetiges Rauschen in Stufe 3, neben dem neuen Expander (Expander = Pausen,
  Raumrauschen = Rauschen unter der Stimme).
- Smash übernimmt die Parallel-Kompression (Stufe 16), statt ein neues Modul zu bauen. Er macht genau das.
- „Chorus“ und „harmonische Verdopplung“ aus Schritt 15/17 laufen über die vorhandenen Doubler-Modi (Modulation,
  Detune). Kein eigener Chorus und kein Harmonizer in dieser Runde.
- Der Exciter ist die Multiband-Sättigung im Höhenband, kein eigenes Modul.
- Input Gain nur außerhalb −18 … −12 dBFS, auf die nächste Fenstergrenze.
- Die Engine ändert den Beat nie selbst, sie empfiehlt nur.
- Der Nutzer kann die Kette im PRO-Modus weiter frei umbauen. Die feste Reihenfolge gilt nur für den Auto Mix.

## Offen für die Umsetzung (Builder entscheidet)

- Genaue Detektor-Formeln und Kalibrierwerte (nach Daten, als benannte Konstanten).
- Interne Struktur (Stufen-Klassen, Kontext-Objekt), Render-Caching zwischen Stufen.
- DSP-Details der neuen Module im Rahmen der obigen Beschreibung.
- Reihenfolge der Umsetzung (Vorschlag: DSP-Module → Stufen-Gerüst → Stufen Phase 1 bis 5 → Output Control →
  Bericht/UI → Kalibrierung → Hör-Kit).
