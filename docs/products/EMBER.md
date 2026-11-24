# EMBER De-Esser (Lind Audio, VOCALYPSE-Familie)

## STAND 2026-11-24 – Gleichmäßigkeit wie Pro-DS (Branch r3/ember-evenness, nicht gemergt) – HIER WEITERMACHEN
- Neuer Detektor-Bezug: jedes S wird gegen die Stimme jetzt (60 ms) bzw. höchstens 10 dB unter dem Vokal davor
  gemessen, dazu ein Schutz für leise Reibelaute (S-Band gegen den Stimmpegel der letzten 10 s).
  Details, Kandidaten, Messungen: `docs/products/ember-r2/GLEICHMAESSIGKEIT.md`.
- Streuung über die S (iqr): **J1–J3 1,69 → 1,23 (Pro-DS 1,27), alle 15 Takes 1,99 → 1,34**; leise Reibelaute
  −0,30 → −0,00 dB (wie Pro-DS), unberührte S 12 → 6 %, weniger Neu-Einsätze. Kosten: etwas tiefer (Kern +0,1 / +0,2
  dB), S-Schwerpunkt auf J1–J3 dunkler (−146 → −190 Hz, Pro-DS −145).
- Presets nachgemessen: 4 Männer-Presets zurück auf ihre Tiefe (Range −1 dB), Default unverändert.
- Tests 2009 bestanden, 0 fehlgeschlagen (mit `EMBER_VOCALS`; QA 381), pluginval 10 SUCCESS, CPU ≈ 0,38 % (vorher 0,37 %).
- Hör-Set: `C:\Users\Shadow\dev\velvox-bench\2026-11-24\evenness`. Offen: Hörurteil, danach mergen.

## STAND 2026-11-24 – S-Tracking gemergt, Presets nachgemessen (Branch feature/ember-fixes)
- r2/ember-research gemergt: **S-Tracking** (das Split-Band folgt jedem S, `Settings::trackS`, Standard an) und exakte
  Split-Tiefe an der Bandmitte. Details: `docs/products/ember-r2/DSP_FORSCHUNG.md`.
- CPU der Engine jetzt ≈ **0,37 % eines Kerns** (vorher 0,28 %).
- Alle 29 Werks-Presets mit der gleichen Methode neu gemessen; **7 nachgestellt** (Harsh S, Extreme, Sharp T, Rescue,
  Bright Female, Powerful Female, Airy), Default 6 kHz / −34 dB unverändert. Gründe und alt → neu:
  `docs/products/ember-r2/PRESETS.md` Abschnitt 8.
- Handbücher DE/EN und Shop-Text: S-Tracking beim Split-Modus erwähnt.
- Tests: 2005 bestanden, 0 fehlgeschlagen (mit `EMBER_VOCALS`, 15 Takes; QA 381), pluginval 10 SUCCESS.
- Hör-Set: `C:\Users\Shadow\dev\velvox-bench\2026-11-24\presets_v2` (alt: `…\presets`).
- Offen: Hörurteil S-Tracking und Presets; HOCH-Knopf auf hohen Stimmen nur noch gleich stark wie MITTEL (dunkler);
  Transienten-Schutz (Hörtest, DSP_FORSCHUNG.md).

## STAND 2026-11-23 (abends) – alle Audit-Fixes + Pro-DS-Niveau (Branch feature/ember-fixes)
Jakob: „mach alle fixes, das rechtliche notieren aber nicht machen + verbessere den algorithmus um auf das selbe level
wie der fabfilter ds zu kommen“. Audit mit 4 Prüfern, danach 4 Agenten parallel (Branches fix/ember-dsp, -auto, -ux, -gui),
zusammengeführt in `feature/ember-fixes` (Basis = Klang-Stand vom 23.11., Commit aeb1e54; feature/ember-sound unverändert).
Details je Bereich: `docs/products/ember-fixes/DSP.md`, `AUTO.md`, `UX.md`, `GUI.md`. Rechtliches (nur notiert):
`docs/products/EMBER_RECHTLICHES.md`.
- **Klang:**
  - Die Glocke ändert die Absenkung pro Sample (Regalia-Mitra), statt alle 16 Samples in Stufen zu springen.
  - Gleiche Form bei 44,1–192 kHz (matched).
  - Weicheres Gate im Breitband.
  - „S-Bereich“ beim Abhören = das bearbeitete Band.
  - Überblendungen beim Umschalten.
  - Das Smart-Band folgt der Frequenz.
  - Threshold-Bereich −50…−10 dB.
  - Neu: Breite (1–4 Okt.), Stereo-Link, L/R·M/S·Mid·Side, Erkennung „Allround“ (Mix/Bus), Lookahead bis 10 ms (Standard 5).
  - **Latenz jetzt fest 10 ms.**
