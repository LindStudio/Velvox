# EMBER r3 – Gleichmäßigkeit über die S (Branch `r3/ember-evenness`, 2026-11-24)

Auftrag: EMBER soll alle S so gleichmäßig behandeln wie FabFilter Pro-DS. Messgröße „iqr“ (Streuung der Absenkung
über die S-Ereignisse eines Takes, Interquartilsabstand der 4–12-kHz-Absenkung je S): EMBER 1,69 auf J1–J3 (Pro-DS
1,27), 1,99 über alle 15 Takes. Das war der letzte messbare Abstand zu Pro-DS. Die anderen Werte durften nicht
schlechter werden. Jakob konnte nicht hören, daher entscheiden nur Messungen.

**Ergebnis:** integriert ist ein neuer Bezug für den Detektor (das S wird gegen „die Stimme jetzt“ gemessen statt
gegen den Vokal davor) plus ein Schutz für leise Reibelaute. iqr **J1–J3 1,69 → 1,23** (Pro-DS 1,27),
**alle 15 Takes 1,99 → 1,34**, die 12 Takes ohne Pro-DS-Vergleich 2,07 → 1,37 (keine Überanpassung an J1–J3).
Leise Reibelaute bleiben jetzt fast unberührt (J1–J3 −0,30 → −0,00 dB wie Pro-DS). Unberührte S halbiert, weniger
Neu-Einsätze. Kosten: etwas mehr Absenkung insgesamt (Kern +0,1 / +0,2 dB) und auf J1–J3 ein dunklerer
S-Schwerpunkt (−146 → −190 Hz). Vier Männer-Presets auf ihre alte Tiefe zurückgestellt.

## Bench
- Ordner `build\bench` in diesem Worktree (nicht versioniert): Skripte aus `r2-research\build\bench` (run.py, ana.py,
  deess_metrics.py, cpu.py, level.py) und `ember-fixes\build\bench` (pb.py, mix.py), Preset-Skripte aus
  `tools/bench/ember_presets`. **Takes, Pro-DS-Renders und Klassen-Dateien werden nicht kopiert**, sondern aus
  `ember-fixes\build\bench\data` bzw. `…\out\*.cls` gelesen. Ausgaben in `build\bench\out\r3`; `run.py` löscht die
  Renders nach der Messung (Platte war voll, `R3KEEP=1` behält sie).
- Skripte versioniert in `tools/bench/ember_evenness` (dazu `candidates_summary.txt`, `presets_before.txt`). Neu: `diag.py` (je S: Absenkung, Energie, Spitzenfrequenz, Dauer, Kontext, angewandte Absenkung), `levdiag.py`
  (dazu Detektor-Pegel und Smart-Gewicht je S), `resid.py` / `resid_run.py` (Zerlegung, siehe unten), `listen_r3.py`
  (Hördateien). `run.py` gibt zusätzlich `resSD` und `rCtx` aus.
- Exes: `exe\base` (= 3a9bbe3), `exe\rx1…rx9` (Prototypen mit Test-Schaltern `rx0…rx15`, Commit fa785cb),
  `exe\new` / `exe\final` (dieser Stand).
- Grundlinie nachgemessen: `exe\base` liefert exakt die Werte aus DSP_FORSCHUNG.md „Endstand“ (iqr 1,99 / 1,69).

## Diagnose: warum streut EMBER mehr?
1. **Es liegt am Detektor, nicht am Band.** Die Absenkung eines S (gemessen 4–12 kHz) korreliert mit der höchsten
   angewandten Absenkung in diesem S mit r = 0,96. Ob ein S wenig abbekommt, entscheidet also, wie laut der Detektor
   es liest – nicht, ob das Band es trifft.
2. **J1 ist vor allem ein Tiefen-Unterschied.** Pro-DS nimmt auf J1 nur −3,01 dB im S-Kern, EMBER-AUTO −4,16.
   Die Streuung wächst mit der Tiefe. Bei gleicher Tiefe (EMBER mit Range 3,5–4 auf J1: Kern −2,90 / −3,23) liegt
   EMBER schon vorher bei iqr 1,66 / 1,95, Pro-DS bei 1,89. Auf J3 (gleiche Tiefe) waren beide gleich (1,14 / 1,16),
   auf J2 nicht (1,04 / 0,76).
