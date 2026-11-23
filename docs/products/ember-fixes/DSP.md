# EMBER – DSP-Fixes und Pro-DS-Niveau (2026-11-23, Branch `fix/ember-dsp`)

Jakob: „mach alle fixes … verbessere den algorithmus um auf das selbe level wie der fabfilter ds zu kommen“.
Code: `Products/Ember/Source/EmberEngine.*`, `EmberParams.*`, `EmberPresets.cpp`, kleine Ergänzungen in `EmberEditor.*`.
Gemessen mit `EmberTests --process` über 15 echte Takes, Skripte in `tools/bench/ember_sound` (`run.py`, `mix.py`,
`cpu.py`, `zip.py`, `lev.py`). Pro-DS-Vergleich nur J1–J3 (Pro-DS Standard, sample-genau).

## Was sich geändert hat

1. **Split-Band ohne Stufen (Fix 1).** Statt die Glocken-Koeffizienten alle 16 Samples neu zu rechnen, läuft eine
   *feste* Glocke (−12 dB) die ganze Zeit; pro Sample wird nur gemischt: `y = x + c·(glocke(x) − x)`.
   Die Absenkung ändert sich damit glatt bei jedem Sample, `c = 0` ist exakt das Original (bit-genauer Null-Test).
   Analog entspricht das einer Regalia-Mitra-Glocke mit festen Polen. Zipper-Test (Ton in der Bandmitte, schwellender
   Pegel): Seitenbänder bei fs/16 vorher −108 dB, jetzt −132 dB unter dem Ton.
2. **Gleiche Form bei 44,1–192 kHz (Fix 2).** Die feste Glocke ist das „matched“-Design aus `EqDesign` (Vicanek), kein
   bilinearer Peak mehr. Abweichung zu 192 kHz: ≤ 0,22 dB bis 12 kHz, ≤ 0,23 dB bis 18 kHz (alte Glocke: 2,7 dB).
3. **Breitband ohne Flattern (Fix 3).** Das Smart-Gate wird mit gleitendem Minimum + gleitendem Mittel geglättet
   (Split 2 ms, Breitband 6 ms). Das Ergebnis ist nie größer als das Gate selbst → die Absenkung kann nicht in den
   folgenden Vokal laufen (Vokal neben S: 0,00 dB), und das Gate rampt linear statt 1-ms-Zappeln.
   Neu-Auslösungen pro S in Breitband: 0,34 → 0,17.
4. **Abhören = was bearbeitet wird (Fix 4).** „S-Bereich“ spielt in Split das S-Band selbst (Hochpass 4. Ordnung an
   seiner Unterkante, damit die Stimme darunter nicht durchklingt), in Breitband das Detektor-Band ab der Frequenz.
   Beide laufen immer mit (kein alter Filterzustand). Umschalten von Abhören, Modus, Lookahead und Stereo-Modus wird
   übergeblendet (15 ms; Stereo-Modus blendet aus, wechselt und blendet erst nach der Latenz wieder ein).
5. **Smart-Band folgt der Frequenz (Fix 5):** 0,65 × Frequenz (3,5–6 kHz), beim Standard 6,2 kHz wie bisher 4 kHz.
6. **Threshold-Bereich −50 … −10 dB (Fix 6).** Der relative Detektor liest höchstens etwa −17 dB (S-Spitzen aller
   15 Takes: −28 … −17 dB); die oberen 15 dB von −60 … 0 taten nichts. Skala unverändert → Presets und AUTO gelten
   weiter; die S-Pegel-Anzeige zeigt jetzt −50 … −10.
7. **CPU (Fix 7):** Detektor-Filter als SIMD (4 Filter je Befehl), schnelle log/exp-Näherung (< 0,0002 dB Fehler),
   Smart-Index nur jedes 4. Sample (kommt aus 5-ms-Hüllkurven), Gate ohne Rechenarbeit, solange es ruht.
   Stereo 44,1 kHz, nur die Engine: Split 0,32 → 0,28 % eines Kerns, Breitband 0,40 → 0,28 % (Stereo Link 50 %: 0,44 %).

## Neu (Pro-DS-Kernfunktionen)

| Parameter | ID | Bereich / Standard | Wirkung |
|---|---|---|---|
| Breite (Width) | `emWidth` | 1–4 Oktaven, 3 | Breite des Split-Bands zwischen den Punkten halber Absenkung. Schmal = nur das S, breit = S + Luft. Die Unterkante bleibt ≥ 0,45 × Frequenz (wächst in die Luft, nicht in die Stimme). |
| Link | `emLink` | 0–100 %, 100 % | Stereo-Kopplung: mischt die Absenkung jedes Kanals mit der gekoppelten (50 % = halb). |
| Stereo | `emStereo` | L/R, M/S, Mid, Side | L/R mit Link; M/S Mitte und Seiten getrennt (mit Link); nur Mitte; nur Seiten. |
| Erkennung „Allround“ | `emDetection` = 3 | – | Für Mixe, Busse, Instrumente (Pro-DS „Allround“): vergleicht das S-Band mit dem Band direkt darunter (0,3–0,6 × Frequenz) statt mit dem Stimmkörper. Smart bleibt „Single Vocal“. |
| Lookahead | `emLookahead` | 0–10 ms, **5 ms** | 5 ms misst sich besser als 2 ms (Delta-Reinheit 99,1 → 99,5 %, Vokal-Frames mit Absenkung −45 %); mehr als 5 ms bringt nichts mehr, ist aber einstellbar. |