- **AUTO:**
  - Stereo-Aufnahme.
  - Pausen auf festem 5-ms-Raster, damit das Ergebnis nicht von der Puffergröße abhängt.
  - Vokale auch in Präsenz und Luft geprüft.
  - Atmer und Hi-Hats werden aussortiert.
  - Schneller: 3–14 statt bis zu 34 Durchläufe.
  - In der Analyse abbrechbar; Projekt- oder Preset-Laden verwirft eine laufende Analyse; race-frei.
  - Sucht nur innerhalb −50…−10 dB.
- **Bedienung:**
  - Stimmlage = 1 Undo-Schritt und überschreibt keinen eigenen Threshold.
  - „rot“ → pink in Texten und Handbüchern.
  - Undo stellt den Preset-Namen wieder her.
  - AUTO lässt eine geparkte B-Einstellung stehen; A/B wird im Projekt gespeichert.
  - Presets umbenennen und löschen, Menü für die Fenstergröße.
  - Bypass-Hinweis bei AUTO, pinke Lizenzkarte.
  - setStateInformation ist thread-sicher.
- **GUI-CPU:**
  - UI-Thread etwa halbiert, Prozess ~20 % weniger (Voll, 60 fps), Bilder pixelgleich.
  - Menü Animation Voll / Reduziert / Aus (Aus ≈ 7 % eines Kerns).
  - Pausiert bei minimiertem Host.
  - Rig ohne statisches juce::Image.
- **Gemessen** (Bench `C:\Users\Shadow\dev\velvox-bench\2026-11-23\bench_merged`, `run.py`):

  | J1–J3 | S-Kern | Luft über S | S-Schwerpunkt | Reinheit Delta | Vokale |
  |---|---|---|---|---|---|
  | Pro-DS Standard | −3,71 dB | −3,62 | −145 Hz | 98,1 % | 0,00 |
  | EMBER vorher AUTO | −4,09 | −2,88 | −32 Hz | 98,0 % | 0,00 |
  | EMBER neu Standard | −4,07 | −3,39 | −48 Hz | 99,1 % | 0,00 |
  | EMBER neu AUTO | −4,05 | −3,44 | −120 Hz | 99,2 % | 0,00 |

  Alle 15 Takes, AUTO: S-Kern −3,48 dB (vorher −3,29), Reinheit 99,5 % (vorher 98,8 %), Vokale 0,00 dB.
- **Tests:** 634 grün (mit EMBER_VOCALS, 15/15 Takes), pluginval 10 SUCCESS (mit GUI). Nicht installiert, nicht gepusht.
- **Hörbeispiele:** `C:\Users\Shadow\dev\velvox-bench\2026-11-23\hoertest_prods_level`
  - 01 Original
  - 02/03 neu AUTO (+ DELTA)
  - 04/05 neu Standard (+ DELTA)
  - 06/07 Pro-DS (nur J1–J3)
  - 08/09 vorher AUTO
- **Bekannt/offen:**
  - Auf dem synthetischen Test-Take hält die Präsenz-Grenze AUTO bei ~2 statt 4,5 dB; die echten Takes treffen alle ihr Ziel.
  - Das Figur-Zeichnen (~3,5 ms pro Bild) ist jetzt der größte GUI-Kostenpunkt; nächster Schritt wäre die GPU.
  - Verdeckte (nicht minimierte) Fenster werden nicht erkannt.
  - Die Klangfarbe des S ist noch etwas heller als bei Pro-DS (−120 statt −145 Hz).
- **Nächste Schritte:** Jakobs Hörurteil → DAW-Test (FL/Ableton/Reaper) → rechtliche Punkte → Installer mit Lizenz AN.

