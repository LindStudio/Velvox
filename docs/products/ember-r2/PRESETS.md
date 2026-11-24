# EMBER – Werks-Presets und Standard-Einstellung (r2, 2026-11-23, Branch `r2/ember-presets`)

> **Stand 2026-11-24 (feature/ember-fixes, mit S-Tracking):** alle 29 Presets auf der Engine mit S-Tracking neu
> gemessen (gleiche Methode, gleiche Takes), 7 Presets nachgestellt, Default unverändert. Tabelle in Abschnitt 4 =
> neuer Stand; alt → neu und die Begründungen in **Abschnitt 8**.

Auftrag: die Werks-Presets neu aufbauen (klarer Zweck, natürlich, aus dem echten DSP begründet, jedes einzeln gemessen)
und den Standard so wählen, dass EMBER direkt nach dem Einfügen auf jeder Stimme brauchbar klingt.
Code: `Products/Ember/Source/EmberPresets.*` (Liste), `EmberParams.h` (Standard = MITTEL), `EmberEditor.cpp` (nur
Preset-Menü, Blättern, MITTEL-Infotext). Werkzeuge in `tools/bench/ember_presets`: `presets.py` = Quelle aller Werte,
`gen_cpp.py` erzeugt den C++-Block, `gen_manual.py` die Handbuch-Tabellen, `runp.py` misst jedes Preset auf seinem
Material, `mkgenre.py` erzeugt das Genre-Material, `listen.py` das Hör-Set, `final_measurements.txt` = Endmessung.

## 1. Namen und Kategorien
- **Namen englisch** in beiden Sprachen (Produktnamen), Title Case. Kurzbeschreibung (1 Satz) DE und EN: sie erscheint
  nach dem Laden bzw. Blättern 5 s in der Statuszeile („Female Pop: …“) und steht in den Handbüchern.
- **Menü:** oben „Default“ (= Startzustand des Plugins), dann 5 Untermenüs; das Untermenü mit dem geladenen Preset ist
  abgehakt. Kategorie-Namen über `tr()`: Gesang-Grundlagen / Vocal Basics, Männerstimmen / Male Vocals,
  Frauenstimmen / Female Vocals, Genres / Genre, Spezialfälle / Special Purpose.
- **‹ ›** geht die Liste in Menü-Reihenfolge durch (die Liste ist nach Kategorie sortiert), danach die eigenen Presets.
- **Regeln (Test):** Jedes Preset nennt Freq, Thr, Range, Amount, Attack, Release, Lookahead, Breite und Erkennung (ändert
  sich ein Standard, wandert kein Preset mit). Keine Frequenz näher als 100 Hz an 5,75 / 7 kHz (Stimmlagen-Grenzen), kein
  Threshold auf −35 / −34 / −33 dB (sonst würde ein Stimmlagen-Knopf den Preset-Threshold überschreiben).
- Entfallen: Gentle (alt), Pop vocal, Rap, Deep/High voice (= Stimmlagen-Knöpfe), Backing vocals, Speech/podcast
  (Breitband), Strong. „Mix / Bus“ bleibt (Spezialfälle).
- `FactoryPreset` hat jetzt `category`, `name`, `infoDe`, `infoEn`, `values` (statt `nameDe`/`nameEn`).

## 2. Was der DSP vorgibt (gemessen, nicht geraten)
- **Frequenz:** Das Split-Band sitzt bei 1,25 × Freq, seine Unterkante bleibt ≥ 0,45 × Freq → bei 3 Oktaven wächst es
  nach oben in die Luft. Je höher die Frequenz, desto dunkler wird das S. Helle Stimmen (F01, F_badboy, F_runwild, J3;
  S-Schwerpunkt 8,2–9,9 kHz), −34 dB: 6 kHz/3 Okt. S-Kern −4,00 dB / Schwerpunkt −99 Hz; 6,5 kHz/2 Okt. −3,90 / −147 Hz;
  7,3 kHz/3 Okt. −4,25 / −305 Hz. **Darum liegen auch die „hellen“ Presets bei 6,2–6,6 kHz mit schmalerem Band
  (2–2,5 Okt.) statt bei 7–8,5 kHz** (dort in Runde 1 −360 … −785 Hz Schwerpunkt = Lispel-Gefahr).
- **Breite:** schmal = nur das S, Luft bleibt (Airy 1,5 Okt.: Luft −2,16 bei S-Kern −3,82; Transparent 1,8 Okt.:
  Schwerpunkt nur −36 Hz).
