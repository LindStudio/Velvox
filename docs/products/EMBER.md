# EMBER De-Esser (Lind Audio, VOCALYPSE-Familie)

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
Ruhig (Jakob: „springt sehr viel herum“): Eine neue Stimmung muss 0,25 s anliegen, jede Stimmung bleibt mindestens
0,9 s, das Maskottchen bekommt die geglättete Reduktion. Standbilder bewegen sich nur leicht (Atmen, sanftes Wiegen).
Clips: `<state>_in_###` → `<state>_###` (Schleife) → `<state>_out_###`; ein Wechsel spielt out → in → Schleife,
ohne Clips wird 0,35 s überblendet. 12 fps (`mascot.json`). Animations-Anleitung für Jakob:
Drive `Plugins/01 EMBER De-Esser/ANLEITUNG_Animation.md`, Werkzeug `tools/mascot/video_to_frames.py`.

Stimmungen: idle, singing, snapping, fire (Stärke = Gain Reduction), overheated (über 9 dB für 0,3 s, „Klingt
gelispelt!“), sleeping (Bypass), listening (AUTO hört zu), proud (AUTO fertig). Fehlen Bilder für eine Stimmung,
zeichnet das Plugin einen Vektor-Teufel als Platzhalter. Bilder: `Products/Ember/Mascot/frames/<stimmung>_001.png …`

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
- Lizenz (Lemon Squeezy) ist für EMBER noch nicht eingebaut: der VOCALYPSE-Lizenzcode hängt an `ProductInfo.h`.
  Vor dem Verkauf: eigene Produkt-ID, Trial/Key.
- Installer, Produktseite und Preis (Plan: 29–39 €, Einführungspreis).
- Presets (Factory-Presets, z. B. „Rap hell“, „Sanft“) sind noch nicht da; der Host speichert alles im Projekt.
