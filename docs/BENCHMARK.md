# Benchmark und Hörtest

## Werkzeuge

| Befehl | Zweck |
|---|---|
| `VelvoxTests --bench <ordner> <ausgabe>` | Auto-Mix über alle `*.wav` (rekursiv): `bench.csv` (eine Zeile pro Datei: Lautheit, Spitze, Bänder vorher/nachher, Basis- und gewählte Bewertung, Variante), `renders/` (Mix + Bericht) und ein blindes Kit Original vs. Mix in `listening/` (`NN_A/B.wav`, gleiche Lautheit, Lösung in `listening_key.csv`) |
| `VelvoxTests --automix <datei> <ausgabe>` | eine Datei, ausführlicher Bericht |
| `VelvoxTests --score <original.wav> <mix.wav>` | bewertet einen fertigen Mix (z. B. einer älteren Version) mit der aktuellen Zielfunktion, ganze Datei |
| `VELVOX_OVERRIDE="rideOn=0,compAttack=2"` | Diagnose: ändert den fertigen Plan vor dem Ganzdatei-Render von `--automix` / `--bench` |
| `python tools/bench/make_ab_kit.py <dirA> <dirB> <aus>` | blindes A/B-Kit aus zwei Render-Ordnern, per ffmpeg (EBU R128) auf gleiche Lautheit, Lösung in `ab_key.csv` |

Windows übergibt Kommandozeilen-Argumente nicht als UTF-8: Dateinamen mit Umlauten bei
`--automix` / `--score` vorher in ASCII umbenennen (`--bench` findet die Dateien selbst und ist nicht betroffen).

Baseline 0.3 bauen: `git worktree add --detach ..\velvox-v03 ab2e014`, dort `cmake -S . -B build` +
`cmake --build build --config Release --target VelvoxTests` (JUCE wie im Hauptbuild).

## Testset 2026-10-24

