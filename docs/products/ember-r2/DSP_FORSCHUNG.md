# EMBER r2 – DSP-Forschung (Branch `r2/ember-research`, 2026-11-24)

Auftrag: 8 DSP-Kandidaten bauen, jeden auf dem Bench gegen Produktion (80ac3e7) und Pro-DS messen, nur die Gewinner
integrieren. Ergebnis: **ein Kandidat integriert (S-Tracking)**, einer wartet auf ein Hörurteil (Transienten-Schutz),
der Rest verworfen.

## Bench
- Ordner `build\bench` (Kopie von `bench_merged`): 15 echte Takes in `data\takes`, Pro-DS-Renders (Standard) für J1–J3
  in `data\prods`. Lauf: `python run.py <exe> name:auto=1` (alle Takes durch `EmberTests --process` mit AUTO).
- Metriken `ana.py` / `deess_metrics.py`: Kern (Absenkung im S-Kern), Luft, Schwerpunkt-Verschiebung des S, unberührt %
  (S-Ereignisse ohne nennenswerte Absenkung), t/k (kurze Transienten), VokGR % (Vokal-Frames mit Absenkung),
  Reinheit % (wie wenig außerhalb der S geändert wird), retrig (Neu-Einsätze je S), eigen/drunter/drüber (Absenkung je S
  an seiner eigenen Spektralspitze / darunter / darüber), iqr (Streuung der Absenkung über die S), spread.
