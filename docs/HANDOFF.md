# HEADLINER – Übergabe (Stand 2026-10-24, Branch `feature/automix`)

## Wo der Code liegt
- Arbeitskopie: `%USERPROFILE%\dev\velvox`, Branch `feature/automix` (NICHT auf GitHub, nur lokal + Drive-Bundle).
- Sicherung: `scripts\backup-to-drive.ps1` → `G:\Meine Ablage\MUSIC\Plugin\_git-backup\velvox.bundle`.
- Auf einem anderen PC fortsetzen: `git fetch drive-backup feature/automix:feature/automix` (Remote `drive-backup` zeigt auf das Bundle), dann `git checkout feature/automix`.
- pluginval: `%USERPROFILE%\dev\tools\pluginval-1.0.4\pluginval.exe` (auf diesem PC am 23.10. neu installiert).
- Python: auf dem PC mit Benutzer `jakob` vorhanden (3.13 + numpy, ffmpeg per winget) – auf dem anderen PC laut Notiz vom 23.10. nicht. `tools/` braucht Python, `make_ab_kit.py` zusätzlich ffmpeg.
- 0.3-Baseline als Arbeitsbaum: `%USERPROFILE%\dev\velvox-v03` (Commit ab2e014, `VelvoxTests` gebaut).