3. **Zerlegung je Take:** Absenkung je S = Steigung × S-Energie + Rest. Pro-DS: Steigung 0,19 (laute S etwas mehr),
   Rest-Streuung 0,51 dB. EMBER: Steigung 0,18 (gleich), **Rest 0,93 dB**. Der Rest hängt bei EMBER am Kontext
   (Korrelation mit „wie viel lauter war die Stimme in den 250 ms davor“: −0,40 über alle, −0,34 auf J1–J3;
   Pro-DS 0,00). Grund: der relative Detektor teilt das S-Band durch eine Hüllkurve der ganzen Stimme mit sofortigem
   Anstieg und 250 ms Abfall. Ein S direkt nach einem lauten Vokal wird gegen diesen Vokal gemessen und liest bis zu
   über 10 dB leiser als dasselbe S nach einer Pause. Dauer (0,11) und Spitzenfrequenz (0,29, Pro-DS 0,34) spielen
   eine kleinere Rolle.
4. Das untere Viertel (wenig Absenkung): leise S, die den Threshold gar nicht oder nur im Knie erreichen, und S nach
   lauten Vokalen mit nur 1–3 dB angewandter Absenkung trotz voller Lautstärke.

## Kandidaten und Messungen (AUTO, alle 15 | J1–J3)
| # | Kandidat (Schalter) | Kern | unber. % | f/leise | VokGR % | Rein % | retrig | drunter | Schwerp. Hz | iqr | Urteil |
|---|---|---|---|---|---|---|---|---|---|---|---|
| – | **Grundlinie** 3a9bbe3 | −3,57 / −4,18 | 12,0 / 7,3 | −0,76 / −0,30 | 0,14 / 0,10 | 99,44 / 99,21 | 0,43 / 0,35 | −1,92 / −2,38 | −97 / −146 | 1,99 / 1,69 | – |
| 1 | Bezug langsamer (Abfall 1 s / 2 s / 10 s / 30 s) | −3,61 / −4,08 (1 s) | 16,8 / 9,5 | −0,17 / −0,02 | 0,13 / 0,08 | 99,44 / 99,17 | 0,40 / 0,35 | −1,81 / −2,22 | −99 / −112 | 2,37 / 2,05 (2 s: 2,45 / 2,20) | verworfen: Kontext bleibt, leise S fallen ganz raus |
| 2 | Bezug mit 500 ms Halten | −3,51 / −4,17 | 18,8 / 9,8 | −0,74 / −0,37 | | | 0,46 / 0,48 | | | 2,62 / 2,42 | verworfen |
| 3 | Bezug = Stimmkörper (150 Hz–3 kHz) statt ganzes Band | −3,40 / −3,98 | 19,2 / 8,4 | | | | 0,50 / 0,45 | | | 2,28 / 1,88 | verworfen (S nach Pausen unter der Pegelgrenze) |
| 4 | Bezug schneller (Abfall 60 / 100 / 150 ms) | −3,79 / −4,30 (60) | 4,0 / 4,1 | **−1,24 / −0,96** | 0,18 / 0,17 | 99,42 / 99,20 | 0,37 / 0,30 | −2,22 / −2,52 | −112 / −190 | 1,28 / 1,17 (100: 1,49 / 1,31) | gleichmäßig, aber leise Reibelaute werden mit abgesenkt |
| 5 | Mischung schnell/250 ms im dB-Bereich (50 %) | −3,70 / −4,20 | 7,1 / 6,0 | −0,89 / −0,56 | 0,16 / 0,12 | | | | | 1,67 / 1,51 | schwächer als 6 |
| 6 | **Bezug = max (60-ms-Hüllkurve, 250-ms-Hüllkurve − G)**, G = 6 / 10 / 15 dB | −3,78 / −4,29 (10) | 4,4 / 4,9 | −1,04 / −0,79 | 0,17 / 0,17 | 99,42 / 99,20 | 0,38 / 0,30 | −2,20 / −2,52 | −112 / −190 | **1,30 / 1,21** (6: 1,48 / 1,26) | Kern der Lösung, braucht 7 |
| 7 | **Schutz leiser Reibelaute** (S-Band gegen Stimmpegel) zu 6 | siehe Endstand | | | | | | | | | **integriert** (10-s-Stimmpegel) |
| 7a | … Stimmpegel = 250-ms-Hüllkurve / 500-ms-Spitze | −3,78 / −4,29 | 4,9 / 5,7 | −1,00 / −0,75 | | | | | | 1,29 / 1,22 | schützt kaum |
| 7b | … Stimmpegel 3 s | −3,78 / −4,29 | 6,3 / 6,5 | −0,47 / −0,00 | 0,14 / 0,08 | | | | | 1,33 / 1,22 | gut, 10 s besser |
| 8 | Absolute Erkennung (Pro-DS-artig), mit Ratio 1,5 | −3,69 / −4,25 | 12,2 / 5,4 | −0,05 / −0,09 | 0,14 / 0,08 | 99,41 / **99,01** | 0,31 / 0,33 | −1,87 / −2,31 | −69 / −124 | 1,72 / 1,64 (4:1: 2,39 / 2,25) | verworfen: Rest-Streuung wie Pro-DS (0,47 / 0,59), aber pegelabhängig, leise Takes stoßen an −50 dB |
| 9 | Langsamer Stimmpegel als Bezug (2–10 s Mittel, auch mit Mischung) | | 17–21 / 12–15 | | | | bis 0,71 | | | 2,1–2,6 / 2,0–2,5 | verworfen (Anlauf am Take-Anfang, AUTO misst die ersten 14 s) |
| 10 | Ratio 2:1 / 8:1, Knie 12 / 15 dB | −3,61 / −4,10 (2:1) | 9,2 / 5,4 | −0,82 / −0,45 | 0,15 / 0,12 | 99,41 / 99,18 | 0,42 / 0,34 | −1,98 / −2,36 | −79 / −138 | 1,74 / 1,55 (8:1: 2,13 / 1,72) | allein zu wenig |
| 10a | 6 + 7 + Ratio 2:1 | −3,76 / −4,23 | 5,2 / 5,1 | −0,24 / −0,00 | 0,14 / 0,08 | 99,41 / 99,17 | 0,34 / 0,27 | −2,16 / −2,49 | **−98 / −175** | **1,24 / 1,16** | Option (nicht integriert, siehe offene Punkte) |
| – | Absenkungs-Halten 0–… (GR hold), `rx5` | | | | | | | | | | nicht weiter verfolgt (Diagnose: Kontext, nicht Halten) |