- Exes: `prod\` (Produktion 80ac3e7), `rx2\` (Prototypen mit Schaltern `xp…`/`xf…`, Commit 74b3129, nur Bench),
  `new2\` (Stand dieses Branches, 77b44ed). Zusatzskripte: `level.py` (K7), `phase.py` (K8), `profile.py`, `show.py`,
  `cpu.py`, `listen_r2.py` (Hördateien).
- Ergebnisse: `out\b1–b3` (Kandidaten), `out\final3.*` (Endstand), `out\final3p.*` (Produktion).

## Kandidaten und Urteile
Referenz Produktion (AUTO, 15 Takes | J1–J3): Kern −3,48 | −4,05 dB, Reinheit 99,45 | 99,21 %, retrig 0,44 | 0,40.

| # | Kandidat | Messung (Prototyp) | Urteil |
|---|---|---|---|
| 1 | Lautheits-/Schärfe-Gewichtung des Detektors | nur Farbe verschoben, retrig 0,51 / 0,46 (schlechter) | verworfen |
| 2 | S-Tracking: drei feste Matched-Glocken eine halbe Oktave auseinander, Gewichte folgen dem S | Kern −3,56 / −4,13, unterhalb des S −1,92 statt −2,18, unberührte S 12,0 statt 13,0 %, retrig 0,42 / 0,33, iqr 1,99 statt 2,11, Schwerpunkt J −153 Hz (Pro-DS −145), Reinheit gleich | **integriert** |
| 3 | Transienten-Schutz (Onset 1 ms/30 ms > 9 dB → 12 ms Absenkung × 0,3) | t/k −1,03 statt −1,65 (J −1,80 statt −2,91), Kern −3,65, Reinheit 99,54 (J 99,47), aber iqr 2,25 (J 2,21 statt 1,79) | **Hörtest nötig** (Hördateien 08/09) |
| 4a | Hybrid Peak/RMS-Detektor | unberührt 15,2 %, iqr schlechter | verworfen |
| 4b | Doppel-Release | retrig 0,41, unberührt 11,5 %, aber leise Reibelaute −0,82, VokGR 0,15 | verworfen (Hörtest optional) |
| 5 | 3-Band-Split (Absenkung je Band nach Pegel) | unberührt 4 %, aber Reinheit 99,27, VokGR 0,20, spread 0,95 (flacher) | verworfen |
| 6 | Breitband mit Tilt | Schwerpunkt −263 / −352 Hz (dumpfer) | verworfen |
| 7 | Pegel-Konsistenz | −20 dB: 0,01 dB Abweichung je S; Pegelfahrt ±8 dB: 0,17 dB, r = 0,979; −30 dB: −0,16 dB (nur M_emerald unter dem −60-dBFS-Boden) | schon gut, nichts zu tun |
| 8 | Phase / Mix 50 % | kein Kamm (Welligkeit 0,00 dB), Gruppenlaufzeit ≤ 37 µs (6 dB) / 55 µs (12 dB) | nichts zu holen |
| – | Extra: Hochpass auf dem Split-Delta (steilere Flanke) | +1,5 … +2,8 dB Anhebung bei 2–4 kHz | verworfen |

## Was integriert wurde (Commits e011d57, 77b44ed)
- **S-Tracking** in `EmberEngine.*` (`Settings::trackS`, Standard an; `track=0|1` in `EmberTests --process`):
  das Split-Band ist eine Mischung aus drei festen Matched-Glocken (halbe Oktave Abstand, je mindestens 1 Okt. breit).
  Ein Tracker (drei Bandpässe à zwei Sektionen auf den Glockenmitten, hinter einem Hochpass an der Bandunterkante,
  5-ms-Energien) bestimmt, wo das aktuelle S liegt; nur die Gewichte bewegen sich, keine Koeffizienten-Modulation,
  ohne Absenkung bleibt der Ausgang bit-exakt. Totzone um die Mitte: ein S auf der Bandmitte bekommt genau die
  mittlere Glocke. Die Tracker-Gewichte laufen mit dem Lookahead wie die Hüllkurve.
- Hochpass vor dem Tracker behebt Allround im Mix (das Bett unter 2,5 kHz zog den Tracker nach unten).
- **Exakte Split-Tiefe an der Bandmitte** (offener QA-Punkt): `blendForGain` liefert genau die verlangte Absenkung auch
  jenseits der festen −12-dB-Glocke (24 dB: vorher 1,27 dB zu wenig, jetzt ≤ 0,04 dB); `splitResponseDb` modelliert das
  Band so, wie die Engine es fährt.
- Während Freq/Width gleiten: Koeffizienten alle 4 Samples (Width-Sweep 1 → 4: HF-Rest −68,6 → −74,7 dB).
- Tests: `testTracking` (Gewichte folgen tiefem/hohem S, Mitte bei gleicher Energie und Stille, Null-Test und Umschalten sauber) und angepasste Split-Tests.

## Endstand gemessen (new2 = 77b44ed, AUTO)
| | Kern | Luft | Schwerpunkt | unberührt % | t/k | VokGR % | Reinheit % | retrig | eigen | drunter | drüber | iqr |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 15 Takes – Produktion | −3,48 | −3,02 | −105 Hz | 13,0 | −1,65 | 0,13 | 99,45 | 0,44 | −2,78 | −2,18 | −2,31 | 2,11 |
| 15 Takes – **neu** | **−3,57** | −2,84 | −97 Hz | **12,0** | −1,68 | 0,14 | 99,44 | **0,43** | −2,84 | **−1,92** | −2,10 | **1,99** |
| J1–J3 – Produktion | −4,05 | −3,44 | −120 Hz | 7,3 | −2,91 | 0,10 | 99,21 | 0,40 | −3,38 | −2,77 | −2,68 | 1,79 |
| J1–J3 – **neu** | **−4,18** | −3,33 | −146 Hz | 7,3 | −3,00 | 0,10 | 99,21 | **0,35** | −3,47 | **−2,38** | −2,51 | **1,69** |
| J1–J3 – Pro-DS | −3,71 | −3,62 | −145 Hz | 9,5 | −2,87 | – | 98,08 | – | −3,05 | −2,64 | −2,85 | 1,27 |

Lesart: mehr Absenkung genau im S-Kern, weniger unterhalb des S (weniger „Lispeln“), weniger unberührte S, weniger
Neu-Einsätze, gleichmäßiger über die S (iqr), Reinheit unverändert. Schwerpunkt-Verschiebung auf J1–J3 jetzt wie
Pro-DS (−146 / −145 Hz). Pro-DS bleibt gleichmäßiger (iqr 1,27) – das ist der größte verbleibende Abstand.

## Tests, CPU, pluginval
- `EmberTests` (Release, mit `EMBER_VOCALS` = 15 Bench-Takes): **2005 bestanden, 0 fehlgeschlagen**, „15 of 15 takes
  set up“, QA-Teil 381 bestanden.
- CPU (`cpu.py`, J1 stereo 44,1 kHz ×3, Minimum aus 12 Läufen, Cloud-PC geteilt): Produktion **0,280 %** eines Kerns,
  neu **0,372 %**, neu mit `track=0` 0,316 %. Prototyp lag bei 0,46 %. Die SSE-Version (Biquad4: Glocken, Tracker-
  Bandpässe und Hochpass als 4-Lane-Register) ist bereits im Commit e011d57; eine weitere Optimierung wäre kein
  einfacher Schritt, daher hier nicht verfolgt.
- pluginval 1.0.4, Strenge 10, `--validate-in-process`, mit GUI-Tests, auf der gebauten VST3 (nicht installiert):
  **SUCCESS**.

## Hördateien
`C:\Users\Shadow\dev\velvox-bench\2026-11-24\research` (48 Dateien, je 30 s = Sekunde 20–50, 32-bit float),
Takes J1_chemicals, J2_kissed, J3_lonely, F01, M40, F_wings; je Take:
- `01_Original`
- `02_Produktion_AUTO`, `03_Produktion_AUTO_DELTA` (80ac3e7)
- `04_NEU_S-Tracking_AUTO`, `05_NEU_S-Tracking_AUTO_DELTA` (dieser Branch)
- `06_ProDS_Standard`, `07_ProDS_Standard_DELTA` (nur J1–J3)
- `08_Option_Transientenschutz_AUTO`, `09_Option_Transientenschutz_AUTO_DELTA` (rx2 `xp2=1`, Prototyp auf Basis
  Produktion, ohne Tracking)

Erzeugt mit `build\bench\listen_r2.py`.

## Offene Punkte
- **Hörurteil S-Tracking** (04/05 gegen 02/03 und 06/07): erst danach nach `feature/ember-fixes` mergen.
- **Hörurteil Transienten-Schutz** (08/09): misst bei t/k und Reinheit besser, streut aber mehr (iqr). Bei positivem
  Urteil sauber auf die Tracking-Engine portieren und neu messen.
- Gleichmäßigkeit über die S (iqr 1,69 gegen Pro-DS 1,27) bleibt der größte messbare Abstand.
- Handbuch/Shop-Text: S-Tracking erwähnen, falls es bleibt; CPU-Angabe (≈ 0,37 % statt 0,28 %) aktualisieren.
- Nicht installiert, nicht gepusht.
