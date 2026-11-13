# EMBER De-Esser (Lind Audio, VOCALYPSE-Familie)

## STAND 2026-11-13 – HIER WEITERMACHEN
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
- Split: dynamisches High-Shelf ab der Frequenz. Ohne Reduktion wird das Shelf übersprungen, also bit-genau.
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
Gleiches Modul und gleiche Regeln wie VOCALYPSE (`docs/LICENSING.md`): 14 Tage Test, Schlüssel aus der Bestell-Mail,
2 Computer, offline bleibt aktiviert, gesperrt = Audio läuft wie im Bypass durch (gleiche Latenz).
- Werte: `Products/Ember/Source/EmberLicence.h` (`lemonStoreId`, `lemonProductId`, `buyUrl`). Datei
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
