# EMBER – Werks-Presets und Standard-Einstellung (r2, 2026-11-23, Branch `r2/ember-presets`)

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
| Default | – | 6000 | -34 | 5 | 50 | 0.5 | 60 | 5 | 3 | Smart | alle 15 | -3.79 | -3.34 | -80 Hz | 9.9 % | 0.0 % | -1.90 | -0.00 / -0.00 | 99.4 % |
| Gentle Vocal | Basics | 6000 | -31 | 3.5 | 50 | 0.5 | 70 | 5 | 2.5 | Smart | alle 15 | -2.45 | -2.06 | -50 Hz | 16.7 % | 0.0 % | -1.25 | -0.00 / -0.00 | 99.5 % |
| Natural De-Essing | Basics | 6000 | -35.5 | 5.5 | 50 | 0.5 | 50 | 5 | 2.5 | Smart | alle 15 | -4.17 | -3.48 | -64 Hz | 7.5 % | 0.0 % | -2.07 | -0.00 / -0.00 | 99.3 % |
| Transparent | Basics | 6000 | -34.5 | 4.5 | 50 | 0.3 | 60 | 6 | 1.8 | Smart | alle 15 | -3.36 | -2.60 | -36 Hz | 7.8 % | 0.0 % | -1.67 | -0.00 / -0.00 | 99.4 % |
| Subtle Reduction | Basics | 6000 | -30 | 2.5 | 50 | 0.5 | 70 | 5 | 3 | Smart | alle 15 | -1.81 | -1.61 | -43 Hz | 20.9 % | 0.0 % | -0.94 | -0.00 / -0.00 | 99.5 % |
| Strong Sibilance | Basics | 6000 | -38 | 7.5 | 50 | 0.5 | 45 | 5 | 3 | Smart | alle 15 | -5.78 | -5.04 | -130 Hz | 4.0 % | 0.0 % | -2.79 | -0.00 / -0.00 | 99.3 % |
| Male Rap | Male | 5300 | -36.5 | 6 | 50 | 0.3 | 35 | 5 | 3 | Smart | M36, M40, M42, M_emerald, M_allittakes | -4.41 | -4.16 | -4 Hz | 5.7 % | 0.0 % | -1.48 | -0.00 / -0.00 | 99.2 % |
| Deep Male Vocal | Male | 4800 | -36 | 5 | 50 | 0.5 | 60 | 5 | 3 | Smart | M36, M40, M42, M_emerald | -3.68 | -3.40 | +78 Hz | 8.4 % | 0.0 % | -1.22 | -0.00 / -0.00 | 99.1 % |
| Bright Male Vocal | Male | 6600 | -34.5 | 5 | 50 | 0.5 | 55 | 5 | 2.5 | Smart | M_notme, M_allittakes | -4.39 | -3.60 | -111 Hz | 2.6 % | 0.0 % | -1.78 | -0.00 / -0.00 | 99.4 % |
| Aggressive Rap | Male | 5400 | -39 | 8 | 60 | 0.3 | 30 | 5 | 3 | Smart | M36, M40, M42, M_emerald, M_allittakes | -6.28 | -6.01 | -46 Hz | 2.5 % | 5.8 % | -2.06 | -0.00 / -0.00 | 99.1 % |
| Modern Pop Male | Male | 6300 | -35.5 | 5.5 | 50 | 0.5 | 45 | 5 | 2.5 | Smart | 6 Männer | -3.63 | -3.68 | -128 Hz | 8.5 % | 0.0 % | -1.38 | -0.00 / -0.00 | 99.4 % |
| Female Pop | Female | 6300 | -34.5 | 5 | 50 | 0.5 | 45 | 5 | 2.5 | Smart | 6 Frauen | -3.91 | -3.12 | -172 Hz | 11.1 % | 0.0 % | -1.81 | -0.00 / -0.00 | 99.6 % |
| Bright Female Vocal | Female | 6400 | -36 | 6 | 50 | 0.5 | 45 | 5 | 2 | Smart | F01, F_badboy, F_runwild | -4.66 | -2.91 | -214 Hz | 14.8 % | 0.0 % | -2.39 | -0.00 / -0.00 | 99.6 % |
| Airy Vocal | Female | 6200 | -34.5 | 5 | 50 | 0.5 | 50 | 5 | 1.5 | Smart | F01, F08, F22, F_badboy, F_runwild | -3.82 | -2.16 | -131 Hz | 12.6 % | 0.0 % | -1.92 | -0.00 / -0.00 | 99.6 % |
| Soft Female Vocal | Female | 6200 | -31 | 3.5 | 50 | 0.7 | 80 | 5 | 2.5 | Smart | F01, F08, F22, F_wings | -2.52 | -2.15 | -135 Hz | 13.5 % | 0.0 % | -1.37 | -0.00 / -0.00 | 99.7 % |
| Powerful Female Vocal | Female | 6200 | -37.5 | 7 | 50 | 0.4 | 40 | 5 | 2.5 | Smart | 6 Frauen | -5.54 | -4.29 | -209 Hz | 7.4 % | 0.0 % | -2.53 | -0.00 / -0.00 | 99.6 % |
| Hypertechno | Genre | 6400 | -37 | 7 | 50 | 0.3 | 40 | 5 | 3 | Smart | F01 club, F22 club, F_runwild club, J1_chemicals club, M_notme club | -6.21 | -5.03 | -123 Hz | 3.2 % | 0.0 % | -2.63 | -0.00 / -0.00 | 99.2 % |
| EDM Lead Vocal | Genre | 6300 | -36 | 6 | 50 | 0.4 | 45 | 5 | 3 | Smart | F01 club, F22 club, F_runwild club, J1_chemicals club, M_notme club | -5.34 | -4.33 | -66 Hz | 3.8 % | 0.0 % | -2.33 | -0.00 / -0.00 | 99.2 % |
| Dance Pop | Genre | 6200 | -35.5 | 5.5 | 50 | 0.4 | 35 | 5 | 2.5 | Smart | F01 club, F22 club, F_runwild club, J1_chemicals club, M_notme club | -4.71 | -3.46 | +11 Hz | 4.0 % | 0.0 % | -2.09 | -0.00 / -0.00 | 99.2 % |
| Hard Dance | Genre | 6200 | -38.5 | 8 | 55 | 0.2 | 30 | 5 | 3 | Smart | F01 club, F22 club, F_runwild club, J1_chemicals club, M_notme club | -7.31 | -5.74 | -100 Hz | 2.2 % | 1.9 % | -3.04 | -0.00 / -0.00 | 99.1 % |
| Melodic Techno | Genre | 6100 | -34.5 | 5 | 50 | 0.6 | 80 | 5 | 2.5 | Smart | F01 club, F22 club, F_runwild club, J1_chemicals club, M_notme club | -4.30 | -3.16 | +41 Hz | 4.0 % | 0.0 % | -1.93 | -0.00 / -0.00 | 99.2 % |
| Trap Vocals | Genre | 5400 | -37 | 6.5 | 50 | 0.3 | 30 | 5 | 3 | Smart | M36 trap, M40 trap, M_emerald trap, M_allittakes trap | -5.31 | -5.14 | -19 Hz | 2.8 % | 0.0 % | -1.99 | -0.00 / -0.00 | 98.5 % |
| Modern Hip-Hop | Genre | 5500 | -35.5 | 5.5 | 50 | 0.4 | 45 | 5 | 3 | Smart | M36 trap, M40 trap, M_emerald trap, M_allittakes trap | -4.52 | -4.44 | -30 Hz | 5.0 % | 0.0 % | -1.68 | -0.00 / -0.00 | 98.6 % |
| Acoustic Vocals | Genre | 6300 | -31.5 | 3.5 | 45 | 0.6 | 90 | 5 | 2.5 | Smart | F08, F22, J1_chemicals, M_allittakes, F_wings | -2.52 | -2.20 | -83 Hz | 10.4 % | 0.0 % | -1.05 | -0.00 / -0.00 | 99.5 % |
| Harsh S Sounds | Special | 6000 | -38 | 8 | 50 | 0.3 | 45 | 5 | 2 | Smart | alle 15 | -5.67 | -4.39 | -103 Hz | 3.8 % | 0.0 % | -2.78 | -0.00 / -0.00 | 99.3 % |
| Extreme Sibilance | Special | 6000 | -42 | 10 | 65 | 0.3 | 45 | 5 | 2.5 | Smart | alle 15 | -8.47 | -6.89 | -219 Hz | 1.2 % | 54.0 % | -3.94 | -0.00 / -0.00 | 99.2 % |
| Sharp T Sounds | Special | 6400 | -38 | 6 | 50 | 0.1 | 50 | 5 | 2.5 | Smart | alle 15 | -4.97 | -4.40 | -155 Hz | 3.2 % | 0.0 % | -2.49 | -0.00 / -0.00 | 99.3 % |
| Vocal Recording Rescue | Special | 6000 | -38.5 | 10 | 50 | 0.2 | 55 | 8 | 3 | Smart | alle 15 | -7.32 | -6.32 | -234 Hz | 2.5 % | 31.7 % | -2.91 | -0.00 / -0.00 | 99.1 % |
| Mix / Bus | Special | 6000 | -41 | 4.5 | 50 | 0.5 | 45 | 5 | 3 | Allround | synth. Mix (15) | -2.87 | – | – | – | – | – | -0.03 / -0.01 | – |

