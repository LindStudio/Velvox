# Changelog

Versionierung nach [SemVer](https://semver.org/lang/de/): MAJOR.MINOR.PATCH. Vor 1.0 können Minor-Versionen neue Funktionen bringen; Projekt-State und Presets bleiben abwärtskompatibel.

## [0.6.1] – Branch `feature/automix` (Feedback-Runde, Entwicklungsstand)

### Geändert – Simpel stellt die Module ein
- **Die SIMPEL-Regler (Presence, Space, Width, Punch, Warmth; Density in den Auto-Mix-Details) stellen die Module direkt
  ein** statt eine versteckte Schicht auf die Parameter zu legen: SPACE setzt Hall- und Delay-Mix samt Ducking, PUNCH den
  Kompressor und die Sättigung usw. ADVANCED zeigt damit genau, was zu hören ist, und ein dort ausgeschaltetes Modul bleibt
  stumm (vorher spielte z. B. der Hall weiter, solange SPACE oben stand). Jede Bewegung ist ein Undo-Schritt, schnelle
  Mausrad-Rasten werden zusammengefasst; zurück auf den Ausgangswert stellt die Module exakt wieder her. Der Regler zeigt
  seine zuletzt gesetzte Stellung. Bewegt der Regler einen Mix über 50 % (Doubler, Delay/Reverb als Insert), bleibt das
  Original wie bisher voll erhalten – das Modul nutzt dann die additive Mix-Kurve.
- Host-Automation der sechs Makro-Parameter bleibt möglich: eine automatisierte Bewegung stellt die Module relativ zur
  vorherigen Stellung um (ohne Undo-Schritt), wie ein Regler-Zug.
- Die Markierung am Regler „wohin ein Makro den Wert schiebt“ und der Ketten-Zustand „nur vom Makro eingeschaltet“
  entfallen (es gibt keine solche Schicht mehr). Der Assistant schlägt keine Makro-Änderungen mehr vor, sondern die
  Modulwerte selbst.

### Alte Projekte – was sich hörbar oder in der Latenz ändert
- **Projekte und Presets mit Makro-Werten über 0 (0.2 – 0.6.0) klingen unverändert:** Beim Laden wird der Makro-Anteil einmal
  in die Modul-Parameter eingerechnet (genau das, was bisher zusätzlich verarbeitet wurde); die Makro-Regler behalten ihre
  Stellung. Nulltest gegen die bisherige Verarbeitung: identisch bis auf Rundungsreste (< −100 dBFS). Danach zeigen die
  Module die eingerechneten Werte (z. B. Hall eingeschaltet, Mix 30 % statt „Hall aus + SPACE“). Auch der gespeicherte
  Auto-Mix-Zustand „Vorher“ wird so umgerechnet. Hob ein Makro den Mix eines Doublers oder eines Delay/Reverb-Inserts mit
  neuer Mix-Kurve über 50 %, wechselt diese Instanz auf die alte (additive) Mix-Kurve – so bleibt das Original wie bisher
  erhalten. Einzige Ausnahme: ein Delay/Reverb-Insert mit neuer Mix-Kurve, dessen eigener Mix schon über 50 % lag, plus
  SPACE – dort ist das Original nach dem Laden etwas lauter als in 0.6.0 (kommt nur in mit 0.6.0 gespeicherten Projekten vor).
- Automatisierte Makros in alten Projekten wirken weiter, aber über die Module: die Automation bewegt sie relativ zur
  gespeicherten Stellung, auf dem Message-Thread (nicht sample-genau; beim Offline-Bounce kann eine schnelle Makro-Fahrt
  leicht verzögert greifen).

### Neu – Abhören
- **EQ-Band solo (Kopfhörer wie Pro-Q 4):** neben dem Punkt unter der Maus und neben dem Namen des gewählten Bands. Man
  hört nur den Bereich des Bands (Bell/Notch/Band Pass: Bandpass mit seiner Güte; Low Shelf/Low Cut: alles darunter;
  High Shelf/High Cut: alles darüber), ohne seine Verstärkung, lautheitsangeglichen (Spitzen höchstens −1 dBFS).
  Klick = an bis zum nächsten Klick, gedrückt halten = nur solange gehalten. Wechsel zwischen Bändern blendet Bereich in
  Bereich über (15 ms), ohne Knackser.
- **Sättigung: einzelne Bänder hören** (Multiband): Kopfhörer oben rechts in jedem Band, man hört nur dieses Band so,
  wie es das Modul verlässt (die drei Solos ergeben zusammen genau den normalen Klang).
- Beide sind Abhör-Zustände: kein Parameter, nicht gespeichert, nicht automatisierbar, kein Undo-Schritt; sie enden mit
  dem Schließen des Panels.

### Geändert
- **Sättigung: Mix** sitzt jetzt in der Hauptreihe des Panels (nicht mehr in „Mehr“).

### Alte Projekte – Abhören
- Ohne aktives Band-Solo ist die Ausgabe bitgleich zu vorher.

## [0.6.0] – Branch `feature/automix` („Pro-Overhaul“, Entwicklungsstand, FL-Studio-Test ausstehend)

### Neu – Oberfläche
- **SIMPEL | ADVANCED** statt Tabs-Wirrwarr: Simpel = Live-Spektrum (vorher gestrichelt, nachher gefüllt), „Vocal analysieren“,
  sechs Makros; Advanced = Modul-Kette oben, darunter das Panel des gewählten Moduls. Tabs: AUTO MIX (in Simpel) bzw.
  MODULE (in Advanced) | VOCAL CHECK. Fenster 1000 × 600, skalierbar.
- **Neue Modul-Panels** mit großer Live-Grafik, 3–5 Hauptreglern und „Mehr“: EQ (Pro-Q-artig), Kompressor (Kennlinie +
  GR-Verlauf), De-Esser (Band im Spektrum), Vocal Smash, Level Ride (Wort-Balken), Smooth (Gewichtungskurve), Pitch,
  Sättigung (Charakter-Kacheln, 3 Bänder), Delay (Echo-Karte), Pulse, Stutter (Schnitte auf der Wellenform), Doubler
  (Stimmen-Bühne), Choir (Halbkreis), Reverb (Raum mit Tiefe-Punkt), Vocal Check.
- **Einheitliche Gesten** für alle Regler und Grafik-Punkte: Ziehen, Shift/Strg fein, Doppelklick/Alt+Klick = Standard,
  Mausrad, Klick auf den Wert = eintippen, Rechtsklick-Menü (Zurücksetzen, Kopieren/Einfügen), Info nach 600 ms.
- **Undo/Redo** (Strg+Z / Strg+Y / Strg+Shift+Z, Rechtsklick): jede Geste, jede Ketten-Änderung, jedes Preset und jedes
  „Anwenden“ ist ein Schritt; schnelle Mausrad-Rasten auf einem Regler werden zu einem Schritt zusammengefasst.
- **Modul-Browser** (Suche, Gruppen) und **Preset-Liste** (Kategorien, Suche, Favoriten, eigene Presets).
- **Vocal Check**: Diagnose mit Vorschlägen und „Anwenden“ / „Alles anwenden“ (je ein Undo-Schritt).
- Entfernt: A/B-Slots (der Auto-Mix behält einen versteckten Vorher-Zustand: **Vorher | Nachher** in Simpel) und die
  VOCAL-CONTROL-Leiste (die Makros sitzen in Simpel).

### Neu – Klang
- **EQ: 16 Bänder pro Instanz** (Bell, Shelves, Low/High Cut 6–48 dB/Okt., Notch, Band Pass, Tilt), analog-gematcht ohne
  Höhen-Stauchung: 15-kHz-Bell 0,27 dB neben der Analog-Kurve (vorher 5,5 dB; Pro-Q 4: 0,06 dB).
- **Pitch: neue Engine** mit 14,6 ms Latenz (vorher 120 ms; Graillon 3: 22 ms), exakte Transposition (0,00 ct statt 2–7 ct
  daneben), Formanten bei ±12 Halbtönen innerhalb 0,2 Oktaven.
- **Sättigung:** 4-fach Oversampling + ADAA, Aliasing −91,5 dB (vorher −36,6 dB; Saturn 2: −70 dB), neuer 3-Band-Modus.
- **Smooth** auf soothe2-Niveau (alle 26 Messwerte bestanden), Gewichtungskurve, Delta-Hören, Speed.
- **Kompressor:** Kennlinie exakt (4,00:1 gemessen), Attack 10,5 ms bei eingestellten 10 ms, Auto Release, Auto Makeup.
  **De-Esser:** Relative-Erkennung (De-Esser aus dem Browser starten darauf), S-Reduktion wie Pro-DS (3,9 vs. 3,8 dB) ohne Transienten-Verlust.
- **Level Ride** mit 80 ms Vorausschau: Lautheitsschwankung 2,56 LU (trocken 3,80; Vocal Rider 2,92).
- **Reverb:** Decay = tatsächliche Nachhallzeit (T30-Fehler 0,8 %; VintageVerb 17 %), weniger Klingeln (Plate 8,3 dB statt 11,1).
- **Delay:** Echozeit auf 0,08 ms genau, sauberer Feedback-Begrenzer, Bandmaschinen-Gleiten bei Zeitänderungen.
- **Doubler:** Modi Classic / 2 / 4 Stimmen mit eigener Verzögerung, Verstimmung (0,00 ct Fehler), Pan und Pegel.
  **Choir:** sauberer Shifter, natürlichere Stimmen. **Pulse:** Amount und vier Kurvenformen. **Stutter** (vorher „Chop“).
- **Mix-Kurve:** neue Instanzen nutzen echtes Dry/Wet (über 50 % Mix wird das Original leiser); alte bleiben, umstellbar
  über „Alte Mix-Kurve – umstellen“ (ein Undo-Schritt). Neue Delay/Reverb aus dem Browser sind Inserts; SPACE und der
  Auto-Mix nutzen Sends (ohne Pegel-Einbruch beim Umschalten).
- Alle Messwerte gegen Referenz-Plugins: `docs/QUALITY_BASELINE.md`.

### Alte Projekte – was sich hörbar oder in der Latenz ändert
- **Mix-Kurve:** alte Projekte behalten die alte Kurve (Mix addiert zum Original); Presets ohne Kurven-Angabe bekommen die alte
  Kurve nur dort, wo ein additiver Mix über 50 % steht – sie klingen wie gespeichert.
- **EQ:** die festen Bänder (HPF/Low/Mid/High/LPF) werden zu den Bändern 1–5 mit der alten Kennlinie (klanggleich, Null-Test
  ≤ −120 dB; per Menü auf die neue umstellbar). **Automation der alten EQ-Parameter ist wirkungslos** (die Spuren laden, tun aber
  nichts mehr). Werks-Presets nutzen die neue Kennlinie (oberhalb ~8 kHz minimal anders).
- **Pitch:** neue Engine auch in alten Projekten – Latenz 120 → ~15 ms (der Host gleicht neu aus), sauberer und tonrichtig.
- **Smooth:** eine Smooth-Instanz in der Kette verzögert um 1024 Samples bei 48 kHz (2048 bei 96, 4096 bei 192 kHz) – **auch
  wenn sie aus ist**, solange sie in der Kette steht.
- **Level Ride:** +80 ms Latenz, **auch wenn er aus ist**, solange er in der Kette steht; stärkeres, wortgenaues Ausgleichen bei
  gleichen Einstellungen (Amount liefert jetzt den eingestellten Anteil).
- **Reverb:** Decay folgt dem eingestellten Wert über das ganze Spektrum; mit dunkler Dämpfung klingen die Mitten bis ~25 % länger
  als in 0.5. Weniger Klingeln, neue Stimmungen der Typen.
- **Kompressor (Peak):** Peak-Hold im Detektor – etwas gleichmäßigere Kompression (GR erreicht bei gehaltenen Tönen die
  Kennlinie, Attack entspricht dem eingestellten Wert statt ~16 ms, Release +10 ms Hold). RMS unverändert.
- **Delay:** Feedback-Begrenzer erst ab −2 dBFS (vorher weich überall) – heiße Feedback-Einstellungen klingen sauberer;
  Drive 2-fach überabgetastet.
- **Doubler (Classic):** nah am alten Klang, aber nicht identisch (Pegel ≤ +0,4 dB, minimal heller, Wobble auf neuem Pfad).
- **Choir:** breitere Stereoverteilung bei Width 30–80 %, sauberer Shifter, leicht ungleiche Stimmpegel (gleiche Energie).
- **Sättigung:** Aliasing-Filterung und DC-Blocker nur auf dem Verzerrungsanteil (kein Bassverlust unter 40 Hz mehr);
  Latenz pro Instanz 3 → 5 Samples bei 48 kHz.
- **Stutter / Pulse** hinter latenten Modulen (Pitch, Sättigung, Smooth) laufen jetzt im Raster des Songs; Stutter-Wiederholungen
  mit Raised-Cosine-Blende (weniger Klick). Parameter-Namen im Host „Chop …“ → „Stutter …“ (IDs unverändert).
- **Auto-Mix:** Ergebnisse können sich verschieben (Ausgangspegel-Trim, gematchter EQ, Reverb erreicht seine Nachhallzeit –
  z. B. wählt ein dunkler 44,1-kHz-Take den Basisplan statt „Mehr Raum“).
- **Anzeige:** Doubler Mod Rate zeigt Werte unter 10 Hz mit zwei Nachkommastellen („0,31 Hz“ statt „0 Hz“).
- Pulse: unverändert (bitgleich).

### Technik
- Referenz-Host `HeadlinerRefHost` + `tools/refhost/bench.py` (Messung gegen Pro-Q 4, Pro-C 2, Pro-DS, soothe2, Saturn 2,
  Graillon 3, Doubler2/4, Timeless 3, VintageVerb, Pro-R 2, Vocal Rider); Ergebnisse in `docs/QUALITY_BASELINE.md`.
- Globale Undo-Historie (`UndoHistory`), Mix-Kurve (`DSP/MixLaw`), EQ-Bänder (`DSP/EqDesign`, `Parameters/EqBands`),
  Vocal Check (`Source/Check/*`). Volle Testsuite > 30 500 Checks.

## [0.5.0] – Branch `feature/automix` (Entwicklungsstand, FL-Studio-Test ausstehend)

### Neu
- **Komplett neue Oberfläche „HEADLINER VOCAL“** nach dem Claude-Design-Paket (`docs/STYLEGUIDE.md`):
  Fenster 1384 × 1040 (skalierbar 70–140 %), Header mit Tabs CHAIN / AUTO MIX / VOCAL CHECK, Kette als Spalten
  mit Neon-Icon, zwei Live-Reglern und Fußleiste ‹ Duplizieren Entfernen ›, Modul-Panels mit Visualizer,
  Segment-Gruppen statt Dropdowns und Regler-Raster in der Modulfarbe, schmale IN/OUT-Meter, VOCAL CONTROL
  (Presence, Space, Width, Punch, Warmth, Vocal Level + Gain Reduction) in allen Tabs.
- Neue Regler (57-Strich-Kranz, vertiefte Kappe, Leuchtpunkt), Schalter und Segmente; Roboto Mono für Werte.
- 15 Neon-Modul-Icons (8 aus dem Design, 7 passend ergänzt).
- Alle 15 Module bleiben bedienbar; Design-Regler ohne DSP-Gegenstück sind nicht gebaut (`docs/DESIGN_MAPPING.md`).

### Geändert
- Gain-Regler aus der Meter-Spalte: Ausgangspegel = VOCAL LEVEL, Input Gain im OUTPUT-Panel.
- AUTO MIX: Feinschliff zeigt nur noch Density und den fairen Vergleich; „Vocal analysieren“.
- Hilfe und Tutorial beschreiben Tabs, Spalten und VOCAL CONTROL.

### Technik
- `Tests/DesignTests.cpp` (Farben, Icons, Regler, Segmente, Tabs, Spalten, Panels, Vocal Control, Texte,
  Alt-Projekte behalten ihre Werte). Wackeliger Hilfe-Test wartet jetzt auf das Öffnen statt 5 ms.

## [0.4.0] – Branch `feature/automix` (Entwicklungsstand, FL-Studio- und Hörtest ausstehend)

### Neu
- **Neuer Look nach dem HEADLINER-Stilguide** (`docs/STYLEGUIDE.md`): Palette #0B1020 / #111827 / #1F2937 / #334155, Neon-Spektrum Cyan → Blau → Violett → Pink, matte Regler mit leuchtendem Verlaufsring und weißem Zeiger, aktive Buttons in Indigo mit Glow-Rahmen, Primärbutton als Blau→Violett-Pille, Überschriften und Reglernamen in Großbuchstaben, Schrift Montserrat (ersetzt Inter).
- **Modulfarben** (Voxity-Stil): jedes Modul hat eine feste Neonfarbe für Icon, Power-Licht, Auswahlrahmen, Aktivitätsbalken und Panel-Kopf.
- **Auto-Mix-Optimierer:** Kandidatensuche über eine dokumentierte Zielfunktion (`docs/AUTOMIX_OBJECTIVE.md`); Varianten werden durch die echte Kette gerendert und bewertet, die zwei besten kombiniert.
- **Preset-Leiste nach dem Stilguide:** `‹ Name ›` mittig, daneben Icons für Speichern und ein Menü (Speichern als, Änderungen verwerfen, Löschen). Modul-Chips in der Auto-Mix-Liste in der jeweiligen Modulfarbe.
- **Planer:** Getragener Gesang (viele gehaltene Töne, > 2 Halbtöne Umfang) bekommt im Basisplan direkt Large Hall (2,3 s, 40 ms Pre-Delay) statt Plate; Takes mit Eigenraum behalten die Plate.
- Reverb 0.4 (16-Line-FDN, 8 Charaktere), Delay 0.4, Mehrfach-Instanzen mit Chain-Editor und Modul-Browser, Vocal-Check-Diagnosen, echtes A/B, EQ-Mausrad-Q (siehe `docs/HANDOFF.md`).

### Geändert
- **Zielfunktion, Term „artifacts“:** bestrafte bisher den Abstand Spitze–Lautheit absolut (bei dynamischen Takes 7–8 Punkte) und damit auch De-Essing (weniger Energie, gleiche Vokal-Spitzen). Jetzt zählt der *Zuwachs* gegenüber dem Original (> 1,5 dB), Headroom nur noch mild. Test: synthetischer heller Take 6,8 → 0,4.
- **Neue Variante „Höhen sanfter“** (High-Shelf 5 kHz, −1,5…−4 dB) für Takes, die heller sind als das Tonal-Fenster – der Planer hob Höhen bisher nur an.
- **Kalibrierung am blinden Hörtest vom 24.10.** (0.3 vs 0.4 stand 5 : 5): Zielfunktion bevorzugt jetzt dicht/upfront (Phrasen-Fenster 3…6 dB, Crest-Untergrenze 5,5 dB), Spitzen-Zuwachs nur noch mild. Kompressor-Attack 3 ms mit Level Ride, sonst 8 ms (vorher 12). Eine zwischenzeitliche Regel „kein Level Ride bei LRA < 3 LU“ wurde nach dem Hörtest zurückgenommen – die Ride-Version war verständlicher. Ergebnis: „Dynamik dichter“ statt „Dynamik leichter“ auf den Titeln, bei denen 0.3 gewann.

### Behoben
- Gelegentliche Knackser: Lesezugriff außerhalb der Delay-Linien (Reverb/Doubler).
- Zielfunktion zählte Float-Überschreitungen im Render (vor der Ausgangsstufe) als Clipping; jetzt nur noch als Lautheitsverlust bewertet.
- Zielfunktion bestrafte bei hohen Stimmen (tiefster Ton > 250 Hz) das leere Band „Körper“ unter dem Grundton (VocalSet male9: Strafe 143).
- Reverb-Schritt hieß „Hall-Hall“ / „Large Hall-Hall“.

### Technik
- **Benchmark-Runner** `VelvoxTests --bench <ordner> <ausgabe>`: `bench.csv`, Renders mit Bericht, blindes Hörtest-Kit (`listening/NN_A|B.wav`, Schlüssel in `listening_key.csv`). Diagnose: `VELVOX_OVERRIDE="rideOn=0,..."` ändert den fertigen Plan vor dem Ganzdatei-Render. Siehe `docs/BENCHMARK.md`.
- Neue Tests `optimizerTests` (Kandidaten gültig/begrenzt/deterministisch, Basis gewinnt ohne Vorteil, EQ-2-Variante, Wahl des besten Kandidaten).
- Test-Klickhelfer wartet länger auf die asynchrone Zustellung (vorher sporadischer Fehler „original off“).

## [0.3.0] – Branch `feature/automix` (Entwicklungsstand, FL-Studio- und Hörtest ausstehend)

### Neu – Automatischer Vocal-Mix
- **Analyse-Popup** „Let's mix your vocal.“ beim ersten Öffnen einer neuen Instanz (Audio analysieren / Später). Ältere Projekte und bereits analysierte Instanzen fragen nicht erneut; „Neu analysieren“ ist jederzeit verfügbar.
- **Begrenzte Erfassung** während der Wiedergabe: vorab reservierter Puffer (max. 24 s), Start bei Signal, Stille wird übersprungen, automatischer Stopp bei 14 s Gesang; Hinweise bei fehlendem Signal, zu leisem Pegel, Übersteuerung und gestoppter Wiedergabe; Analyse mit vorhandenem Material möglich.
- **Analyse:** BS.1770-Lautheit, Lautheitsumfang, Wortdynamik, Peaks/Clipping, Crest, Rauschabstand, Tonhöhe, Langzeitspektrum, S-Laute, Resonanzen (ohne Teiltöne), Raum, Stereo, optional Beat-Überlappung; jede Feststellung mit Konfidenz.
- **Planung und Prüfung:** regelbasierte Einstellung von EQ, Level Ride, Kompressor, Vocal Smash, De-Esser, Smooth, Sättigung, Doubler, Hall und Delay; Prüf-Renderings durch die echte Kette mit Korrekturen; Lautheit an das Original angeglichen, 1 dB Reserve.
- **Auto-Mix-Ansicht:** Vocal-Status, tatsächlich eingestellte Schritte mit Begründung (Klick öffnet das Modul), Vorher/Mix- und Original-Vergleich, Wiederherstellen, Feinschliff-Makros.
- **Modulkarten:** Auto-Abzeichen, Bypass-Zustand, Farbakzent nach Position.
- **Infokarten** an Reglern mit aktuellem Wert, Wirkung und Rolle im Auto-Mix (nach 0,45 s).

### Geändert
- Text-Assistent (Freitext, Chat-Ansicht) aus Oberfläche und Build entfernt; Ansichten heißen jetzt „Auto Mix“ und „Module“.
- Liquid-Glass-Gestaltung: Navy-Grund, Licht in Cyan/Blau/Violett hinter Glasflächen, Glas-Panels mit Glanzkante, Wertebögen im Verlauf.
- Hilfe: neue Seite „Automatic mix“, Texte an den neuen Ablauf angepasst.
- State-Version 3 (Knoten „AutoMix“). Parameter-IDs, Bereiche, Presets und Automation unverändert.

### Technik
- `SettingsReader`: eine Parameter→DSP-Abbildung für Plugin und Offline-Rendering.
- Test-Harness legt den Prozessor auf den Heap (der Prozessor ist ~220 kB groß).
- Neue Tests: `AutoMixTests` (Erfassung, Analyse, Regeln, Engine auf 8 Materialtypen, Workflow mono/stereo/44,1/96 kHz/Sidechain, Zustand, Editor-Ablauf), synthetische Test-Vocals.
- `VelvoxTests --automix <wav> <ordner>` für Tests mit echten Aufnahmen.

## [Unveröffentlicht] – Branch `ui-redesign`

### Neu – Smart Assistant (lokal, ohne KI-Dienst)
- **Assistant-Tab** in der Kopfzeile (neben Beginner/Pro): Klangwünsche frei formulieren („Weniger Tiefen“, „etwas mehr Reverb“, „präsenter, aber nicht schärfer“). Enter analysiert, Shift+Enter fügt eine neue Zeile ein.
- **Regelbasierte Textinterpretation** (Deutsch, gängige englische Begriffe): Synonyme, Komparative, Beschwerden („zu basslastig“, „klingt dumpf“), Intensität (etwas / moderat / deutlich / stark, „aber nicht zu viel“). Negationen werden als Grenze verstanden („nicht schärfer“, „nicht mehr Höhen“), nie als Gegenteil. Widersprüche und vage Wünsche („alles extremer“) führen zu Rückfragen statt zu Änderungen.
- **Änderungsvorschläge** mit Modul, Parameter, aktuellem und vorgeschlagenem Wert, Begründung und Kennzeichnung Messung/Regel. Jeder Vorschlag lässt sich einzeln ab- und anwählen.
- **Anwenden** über die normalen Parameter (Gesten für DAW-Automation, sichtbar in den Modulen, gespeichert mit Projekt und Presets). Validierung vorab, alles oder nichts. Wurden Werte seit der Analyse geändert, wird neu geplant statt alte Werte zu setzen.
- **Rückgängig** stellt den Zustand vor der letzten Assistant-Aktion wieder her; inzwischen vom Nutzer geänderte Werte bleiben erhalten.
- **Beat-Sidechain:** Verbindungsstatus und Pegel (RMS) werden lock-frei gemessen und angezeigt. Ein Frequenzvergleich Vocal gegen Beat (Maskierung) ist noch nicht implementiert und wird als „nicht verfügbar“ gekennzeichnet.
- Vocal-Check-Messungen fließen ein, wenn genug Stimme analysiert wurde. Keine Netzwerkzugriffe, keine Sprachmodelle.
- **Sofort anwenden** (Schalter, pro Nutzer gespeichert, standardmäßig an): erkannte Wünsche werden direkt nach der Analyse umgesetzt; ein Klick auf Rückgängig nimmt alles zurück.
- **Stilprofile** „im Stil von Ski Aggu / Tream“: typische, hörbare Merkmale als allgemeine Regeln (kein Nachbau von Aufnahmen). Eigene Wünsche haben Vorrang, widersprüchliche Merkmale mehrerer Stile bleiben unverändert, unbekannte Namen werden gemeldet.
- Neue Klangwörter: klar / klare Stimme, crisp, spritzig, frisch.

### Geändert – Lesbarkeit und Layout (v0.4 „Anthracite“)
- **Größere Designfläche:** 1440 × 900 statt 1280 × 800. Das Fenster öffnet in voller Größe, wenn der Bildschirm (inklusive Windows-Skalierung) Platz hat, sonst in der größten passenden Größe. Es ist frei skalierbar von 70 % bis 160 %.
- **Modulnavigation:** große Modulkarten (80 px hoch, mindestens 168 px breit) mit vollem Namen, Symbol, Ein/Aus-Schalter, Status, Position und Aktivitätsbalken. Die Reihe scrollt horizontal (Mausrad, Pfeile, Scrollbalken), statt die Karten zu verkleinern. Beim Ziehen scrollt sie an den Rändern automatisch mit, das ausgewählte Modul wird automatisch sichtbar gemacht. Input und Output sind als feste Ports abgesetzt, parallele Sends haben eine eigene Spur mit markierter Ablagezone.
- **Typografie:** durchgehend größere Schrift, Beschriftungen in normaler Schreibweise statt Versalien, hellere Sekundärtexte; ausgeschaltete Module sind weniger stark abgeblendet.
- **3D-Metallknobs:** gebürstetes dunkles Metall mit Fase, Rändelung, Lichtreflexen und cyanfarbenem Zeiger; Haupt-, Neben- und Detailregler sind unterschiedlich groß (bis ~120 px im Beginner-Modus). Die Metalloberfläche wird je Größe einmal gerendert und zwischengespeichert.
- **Seitenleisten** breiter (150 px): größere LUFS-Anzeige, feinere und besser lesbare Pegelskala, größere Gain-Regler, Mono und Clip Guard als volle Schaltflächen.
- **Pro-Panels:** größere Kopfzeile mit „Off“-Kennzeichnung, lesbare Gruppenüberschriften, mehr Abstand; Visualisierungen und Reglerbereiche je Modul neu gewichtet.
- Dunkler Anthrazit-Hintergrund und dezente Cyan-Akzente; die Hintergrund-Leuchteffekte sind deutlich zurückgenommen.
- DSP, Parameter-IDs, Wertebereiche, Presets und Automation sind unverändert. Die Fenstergröße wird unter neuem Schlüssel gespeichert, damit ältere Projekte in der neuen Standardgröße öffnen.

### Neu – Bedienung, Hilfe und Organisation
- **Offline-Hilfe:** HELP-Button oben rechts; allgemeine Kapitel (Signalkette, Module organisieren, Bedienung, Beginner/Pro, Presets, Vergleich, Meter, typische Vocal-Probleme) und eine Seite pro Modul mit sieben festen Abschnitten. Der **?**-Button in jedem Modul öffnet direkt dessen Seite.
- **Onboarding:** Beim ersten Start eine Begrüßung (Tutorial starten / Direkt zum Plugin / Später ansehen) und eine Tour in 9 Schritten mit Hervorhebung. Klicks außerhalb der Karte gehen an das Plugin. Der Abschluss wird in den Benutzereinstellungen gespeichert, die Tour lässt sich aus der Hilfe neu starten.
- **Module organisieren:** Rechtsklick auf eine Kachel (öffnen, ein/aus, ausblenden, an Anfang/Ende, Hilfe), dazu der **Modules**-Button mit Übersicht aller Module inklusive ausgeblendeter Module und deren Status. Ausblenden ändert nur die Ansicht. Die Standardreihenfolge lässt sich wiederherstellen, das eigene Layout speichern und laden. Griff-Markierung beim Hover, Drag & Drop berücksichtigt ausgeblendete Module.
- **Presets:** Kategorien Clean, Punchy, Aggressive, Wide, Creative, Utility, Classic. Factory- und User-Presets sind im Menü getrennt, das Preset-Feld zeigt „(edited)“ nach Änderungen, im Menü gibt es „Änderungen verwerfen“ und „Speichern“.
- **Bypass** ist unübersehbar: bernsteinfarbener Rahmen, Label „Bypassed“ und ein amberfarbener Schalter.
- **Regler:** Shift/Strg-Feinjustierung jederzeit ohne Sprung (8× feiner), auch für Parameter mit Rasterung.
- **Beginner:** Ein Klick auf ein Modul zeigt eine Kurzbeschreibung mit „Edit in Pro“ und „?“.

### Behoben
- Wertefelder mancher Regler (z. B. EQ Mix) erschienen im JUCE-Standardstil mit Rahmen und falscher Schrift.

### Geändert
- **Neues Design „Deep Dark“:** komplett überarbeitete Oberfläche, monochrom (Schwarz, Grau, Weiß) mit Glas-Flächen und weichem Licht statt Neonfarben. Rot erscheint nur noch bei echten Problemen (Clipping, Phasenauslöschung).
  - Regler mit beleuchteter Kappe und leuchtendem Wertebogen; die Beginner-Makros leuchten stärker, je weiter sie aufgedreht sind
  - schlanke Kopfzeile mit Segment-Schaltern (Preset, A/B, Beginner/Pro), Signalkette als feine Kacheln, Meter als schmale Seitenleisten
- **Live-Spektrum:** Eingang (Kontur) und Ausgang (beleuchtet) als großes Hero-Element im Beginner-Modus und als Streifen über den Pro-Panels; im EQ liegt die Filterkurve direkt über dem Spektrum.
- **Schrift Inter** (OFL) ist eingebettet und sieht deshalb auf jedem System gleich aus.
- **Aurora-Akzent:** Teal → Blau für Wertebögen, Glows, Spektrum, Meter und aktive Elemente. Dazu kommt ein langsam treibender Nebel über der Kette, der mit dem Pegel des Vocals heller wird.
- **3D-Regler:** eingelassene Rille mit Aurora-Bogen, erhabene Kappe mit Schlagschatten, Fase, Glanzlicht und einer Riffelung, die sich mitdreht; als Zeiger dient eine leuchtende LED-Linie.
- **Animationen (60 fps):** Regler gleiten zu neuen Werten (Preset, Automation, A/B), Hover-Licht blendet ein, die Auswahl-Markierung gleitet durch die Kette, Panels blenden beim Wechsel weich ein.
- **Info-Popup:** Die Beschreibungstexte unter den Makros sind weg. Ruht die Maus etwa 1 s auf einem Regler, erscheint eine Karte mit Titel und Erklärung.
- **Eigene Ansicht pro Modul** (ersetzt den Spektrum-Streifen):
  - EQ: Spektrum mit Kurve und ziehbaren Band-Punkten
  - Kompressor: Pegel- und GR-Verlauf sowie Kennlinie mit Live-Punkt
  - De-Esser: S-Band über dem Spektrum und Reduktionsverlauf
  - Sättigung: Kennlinie und Obertöne mit THD
  - Doubler: Stereo-Bühne mit treibenden Stimmen
  - Pitch: Klaviatur mit Transpositionsbogen
  - Chop: Step-Sequencer mit Playhead
  - Delay: Echos auf der Zeitachse, live geduckt
  - Reverb: Nachhall-Hüllkurve mit Pre-Delay und Early Reflections
  - Output: Goniometer und LUFS-Verlauf

### Technik
- `SampleTap`: lock-freier Abgriff für das Spektrum am Eingang (nach Input-Gain) und am Ausgang; `StereoTap` für das Goniometer. Beide lesen das Signal nur; alle Prüfungen inklusive der bitgenauen Vergleiche bestehen unverändert (jetzt 1855).
- Die Meter werden einmal pro Frame zentral abgefragt (`LiveData`), alle Ansichten lesen daraus.
- Fenstergröße, Parameter, State und Presets sind unverändert.

## [0.2.0] – 2026-10-14 (Release Candidate, FL-Studio-Test ausstehend)

### Neu
- **Beginner- und Pro-Modus:**
  - sechs Makros (Presence, Warmth, Aggression, Width, Space, Density) als Offset-Schicht auf die echten DSP-Parameter
  - Pro-Detailansicht pro Modul; Markierung am Regler zeigt, wohin Makros einen Wert schieben
- **Flexibles Routing:**
  - Drag & Drop der Module
  - Delay und Reverb als paralleler Send oder Insert; Delay → Reverb
  - Umbau ohne Klick
- **Vocal Doubler:** zwei modulierte Stimmen, echte Tonhöhenvariation, Regler „Mono safe“ (bei 100 % bleibt die Monosumme bitgenau erhalten)
- **Reverb-/Delay-Ducking:** je Amount, Threshold, Attack und Release; Quelle intern (Vocal) oder externer Sidechain-Eingang
- **Stereo:** Width 0–200 % (nur das Seitensignal), Mono below, Korrelationsanzeige, Mono-Check
- **Level-Matcher (BS.1770):** Bypass match (fairer A/B) und Auto gain; begrenzt, langsam, mit Gate
- **Pitch:** kreativer Pitch-Shifter (±24 Halbtöne, Formant-Erhalt oder -Verschiebung, Mix), Latenzmeldung an den Host
- **Vocal Chop:** host-synchrone 16-Step-Patterns mit Stutter
- **Pro-Erweiterungen:**
  - EQ: High Cut sowie Low-/High-Band als Shelf oder Bell
  - Saturation: Charaktere Warm/Tape/Tube/Hard
  - De-Esser: Breitband-Modus
  - Kompressor: RMS-Detektor und Sidechain-Hochpass
- **Neues Design:** Neon-Club-Look, Signalkette als Kacheln, LUFS-Meter, Gain-Reduction-Anzeigen, Live-Hinweise
- **Presets:** 11 neue Presets in Kategorien; die v0.1-Presets bleiben unter „Classic (v0.1)“
- **Smart-Mixing-Grundlage ohne KI:** `ProposalSession` (Vergleichen, Übernehmen, Verwerfen)

### Geändert
- Presets und A/B lassen Input-Gain und Level-Match-Einstellungen unverändert.
- State-Version 2: Beim Laden älterer Projekte werden fehlende Parameter auf den Standardwert gesetzt; v0.1-Projekte klingen unverändert.
- Preset-Dateiformat 2 mit Routing; Format 1 lädt weiterhin.
- Fenster 1280 × 800, skalierbar von 75 bis 160 %.

### Abhängigkeiten
- Neu: Signalsmith Stretch 1.4.0 und Signalsmith Linear 0.6.4 (MIT, header-only, in `third_party/`).

### Qualität
- 1850 automatisierte Prüfungen, darunter UI-Drag-and-drop mit Maus-Ereignissen und Worst-Case-CPU (3,7 % @ 48 kHz).
- pluginval Strenge 10: SUCCESS.

## [0.1.0] – 2026-10-14
- Erste Version: EQ, Kompressor, De-Esser, Saturation (2× OS), Delay, Reverb (FDN)
- 6 Factory-Presets, User-Presets, A/B, State-Speicherung, Clip Guard, latenzkompensierter Bypass
- 243 Offline-Prüfungen, pluginval Strenge 10
- In FL Studio vom Nutzer erfolgreich getestet