Lesart: jede Variante, die das S nicht mehr gegen den Vokal davor misst (4, 6), macht die S gleichmäßiger – bis
unter Pro-DS –, liest aber auch leise Reibelaute („f“, weiches „s“, Hauchlaute) so laut wie echte S. Der Schutz (7)
löst das: ein S muss außerdem in die Nähe des Stimmpegels der letzten Sekunden kommen. Absolute Erkennung (8) erreicht
die geringe Rest-Streuung von Pro-DS, streut aber über den Pegel mehr und gibt die Pegelunabhängigkeit auf.

## Was integriert wurde (Commit 5a99d12)
In `EmberEngine.*`, nur im Detektor (Band, Tracking, Hüllkurve, Gate unverändert):
- **Bezug des relativen Detektors:** `max (Hüllkurve 60 ms, Hüllkurve 250 ms − 10 dB)` statt der 250-ms-Hüllkurve
  allein (`refNowReleaseMs`, `refVowelReleaseMs`, `refVowelDb`). Ein S nach einer Pause liest wie bisher (beide
  Hüllkurven = das S selbst); direkt nach einem lauten Vokal wird es höchstens 10 dB unter diesem Vokal gemessen
  statt am vollen Vokal. Die Grenze „Beinahe-Stille wird nie bearbeitet“ (−60 dBFS) hängt weiter an der
  250-ms-Hüllkurve.
