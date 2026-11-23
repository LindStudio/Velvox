# EMBER r2 DSP-Forschung – Zwischenstand (2026-11-23, Branch r2/ember-research)

Bench: `C:\Users\Shadow\dev\velvox-wt\r2-research\build\bench` (Kopie von bench_merged, 4 Worker; neue Metriken in
`ana.py`: eigen/drunter/drueber = Absenkung je S an dessen eigener Spitze / darunter / darüber, slope, spread, iqr).
Ergebnisse: `out/b1.json`, `b2.json`, `b3.json` (+ `.txt`). Skripte neu: `level.py` (K7), `phase.py` (K8), `profile.py`,
`show.py`. Exes: `prod/` (Produktion 80ac3e7), `rx2/` (Prototypen mit xp/xf-Schaltern, Commit 74b3129), `new/`.

## Ergebnisse (AUTO, 15 Takes | J1–J3; Produktion: Kern −3,48 | −4,05, Reinheit 99,45 | 99,21, retrig 0,44 | 0,40)
| Kandidat | Ergebnis | Urteil |
|---|---|---|
| 2 S-Tracking (3 feste Glocken, Gewichte folgen dem S, γ=1, 2-Okt.-Glocken) | Kern −3,56 / −4,13, unterhalb des S −1,92 statt −2,18, unberührte S 12,0 statt 13,0 %, retrig 0,42 / 0,33, iqr 1,99 statt 2,11, Schwerpunkt J −153 Hz (Pro-DS −145), Reinheit gleich, VokGR +0,01 | **wird integriert** |
| 5 3-Band-Split (Absenkung je Band nach Pegel) | unberührt 4 %, aber Reinheit 99,27, VokGR 0,20, spread 0,95 (weniger Dynamik) | verworfen |
| 1 Lautheits-/Schärfe-Gewichtung des Detektors | nur Farbe verschoben, retrig 0,51/0,46 | verworfen |
| 3 Transienten-Schutz (Onset 1 ms/30 ms > 9 dB → 12 ms Absenkung ×0,3) | t/k −1,03 statt −1,65 (J −1,80 statt −2,91), Kern −3,65, Reinheit 99,54 (J 99,47), aber iqr 2,25 (J 2,21 statt 1,79) | Hörtest nötig |
| 4 Hybrid Peak/RMS | unberührt 15,2 %, iqr schlechter | verworfen |
| 4 Doppel-Release | retrig 0,41, unberührt 11,5, aber leise Reibelaute −0,82, VokGR 0,15 | verworfen (Hörtest optional) |
| 6 Breitband mit Tilt | Schwerpunkt −263/−352 Hz (dumpfer) | verworfen |
| 7 Pegel-Konsistenz | −20 dB: 0,01 dB Abweichung je S; Pegelfahrt ±8 dB: 0,17 dB, r=0,979; −30 dB: −0,16 (nur M_emerald unter −60-dBFS-Boden) | schon gut, verworfen |
| 8 Phase / Mix 50 % | kein Kamm (Welligkeit 0,00 dB), Gruppenlaufzeit ≤ 37 µs (6 dB) / 55 µs (12 dB) | verworfen (nichts zu holen) |
| Extra: Hochpass auf dem Split-Delta (steilere Flanke) | +1,5…+2,8 dB Anhebung bei 2–4 kHz | verworfen |

CPU Prototyp Tracking 0,46 % (Prod 0,28 %); Produktionsversion mit SIMD in Arbeit.

## In Arbeit (uncommittet bis zu diesem WIP-Commit)
Saubere Integration von K2 in `EmberEngine.*` (`Settings::trackS`, Standard an; `track=` in `--process`).
Stand: 1 Test rot – Allround im Mix (S −1,39 statt > 1,5 dB): das Bett unter 2,5 kHz zieht den Tracker nach unten;
Fix (Hochpass an der Bandunterkante vor dem Tracker, `trackHp`) eingebaut, Ergebnis noch nicht geprüft.

## Nächste Schritte
1. Build + Tests (Allround-Test), dann Bench `new\EmberTests.exe` auto:auto=1 gegen prod (Zahlen ≈ tg1 aus b2?).
2. CPU (`cpu.py`), Tests für Tracking (Null-Test, Gewichte folgen tiefem/hohem S, track=0 = alte Glocke), EMBER_VOCALS-Lauf, VST3 + pluginval.
3. Hördateien nach `C:\Users\Shadow\dev\velvox-bench\2026-11-24\research` (Original, Produktion, neu, Pro-DS, je DELTA; plus Transienten-Schutz aus rx2 `xp2=1`).
4. Bericht `DSP_FORSCHUNG.md`.