- **Tiefe Männerstimmen** (S-Spitze 4,5–5,5 kHz): 4,8–5,5 kHz; der Schwerpunkt bleibt ≥ 0 (S eher heller als dunkler).
- **Erkennung:** Relativ statt Smart nimmt Vokale mit (Rescue relativ: Vokal-Luft −0,75 dB, Reinheit 66 %) → alle
  Gesangs-Presets Smart; nur Mix / Bus Allround (auf Solostimmen ungeeignet: Reinheit 74 %).
- **Sharp T:** t/k-Knackser (< 30 ms) bremst das Smart-Tor (5-ms-Hüllkurven); die t/k-Absenkung ist bei jeder
  Einstellung ≈ 0,5 × S-Kern. Am besten (6,4 kHz/−38/Range 6): Attack 0,1 ms (−2,50 statt −2,37 bei 0,5 ms) und
  Lookahead 5 ms (3 ms −2,43, 7 ms −2,17, 10 ms −2,11); Release 15–25 ms war schlechter als 50–60 und flatterte mehr
  (0,44–0,49 Neu-Auslösungen pro S). Der Auftrag nannte „kurzes Release, viel Lookahead, hohe Frequenz“ – die Messung
  widerspricht, gewählt wurde nach Messung. Der Vorteil auf t/k ist klein, ehrlich gesagt.
- **Schnelles Release** (Genres) erhöht das Neu-Auslösen pro S (Club: Release 25–30 ms → 0,45–0,50, 40–45 ms →
  0,35–0,40); daher 30–45 ms statt 20–25.

## 3. Material
- 15 echte trockene Takes (`bench_merged\data\takes`), Gruppen nach S-Spitze: tief F_wings, M36, M40, M42, M_emerald;
  mittel F08, F22, J1, M_allittakes; hoch F01, F_badboy, F_runwild, J2, J3, M_notme. Männer M*, Frauen F*.
- **Genre-Material gibt es nicht.** Ersatz (`mkgenre.py`, je die ersten 90 s, liegt in `build\bench\data\genre`):
  - *club* (Hypertechno, EDM, Dance Pop, Hard Dance, Melodic Techno): F01, F22, F_runwild, J1, M_notme mit HP 120 Hz,
    Kompressor 8:1 (Attack 1 ms, Release 40 ms, 13–14 dB GR), +3 dB bei 3,5 kHz, +6 dB High-Shelf 8 kHz, Soft-Clip, −1 dBFS.
  - *trap* (Trap, Modern Hip-Hop): M36, M40, M_emerald, M_allittakes mit HP 80 Hz, 5:1 (3/80 ms, 8–10 dB GR), +4 dB Shelf 10 kHz.
  - Acoustic: trockene mittlere Takes. Mix / Bus: synthetischer Mix (`mix.py`: Take + lautes Rauschbett 60 Hz–2,5 kHz).
- Messung: `EmberTests --process … preset=<Name>` (lädt das Preset wie das Plugin), dann die `run.py`-Metriken
  (20-ms-Frames): S-Kern (±½ Okt. um die S-Spitze), Luft über dem S, S-Schwerpunkt, unberührte S (< 0,5 dB), S über
  8 dB, t/k-Knackser, Vokale Luft/Präsenz, Delta-Reinheit. dB negativ = abgesenkt, Mittel über das Material.

## 4. Die Presets (Werte und Messung)
Amount in %, Attack/Release/LA in ms, Breite in Oktaven.

