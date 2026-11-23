# EMBER QA – Zwischenstand (Branch r2/ember-qa, 2026-11-23)

## Fertig
- Neue Suite `Products/Ember/Tests/EmberQaTests.cpp` (`EmberTests --qa`, `--qa-full`), läuft im normalen Gesamtlauf mit.
  Quick: 343 Checks grün (~15 s). Full: 396 grün, 2 rot (Speicher, siehe unten). Gesamtlauf mit EMBER_VOCALS:
  974 grün, 1 rot (`rig 2x screen < 16 ms` – Zeitmessung, schon vor meinen Änderungen rot, Maschinenlast), 15/15 Takes.
- Behobene Fehler (Commit e8f6dd1):
  1. Host-Texteingabe: „6,20 kHz“ → 2 kHz, „−35,5“ → 0, Komma verlor Nachkommastellen (EmberParams `parseTyped`).
  2. `nan`/`inf` im Projekt → Parameter NaN (jetzt Standardwert).
  3. Preset-Blättern blieb an kaputter User-Preset-Datei hängen (wird übersprungen).
  4. „*“ (geändert) ging beim Projekt-Laden verloren (Preset-Werte werden mitgespeichert).
  5. Klick beim Stereo-Modus-Wechsel während „S-Bereich“-Abhören (−27 dB → −89 dB).
  6. Bypass nicht bit-genau (1 ulp) → jetzt exakt.
  7. Absenkungs-Anzeige ignorierte Mix (Mix 0 % zeigte 9 dB) → zeigt jetzt die echte Absenkung.
  8. `fast::exp2` Rundung: Gain blieb ~5 s nach einem S bei 0,99999986 (Relative/Absolute) → jetzt 0,9 s bis bit-genau.
  9. Breite-Einheit „oct“ im deutschen UI → „Okt.“
- Messwerte: Latenz gemeldet = gemessen bei allen 6 Raten, allen Lookaheads, Split/Breitband/Bypass (10 ms).
  Null bit-genau in 60 Fällen. THD+N unter ~9 dB Absenkung: −70 … −81 dB (Standardzeiten).
  GR-Genauigkeit 180 Fälle: ≤ 0,035 dB (Split), ≤ 0,018 dB (Breitband); S-Pegel-Anzeige ≤ 0,005 dB.
  Blockgrößen 1…4096 + variabel: bit-identisch (1320 Läufe). Determinismus bit-identisch.
  CPU 48 kHz/512 Stereo (EPYC 9354, geteilte Maschine): Standard 37 µs/Block = 0,35 % Echtzeit, Worst 88 µs = 0,83 %;
  Prozess mit offenem Editor 32 % eines Kerns, geschlossen 0,4 %. Info-Karten: 19 (SIMPLE) / 30 (ADVANCED) Controls,
  DE+EN vollständig, nichts unübersetzt. Klicks: alle Umschaltungen ≥ 26 dB unter hartem 5-dB-Sprung.

## In Arbeit – SPEICHERLECK (echter Fehler)
- Jedes Öffnen des Editors kostet ~2,3 MB, die nie frei werden (120× öffnen: 148 → 398 MB). Handles/GDI stabil.
- Ursache gefunden: `vocalypse::ui::EmbeddedFonts` (Source/UI/Theme.cpp) liegt in einem SharedResourcePointer und wird
  mit dem letzten Editor zerstört; beim nächsten Öffnen ruft es erneut `Typeface::createSystemTypefaceFor` auf, und JUCE 8
  (DirectWriteCustomFontCollectionLoader::addRawFontData) behält jede Font-Kopie bis Prozessende.

## Nächste Schritte
1. Fix: Fonts einmal pro Prozess erzeugen (z. B. der Processor hält einen `SharedResourcePointer<EmbeddedFonts>`, oder
   ein Cache, der die Typeface-Ptr nicht neu anlegt); `EmberTests --qa-full` → Editor-Schleife < 0,1 MB pro Öffnen.
2. VST3 + Standalone bauen, pluginval Strenge 10 (mit GUI) laufen lassen.
3. Vollen Gesamtlauf mit EMBER_VOCALS wiederholen, TESTBERICHT.md schreiben, committen.