- **Schutz leiser Reibelaute** (nur Smart „Single Vocal“, nicht Allround): Stimmpegel = Mittel des Logarithmus der
  250-ms-Hüllkurve über 10 s, nur solange die Stimme da ist (über −100 dBFS und höchstens 30 dB unter dem eigenen
  Pegel, damit Pausen, Ausklänge und Atmer ihn nicht herunterziehen; die ersten Sekunden ein einfacher Mittelwert,
  kein langsamer Anlauf). Das S-Band (5-ms-RMS, wie beim Smart-Index) muss −28 … −22 dB unter diesem Pegel erreichen,
  sonst geht das Smart-Gewicht weich auf 0 (`voiceLevelMs`, `voiceGateDb`, `weakNoneDb`, `weakFullDb`). Je Kanal
  ebenso (Link < 100 %). Gerechnet jedes 4. Sample (mit dem Smart-Index).
- Test `testEvenness`: dasselbe S nach einer Pause und direkt nach einem lauten Vokal (neu −2,96 / −2,64 dB, mit dem
  alten Bezug −2,95 / −2,18 → schlägt fehl) und ein Reibelaut 36 dB unter dem S (0,000 dB).
- Ratio, Knie, Range, Attack/Release, Threshold-Bereich und AUTO-Logik unverändert. AUTO stellt sich selbst ein
  (Threshold im Mittel 1,7 dB höher als vorher, M42 +7 dB; Frequenz gleich).

## Endstand (AUTO)
| | Kern | Luft | Schwerp. | unber. % | f/leise | t/k | VokGR % | Rein % | retrig | eigen | drunter | drüber | iqr |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 15 Takes – vorher | −3,57 | −2,84 | −97 Hz | 12,0 | −0,76 | −1,68 | 0,14 | 99,44 | 0,43 | −2,84 | −1,92 | −2,10 | 1,99 |
| 15 Takes – **neu** | **−3,78** | −3,10 | −112 Hz | **6,3** | **−0,23** | −1,78 | 0,14 | 99,43 | **0,38** | −3,19 | −2,16 | −2,36 | **1,34** |
| 12 andere – vorher | −3,41 | −2,72 | −84 Hz | 13,2 | | −1,35 | 0,15 | 99,50 | 0,44 | −2,69 | −1,80 | −1,99 | 2,07 |
| 12 andere – **neu** | −3,65 | −2,99 | −92 Hz | 6,2 | | −1,47 | 0,15 | 99,48 | 0,40 | −3,07 | −2,07 | −2,28 | **1,37** |
| J1–J3 – vorher | −4,18 | −3,33 | −146 Hz | 7,3 | −0,30 | −3,00 | 0,10 | 99,21 | 0,35 | −3,47 | −2,38 | −2,51 | 1,69 |
| J1–J3 – **neu** | **−4,29** | −3,50 | −190 Hz | **6,5** | **−0,00** | −3,04 | **0,08** | 99,20 | **0,28** | −3,69 | −2,50 | −2,66 | **1,23** |
| J1–J3 – Pro-DS | −3,71 | −3,62 | −145 Hz | 9,5 | −0,00 | −2,87 | – | 98,08 | – | −3,05 | −2,64 | −2,85 | 1,27 |

iqr je Take vorher → neu: F01 2,42 → 2,01, F08 2,45 → 1,38, F22 1,78 → 0,80, **F_badboy 2,61 → 2,92**,
F_runwild 1,96 → 1,01, F_wings 2,19 → 1,41, J1 2,88 → 1,74, J2 1,04 → 1,04, J3 1,14 → 0,91, **M36 0,80 → 0,88**,
M40 2,28 → 1,11, M42 1,89 → 0,96, M_allittakes 1,36 → 0,81, M_emerald 2,76 → 2,16, M_notme 2,35 → 0,97.
Rest-Streuung (siehe Diagnose) alle 0,93 → 0,76, Kontext-Korrelation −0,40 → −0,06.

