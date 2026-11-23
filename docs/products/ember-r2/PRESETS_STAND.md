# EMBER r2 – Presets: Zwischenstand (2026-11-23, Branch r2/ember-presets)

## Fertig (im Code, gemessen)
- 29 Werks-Presets (Default + 28) in 5 Kategorien, englische Namen, Kurzbeschreibung DE/EN
  (`Products/Ember/Source/EmberPresets.*`); Menü mit Untermenüs, Beschreibung in der Statuszeile nach Laden/Blättern.
- Standard neu: 6 kHz / −34 dB (vorher 6,2 kHz / −32); MITTEL-Knopf = Standard (`EmberParams.h`).
  15 Takes: tiefe Stimmen S-Kern −2,86 statt −2,14 dB, hohe −4,15 statt −3,90, S-Farbe −80 statt −127 Hz.
- `EmberTests --process … preset=<Name|Index>`; neue Tests (Bibliothek, Blättern, alle Presets auf allen Takes, Stärke-Leiter).
- Handbücher DE/EN (Preset-Tabelle), Shop-Text.
- Messwerte aller Presets über den Plugin-Pfad: `tools/bench/ember_presets/final_measurements.txt`.

## Offen / nächste Schritte
1. Volle Tests mit `EMBER_VOCALS=…\2026-11-23\ember-data\takes` laufen lassen (ohne VOCALS: 1449/2 FAIL, beide FAIL
   inzwischen behoben, noch nicht neu gelaufen), pluginval Strenge 10.
2. Hör-Set rendern nach `C:\Users\Shadow\dev\velvox-bench\2026-11-24\presets` (je Preset 1 Take, 20 s, + Delta).
3. `docs/products/ember-r2/PRESETS.md` (ausführlich: Tabellen pro Preset, Standard-Auswertung) schreiben.

## Bench
- `C:\Users\Shadow\dev\velvox-wt\r2-presets\build\bench` (Kopie von bench_merged + `data\genre` = komprimierte/helle
  Club- und Trap-Versionen, erzeugt mit `mkgenre.py`). Skripte auch in `tools/bench/ember_presets`.
- `python runp.py <exe> <tag>` misst jedes Preset auf seinem Material (`presets.py` = Quelle der Werte, `gen_cpp.py`
  erzeugt den C++-Block, `gen_manual.py` die Handbuch-Tabellen).
