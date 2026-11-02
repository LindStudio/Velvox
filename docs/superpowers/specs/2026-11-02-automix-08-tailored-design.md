# Auto Mix 0.8 – „auf die Stimme angepasst“ (Design)

Stand 2026-11-02. Grundlage: Jakobs Hörtest des Auto Mix (neuer Stand 8c1fd93) auf seiner eigenen Stimme.

## Warum

Jakob: „klingt besser, aber noch nicht top. Viele Entscheidungen klingen random. Jedes Modul klingt, als wäre es nicht
genau auf die Stimme angepasst. Die Stimme wird nur lauter gemacht, um über dem Beat zu stehen, statt richtig gemixt zu
werden.“

Ziel ist ein **finales Mixing-Plugin**, kein schneller Vormix für die Aufnahmesession: Bei normalen bis guten
Aufnahmen soll der Auto Mix die Stimme fertig machen. Feinschliff macht der Nutzer danach in den Modulen. Bei
schlechten Aufnahmen darf das Ergebnis schwächer sein.

Referenz (Jakob): Partysongs – Tream, FiNCH, Cover-Songs. „Bei Tream klingt die Stimme **voller**, hat oben herum
trotzdem **Brillanz**, **jedes Wort ist klar und verständlich**, und sie **sitzt im Beat**.“

### Befund im Code (Ursache für „random“)

- Viele Module bekommen feste Werte, egal welche Stimme: Smash immer 40 % / 18 % Blend, Sättigung immer 5 dB Drive /
  30 %, Doubler immer 20 %, Kompressor fast immer 3:1 mit 4 dB unter dem Median, Release immer 110 ms, Delay immer
  1/8 punktiert 9 %.
- Der EQ sieht die Stimme nur in 5–7 groben Bändern (Körper, Tiefmitten, Präsenz, Höhen, Luft) mit festen
  Fenstern. Er kann deshalb nur grobe Standardzüge setzen (Low Cut, breite Tiefmitten-Absenkung, Präsenz-Glocke,
  Shelf) und nicht „das Dröhnen dieser Stimme bei 340 Hz“.
- Genre und gewünschter Charakter sind unbekannt. Alles ist auf einen festen „Party-Lead“ eingestellt.

## Ziele

1. **Jedes Modul begründet sich aus dieser Stimme** (und Genre/Vibe). Kein Modul bekommt bei allen Stimmen dieselben
   Werte, außer es gibt dafür einen ausgesprochenen Grund im Bericht.
2. **EQ gezielt:** schlechte Frequenzen dieser Stimme (Dröhnen, Pappigkeit, Nasales, Härte, Resonanzen) werden genau
   dort abgesenkt, fehlende Qualitäten (Körper, Verständlichkeit, Brillanz) genau dort angehoben, wo sie dieser Stimme
   gegenüber dem Zielbild fehlen. Hat eine Stimme kein Problem in einem Bereich, wird dort nichts gemacht.
3. **Kompressor gezielt:** Ratio, Schwelle, Attack und Release folgen der gemessenen Dynamik, den Konsonanten und dem
   Sprech-/Silbentempo dieser Stimme und dem Ziel von Genre/Vibe.
4. **Genre und Vibe** werden beim Start abgefragt und verschieben das Zielbild und die Effekt-Entscheidungen.
5. **Beat-Sidechain** wird am Anfang erklärt (mit FL-Studio-Anleitung) und ihr Status live angezeigt.
6. **Kein vorzeitiges Analysieren:** Der Auto Mix hört die volle vorgegebene Zeit zu; „Jetzt analysieren“ gibt es
   nicht mehr.
7. Das Zielbild ist **datengetrieben und neu erzeugbar**: Bringt Jakob später Acapellas, wird es mit einem Befehl
   neu berechnet (keine Code-Änderung).

## Nicht-Ziele