| Preset | Kat. | Freq | Thr | Range | Amt | Att | Rel | LA | Breite | Erk. | Material | S-Kern | Luft | Schwerp. | unber. | >8 dB | t/k | Vokal Luft/Präs. | Rein |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Default | – | 6000 | -34 | 5 | 50 | 0.5 | 60 | 5 | 3 | Smart | alle 15 | -3.68 | -3.02 | -96 Hz | 9.9 % | 0.0 % | -1.83 | -0.00 / -0.00 | 99.4 % |
| Gentle Vocal | Basics | 6000 | -31 | 3.5 | 50 | 0.5 | 70 | 5 | 2.5 | Smart | alle 15 | -2.33 | -1.79 | -68 Hz | 17.2 % | 0.0 % | -1.18 | -0.00 / -0.00 | 99.5 % |
| Natural De-Essing | Basics | 6000 | -35.5 | 5.5 | 50 | 0.5 | 50 | 5 | 2.5 | Smart | alle 15 | -3.93 | -2.95 | -98 Hz | 7.6 % | 0.0 % | -1.94 | -0.00 / -0.00 | 99.4 % |
| Transparent | Basics | 6000 | -34.5 | 4.5 | 50 | 0.3 | 60 | 6 | 1.8 | Smart | alle 15 | -3.03 | -2.06 | -86 Hz | 8.4 % | 0.0 % | -1.48 | -0.00 / -0.00 | 99.4 % |
| Subtle Reduction | Basics | 6000 | -30 | 2.5 | 50 | 0.5 | 70 | 5 | 3 | Smart | alle 15 | -1.77 | -1.48 | -49 Hz | 21.0 % | 0.0 % | -0.91 | -0.00 / -0.00 | 99.5 % |
| Strong Sibilance | Basics | 6000 | -38 | 7.5 | 50 | 0.5 | 45 | 5 | 3 | Smart | alle 15 | -5.58 | -4.48 | -157 Hz | 3.9 % | 0.0 % | -2.67 | -0.00 / -0.00 | 99.3 % |
| Male Rap | Male | 5300 | -36.5 | 6 | 50 | 0.3 | 35 | 5 | 3 | Smart | M36, M40, M42, M_emerald, M_allittakes | -4.23 | -3.68 | +37 Hz | 5.7 % | 0.0 % | -1.41 | -0.00 / -0.00 | 99.2 % |
| Deep Male Vocal | Male | 4800 | -36 | 5 | 50 | 0.5 | 60 | 5 | 3 | Smart | M36, M40, M42, M_emerald | -3.55 | -3.10 | +122 Hz | 8.4 % | 0.0 % | -1.16 | -0.00 / -0.00 | 99.2 % |
| Bright Male Vocal | Male | 6600 | -34.5 | 5 | 50 | 0.5 | 55 | 5 | 2.5 | Smart | M_notme, M_allittakes | -4.10 | -2.57 | -86 Hz | 2.6 % | 0.0 % | -1.64 | -0.00 / -0.00 | 99.4 % |
| Aggressive Rap | Male | 5400 | -39 | 8 | 60 | 0.3 | 30 | 5 | 3 | Smart | M36, M40, M42, M_emerald, M_allittakes | -5.99 | -5.18 | +32 Hz | 2.5 % | 0.0 % | -1.96 | -0.00 / -0.00 | 99.1 % |
| Modern Pop Male | Male | 6300 | -35.5 | 5.5 | 50 | 0.5 | 45 | 5 | 2.5 | Smart | 6 Männer | -3.36 | -2.98 | -81 Hz | 8.5 % | 0.0 % | -1.28 | -0.00 / -0.00 | 99.4 % |
| Female Pop | Female | 6300 | -34.5 | 5 | 50 | 0.5 | 45 | 5 | 2.5 | Smart | 6 Frauen | -3.68 | -2.64 | -209 Hz | 10.9 % | 0.0 % | -1.68 | -0.00 / -0.00 | 99.6 % |
| Bright Female Vocal | Female | 6200 | -36 | 6 | 50 | 0.5 | 45 | 5 | 2.5 | Smart | F01, F_badboy, F_runwild | -4.50 | -2.72 | -235 Hz | 12.8 % | 0.0 % | -2.27 | -0.00 / -0.00 | 99.6 % |
| Airy Vocal | Female | 6000 | -34.5 | 5 | 50 | 0.5 | 50 | 5 | 1.5 | Smart | F01, F08, F22, F_badboy, F_runwild | -3.58 | -1.93 | -187 Hz | 12.1 % | 0.0 % | -1.75 | -0.00 / -0.00 | 99.6 % |
| Soft Female Vocal | Female | 6200 | -31 | 3.5 | 50 | 0.7 | 80 | 5 | 2.5 | Smart | F01, F08, F22, F_wings | -2.39 | -1.88 | -161 Hz | 14.8 % | 0.0 % | -1.28 | -0.00 / -0.00 | 99.7 % |
| Powerful Female Vocal | Female | 6000 | -37.5 | 7 | 50 | 0.4 | 40 | 5 | 3 | Smart | 6 Frauen | -5.48 | -4.13 | -218 Hz | 6.4 % | 0.0 % | -2.49 | -0.00 / -0.00 | 99.6 % |
| Hypertechno | Genre | 6400 | -37 | 7 | 50 | 0.3 | 40 | 5 | 3 | Smart | F01 club, F22 club, F_runwild club, J1_chemicals club, M_notme club | -5.98 | -4.45 | -213 Hz | 3.5 % | 0.0 % | -2.52 | -0.00 / -0.00 | 99.3 % |
| EDM Lead Vocal | Genre | 6300 | -36 | 6 | 50 | 0.4 | 45 | 5 | 3 | Smart | F01 club, F22 club, F_runwild club, J1_chemicals club, M_notme club | -5.17 | -3.87 | -147 Hz | 3.8 % | 0.0 % | -2.24 | -0.00 / -0.00 | 99.3 % |
| Dance Pop | Genre | 6200 | -35.5 | 5.5 | 50 | 0.4 | 35 | 5 | 2.5 | Smart | F01 club, F22 club, F_runwild club, J1_chemicals club, M_notme club | -4.47 | -2.93 | -101 Hz | 4.0 % | 0.0 % | -1.97 | -0.00 / -0.00 | 99.3 % |
| Hard Dance | Genre | 6200 | -38.5 | 8 | 55 | 0.2 | 30 | 5 | 3 | Smart | F01 club, F22 club, F_runwild club, J1_chemicals club, M_notme club | -7.04 | -5.08 | -244 Hz | 2.2 % | 0.0 % | -2.92 | -0.00 / -0.00 | 99.2 % |
| Melodic Techno | Genre | 6100 | -34.5 | 5 | 50 | 0.6 | 80 | 5 | 2.5 | Smart | F01 club, F22 club, F_runwild club, J1_chemicals club, M_notme club | -4.10 | -2.69 | -65 Hz | 4.0 % | 0.0 % | -1.82 | -0.00 / -0.00 | 99.3 % |
| Trap Vocals | Genre | 5400 | -37 | 6.5 | 50 | 0.3 | 30 | 5 | 3 | Smart | M36 trap, M40 trap, M_emerald trap, M_allittakes trap | -5.03 | -4.50 | +27 Hz | 2.8 % | 0.0 % | -1.90 | -0.00 / -0.00 | 98.6 % |
| Modern Hip-Hop | Genre | 5500 | -35.5 | 5.5 | 50 | 0.4 | 45 | 5 | 3 | Smart | M36 trap, M40 trap, M_emerald trap, M_allittakes trap | -4.29 | -3.92 | +5 Hz | 5.0 % | 0.0 % | -1.60 | -0.00 / -0.00 | 98.7 % |
| Acoustic Vocals | Genre | 6300 | -31.5 | 3.5 | 45 | 0.6 | 90 | 5 | 2.5 | Smart | F08, F22, J1_chemicals, M_allittakes, F_wings | -2.37 | -1.87 | -93 Hz | 10.8 % | 0.0 % | -0.98 | -0.00 / -0.00 | 99.5 % |
| Harsh S Sounds | Special | 6000 | -38 | 8 | 50 | 0.3 | 45 | 5 | 2.5 | Smart | alle 15 | -5.60 | -4.07 | -163 Hz | 3.5 % | 0.0 % | -2.69 | -0.00 / -0.00 | 99.3 % |
| Extreme Sibilance | Special | 6000 | -42 | 10 | 65 | 0.3 | 45 | 5 | 3 | Smart | alle 15 | -8.48 | -6.58 | -312 Hz | 1.2 % | 50.9 % | -3.88 | -0.00 / -0.00 | 99.2 % |
| Sharp T Sounds | Special | 6400 | -38 | 6 | 50 | 0.1 | 50 | 5 | 3 | Smart | alle 15 | -4.92 | -4.14 | -159 Hz | 3.2 % | 0.0 % | -2.44 | -0.00 / -0.00 | 99.3 % |
| Vocal Recording Rescue | Special | 6000 | -39.5 | 10 | 50 | 0.2 | 55 | 8 | 3 | Smart | alle 15 | -7.26 | -5.71 | -261 Hz | 1.9 % | 22.9 % | -2.87 | -0.00 / -0.00 | 99.1 % |
| Mix / Bus | Special | 6000 | -41 | 4.5 | 50 | 0.5 | 45 | 5 | 3 | Allround | synth. Mix (15) | -2.76 | – | – | – | – | – | -0.02 / -0.01 | – |

