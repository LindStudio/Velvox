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