- Keine Online-KI, kein eigenes neuronales Netz (zu wenig Trainingsdaten; im Chat mit Jakob besprochen).
- Keine Änderung an den DSP-Modulen selbst, keine neuen Parameter-IDs, Plugin-Codes bleiben stabil.
- Kein Mastering/Limiter der Stimme, keine Lautheitsmaximierung.

## Anti-Ziele (wäre ein Fehlschlag, auch wenn es „funktioniert“)

- Die Stimme wird wieder nur lauter/heller, statt geformt.
- Rauschen oder Zischeln wird durch Anhebungen lauter.
- Eine schon gute Aufnahme wird überbearbeitet (viele große EQ-Züge auf einer Stimme, die schon nah am Ziel ist).
- Die Entscheidungen sind nicht erklärbar (jede Entscheidung braucht einen Bericht-Satz mit Messwert).

## Entscheidungen

### Start-Ablauf (UI, Analyse-Overlay)

- **Intro-Karte** (statt „Vocal analysieren / Später“ direkt):
  1. **Genre** (eine Auswahl, Chips): *Party* (Standard), *Deutschrap / Hip-Hop*, *Pop*, *EDM / Techno*,
     *Ballade / Akustik*.
  2. **Vibe** (eine Auswahl, Chips): *Druckvoll* (Standard), *Aggressiv*, *Floaty*, *Warm*, *Klar & natürlich*.
  3. **Beat-Kasten:** „Beat als Sidechain hinzufügen (empfohlen) – dann passt der Auto Mix die Stimme an den Beat an
     und stellt den Pegel über dem Beat ein.“ Darunter die FL-Studio-Schritte (Mixer: Beat-Kanal auswählen →
     Rechtsklick auf den Pfeil des Headliner-Kanals → „Sidechain to this track“ → im Plugin-Wrapper unter Eingang den
     Sidechain-Eingang setzen). Status-Punkt: „Beat erkannt“ / „Kein Beat am Sidechain“.
  4. Button „Auto Mix starten“.
- Die Auswahl wird pro Instanz im Projekt gespeichert und beim nächsten Start vorausgewählt.
- **Zuhören:** Fortschrittsbalken bis zur Zielzeit (14 s Gesang), Text „Noch X s Gesang“. Der Primär-Button ist
  während des Zuhörens **nicht vorhanden** (nur „Abbrechen“). Ist die Zielzeit erreicht, startet die Analyse
  automatisch (bestehendes Verhalten des CaptureBuffer). Stoppt der Song vorher: Hinweis „Weiter abspielen – noch X s
  nötig“, die Aufnahme setzt beim nächsten Abspielen fort. Ist der Puffer (24 s Signal inkl. Lücken) voll, bevor 14 s
  Gesang da sind, wird analysiert, was da ist (bestehende Regel, `conservative`).
- Der Beat-Status ist auch während des Zuhörens sichtbar.

### Zielbild („Target“) pro Genre, verschoben durch den Vibe

- Ein Zielbild besteht aus: **Zielkurve** (Pegel pro Terzband 80 Hz – 16 kHz relativ zum Kern 300 Hz – 2 kHz, mit
  Streuung pro Band), **Dynamik-Ziel** (Phrasen-Spreizung, Crest-Faktor), **Dichte** (Smash/Sättigung-Bereich) und
  **Raum** (Hall-/Delay-Anteil, Hallzeit relativ zum Tempo, Doubler).
- **Party** wird aus den vorhandenen fertigen Profi-Vocals gemessen (`velvox-bench/2026-10-29/train/pro`: Tream,
  HBz, Loona, …; getrennte Stems). Weil getrennte Stems oben Beat-Reste haben können, wird oberhalb 10 kHz die
  Streuung breiter gesetzt und der Wert gegen die bisherigen Fenster plausibilisiert.
- Die anderen Genres sind **vorerst Ableitungen von Party mit dokumentierten Offsets** (im Code als „geschätzt“
  markiert), bis es Daten gibt: Rap trockener, dichter, mehr Präsenz; Pop etwas mehr Luft, weniger Dichte; EDM heller,
  mehr Raum; Ballade mehr Dynamik, mehr Körper, mehr Hall, weniger Sättigung.