**Abnahme:** Vokale Luft/Präsenz bei allen Presets 0,00 dB (Mix / Bus im Mix 0,03 / 0,01), Reinheit ≥ 98,5 %.
Über 8 dB nur, wo gewollt: Extreme 54 %, Rescue 32 % (großer Range für sehr harte Aufnahmen: die lautesten S werden
tief abgesenkt, die Vokale bleiben unberührt), Aggressive Rap 5,8 %, Hard Dance 1,9 %, sonst 0.
S-Schwerpunkt (Pro-DS Standard auf J1–J3: −145 Hz bei −3,7 dB): am stärksten Rescue −234, Extreme −219, Bright Female
−214, Powerful Female −209 Hz – bei 4,7–8,5 dB Absenkung, pro dB also nicht dunkler als Pro-DS.
Leise Reibelaute (f, weiches s) verlieren bei den tiefen Männer-Presets 1,8–3,0 dB (Deep Male, Rap, Aggressive, Trap) –
Folge des tiefen Bands (Default 0,8 dB). Melodic Techno und Dance Pop werden leicht heller (+41 / +11 Hz).

**Stärke-Reihenfolge** (alle 15 Takes, S-Kern): Subtle −1,81 < Gentle −2,45 < Transparent −3,36 < Default −3,79 <
Natural −4,17 < Strong −5,78 < Rescue −7,32 < Extreme −8,47 (der Test prüft Subtle < Gentle < Natural < Strong < Rescue
< Extreme über die entfernte Energie aller Takes: +2,3 / +5,8 / +7,8 / +9,2 / +10,0 dB gegen Subtle). Genre (club):
Melodic Techno −4,30 < Dance Pop −4,71 < EDM −5,34 < Hypertechno −6,21 < Hard Dance −7,31; trap: Hip-Hop −4,52 <
Trap −5,31. Männer: Deep Male −3,68 (tiefe Takes), Rap −4,41, Aggressive Rap −6,28.

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
- Hör-Set: `C:\Users\Shadow\dev\velvox-bench\2026-11-24\presets` – je Preset ein passender Take, 20 s mit den meisten S,
  bearbeitet + `_DELTA` (`<Kategorie>_<Preset>__<Take>.wav`; Genre auf den club/trap-Versionen, Mix / Bus im Mix).

## 7. Nicht auf echtem Material geprüft
- Genre-Presets nur auf nachgebauten (komprimiert + aufgehellt) Versionen trockener Takes, nicht auf echten
  Hypertechno-/EDM-/Trap-Produktionen; Hall/Delay (Melodic Techno) gar nicht.
- Mix / Bus nur im synthetischen Mix (Rauschbett), kein echter Mix, Bus oder Instrument.
- Sharp T: kein Material mit gezielt harten t/k; gemessen nur die t/k-Knackser der 15 Takes.
- Kein Hörurteil: das Hör-Set liegt bereit.
