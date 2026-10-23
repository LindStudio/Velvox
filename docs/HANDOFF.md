# HEADLINER – Übergabe (Stand 2026-10-23, Branch `feature/automix`)

## Wo der Code liegt
- Arbeitskopie: `%USERPROFILE%\dev\velvox`, Branch `feature/automix` (NICHT auf GitHub, nur lokal + Drive-Bundle).
- Sicherung: `scripts\backup-to-drive.ps1` → `G:\Meine Ablage\MUSIC\Plugin\_git-backup\velvox.bundle`.
- Auf einem anderen PC fortsetzen: `git fetch drive-backup feature/automix:feature/automix` (Remote `drive-backup` zeigt auf das Bundle), dann `git checkout feature/automix`.
- pluginval: `%USERPROFILE%\dev\tools\pluginval-1.0.4\pluginval.exe` (auf diesem PC am 23.10. neu installiert).
- Python ist auf diesem PC NICHT installiert (Tools in `tools/` brauchen es).

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

## Nächste Schritte
1. **Stil-Feinschliff** (Snapshots: `VelvoxTests --snapshot out.png x 0 pro <sel> none` bzw. `x 0 beginner -1 mixed`):
   - Modul-Chips in der Auto-Mix-Liste („EQ“, „Ride“, „Comp“) noch cyan → `moduleColour` verwenden.
   - Preset-Leiste oben wie im Guide (`< Festival Punch >` mittig, Icons Settings/Menü), evtl. Lade-/Startbildschirm (Guide Nr. 13).
   - Meter-Werte größer/fetter; Kompakt-/Minimal-Ansicht (Guide Nr. 7) wäre neu.
   - Optional: Voxity-artige große Neon-Icons pro Modul im Panel-Kopf.
2. **Auto-Mix:** Beim hellen/sibilanten Testmaterial bleiben hohe Strafen (tonal ~9, sibilance ~4–7, artifacts ~7–8) – prüfen, warum „artifacts“ (Peak bei Ziel-Lautheit) so hoch ist.
3. Planer: neue Reverb-Charaktere direkt im Basisplan nutzen (getragen → Large Hall; bisher nur als Variante).
4. Benchmark-Runner `--bench <dir> <out>` + Baseline 0.3 (Commit ab2e014) + Hörtest-Kit.
5. Datensatz VocalSet (lag auf dem anderen PC unter `%USERPROFILE%\dev\velvox-data`, hier nicht vorhanden). Split nach Sängern: dev f1–f6/m1–m7, val f7–f8/m8–m9, held-out f9/m10/m11.
6. In FL Studio testen (neuer Look, Pops, Browser, Mausrad-Q), Hörtest.

## Hilfen
- Build + alle Tests: `scripts\build.ps1`. Nur Tests bauen: VS-CMake (`vswhere` → `...\CMake\bin\cmake.exe`) `--build build --config Release --target VelvoxTests`.
- Einzelne Suite: `VelvoxTests --test "Velvox automatic mix"`; `--seq "A|B"`, `--from <name>`, `--category`.
- FX-Labor: `VelvoxTests --fxrender <dir> [vocal.wav]`; Pop-Scan: `VelvoxTests --popscan <tone|vocal|silence|floor> <automix> <editor> <sek> <fs> <realtime>`.