## STAND 2026-11-23 – Klang verbessert (Branch feature/ember-sound, nicht committet)
Jakob: „verbessere den algorithmus“. Grundlage: Klangtest `docs/products/EMBER_KLANG_TEST.md` (19.11.), gemessen mit
`EmberTests --process` über 15 echte Takes (Skripte `run.py`/`grp.py` im Session-Scratchpad, Takes aus dem Klangtest).
- **Split = breite dynamische Glocke** um das S (Mitte 1,25 × Frequenz, Güte 0,4) statt offenem High-Shelf.
  AUTO, 15 Takes: S-Kern −3,3 dB (gleich), Luft über dem S −2,5 statt −4,5 dB, S-Farbe −77 statt −605 Hz
  (Pro-DS −145 Hz). J1–J3: S-Kern −4,1 dB (Pro-DS −3,7). Vokale weiter 0,00 dB.
- **Detektor gleicht die Frequenz aus** (+5 dB pro Oktave, Bezug 6,5 kHz): Threshold bedeutet überall dasselbe.
- **Stimmlage-Knöpfe** setzen Frequenz + Threshold: TIEF 5 kHz/−35, MITTEL 6,2 kHz/−32 (= Standard), HOCH 7,4 kHz/−33;
  nach AUTO bleibt dessen Threshold. Jeder Knopf ist für seine Stimmgruppe der stärkste (vorher war HOCH immer der schwächste).
  Tiefe Stimmen ohne AUTO: S-Kern −2,0 dB mit Standard, −3,3 mit TIEF (vorher −0,5 / −1,4).
- **AUTO:** Frequenz aus der Mitte der S-Energie (log. Schwerpunkt 3–12 kHz) statt dem Maximum → pro Stimme stabil
  (F01: 5,5–6,6 statt 5,0–8,7 kHz); Ecke 4,5–9,5 kHz; Erfolg im S-Band gemessen; Threshold nie unter −45 dB (vorher 3 von
  15 Takes bei −60); Threshold nach dem Verkleinern der Range neu gesucht → Ziel wird getroffen (vorher 0,6 dB zu wenig).
- **Amount 100 % = 1,5-fach** (vorher doppelt). Presets an den neuen Detektor angepasst, „Kräftig“ −38 dB / Range 7.
- Spektrum zeigt im Split-Modus die Glocke. Texte, Handbuch DE/EN, Shop-Text angepasst.
- 296 Tests grün (356 mit EMBER_VOCALS, 15/15 Takes), pluginval 10 SUCCESS. Nicht installiert.
- Hörbeispiele: Drive `Plugins/01 EMBER De-Esser/Hoertest/2026-11-23_Glocke_neu` (vorher / neu / Pro-DS, je mit DELTA).
- Offen: Jakobs Hörurteil; danach committen, in feature/ember-live-mascot bzw. feature/ember-deesser übernehmen.

## STAND 2026-11-15 – HIER WEITERMACHEN
**Später am 15.11. – Politur** (Jakob: „poliere das gesamte plugin, alle meldungen, bugs, sicherheitslücken, popups“;
VOCALYPSE gibt es nicht mehr, nur EMBER). Zwei unabhängige Code-Reviews (Audio/Logik, UI/Texte/Lizenz), Funde geprüft:
- Ember tanzt nicht mehr bei Pause (Host-Transport; erst wenn der Host einmal „playing“ gemeldet hat). Stimmlage:
  es leuchtet der Bereich, in dem die Frequenz liegt (auch nach AUTO).
- Audio: NaN/inf aus Preset/Projekt schaltet nicht mehr dauerhaft stumm; AUTO-Puffer (30 s) erst bei erster
  Nutzung; NaN-Blöcke nicht in die Analyse; Plugin schließen während der Analyse wartet nicht mehr; AUTO setzt
  Attack/Lookahead/Mix/Output so, wie es gerechnet hat; Moduswechsel setzt den Shelf zurück; Shelf kehrt nach dem S
  exakt zu 0 dB zurück; Preset-Namen mit Punkt; A/B beim Laden eines Zustands neu.
- Lizenz: Schlüssel wird gesäubert (Leerzeichen/Umbrüche aus Mails); Wochenprüfung sperrt nur bei klarer Antwort
  (2xx ungültig oder 404), nicht bei 429/5xx; fremder Schlüssel gibt seinen Aktivierungsplatz zurück + eigene
  Meldung; Freigeben fragt nach und klappt auch, wenn die Aktivierung im Dashboard schon gelöscht ist; Dialoge
  prüfen, ob der Manager noch lebt; Menü-Callback mit SafePointer; Sperrkarte liest die Datei neu (anderer
  Prozess hat aktiviert), lässt die obere Leiste frei, Knopf „Schlüssel eingeben“, Statuszeile passend.