**Abnahme (2026-11-24, mit S-Tracking):** Vokale Luft/Präsenz bei allen Presets 0,00 dB (Mix / Bus im Mix 0,02 /
0,01), Reinheit ≥ 98,6 %, VokGR % unverändert. Über 8 dB nur, wo gewollt: Extreme 51 %, Rescue 23 % (großer Range für
sehr harte Aufnahmen: die lautesten S werden tief abgesenkt, die Vokale bleiben unberührt), sonst 0 (Aggressive Rap
und Hard Dance vorher 5,8 / 1,9 %, siehe Abschnitt 8).
S-Schwerpunkt: mit S-Tracking werden die S bei gleicher Absenkung etwas dunkler (das Band rückt zum S, unterhalb des
S wird weniger abgesenkt; Pro-DS Standard auf J1–J3: −145 Hz bei −3,7 dB ≈ 39 Hz/dB). Am stärksten Extreme −312
(−8,5 dB, 37 Hz/dB), Rescue −261 (36 Hz/dB), Hard Dance −244 (35 Hz/dB), Bright Female −235 (52 Hz/dB), Powerful
Female −218 (40 Hz/dB). Über Pro-DS je dB liegen nur die hellen Frauen-Presets (Bright Female 52, Female Pop 57,
Airy 52, Soft Female 67 Hz/dB) – auf den hellsten Takes (S-Spitze 8–10 kHz). Die Männer-Presets werden heller
als vorher (Male Rap +37, Aggressive +32, Deep Male +122 Hz).
Leise Reibelaute (f, weiches s) verlieren bei den tiefen Männer-Presets 1,7–2,8 dB (Deep Male, Rap, Aggressive, Trap) –
Folge des tiefen Bands (Default 0,8 dB).