**Bewertung gegen die Vorgaben:**
- Kern, unberührt, retrig, f/leise, VokGR: besser oder gleich. Reinheit −0,01 dB-Punkte (Rauschen).
- **drunter** (Lispel-Gefahr) absolut −1,92 → −2,16 (J −2,38 → −2,50), weil mehr S überhaupt abgesenkt werden und
  tiefer. Im Verhältnis zur Absenkung am S selbst gleich: drunter/eigen 0,68 → 0,68 (J 0,69 → 0,68). Bei gleicher
  Tiefe (Amount 47 %, Kern −3,58 wie vorher): drunter −2,08, iqr 1,22 / J 1,16. Pro-DS senkt unterhalb mehr ab (−2,64).
- **t/k** −1,68 → −1,78 (J −3,00 → −3,04): bei gleicher Tiefe −1,70, also nur Folge der Tiefe.
- **S-Schwerpunkt** auf J1–J3 −146 → −190 Hz (Pro-DS −145), über alle −97 → −112 Hz. Bei gleicher Tiefe −175 Hz
  (J) / −104 Hz. Das ist der einzige Wert, der spürbar wandert (siehe offene Punkte, Option 10a).

**Pegel-Konsistenz** (`level.py`, Standard-Einstellung ohne AUTO, Abweichung je S): −20 dB Eingang 0,01 → 0,03 dB,
Pegelfahrt ±8 dB (5 s Periode) 0,16 → 0,17 dB (r 0,979 → 0,923; r sinkt auch, weil die Absenkung je S selbst weniger
streut). Die Spalte „−30 dB“ des Skripts ist ungültig (`out=30` wird auf +24 dB begrenzt).

## Presets
Gleiche Methode wie PRESETS.md Abschnitt 8 (`runp.py`, Preset wie im Plugin geladen, gleiches Material). Ohne
Nachstellen: alle 29 werden **gleichmäßiger** (iqr z. B. Default 1,66 → 1,01, Male Rap 1,91 → 0,95), leise Reibelaute
werden kaum noch berührt (Default −0,78 → −0,28 dB, Trap −2,02 → −0,00), unberührte S etwa halbiert, Schwerpunkt
fast überall heller (Default −96 → −80 Hz), aber **alle etwas stärker**: Genre-Presets +0,02 … +0,19 dB, Basics und
Frauen +0,2 … +0,5 dB, **Männer +0,6 … +0,9 dB** (die Männer-Takes haben die meisten S direkt nach lauten Vokalen).

Regel (Spiegel von Abschnitt 8 (a)): mehr als 0,5 dB stärker als vorher → zurück auf die alte Tiefe, mit einem Wert.
Range wirkt hier direkt (Threshold kaum).

| Preset | Änderung | S-Kern vorher / ohne Nachstellen / neu | Schwerp. vorher / neu | weitere |
|---|---|---|---|---|
| Male Rap | Range 6 → 5 | −4,23 / −4,96 / **−4,25** | +37 / +63 Hz | f/leise −2,01 → −0,57, unberührt 5,7 → 3,3 % |
| Deep Male Vocal | Range 5 → 4 | −3,55 / −4,24 / **−3,48** | +122 / +124 Hz | f/leise −1,74 → −0,47, unberührt 8,4 → 4,2 % |
| Aggressive Rap | Range 8 → 7 | −5,99 / −6,89 / **−6,25** | +32 / +76 Hz | bleibt klar stärker als Male Rap |
| Modern Pop Male | Range 5,5 → 4,5 | −3,36 / −3,98 / **−3,40** | −81 / −47 Hz | f/leise −1,34 → −0,33 |

