# EMBER De-Esser (Lind Audio, VOCALYPSE-Familie)

Stand 2026-11-10, Version 0.9.0 (Beta). Erstes Einzel-Plugin nach dem neuen Plan (Jakob 2026-11-10): jedes Modul
einzeln verkaufen, später ein Hub, der alle verbindet. Plan-Seite: https://claude.ai/artifact/T2uCsbjZGFrqUMHBDkYknZ

## Regeln der Familie (Jakob)
- Name = Maskottchen + Funktion: **EMBER De-Esser**.
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
- Kennlinie 4:1 mit 6 dB Soft-Knee, Range begrenzt, **Zähmen** skaliert das Ergebnis (50 % = wie eingestellt,
  100 % = doppelt, 0 % = aus), höchstens 24 dB.
- Split: dynamisches High-Shelf ab der Frequenz. Ohne Reduktion wird das Shelf übersprungen, also bit-genau.
  Breitband: die ganze Stimme taucht kurz ab.
- Lookahead 0–5 ms bei **fester Latenz von 5 ms** (der Host sieht nie eine Änderung).
- Hören: Aus / S-Band (der Detektor-Bereich) / Delta (was entfernt wird). Dazu Mix, Output und Bypass (Host-Bypass,
  20 ms Überblendung, Latenz bleibt gleich).

## AUTO (`EmberAuto.*`)
1. Klick auf AUTO oder auf Ember: Die Pausen werden übersprungen, er nimmt bis zu 12 s Gesang auf (höchstens 30 s,
   FERTIG geht ab 3 s).
2. Analyse im Hintergrund-Thread: Die Frames werden in S-Laute (Höhen über 4 kHz überwiegen) und Vokale getrennt.
   Die S-Spitze wird zwischen 4 und 12 kHz gesucht, die Frequenz liegt bei 70 % davon (begrenzt auf 4,5–9 kHz).
3. Threshold durch **Ausprobieren**: Die Engine rendert den Take, eine binäre Suche trifft genau den Wert, bei dem die
   S-Laute 5 dB Energie verlieren. Verlieren die Vokale mehr als 0,3 dB, wird der Threshold angehoben. Die Range wird
   aus dem 95-%-Wert der S-Reduktion plus 2 dB bestimmt (4–12 dB).
4. Ember zeigt „Passt!“, die Statuszeile sagt, was eingestellt wurde.

Echte Takes (2026-11-10, 11 SONOVOX-Leads und 6 VocalSet-Takes): alle 17 eingestellt, Frequenz 4,5–7,65 kHz,
S-Laute −4,1 bis −5,0 dB, Vokale höchstens −0,1 dB.

## Maskottchen (`EmberMascot.*`)
Stimmungen: idle, singing, snapping, fire (Stärke = Gain Reduction), overheated (über 9 dB für 0,3 s, „Klingt
gelispelt!“), sleeping (Bypass), listening (AUTO hört zu), proud (AUTO fertig). Fehlen Bilder für eine Stimmung,
zeichnet das Plugin einen Vektor-Teufel als Platzhalter. Bilder: `Products/Ember/Mascot/frames/<stimmung>_001.png …`

## Bauen, testen, installieren
- `scripts\build.ps1` baut alles (auch `Ember_VST3`, `Ember_Standalone`, `EmberTests`) und führt beide Test-Runner aus.
- `EmberTests.exe`: 104 Checks (Null-Test, S-Reduktion, Range/Zähmen, Breitband, Delta, Bypass, Lookahead,
  Blockgrößen 1–4096, NaN, Latenz bei 44,1–192 kHz, AUTO bei 44,1/48/96 kHz, Capture, State, Busse, Maskottchen,
  Editor in beiden Ansichten und Sprachen).
  - `EMBER_VOCALS=<Ordner;Datei;…>` prüft AUTO zusätzlich an echten Takes.
  - `EmberTests --render <wav> <ordner>` → `<name>_ember.wav` und `<name>_delta.wav` (32-Bit-Float).
  - `EmberTests --snapshots <ordner>` → PNGs von SIMPLE und ADVANCED (de/en).
- pluginval Stufe 10: SUCCESS (2026-11-10).
- Installieren (FL Studio vorher schließen): `.\scripts\install-vst3.ps1 -Product Ember` → `EMBER De-Esser.vst3`.

## Offen
- Jakobs Hörtest: `%USERPROFILE%\dev\velvox-bench\2026-11-10\ember_hoertest\` (Original / Ember / Delta, 6 Takes).
- Echte Maskottchen-Bilder (Jakob).
- Lizenz (Lemon Squeezy) ist für EMBER noch nicht eingebaut: der VOCALYPSE-Lizenzcode hängt an `ProductInfo.h`.
  Vor dem Verkauf: eigene Produkt-ID, Trial/Key.
- Installer, Produktseite und Preis (Plan: 29–39 €, Einführungspreis).
- Presets (Factory-Presets, z. B. „Rap hell“, „Sanft“) sind noch nicht da; der Host speichert alles im Projekt.