**Latenz: jetzt fest 10 ms** (vorher 5 ms), bei jeder Abtastrate und jedem Lookahead gleich.
Neues Werks-Preset „Mix / Bus“ (Allround, −40 dB, Range 4). Externer Sidechain: nicht gebaut.

## Messung (Mittel)

| | S-Kern | Luft über S | S-Farbe | unberührte S | Vokal Luft/Präsenz | Vokal neben S | Delta-Reinheit |
|---|---|---|---|---|---|---|---|
| **J1–J3** Pro-DS Standard | −3,71 dB | −3,62 | −145 Hz | 9,5 % | 0,00 / 0,00 | −0,02 | 98,1 % |
| J1–J3 EMBER vorher Standard | −3,94 | −2,68 | +53 Hz | 7,0 % | 0,00 / 0,00 | −0,01 | 97,8 % |
| **J1–J3 EMBER neu Standard** | **−4,07** | −3,39 | −48 Hz | 7,3 % | 0,00 / 0,00 | 0,00 | **99,1 %** |
| J1–J3 EMBER neu AUTO | −3,95 | −3,36 | −122 Hz | 7,3 % | 0,00 / 0,00 | 0,00 | 99,2 % |
| J1–J3 EMBER neu Breitband | −4,14 | −4,08 | −139 Hz | 8,1 % | 0,00 / 0,00 | 0,00 | 99,5 % |
| 15 Takes vorher Standard | −3,24 | −2,60 | −99 Hz | 14,2 % | 0,00 / 0,00 | −0,03 | 98,7 % |
| 15 Takes neu Standard | −3,37 | −3,01 | −127 Hz | 14,1 % | 0,00 / 0,00 | 0,00 | 99,5 % |
| 15 Takes neu AUTO | −3,21 | −2,77 | −110 Hz | 15,2 % | 0,00 / 0,00 | 0,00 | 99,5 % |

Ziele gegen Pro-DS (J1–J3): S-Kern stärker (−4,07 gegen −3,71), S-Farbe weniger verschoben (−48 gegen −145 Hz),
weniger Luft weg (−3,39 gegen −3,62), Vokale unberührt, Delta reiner (99,1 gegen 98,1 %).
Die Luft über dem S nimmt jetzt etwas mehr ab als vorher (−3,4 statt −2,7 dB): die alte bilineare Glocke ließ bei
44,1/48 kHz oben ungewollt nach („Cramping“). Wer nur das S will: Breite 2 Oktaven (J1–J3: Luft −2,66, S-Kern −3,84).

Allround im künstlichen Mix (15 Takes + lautes Bett bis 2,5 kHz, Threshold −40): Smart −0,3 dB auf den S (versagt,
wie erwartet), Allround −2,9 dB, Vokale 0,02 dB. Auf einer Solostimme: S-Kern −3,46, Vokale ≤ 0,06 dB, Reinheit 89 %.

## Tests

`EmberTests`: 351 Checks grün, mit `EMBER_VOCALS` (15 echte Takes, alle durch Engine und AUTO) 428 grün.
Neu: Zipper (Seitenbänder < −90 dB), Bandform 44,1–192 kHz, Link 0/50/100 % und L/R / M/S / Mid / Side mit
Null-Tests (bit-genau), Abhör-Band, kein Klick beim Umschalten (Modus, Abhören, Lookahead, Stereo-Modus),
Allround im Mix. Der Echt-Takes-Test meldet jetzt einen Fehler, wenn `EMBER_VOCALS` keine Dateien findet.
pluginval Strenge 10 (`--skip-gui-tests`): SUCCESS.

## Hörbeispiele

`C:\Users\Shadow\dev\velvox-bench\2026-11-23\hoertest_prods_level`: J1, J2, J3, F01, M40, F_wings – je
`01_original`, `02/03` EMBER neu AUTO (+ DELTA), `04/05` EMBER neu Standard (+ DELTA), `06/07` Pro-DS Standard
(+ DELTA, nur J1–J3), `08/09` EMBER vorher AUTO (+ DELTA).

## Für AUTO (anderer Agent)

- API unverändert kompatibel; `Settings` hat neu `widthOct`, `stereoLink`, `stereoMode`, `allround` (Standards = altes
  Verhalten außer der Bandform), `lookaheadMs` Standard 5 ms.
- `Detector::setAllround()` neu; `Detector` folgt mit `setFrequency()` jetzt auch mit dem Smart-Band.
- Threshold-Parameter nur noch −50 … −10 dB (`thresholdMinDb/MaxDb` in `EmberParams.h`): AUTO sucht −45 … 0 –
  die obere Grenze sollte `thresholdMaxDb` sein.
- Latenz 10 ms (`Engine::latencyFor`).