Gelassen (0,3–0,5 dB stärker, Zweck unverändert, gleichmäßiger und weniger Lispel-Risiko bei leisen Lauten):
Default −3,68 → −4,15 (Vorgabe: Default bleibt), Rescue −7,26 → −7,77 (über 8 dB 23 → 32 %, war vor dem Tracking
32 %), Strong −5,58 → −6,07, Harsh −5,60 → −6,03, Gentle −2,33 → −2,75, Natural −3,93 → −4,32, Soft Female
−2,39 → −2,76, Extreme −8,48 → −8,84 (über 8 dB 51 → 60 %), übrige ≤ 0,33 dB. Mix/Bus (Allround): −2,76 → −2,82.
Vollständige Tabellen: `build\bench\out\r3\presets_base.txt` / `presets_new.txt` / `presets_final.txt`;
Endmessung auch in `tools/bench/ember_presets/final_measurements.txt`.

## Tests, CPU, pluginval
- `EmberTests` (Release, mit `EMBER_VOCALS` = 15 Bench-Takes): **2009 bestanden, 0 fehlgeschlagen** (2005 + 4 neue
  Checks), „15 of 15 takes set up“, QA 381 bestanden. (Ein Lauf parallel zum Preset-Bench meldete einmal
  „[rig] 2x screen: under 16 ms a frame“ – reine GUI-Zeitmessung unter Last, im Leerlauf grün.)
- pluginval 1.0.4, Strenge 10, `--validate-in-process`, mit GUI-Tests, auf der gebauten VST3 (nicht installiert):
  **SUCCESS**.
- CPU (`cpu.py`, J1 stereo 44,1 kHz ×3, Minimum aus 12 Läufen, Cloud-PC geteilt): vorher **0,368 %** eines Kerns,
  neu **0,376–0,384 %** (zwei Läufe; +0,01–0,02 %). Mit Link 50 % (Kanäle getrennt): 0,552 → 0,568–0,580 % – dieser
  Pfad lag schon vorher über 0,5 %, neu ist das nicht.

## Hördateien
`C:\Users\Shadow\dev\velvox-bench\2026-11-24\evenness` (je 30 s = Sekunde 20–50, 32-bit float), Takes J1_chemicals,
J2_kissed, J3_lonely, F01, M40, F_wings und F_badboy (der einzige Take, auf dem iqr steigt); je Take
`01_Original`, `02/03_VORHER_AUTO (+DELTA)` (3a9bbe3), `04/05_NEU_AUTO (+DELTA)`, `06/07_ProDS_Standard (+DELTA)`
(nur J1–J3). Erzeugt mit `build\bench\listen_r3.py`.
Worauf hören: S direkt nach lauten Vokalen (vorher zu schwach), leise Reibelaute und Hauchlaute im DELTA (sollen
fehlen), ob die S auf J1–J3 dunkler / lispelnder wirken als vorher.

## Offene Punkte
- **Hörurteil** (04/05 gegen 02/03 und 06/07), danach nach `feature/ember-deesser` mergen. Nicht gemergt, nicht
  gepusht, nicht installiert.
- **S-Schwerpunkt J1–J3 −190 Hz** (vorher −146, Pro-DS −145). Option 10a (zusätzlich Ratio 4:1 → 2:1): −175 Hz,
  iqr 1,16 / 1,24, Reinheit −0,03; ändert aber die Kennlinie aller Presets, daher nicht integriert. Bei „klingt
  dunkler“ zuerst diese Option messen.
- F_badboy streut mehr (2,61 → 2,92), M36 leicht (0,80 → 0,88): nicht untersucht.
- Der Schutz leiser Reibelaute hängt an einem 10-s-Stimmpegel: bei sehr großen Pegelsprüngen innerhalb weniger
  Sekunden (z. B. Flüstern direkt nach Schreien) bleibt ein lauter S im leisen Teil ggf. bis zu ~10 s etwas
  geschützt. Pegelfahrt ±8 dB gemessen unauffällig (0,17 dB).
- Link < 100 % (Kanäle getrennt) und Allround: Bezug neu, Schutz nur Single Vocal; nur über die Tests geprüft
  (Allround im Mix −4,31 dB, Stereo-Tests grün), nicht auf dem Bench gemessen.
- Aus r2 weiter offen: Transienten-Schutz (Hörtest), HOCH-Knopf.