- **Vibe-Offsets:** Aggressiv = mehr Präsenz/Biss, dichter, kräftigere Sättigung, trockener. Floaty = mehr Luft,
  weicher komprimiert (langsamere Attack), mehr Hall/Delay und Breite. Warm = mehr Körper, weniger Luft, Tape. Klar &
  natürlich = nah an der Aufnahme, wenig Dichte, wenig Effekte. Druckvoll = Genre-Standard.
- **Werkzeug:** `VelvoxTests --target-curve <ordner> <genre>` misst alle WAVs eines Ordners wie das Plugin und
  schreibt die Zielkurve als generierten Header (wie `--check-template`). Neue Acapellas = Befehl erneut ausführen.

### EQ: Kurvenabgleich statt fester Züge

- Stimme und Zielkurve werden auf Terzbänder (aus dem 1/12-Oktav-Spektrum, geglättet) gebracht. Abweichung
  = Stimme − Ziel, in Einheiten der Zielstreuung (was im Profi-Bereich liegt, zählt nicht).
- **Gieriger Fit:** Größte zusammenhängende Abweichung zuerst; Glocke in der Mitte des Bereichs, Q aus seiner Breite
  (0,7 … 4), Gain = Teil-Korrektur (≈ 70 % der Abweichung). Absenkungen bis −6 dB, Anhebungen bis +4 dB. Breite
  Bereiche an den Rändern als Shelf (Körper < 250 Hz, Brillanz > 8 kHz). Danach Restabweichung mit dem Modell
  (`responseDb`) neu rechnen, nächster Bereich, bis das Budget erreicht ist oder nichts mehr über der Schwelle liegt.
- **Grenzen gegen Anti-Ziele:** Anhebungen über 5 kHz begrenzt nach Rauschabstand und S-Lauten (wie heute: max. 2 dB
  bei Rauschen); keine Anhebung in einem Bereich mit gemessener Resonanz; Notches für anhaltende Resonanzen bleiben
  (bestehende Logik) und haben Vorrang.
- **Beat:** Wo der Beat die Stimme im Bereich 1–5 kHz verdeckt, wird das Ziel dort um bis zu 2 dB angehoben
  (bestehende Masking-Messung, aber pro Terzband).
- **Benannte Züge im Bericht** nach Bereich: „Dröhnen 340 Hz −3,5 dB“, „Pappig 700 Hz −2 dB“, „Nasal 1,2 kHz“,
  „Härte 3,5 kHz“, „Körper +2 dB (Shelf 180 Hz)“, „Verständlichkeit 3 kHz +2 dB“, „Brillanz +3 dB (Shelf 10 kHz)“ –
  jeweils mit dem Messwert („+3,1 dB über dem Party-Ziel“).
- Budget: bis 8 Bänder in EQ 1 (inkl. Low Cut, max. 3 Notches); Nutzer- und Vocal-Check-Bänder bleiben unangetastet.
- **Geschlossener Kreis:** Nach dem Render wird das Ergebnis wieder auf Terzbänder gemessen und die Gains der Auto-Mix-
  Bänder werden nachgeregelt (bis zu 2 Runden), weil Kompressor, Sättigung und Effekte die Balance verschieben.
- Höhen-Anhebungen wandern in EQ 2 hinter die Dynamik, wenn das in der Bewertung besser abschneidet (bestehende
  Variante bleibt).

### Dynamik: Kompressor, Ride, Smash

- **Neue Messungen:** Silben-/Onset-Rate (Onsets pro Sekunde), Onset-Schärfe (wie stark Konsonanten über dem
  folgenden Vokal liegen).