**Stärke-Reihenfolge** (alle 15 Takes, S-Kern): Subtle −1,77 < Gentle −2,33 < Transparent −3,03 < Default −3,68 <
Natural −3,93 < Strong −5,58 ≈ Harsh −5,60 < Rescue −7,26 < Extreme −8,48 (der Test prüft Subtle < Gentle < Natural <
Strong < Rescue < Extreme über die entfernte Energie aller Takes: +2,2 / +5,6 / +7,8 / +9,4 / +10,2 dB gegen Subtle).
Genre (club): Melodic Techno −4,10 < Dance Pop −4,47 < EDM −5,17 < Hypertechno −5,98 < Hard Dance −7,04; trap:
Hip-Hop −4,29 < Trap −5,03. Männer: Deep Male −3,55 (tiefe Takes), Rap −4,23, Aggressive Rap −5,99.

## 5. Standard-Einstellung (Teil 3)
Alle 15 Takes, Smart, Range 5, Attack 0,5, Release 60, Lookahead 5, Breite 3 (S-Kern dB / unberührte S):

| Kandidat | tief (5) | mittel (4) | hoch (6) | alle | Schwerp. alle | leise Reibel. |
|---|---|---|---|---|---|---|
| A 6,2 kHz / −32 (alt) | −2,14 / 21,6 % | −4,10 / 9,2 % | −3,90 / 11,0 % | −3,37 / 14,1 % | −127 Hz | −0,60 |
| B 6,2 / −33 | −2,47 / 20,2 % | −4,29 | −4,07 | −3,59 / 12,9 % | −120 | −0,70 |
| C 6,2 / −34 | −2,78 / 14,6 % | −4,42 | −4,21 | −3,79 / 10,0 % | −112 | −0,79 |
| E 6,2 / −33, Range 6 | −2,58 | −4,87 | −4,62 | −4,01 / 12,8 % | −156 | −0,73 |
| G 6,2 / −35 | −3,06 / 11,5 % | −4,52 | −4,31 | −3,95 / 8,2 % | −103 | −0,88 |
| I 5,9 / −33 | −2,58 / 19,7 % | −4,28 | −3,98 | −3,59 / 12,3 % | −75 | −0,72 |
| F 5,9 / −34 | −2,90 / 14,8 % | −4,42 | −4,12 | −3,79 / 9,9 % | −64 | −0,82 |
| **H 6,0 / −34 (neu)** | **−2,86 / 14,6 %** | **−4,42 / 7,1 %** | **−4,15 / 7,8 %** | **−3,79 / 9,9 %** | **−80** | **−0,81** |
| J 5,9 / −34, Range 5,5 | −3,01 | −4,77 | −4,44 | −4,05 / 9,9 % | −78 | −0,85 |
| K 5,9 / −34, Release 50 | −2,83 | −4,37 | −4,09 | −3,74 / 10,1 % | −67 | −0,78 |
| L 5,9 / −34, Release 80 | −2,99 | −4,48 | −4,17 | −3,86 / 9,3 % | −60 | −0,88 |
| M 6,0 / −35 | −3,14 / 10,0 % | −4,52 | −4,26 | −3,96 / 7,6 % | −71 | −0,90 |