13 Vocals aus `Documents\Image-Line\FL Studio\Audio` (Splice-Hooks trocken/nass, separierte
Song-Stems, ein Lo-Fi-Loop). Ergebnisse und Hörtest-Kits lokal unter
`%USERPROFILE%\dev\velvox-bench\2026-10-24\` (nicht im Repo – urheberrechtlich geschütztes Material).

**Bewertung mit der aktuellen Zielfunktion, ganze Datei (`--score`, niedriger = besser):**

| | 0.3 (ab2e014) | 0.4 (2a5e379) |
|---|---|---|
| Mittelwert, 13 Dateien | 15,43 | 12,87 |
| ohne Lo-Fi-Loop (DYL, tonal ~94 unerreichbar) | 8,73 | 6,08 |
| besser bei | – | 13 von 13 |

Achtung: 0.4 optimiert genau diese Zielfunktion – das ist ein Hinweis, kein Beweis. Entscheidend
ist der Hörtest.

**Was die Fehlersuche in 0.4 ergeben hat** (Commits 9727847 … 2a5e379):
- Nasse, gleichmäßige Hooks (LRA 1–1,8 LU) wurden als „große Schwankungen“ (13–18 dB) gelesen –
  die Hallfahnen galten als leise Wörter. Level Ride hob sie in den nächsten Wortanfang: +7 dB
  Spitzen, Mix bis zu 9 dB leiser als das Original. Betraf 0.3 genauso (DS_VAH2: −17,9 LUFS).
- Clipping-Zählung im Float-Render (vor dem Clip Guard) und geerbtes Clipping der Aufnahme.
- Helle Takes: der Planer konnte Höhen nur anheben → Variante „Höhen sanfter“.

## Offene Beobachtungen

- **TREAM – GRÜNER SCHEIN:** auf der ganzen Datei artifacts ~11 und Spitze 0 dBFS (in beiden
  Versionen), im analysierten Abschnitt unauffällig – lautere Stellen außerhalb des
  Aufnahme-Abschnitts. Der Clip Guard greift dort. Prüfen, ob die Analyse den lautesten Teil sehen muss.
- Tonal-Fenster „Top“ (−16…−7 dB) vs. fertige Profi-Vocals (−1…−9 dB), siehe `AUTOMIX_OBJECTIVE.md`.

## Hörtest – so geht's

1. Kit-Ordner öffnen (`hoertest_0.3_vs_0.4` bzw. `hoertest_original_vs_0.4`), je Paar `NN_A` und
   `NN_B` hören (gleich laut), notieren: A besser / B besser / egal, plus Stichwort (zu dumpf, zu
   scharf, zu weit hinten, pumpt …).
2. Erst danach `ab_key.csv` / `listening_key.csv` öffnen und auflösen.
3. Ergebnis hier eintragen – die Gewichte der Zielfunktion werden daran kalibriert.

## Hörtest 2026-10-24 (Jakob, blind, gleiche Lautheit) – aufgelöst

**0.3 vs 0.4:** 5 : 5, 3× unentschieden – kein hörbarer Fortschritt insgesamt.

| Datei | Gewinner | Kommentar | 0.4 hatte |
|---|---|---|---|
| 01 DS_MT3 | 0.3 | verständlicher | kein Ride (LRA-Regel), De-Esser stärker |
| 02 DS_MYRNE | 0.4 | Reverb | kein Ride |
| 03 DS_VAH2 | 0.3 | verständlicher | kein Ride (Basisplan) |
| 04 DYL | – | beide etwas harsch | |
| 05 HAUSVERBOT | 0.3 | | Höhen sanfter + De-Esser |
| 06 HDGDL | 0.3 | verständlicher | Dynamik dichter + De-Esser |
| 07 Loona | 0.4 (knapp) | beide nicht gut | Höhen sanfter + De-Esser |
| 08 MYLK | 0.4 | weniger harsch, cleaner | De-Esser + Höhen sanfter |
| 09 GRÜNER SCHEIN | 0.3 (viel) | komprimierter, klarer | Basisplan |
| 10 Taylor Swift | 0.4 | Reverb | De-Esser stärker |
| 11 DIE NACHBARIN | – | 0.4 mehr Reverb, beide gut | |
| 12 München | 0.4 | mehr upfront | Dynamik leichter + De-Esser |
| 13 WIR SIND… | – | | |

**Original vs 0.4:** Mix besser 7× (01, 03, 05, 06, 08, 09, 10 – „upfront“, „Kompression“, „Reverb und Delay besser“),
Original besser 2× (04 DYL Lo-Fi, 11 DIE NACHBARIN mit Large Hall + Dynamik leichter), 4× gleich/beides schwach (02, 07, 12, 13).

**Schlüsse:**
- Gewünscht: dicht, upfront, verständlich. Die Zielfunktion bestraft genau das (Crest-Untergrenze 7 dB mit Gewicht 1,5,
  Spitzen-Zuwachs). Bei gleicher Lautheit hört man Spitzen nicht – der Zuwachs-Term ist zu stark gewichtet.
- Die LRA-Regel gegen Level Ride (2a5e379) hat bei 2 von 3 betroffenen Hooks hörbar verloren („verständlicher“ mit Ride).
  Die leisen Stellen waren offenbar doch leise Silben, nicht nur Hallfahnen → Regel zurücknehmen, die Spitzen anders lösen
  (Ausgangsstufe/Kompressor statt Ride abschalten).
- Reverb/Delay von 0.4 kommen gut an; Large Hall + Dynamik leichter (11) verlor gegen das Original.
- Harshness bleibt Thema (04, 08) – „Höhen sanfter“ half bei 08, bei 05 nicht.

## Nach der Kalibrierung (2b8b9d9 + Körper-Fix)

- Echte Vocals (13): Mittel Basis 12,87 → gewählt 11,02. „Dynamik dichter“ jetzt bei 7 Titeln, „Dynamik leichter“ bei keinem
  (u. a. GRÜNER SCHEIN und DIE NACHBARIN, wo der Hörtest dichter wollte); Level Ride auf den Hooks wieder an.
- **VocalSet v1.1, Validierungs-Sänger** (f7, f8, m8, m9; `excerpts`, 28 Dateien, `%USERPROFILE%\dev\velvox-data`):
  alle 28 gemischt, Mittel 1,58 → 1,32. Vorher 13,8 durch zwei Ausreißer (male9, tonal 143): Das Band „Körper“
  (100–300 Hz) liegt bei hohen Stimmen (tiefster Ton > 250 Hz) unter dem Grundton – dort zählt jetzt nur noch zu viel Körper.
  Held-out-Sänger (f9, m10, m11) bleiben für die Schlussprüfung unberührt.
- Zweites Hörtest-Kit 0.3 vs. neu: `%USERPROFILE%\dev\velvox-bench\2026-10-24\hoertest2_0.3_vs_neu`.