- Texte DE/EN überarbeitet (Gedankenstriche, „E-Mail“, Range 4–8 dB, AUTO bis 12 s Gesang), Handbuch:
  AUTO 2,5–4,5 dB und neuer Abschnitt „Aktivieren“ (2 Computer – Aktivierungslimit im LS-Produkt prüfen!).
- 220 Checks (256 mit EMBER_VOCALS), pluginval 10 SUCCESS, installiert (Lizenz AN).
- Vor dem Release: Live-Produkt-ID + Live-Checkout in `EmberLicence.h`, mit `-DEMBER_LICENSING=ON` bauen.
- Unten in diesem Dokument sind ältere Abschnitte (AUTO 1,5–3,5 dB, Launen, 14 Tage Test) historisch.

Jakob: „ember läuft nur über schlüssel, den schlüssel bekommt man nur per mail“ und „wir brauchen sleep, idle,
zuhören und tanzen“; beim Tanzen waren an der Taille rote Blöcke zu sehen, wenn die Arme abstanden.
- **Lizenz ohne Testzeit:** `licensing::Config` hat jetzt `trialDays` (VOCALYPSE 14, EMBER 0) und `freeKey`.
  EMBER ist ohne Schlüssel ab dem ersten Start gesperrt (Audio unbearbeitet durch), die Sperrkarte sagt
  „EMBER braucht einen Lizenzschlüssel“ und bietet „Gratis-Schlüssel holen“ (Checkout-Link) und „Lizenzschlüssel
  eingeben“. Tests: EMBER „first start without key -> locked“, LicenseTests „0 trial days“.
- **Maskottchen: nur noch 4 Stimmungen** `sleeping`, `idle`, `dancing`, `listening` (Feuer, Schnappen, Überhitzt,
  Stolz entfernt). Jedes abgesenkte S ist ein **Akzent im Tanz**: kurzes Nicken/Einknicken zur Hand hin, Funken und
  ein „S“ fliegen von der Hand weg (Stärke nach Reduktion; `MascotBrain::getAccent*`, ein Akzent pro S, frühestens
  alle 0,15 s). Über 9 dB: nur noch die Sprechblase „Klingt gelispelt!“.
- **Arme als eigene Ebenen** in `tools/mascot/puppet_ember.py` (unter `ARM_CUT` trennt die Kontur Arm und Bauch;
  der Bauch behält die gemeinsame Linie) -> keine roten Blöcke mehr. Neuer Tanz (120 BPM, Arme abwechselnd raus).
  `mascot.json` hat statt `mouth` jetzt `hands` (Handposition je Tanz-Bild). 156 Bilder, 4,9 MB.
- Handbuch DE/EN, Shop-Text, Erklärkarte angepasst. Vorschau: Drive `Maskottchen/Animation/Puppe_Vorschau/
  Tanz_mit_S-Funken_2026-11-15.mp4` (die alten 01–08-Vorschauen sind überholt).
- 203 EMBER-Checks grün (Lizenz AN, `build-licence`). Nicht installiert (mit Lizenz AN und ohne Testzeit wäre
  EMBER bei Jakob sofort gesperrt, bis er einen Test-Schlüssel aktiviert).
- Offen: Website (`site_text.py` „moods“ zeigt noch fire/overheated-Bilder), Jakobs Urteil zum Tanz.

## STAND 2026-11-13
Jakob verglich EMBER mit FabFilter Pro-DS (gleiche Vocal, gleiche Fenstergröße): Schrift unlesbar, im Delta hört man
die Stimme. Erledigt (feature/ember-deesser, installiert mit Lizenz AN aus `build-licence`, nicht gepusht):
- **Smart-Gate neu** (`EmberEngine.h`): Index = 5-ms-RMS über 4 kHz gegen Stimmkörper 150 Hz–3 kHz, Gewicht 0 bei
  −1 dB, 1 bei +5 dB; das Gewicht schaltet die fertig geglättete Reduktion mit eigener 0,5/1-ms-Hüllkurve, das Release
  reicht nie in den Vokal nach dem S. AUTO-Ziel 2,5–4,5 dB (war 1,5–3,5), Range 4–8 dB.
