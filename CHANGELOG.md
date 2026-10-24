# Changelog

Versionierung nach [SemVer](https://semver.org/lang/de/): MAJOR.MINOR.PATCH. Vor 1.0 können Minor-Versionen neue Funktionen bringen; Projekt-State und Presets bleiben abwärtskompatibel.

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