Entscheidung **6 kHz / −34 dB** (Rest unverändert): tiefe Stimmen 0,7 dB mehr (M42 unberührte S 42 → 27 %, M40 −2,00 →
−2,74), helle nur 0,25 dB mehr (stärkster Take F22 −4,62; helle Frauen F01 −4,01, F_badboy −4,34, F_runwild −3,56),
S-Farbe weniger verschoben (−80 statt −127 Hz), Vokale 0,00, Reinheit 99,4 %. −35 dB (M) wäre für tiefe Stimmen noch
besser, nähme hellen Stimmen aber deutlich mehr als Pro-DS (−3,71 auf J1–J3). Release 50/80 und Range 5,5–6 brachten
keinen klaren Vorteil (mehr leise Reibelaute weg bzw. dunkler).
Geändert: `voiceMidHz` 6200 → 6000, `voiceMidThrDb` −32 → −34 (MITTEL = Standard, damit „Threshold geht mit, solange er
auf einem Stimmlagen-Wert steht“ weiter gilt), Infotext MITTEL, Handbücher. **Nicht geändert:** der `Settings`-Standard
in `EmberEngine.h` / `EmberAuto.h` (6,2 kHz / −32; nur Vorgabe für Tests und `--process` ohne Preset – das Plugin liest
die Parameter). r2-research arbeitet an der Engine; beim Zusammenführen angleichen.

**Stimmlagen-Knöpfe** nachgemessen: TIEF (5 kHz / −35) auf tiefen Stimmen −3,43 dB (MITTEL neu −2,86), HOCH
(7,4 kHz / −33) auf hohen −4,24 (MITTEL neu −4,15) → jeder Knopf bleibt der stärkste für seine Gruppe; Werte
unverändert (TIEF −36/−37: −3,68/−3,90 aber mehr leise Reibelaute weg; HOCH −34/−35: −4,36/−4,45, dunkler).
Ein Wert passt nicht für alle: Default ist der Kompromiss, TIEF/HOCH zielen auf die Stimmgruppe, AUTO stellt Frequenz,
Threshold und Range auf die einzelne Stimme ein.

## 6. Tests, Hör-Set
- Neu in `EmberTests`: „factory preset library“ (29 Presets, Default = Startzustand, eindeutige Namen, Texte DE/EN,
  jeder Wert eine Einstellung in ihrem Bereich, nach Kategorie sortiert, jede Kategorie belegt, kein Threshold auf einem
  Stimmlagen-Wert, ‹ › in Kategorie-Reihenfolge); in den Echt-Takes: alle 29 Presets × 15 Takes (je 20 s) endlich und
  die Stärke-Leiter. `EmberTests --process … preset=<Name|Index>` (`_` für Leerzeichen).
- 1612 Checks grün mit `EMBER_VOCALS` (15 of 15 takes set up), pluginval Strenge 10 `--validate-in-process`: SUCCESS.
  2026-11-24 (S-Tracking + nachgestellte Presets): **2005 bestanden, 0 fehlgeschlagen** (QA-Teil 381, 15 of 15 takes),
  pluginval 10 SUCCESS.
- Hör-Set: `C:\Users\Shadow\dev\velvox-bench\2026-11-24\presets` – je Preset ein passender Take, 20 s mit den meisten S,
  bearbeitet + `_DELTA` (`<Kategorie>_<Preset>__<Take>.wav`; Genre auf den club/trap-Versionen, Mix / Bus im Mix).
  **Neu (S-Tracking, nachgestellte Presets):** `C:\Users\Shadow\dev\velvox-bench\2026-11-24\presets_v2` (gleiche Takes,
  Ausschnitte und Namen; `listen.py <Ordner>`). Der alte Ordner bleibt zum Vergleich.

## 7. Nicht auf echtem Material geprüft
- Genre-Presets nur auf nachgebauten (komprimiert + aufgehellt) Versionen trockener Takes, nicht auf echten
  Hypertechno-/EDM-/Trap-Produktionen; Hall/Delay (Melodic Techno) gar nicht.
- Mix / Bus nur im synthetischen Mix (Rauschbett), kein echter Mix, Bus oder Instrument.
- Sharp T: kein Material mit gezielt harten t/k; gemessen nur die t/k-Knackser der 15 Takes.
- Kein Hörurteil: das Hör-Set liegt bereit (`presets_v2` = Stand mit S-Tracking).

## 8. Nachmessung mit S-Tracking (2026-11-24, Branch `feature/ember-fixes`)
Nach dem Merge von r2/ember-research (S-Tracking, `Settings::trackS` an; exakte Split-Tiefe an der Bandmitte) alle
29 Presets neu gemessen, **gleiche Methode und gleiche Takes** wie oben: `runp.py` (`EmberTests --process … preset=<Name>`,
lädt das Preset wie das Plugin), `run.py`-Metriken, gleiches Material je Preset. Bench = Kopie der r2-presets-Bench in
`build\bench` dieses Worktrees (Skripte, Takes, Genre- und Mix-Material, Klassen-Dateien unverändert). Gegenprobe: die
alte Preset-Exe (`r2-presets\build\bench\ember_presets.exe`) liefert in der Kopie **exakt** die Werte der alten Tabelle.
Endmessung: `tools/bench/ember_presets/final_measurements.txt`; neu dort `var.py` (Varianten eines Presets) und
`cmp.py` (zwei Messungen vergleichen).