- **Bench** `tools/bench/deess_bench.py <takes> <out>` rendert EMBER (AUTO) und Pro-DS (Standard, über den RefHost) und
  misst mit `deess_metrics.py`. 9 Takes (`velvox-bench/2026-11-13/prods/takes`, inkl. Jakobs 3 Vocals), Mittel:
  S −3,03 dB (Pro-DS −2,69), Vokale 0,00 dB, Delta-Reinheit 98,5 % (Pro-DS 98,7 %, vorher 87,6 %).
- **Schrift 35 % größer** (`ui::setTextScale`, nur EMBER; Wortmarke bleibt gleich groß).
- Hörvergleich für Jakob: Drive `Plugins/01 EMBER De-Esser/Hoertest/2026-11-13_vs_ProDS`.
- 248 Checks grün (mit `EMBER_VOCALS`), pluginval 10 SUCCESS (`--skip-gui-tests`).
- **Neues Maskottchen** (Jakob: KI-Videos „machen es nicht richtig“, Fiverr 500 €): Ember aus `Embernew.png` als
  2D-Puppe, alle 8 Stimmungen per `tools/mascot/puppet_ember.py` (siehe `Products/Ember/Mascot/README.md`), Feuer
  live aus dem Mund. 228 Bilder, 7,2 MB. Vorschau-Videos: Drive `Maskottchen/Animation/Puppe_Vorschau`. Installiert,
  pluginval 10 SUCCESS (mit GUI). Offen: Jakobs Urteil zu den 7 neuen Stimmungen; Website-Hörbeispiele
  (Drive `Vocal vergleiche`, live oder Vorschau?).

## STAND 2026-11-12
Erledigt (Branch feature/ember-deesser, nicht gepusht, nicht installiert):
- Maskottchen-Speicher: Bilder werden erst beim Anzeigen dekodiert, geteilter Cache (f7e7dd1, 481 → 185 MB).
- Lizenz eingebaut, Standard AUS (`-DEMBER_LICENSING=ON`), Lemon Squeezy TESTMODUS: Store 522230, Produkt 1522200,
  Checkout-Link als Kaufen-Link (`EmberLicence.h`). Echter API-Test grün (`EMBER_LICENCE_TEST_KEY`, Schlüssel nie im Repo).
- „VOCALYPSE“ aus Kopf und Über-Fenster entfernt; `--snapshots` schreibt auch @2x-PNGs für die Website.
Offen:
- Jakob poliert und testet das Plugin selbst; danach: Lizenz einschalten, Installer bauen, bei Lemon Squeezy hochladen.
- Entscheidung Jakob: Ohne Schlüssel gar nicht laufen oder unbegrenzt laufen (EMBER ist gratis; 14-Tage-Test passt nicht)?
- Lemon Squeezy: Preis 0 € und Newsletter-Zustimmung erledigt (Jakob); Store-Prüfung läuft. Nach Freigabe: Produkt in den
  Live-Modus kopieren → neue Produkt-ID in `EmberLicence.h`, neuer Checkout-Link auf die Website.
- Website lind-audio.com: siehe Repo `%USERPROFILE%\dev\lind-audio-web` (README im build.py-Kopf).

## STAND 1.0.0 (2026-11-10) – verkaufsfertig ohne Lizenz
Jakob: „UX und S-Algorithmus verbessern, Erklärtext beim Hovern, Englisch, Ember nur in SIMPLE, komplett verkaufsfertig
(außer Lizenzierung)“.
- **Smart-Erkennung** (neuer Standard, `emDetection` 0 = Smart / 1 = Relativ / 2 = Absolut): Der Detektor vergleicht das
  S-Band mit dem Stimmkörper (250 Hz–3 kHz, Hüllkurven 30 ms). Die Reduktion wird mit
  `smoothstep(index, −12 dB, −3 dB)` gewichtet. Gemessen bei kräftiger Handeinstellung (Thr −50 dB, Range 10 dB) an
  4 echten Takes: S-Laute gleich (−4,9 bis −7,3 dB), Luft der Vokale (8–16 kHz) −0,5 bis −4,3 statt −3,9 bis −8,7 dB,
  Präsenz der Vokale (2–5 kHz) −0,04 bis −0,45 statt −0,5 bis −1,1 dB. Mit AUTO-Einstellungen ändert sich kaum etwas
  (AUTO ist schon vorsichtig).
