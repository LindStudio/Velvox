# HEADLINER – Übergabe (Stand 2026-10-21, Branch `feature/automix`)

## Erledigt (committed, getestet)
- **Umbenennung** zu HEADLINER by Lind Audio, Logo freigestellt (`Resources/Images`), Plugin-Codes/State-IDs unverändert → alte Velvox-Projekte laden. Bundle `Headliner.vst3`; `install-vst3.ps1` entfernt alte `Velvox.vst3`.
- **Pops:** Ursache war der Out-of-bounds-Read in Reverb/Doubler-Delaylines (Fix 76156cb vom 19.10.). Die in FL installierte Version stammte von *vor* dem Fix. Popscan (300 s Ton/Vocal/Stille/Rauschen, Realtime mit Editor): 0 Ereignisse. Detektor kennt jetzt Delay-Echos.
- **Reverb 0.4:** 16-Line-FDN, 8 Charaktere (Room, Plate, Hall, Large Hall, Ambience, Atmosphere, Cinematic, Ethereal), bis 20 s, Early/Diffusion/Movement/Bass/HighCut. Cache-Set-Staffelung der Linien (sonst je nach Heap bis 2× CPU).
- **Delay 0.4:** Zeit-Crossfade, Spread, Drive, Diffusion, Wow.
- **Multi-Instanz-Kette:** EQ×4, Comp/DeEss/Sat×3, Delay/Reverb/Smooth/Smash/Ride/Doubler/Choir×2 (IDs `<id>_<k>`), Routing v2 (`v2,eq,comp,eq#2,…`), Seqlock-Übergabe, Migration alter Projekte.
- **Chain-Editor + Modul-Browser** (Kategorien, Suche, +-Einfügepunkte, Duplizieren, Entfernen, Undo/Redo), Panels pro Instanz.
- **EQ Mausrad-Q** über Band-Punkten (`eqWheelQ`, Shift = fein).
- **Vocal Check:** Belege, „Warum wichtig“, Vorschlag, Konfidenz, Status (danach/davor/Auto-Mix), Rauschboden, Messwert-Chips.
- **A/B:** echte Slots, A = B-Anzeige, Auto-Mix füllt A = vorher / B = Mix, klickfrei gemessen.
- **Tests:** 4453 Checks, 0 Fehler (Stand Commit 115bf8b). pluginval für 0.4 **noch nicht** gelaufen.

## In Arbeit (dieser letzte Commit, nur teilweise getestet)
- **Auto-Mix-Optimierer:** `Source/AutoMix/AutoMixObjective.*` (Zielfunktion) + `AutoMixEngine::candidatesFor` + Kandidatensuche in `run()` (Varianten: Dynamik leichter/dichter, Sättigung, Höhen nach der Dynamik als EQ 2, Effekte trockener/mehr Raum, De-Esser stärker, Kombination der zwei besten). Kompiliert; Auto-Mix-Tests 444/444 grün; volle Suite nach diesem Schritt **nicht** gelaufen.

## Nächste Schritte
1. Volle Testsuite + pluginval (`%USERPROFILE%\dev\tools\pluginval-1.0.4`) laufen lassen.
2. Tests für den Optimierer (Kandidaten gültig, Base gewinnt ohne Vorteil, EQ-2-Variante setzt Routing) + `docs/AUTOMIX_OBJECTIVE.md`.
3. Planer: neue Reverb-Charaktere/Parameter direkt nutzen (Sustained → Large Hall).
4. Benchmark-Runner `--bench <dir> <out>` (JSON/WAV/CSV pro Datei), Baseline mit 0.3 (Commit ab2e014) rendern, Python-Vergleich + Hörtest-Kit (randomisiert, lautheitsangeglichen).
5. Datensatz: VocalSet (CC BY 4.0) geprüft/entpackt in `%USERPROFILE%\dev\velvox-data\sources\vocalset11` (3613 Dateien). Split nach Sängern vorschlagen (dev f1–f6/m1–m7, val f7–f8/m8–m9, held-out f9/m10/m11). Sonovox-Demo (nur Evaluation) lief im Hintergrund weiter: `python tools/dataset/fetch.py --source sonovox-free-demo --allow-evaluation` setzt fort.
6. In FL Studio testen (Pops über längere Wiedergabe, Browser, Mausrad-Q), Hörtest.

## Hilfen
- Build: `scripts\build.ps1`; nur Tests: `cmake --build build --config Release --target VelvoxTests`
- Snapshots: `VelvoxTests --snapshot out.png x 0 pro <selection> [none|mixed|check|welcome]`
- FX-Labor: `VelvoxTests --fxrender <dir> [vocal.wav]` + `python tools/bench/fx_analyse.py <dir>`
- Pop-Scan: `VelvoxTests --popscan <tone|vocal|silence|floor> <automix> <editor> <sek> <fs> <realtime>`
- Testreihenfolge: `--seq "A|B"`, `--from <name>`