**Was das S-Tracking an den alten Presets verschob** (alle 29, ohne Nachstellen):
- S-Kern 0,04–0,78 dB weniger (Default −3,79 → −3,68), Luft über dem S 0,1–1,0 dB weniger abgesenkt (die Luft bleibt
  mehr), Reinheit gleich oder +0,1 %, VokGR % und Vokal-Luft/-Präsenz unverändert (0,00), unberührte S fast gleich.
- Über 8 dB deutlich seltener: Extreme 54 → 31 %, Rescue 32 → 19 %, Aggressive Rap 5,8 → 0, Hard Dance 1,9 → 0.
- S-Schwerpunkt: Frauen- und club-Presets dunkler (bis −143 Hz), Männer-Presets heller (bis +78 Hz). Grund: das Band
  rückt zum S und senkt unterhalb des S weniger ab (siehe DSP_FORSCHUNG.md, „drunter“ −2,18 → −1,92).
- **Schmale Bänder verlieren am meisten** (Harsh S 2 Okt. −0,78, Bright Female 2 Okt. −0,56, Transparent 1,8 Okt.
  −0,33): die drei Tracking-Glocken sind je Breite − 1 Okt. breit, mindestens 1 Okt. Unter 2 Okt. ändert die Breite
  daher nichts mehr (Transparent 1,8 → 2,0 gemessen: S-Kern gleich −3,03); Abschnitt 2 „Breite“ gilt nur noch über 2 Okt.

**Regel fürs Nachstellen** (so wenig wie möglich, nur was aus seinem Zweck gedriftet ist): (a) S-Kern mehr als 0,3 dB
schwächer, (b) die gewollte Tiefe über 8 dB verloren (Extreme, Rescue), (c) S-Schwerpunkt mehr als 50 Hz dunkler als
vorher und dunkler als 50 Hz je dB Absenkung (Lispel-Gefahr). Bevorzugt ein Wert; Varianten je Preset in `var.py`
gemessen (Threshold wirkt mit S-Tracking kaum, Breite und Frequenz wirken stark).

### 8.1 Geänderte Presets (alt → neu)
| Preset | Änderung | Grund | S-Kern alt / nur S-Tracking / neu | Schwerp. alt / ST / neu | weitere |
|---|---|---|---|---|---|
| Harsh S Sounds | Breite 2 → 2,5 | (a) −0,78 dB | −5,67 / −4,89 / **−5,60** | −103 / −170 / −163 Hz | t/k −2,78 / −2,38 / −2,69; Luft/Kern 0,81 / 0,70 / 0,76 (Luft bleibt mehr als bei Default 0,82) |
| Extreme Sibilance | Breite 2,5 → 3 | (a) −0,67 dB, (b) | −8,47 / −7,80 / **−8,48** | −219 / −276 / −312 Hz | über 8 dB 54 / 31 / **51 %**; 37 Hz/dB ≈ Pro-DS. Range 12 statt Breite: −8,48, aber −361 Hz |
| Sharp T Sounds | Breite 2,5 → 3 | (a) −0,34 dB | −4,97 / −4,63 / **−4,92** | −155 / −171 / −159 Hz | t/k (sein Zweck) −2,49 / −2,31 / **−2,44**; Threshold −39 brachte nur −0,05 |
| Vocal Recording Rescue | Threshold −38,5 → −39,5 | (b) über 8 dB 32 → 19 %, S-Kern −0,29 | −7,32 / −7,03 / **−7,26** | −234 / −264 / −261 Hz | über 8 dB 32 / 19 / **23 %**; Range 11 wäre −7,31 / 27 %, aber −304 Hz |
| Bright Female Vocal | Freq 6400 → 6200, Breite 2 → 2,5 | (a) −0,56 dB, 63 Hz/dB | −4,66 / −4,10 / **−4,50** | −214 / −261 / **−235 Hz** | unberührt 14,8 / 15,0 / 12,8 %; Luft/Kern 0,62 / 0,49 / 0,60. Nur Breite: −4,54 aber −266 Hz; 6 kHz: −202 Hz, aber kein „hoher“ S-Bereich mehr |
| Powerful Female Vocal | Freq 6200 → 6000, Breite 2,5 → 3 | (a) −0,37 dB, (c) +68 Hz | −5,54 / −5,17 / **−5,48** | −209 / −277 / **−218 Hz** | unberührt 7,4 / 7,4 / 6,4 %; nur Breite 3: −5,47 aber −267 Hz |
| Airy Vocal | Freq 6200 → 6000 | (c) +88 Hz, 61 Hz/dB | −3,82 / −3,61 / **−3,58** | −131 / −219 / **−187 Hz** | S-Kern bleibt; Luft/Kern 0,56 / 0,55 / 0,54 (Luft bleibt). Breite 1,5 ändert nichts (s. o.) |