## Erledigt am 23.10. (committed, getestet)
- **Volle Testsuite: 9797 Checks, 0 Fehler. pluginval Strenge 10: SUCCESS** (vor und nach dem Restyle).
- **Neuer Look nach dem Stilguide** (`HEADLINER Audioplugin UI-Stilguide.png`, `PluginStil.png` im Drive-Ordner), dokumentiert in `docs/STYLEGUIDE.md`:
  - Palette in `Source/UI/Theme.h` (#0B1020/#111827/#1F2937/#334155, Cyan #00D1FF, Blau #6366FF, Violett #A855F7, Pink #F472B6), `neon()`, `moduleColour()` pro Modul.
  - Montserrat statt Inter (`Resources/Fonts`, OFL, `THIRD_PARTY_LICENSES.md` angepasst), Größen ×0,93.
  - Regler: matter Knopf + leuchtender Verlaufsring (`VelvoxLookAndFeel::drawRotarySlider`, `dialBody`).
  - Buttons: aktiv = `drawNeonFill` (Indigo + Blau→Violett-Rahmen), Primär = Blau→Violett-Pille.
  - Modulfarbe an Ketten-Kacheln (`ChainView.cpp`), Panel-Kopf und Power-Button (`DetailPanels.cpp`, `PowerButton::setHue`).
  - Großbuchstaben: Reglernamen, Choice-Captions, Abschnitte, Panel-Titel, AUTO MIX / VOCAL-STATUS / FEINSCHLIFF, INPUT/OUTPUT.
  - Hintergrundlicht: blau links, violett rechts oben, pink rechts unten (`PluginEditor.cpp`).
- **Optimierer-Tests** (`Tests/AutoMixTests.cpp` → `optimizerTests()`) + `docs/AUTOMIX_OBJECTIVE.md`.
- Sporadischer Testfehler „original off“ behoben (Klickhelfer wartet 150 statt 10 ms).
- CHANGELOG-Eintrag 0.4.0.

## Erledigt am 24.10. (committed, getestet)
- Preset-Leiste nach Guide: `‹ Name ›` mittig, Disketten-Icon (Speichern) + Menü-Icon (Speichern als / Änderungen verwerfen / Löschen) in `TopBar.cpp` (`paintOverChildren` zeichnet die Icons, `showPresetActions`). Hilfe- und Tutorial-Texte angepasst.
- Modul-Chips der Auto-Mix-Liste in `moduleColour` (`AutoMixView.cpp`).
- **„artifacts“ war hoch, weil der Term den absoluten Abstand Spitze–Lautheit bestrafte** (dynamischer Take mit 15–16 dB PLR bei Ziel −14 LUFS → ~7 Punkte), obwohl die Ausgangsstufe nur leiser dreht. Schlimmer: De-Essing senkt die Lautheit bei gleichen Vokal-Spitzen und wurde dafür bestraft. Neu: Zuwachs Spitze−Lautheit ggü. Original (> 1,5 dB, Gewicht 1) + Headroom mild (0,25, s = 2). Synthetischer heller Take: 6,8 → 0,4. Echtes Vocal (MYLK dry chorus) lag schon vorher bei 0,3.
- Planer: getragener Gesang ohne Eigenraum → Large Hall 2,3 s / 40 ms im Basisplan (Test „Reverb character follows the style“).
- Bugfix: Reverb-Schritt hieß „Hall-Hall“ / „Large Hall-Hall“ (`AutoMixEngine.cpp`).
- Variante „Höhen sanfter“ (High-Shelf 5 kHz −1,5…−4 dB) für helle Takes (595a17c).
- **Level Ride nur bei bestätigter Schwankung** (LRA ≥ 3 LU): nasse Hooks bekamen sonst ihre Hallfahnen in den nächsten Wortanfang geschoben (bis 9 dB Lautheitsverlust). Clipping-Zählung nur noch neu und hörbar (2a5e379).
- **Benchmark:** `--bench`, `--score`, `VELVOX_OVERRIDE`, `tools/bench/make_ab_kit.py`. 0.4 schlägt 0.3 auf allen 13 echten Vocals (mit Vorbehalt, siehe `docs/BENCHMARK.md`). Hörtest-Kits unter `%USERPROFILE%\dev\velvox-bench\2026-10-24`.
- **Volle Testsuite: 10263 Checks, 0 Fehler. pluginval Strenge 10 (mit GUI-Tests): SUCCESS.**

## Nächste Schritte
1. **Stil-Feinschliff** (Snapshots: `VelvoxTests --snapshot out.png x 0 pro <sel> none` bzw. `x 0 beginner -1 mixed`):
   - Settings-Icon in der Preset-Leiste (Guide) – es gibt noch kein Einstellungsmenü; evtl. Lade-/Startbildschirm (Guide Nr. 13).
   - Meter-Werte größer/fetter; Kompakt-/Minimal-Ansicht (Guide Nr. 7) wäre neu.
   - Optional: Voxity-artige große Neon-Icons pro Modul im Panel-Kopf.
2. **Hörtest** mit den Kits (siehe `docs/BENCHMARK.md`) – danach Gewichte und Tonal-Fenster „Top“ kalibrieren.
3. Auto-Mix: GRÜNER SCHEIN – lautere Stellen außerhalb des Analyse-Abschnitts (Spitze 0 dBFS auf der ganzen Datei); Kompressor mit 12 ms Attack hebt auf dichten Hooks die Spitzen an (MYRNE, GR 4,7 dB) – Attack an die Dichte koppeln?
4. Datensatz VocalSet (lag auf dem anderen PC unter `%USERPROFILE%\dev\velvox-data`, hier nicht vorhanden). Split nach Sängern: dev f1–f6/m1–m7, val f7–f8/m8–m9, held-out f9/m10/m11.
5. In FL Studio testen (neuer Look, Pops, Browser, Mausrad-Q), Hörtest.

## Hilfen
- Build + alle Tests: `scripts\build.ps1`. Nur Tests bauen: VS-CMake (`vswhere` → `...\CMake\bin\cmake.exe`) `--build build --config Release --target VelvoxTests`.
- Einzelne Suite: `VelvoxTests --test "Velvox automatic mix"`; `--seq "A|B"`, `--from <name>`, `--category`.
- FX-Labor: `VelvoxTests --fxrender <dir> [vocal.wav]`; Pop-Scan: `VelvoxTests --popscan <tone|vocal|silence|floor> <automix> <editor> <sek> <fs> <realtime>`.