- **Oberfläche (900 × 600):** Erklärkarte bei jedem Regler, Button und Umschalter (`InfoPopupHost` im Panel,
  `Segments::setInfo` neu im gemeinsamen Kit). Sprache: Wie das System / Deutsch / English (Menü, gespeichert in
  `%APPDATA%\Lind Audio\EMBER.settings`, das Panel wird neu aufgebaut). Ember nur in SIMPLE. In ADVANCED: volles
  Spektrum und S-PEGEL-Anzeige mit ziehbarem Threshold (grau = Smart hört kein S). Preset-Leiste ‹ Name › mit
  9 Werks-Presets, eigenen Presets (`Dokumente\Lind Audio\EMBER\Presets\*.emberpreset`), * = geändert,
  SPEICHERN, A/B mit A → B. Menü ≡: Sprache, Handbuch, Ember-Tipp, Über EMBER. Beim ersten Start sagt Ember
  „Klick mich für AUTO!“. Die Fenstergröße wird gemerkt. GR-Anzeige 0–12 dB mit Zahlenwert.
- **Verkauf:** Version 1.0.0, App-Icon (Embers Kopf aus Jakobs Front-Bild), Installer
  `scripts\make-installer.ps1` (Inno Setup 6, per-user per winget installiert) →
  `build\installer\EMBER_De-Esser_1.0.0_Windows_Setup.exe`, DE/EN, Lizenzvertrag (ENTWURF), VST3 + Standalone +
  Handbücher, Deinstallation getestet (die Presets des Nutzers bleiben). Handbuch DE/EN, EULA und Shop-Text in
  `Products/Ember/Installer` und `docs/products/EMBER_Shop.md`. Release-Kopie: Drive
  `Plugins/01 EMBER De-Esser/Release/1.0.0`.
- **Tests:** 193 Checks (261 mit `EMBER_VOCALS`), CPU 0,26 % eines Kerns, pluginval 10 SUCCESS auf der installierten
  Version, die Standalone-App startet.
- **Offen vor dem Verkauf:** Lizenz/Kopierschutz, Code-Signing-Zertifikat (sonst warnt SmartScreen), EULA anwaltlich
  prüfen lassen (Adresse fehlt), Support-Adresse und URL (`support@lind-audio.com`, `lind-audio.com/ember` sind
  Platzhalter), Mac-Version (braucht einen Mac), Shop/Zahlung, Produktseite.

Stand 2026-11-10, Version 0.9.0 (Beta). Erstes Einzel-Plugin nach dem neuen Plan (Jakob 2026-11-10): jedes Modul
einzeln verkaufen, später ein Hub, der alle verbindet. Plan-Seite: https://claude.ai/artifact/T2uCsbjZGFrqUMHBDkYknZ

## Regeln der Familie (Jakob)
- Name = Maskottchen + Funktion: **EMBER De-Esser**.
- Bedienelemente mit den üblichen Profi-Begriffen (Amount, Threshold, Range, Delta …), keine Spaßnamen (Jakob 2026-11-10).
- Layout, Knobs, Buttons und Segmente sind dieselben wie in VOCALYPSE (gemeinsamer UI-Kit aus `Source/UI`).
  Pro Plugin ändern sich nur die Farbe (EMBER: Glutrot `#ff3b1f`) und das Maskottchen.
- Jedes Plugin hat SIMPLE und ADVANCED und einen AUTO-Knopf.
- Maskottchen in 2D, Jakob liefert die Bilder (`Products/Ember/Mascot/README.md`).

## Kennungen (nie ändern)
- Hersteller `Lnda`, Plugin-Code `Embr`, Bundle-ID `com.lindaudio.ember`, State-Typ `EmberState`.
- Parameter-IDs mit Präfix `em` (`emFreq`, `emThreshold`, `emRange`, `emAmount`, `emAttack`, `emRelease`,
  `emLookahead`, `emMix`, `emOutput`, `emMode`, `emDetection`, `emListen`, `emBypass`), Version-Hint 1.

## Klang (DSP, `Products/Ember/Source/EmberEngine.*`)
- Detektor: Hochpass 4. Ordnung (Butterworth) an der Frequenz, Stereo gekoppelt, Spitzenwert.
  Erkennung relativ zum Gesamtpegel (Standard), damit laute und leise Phrasen gleich behandelt werden, oder absolut.
  Unter −60 dBFS (fast Stille) wird nie reduziert.