Texte (Kurzbeschreibungen) passen weiter: Harsh 2,5 Okt. und Bright Female 6,2 kHz / 2,5 Okt. sind noch schmaler bzw.
höher als Default. Alle Regeln aus Abschnitt 1 eingehalten (keine Frequenz bei 5,75 / 7 kHz, kein Threshold −35/−34/−33).

### 8.2 Gedriftet, aber gelassen
| Preset | alt → mit S-Tracking | warum gelassen |
|---|---|---|
| Transparent | S-Kern −3,36 → −3,03 (−0,33), Schwerp. −36 → −86 Hz | Grenzfall von (a). Der einzige wirksame Hebel (Range 5: −3,28) färbt dunkler (−101 Hz) – gegen den Zweck „kaum Färbung“; Breite 2 statt 1,8 wirkt nicht, Threshold −35,5 nur −0,07. Rolle bleibt: zwischen Gentle und Default |
| Aggressive Rap | −6,28 → −5,99 (−0,29), über 8 dB 5,8 → 0 % | unter 0,3 dB, bleibt klar stärker als Male Rap (−4,23); Schwerp. −46 → +32 Hz (heller). Falls gewünscht: Range 8 → 9 = −6,28 / 9,4 % über 8 dB |
| Hard Dance | −7,31 → −7,04, über 8 dB 1,9 → 0 %, Schwerp. −100 → −244 Hz | unter 0,3 dB; 35 Hz/dB (unter Pro-DS 39); stärkstes Genre-Preset bleibt |
| Bright Male Vocal | −4,39 → −4,10 (−0,29) | unter 0,3 dB; Luft −3,60 → −2,57 (bleibt mehr), Schwerp. heller (−111 → −86 Hz) |
| Modern Pop Male, Trap Vocals, Natural, Hypertechno, Female Pop, Hip-Hop | −0,23 … −0,28 dB | unter 0,3 dB. Female Pop −172 → −209 Hz (+37, unter 50): Freq 6300 → 6100 brächte −174 Hz bei gleichem S-Kern (Option) |
| Dance Pop, Melodic Techno, EDM Lead | Schwerp. +11 → −101, +41 → −65, −66 → −147 Hz | S-Kern nur −0,17 … −0,24; 16–28 Hz/dB, also weniger dunkel je dB als Pro-DS |
| Soft Female Vocal | −2,52 → −2,39, Schwerp. −135 → −161 Hz | +26 Hz (unter 50); Freq 6000 brächte −135 Hz bei gleichem S-Kern (Option) |
| alle übrigen | ≤ 0,2 dB | ohne Befund |

### 8.3 Default und Stimmlagen-Knöpfe nachgemessen
Default (= MITTEL, 6 kHz / −34) **unverändert**: alle 15 −3,68 dB / 9,9 % unberührt / −96 Hz (vorher −3,79 / 9,9 % / −80);
je Gruppe (S-Kern / unberührt): tief −2,73 / 14,6 % (vorher −2,86 / 14,6), mittel −4,27 / 7,1 % (−4,42 / 7,1), hoch
−4,08 / 7,8 % (−4,15 / 7,8). Die Verschiebung ist über alle Gruppen gleich (−0,07 … −0,15 dB), die Abwägung aus
Abschnitt 5 bleibt.
Stimmlagen-Knöpfe: TIEF (5 kHz / −35) auf tiefen Stimmen −3,29 (MITTEL −2,73) → bleibt der stärkste für seine Gruppe.
**HOCH (7,4 kHz / −33) auf hohen Stimmen −4,08 = MITTEL −4,08**, aber dunkler (−279 statt −118 Hz) und mehr unberührte
S (11,1 statt 7,8 %) – vorher war HOCH 0,09 dB stärker. 7,1 / 7,2 kHz ändern daran nichts (−4,07 / −4,08, −244 / −255 Hz).
Nicht geändert (kein Preset, ändert Stimmlagen-Logik und AUTO-Bezug) → offener Punkt.

### 8.4 CPU
Engine mit S-Tracking ≈ **0,37 % eines Kerns** (vorher 0,28 %; `cpu.py`, J1 stereo 44,1 kHz, Messung aus
DSP_FORSCHUNG.md). Die Presets ändern daran nichts (gleiche Engine-Pfade).
