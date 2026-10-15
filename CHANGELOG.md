# Changelog

Versionierung nach [SemVer](https://semver.org/lang/de/): MAJOR.MINOR.PATCH. Vor 1.0 können Minor-Versionen neue Funktionen bringen; Projekt-State und Presets bleiben abwärtskompatibel.

## [Unveröffentlicht] – Branch `ui-redesign`

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