- Kennlinie 4:1 mit 6 dB Soft-Knee, Range begrenzt, **Amount** skaliert das Ergebnis (50 % = wie eingestellt,
  100 % = doppelt, 0 % = aus), höchstens 24 dB.
- Split: breite dynamische Glocke um das S (Mitte 1,25 × Frequenz, Güte 0,4; bis 2026-11-22 High-Shelf ab der Frequenz).
  Ohne Reduktion wird die Glocke übersprungen, also bit-genau.
  Breitband: die ganze Stimme taucht kurz ab.
- Lookahead 0–5 ms bei **fester Latenz von 5 ms** (der Host sieht nie eine Änderung).
- Hören: Aus / S-Band (der Detektor-Bereich) / Delta (was entfernt wird). Dazu Mix, Output und Bypass (Host-Bypass,
  20 ms Überblendung, Latenz bleibt gleich).

## AUTO (`EmberAuto.*`) – subtil (Jakob 2026-11-10: „nur subtil die S-Laute wegnehmen, nicht die Stimme zerstören“)
1. Klick auf AUTO oder auf Ember: Die Pausen werden übersprungen, er nimmt bis zu 12 s Gesang auf (höchstens 30 s,
   FERTIG geht ab 3 s).
2. Analyse im Hintergrund-Thread: Die Frames werden getrennt in S-Laute (Höhen über 4 kHz überwiegen), Vokale und
   gemischte Frames (S direkt am Vokal). Die S-Spitze wird zwischen 4 und 12 kHz gesucht, die Frequenz liegt bei 80 %
   davon (begrenzt auf 5–9,5 kHz), damit die Präsenz (2–5 kHz) unberührt bleibt.
3. Ziel je nach Schärfe: Wie laut sind die S gegenüber den Vokalen? Weiche S (14 dB darunter) verlieren 1,5 dB,
   scharfe S (2 dB darunter) 3,5 dB.
4. Threshold durch **Ausprobieren**: Die Engine rendert den Take mit einer Range von 6 dB, eine binäre Suche trifft das
   Ziel. Verlieren die Vokale mehr als 0,1 dB oder die gemischten Frames mehr als 0,5 dB, wird der Threshold angehoben.
   Range = 90-%-Wert der S-Reduktion plus 1 dB (3–6 dB), Release 35 ms (lässt vor dem nächsten Vokal los).
5. Defaults ohne AUTO: Range 5 dB; Stimme Tief / Mittel / Hoch = 5 / 6,5 / 8 kHz.

Echte Takes (17 Takes, 2026-11-10): Frequenz 5–8,7 kHz, S-Laute −1,2 bis −2,6 dB (Energie), Range 3–5 dB,
Vokale höchstens −0,02 dB, S-nahe Vokalteile höchstens −0,45 dB. Gerendert: unter 2 kHz 0,00 dB, 2–4 kHz höchstens
−0,09 dB, größte Reduktion 3–5 dB (vorher 9–11 dB).

## Maskottchen (`EmberMascot.*`)
Speicher (2026-11-12): Die PNGs bleiben komprimiert im Plugin; ein Bild wird erst beim Anzeigen dekodiert (1,7 ms)
und in einem Cache für 24 Bilder gehalten, den alle EMBER-Fenster teilen. Vorher wurden beim Öffnen jedes Fensters alle
189 Bilder entpackt (281 MB). Gemessen im RefHost mit offenem Fenster: 481 MB → 185 MB für den ganzen Prozess.
Ruhig (Jakob: „springt sehr viel herum“, „wechselt zwischen tanzen und feuer“): Feuer bleibt, bis 4 s kein S kam
(Snapping 2,5 s). Eine neue Stimmung muss 0,25 s anliegen, jede Stimmung bleibt mindestens
2 s, das Maskottchen bekommt die geglättete Reduktion. Standbilder bewegen sich nur leicht (Atmen, sanftes Wiegen).
Clips: `<state>_in_###` → `<state>_###` (Schleife) → `<state>_out_###`; ein Wechsel spielt out → in → Schleife,
ohne Clips wird 0,35 s überblendet. 12 fps (`mascot.json`). Animations-Anleitung für Jakob:
Drive `Plugins/01 EMBER De-Esser/ANLEITUNG_Animation.md`, Werkzeug `tools/mascot/video_to_frames.py`.