- **Kompressor:** Schwelle und Ratio werden per Render-Suche so gewählt, dass Phrasen-Spreizung und Crest-Faktor im
  Dynamik-Ziel von Genre/Vibe landen (wenige Renders: 3–4 Schwellen-/Ratio-Paare). Attack aus Onset-Schärfe und Vibe
  (scharfe Konsonanten + Aggressiv → schneller; Floaty → langsamer, Transienten bleiben). Release aus der Silbenrate
  (≈ Silbenabstand, 40–250 ms), sodass der Kompressor zwischen den Silben zurückkommt.
- **Ride:** wie bisher, Ziel aus dem Dynamik-Ziel.
- **Smash (Parallel):** Blend so gewählt, dass die Dichte (Crest nach der Kette) das Ziel erreicht; 0 %, wenn der
  Kompressor allein reicht. Ton aus der Zielkurve (dunkle Stimme → heller Smash).

### Färbung und Raum

- **Sättigung:** Charakter aus Vibe und Stimme (dünn → Tube für Körper, dunkel → Tape, scharf → Warm, Aggressiv →
  Tube/Hard), Drive/Mix nach Fülle-Defizit (Körper unter Ziel) und Vibe; aus, wenn die Präsenz-/Härte-Messung nach
  dem Render über das Ziel steigt.
- **Doubler:** nach Genre/Vibe und Gesangsart (Rap-Lead wenig/aus, Floaty mehr), nie bei schon breiten Aufnahmen.
- **Hall/Delay:** Typ, Anteil, Hallzeit und Pre-Delay aus Genre/Vibe, Tempo (Host-BPM, falls vorhanden) und
  Beat-Dichte; Delay-Teilung und Feedback nach Vibe und Gesangsart.
- **De-Esser, Smooth, Room Noise, Breath:** bleiben messbasiert wie bisher; das Ziel der S-Balance kommt aus dem
  Zielbild.

### Bewertung (Objective)

- Der Tonal-Term misst die Abweichung von der **Zielkurve** pro Terzband (in Einheiten der Streuung) statt der fünf
  Fenster. Der Dynamik-Term nutzt das Dynamik-Ziel von Genre/Vibe.
- Mit Beat: zusätzlicher Term „sitzt im Beat“ = verdeckte Verständlichkeit 1–5 kHz bei 1 LU über dem Beat.
- Die bestehende Varianten-Suche bleibt, die Varianten werden aus Genre/Vibe abgeleitet.
- Zeitbudget der Analyse: bis ca. 25 s (heute 12 s).

## Wie wir prüfen, dass es besser ist

- Bench auf dem Trainingssatz (23 trockene Stimmen): Abweichung von der Party-Zielkurve nach dem Mix (Median über
  Terzbänder 100 Hz – 12 kHz) **kleiner als beim Stand 8c1fd93**.
- **Vielfalt:** Für jedes Modul streuen die gewählten Hauptwerte über die 23 Stimmen (kein Modul mit identischen Werten
  bei allen Stimmen, ausgenommen begründete Konstanten); das Bench-Skript zeigt das.
- Keine „DUMB“-Entscheidung (bestehende Prüfung) und keine Anhebung > 2 dB über 5 kHz bei Rauschabstand < 28 dB.
- Unterschiedliche Genre/Vibe-Wahl ergibt hörbar und messbar unterschiedliche Mixe derselben Stimme.
- Alle Tests grün, pluginval 10. Vorher/Nachher-Renders von Jakobs Stimme für seinen Hörtest.

## Dem Bauenden überlassen

- Genaue Offsets der geschätzten Genres/Vibes (dokumentiert im Code).
- Die Glättung/Schwellen des Kurvenfits, solange die Prüfungen oben halten.
- Layout der Intro-Karte im bestehenden Liquid-Glass-Stil.

## Selbst getroffene Entscheidungen (Jakob hat „mach selbst“ gesagt)

Genre- und Vibe-Liste, Standard Party + Druckvoll, 14 s bleibt die Zielzeit, Zielbild vorerst aus den vorhandenen
getrennten Profi-Stems, Analysezeit bis ca. 25 s, EQ-Budget 8 Bänder.