Stimmungen: idle, singing, snapping, fire (Stärke = Gain Reduction), overheated (über 9 dB für 0,3 s, „Klingt
gelispelt!“), sleeping (Bypass), listening (AUTO hört zu), proud (AUTO fertig). Fehlen Bilder für eine Stimmung,
zeichnet das Plugin einen Vektor-Teufel als Platzhalter. Bilder: `Products/Ember/Mascot/frames/<stimmung>_001.png …`

## Lizenz (2026-11-12, eingebaut, noch AUS)
**2026-11-16: Anbieter jetzt Polar.sh** (Lemon Squeezy nimmt keine neuen Stores mehr, Jakobs Antrag abgelehnt).
Polar-Kundenportal-API ohne Geheimnis im Plugin (`/v1/customer-portal/license-keys/activate|validate|deactivate`,
JSON). Organisation 406e9435-…, Produkt 5c48f7e6-… in `EmberLicence.h`; offen: Benefit-ID (Prüfung „richtiges
Produkt“), Sandbox ja/nein (`polarSandbox`), Checkout-Link (`buyUrl`), Testschlüssel. Beim Lizenz-Benefit MUSS ein
Aktivierungs-Limit gesetzt sein, sonst lehnt Polar „activate“ mit 403 ab.
Gleiches Modul und gleiche Regeln wie VOCALYPSE (`docs/LICENSING.md`): 14 Tage Test, Schlüssel aus der Bestell-Mail,
2 Computer, offline bleibt aktiviert, gesperrt = Audio läuft wie im Bypass durch (gleiche Latenz).
- Werte: `Products/Ember/Source/EmberLicence.h` (`polarOrganizationId`, `polarBenefitId`, `buyUrl`). Datei
  `%APPDATA%\Lind Audio\EMBER\EMBER.license`, eigene Signatur (eine VOCALYPSE-Datei gilt nicht).
- Einschalten: `cmake -S . -B build -DEMBER_LICENSING=ON`, bauen, EmberTests, pluginval, Testschlüssel aktivieren.
- Oberfläche: ≡ > Lizenz (Status, Schlüssel eingeben, Computer freigeben, Kaufen); Sperrkarte über dem Panel
  (`Source/UI/LicenceUI.*`, gemeinsam mit VOCALYPSE).
- Jakobs Anleitung für Lemon Squeezy: https://claude.ai/artifact/KHmzAvoA58gojurohHMkkT
- Offen: Gratis-Release mit Mail-Liste statt Verkauf? (Jakob 2026-11-12, später besprechen) – die Lizenz bleibt
  dann gleich, nur der Preis ist 0 €.

## Bauen, testen, installieren
- `scripts\build.ps1` baut alles (auch `Ember_VST3`, `Ember_Standalone`, `EmberTests`) und führt beide Test-Runner aus.
- `EmberTests.exe`: 104 Checks (Null-Test, S-Reduktion, Range/Amount, Breitband, Delta, Bypass, Lookahead,
  Blockgrößen 1–4096, NaN, Latenz bei 44,1–192 kHz, AUTO bei 44,1/48/96 kHz, Capture, State, Busse, Maskottchen,
  Editor in beiden Ansichten und Sprachen).
  - `EMBER_VOCALS=<Ordner;Datei;…>` prüft AUTO zusätzlich an echten Takes.
  - `EmberTests --render <wav> <ordner>` → `<name>_ember.wav` und `<name>_delta.wav` (32-Bit-Float).
  - `EmberTests --snapshots <ordner>` → PNGs von SIMPLE und ADVANCED (de/en).
- pluginval Stufe 10: SUCCESS (2026-11-10).
- Installieren (FL Studio vorher schließen): `.\scripts\install-vst3.ps1 -Product Ember` → `EMBER De-Esser.vst3`.

## Offen
- Jakobs Hörtest (subtil): Drive `Plugins/01 EMBER De-Esser/Hoertest/2026-11-10_subtil` (FLAC; WAV in
  `velvox-bench/2026-11-10/ember_hoertest_subtil`).
- Echte Maskottchen-Bilder (Jakob).
- Lizenz: eingebaut (siehe oben), es fehlen nur Jakobs Lemon-Squeezy-Werte.
- Installer, Produktseite und Preis (Plan: 29–39 €, Einführungspreis).
- Presets (Factory-Presets, z. B. „Rap hell“, „Sanft“) sind noch nicht da; der Host speichert alles im Projekt.
