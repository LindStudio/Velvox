# EMBER – Klangtest des De-Essers (19.11.2026)

Stand: Branch `feature/ember-live-mascot`, Tag `ember-vor-hoelle` (Klang-Code unverändert getestet).
Gemessen in einem eigenen Arbeitsordner (`audit/ember-sound`), am ausgelieferten Code wurde nichts geändert.

---

## Kurzfazit (für Eilige)

**Was schon richtig gut ist (und bleiben soll):**
- **Vokale bleiben unberührt.** Über 15 echte Takes verlieren Vokale 0,00 dB „Luft“ (10–16 kHz) und 0,00 dB Präsenz
  (2–5 kHz) – auch der Vokal direkt neben dem S verliert nur 0,02–0,03 dB. Die Smart-Erkennung ist der große Pluspunkt
  von EMBER. Kein Pumpen, keine Atmer-Bearbeitung, keine hellen Vokale, die fälschlich abgesenkt werden.
- **Technisch sauber:** Null-Test bit-genau, Latenz exakt (44,1/48/96 kHz, jede Lookahead-Einstellung), keine
  hörbaren Klicks (Zipper −68 dB unter dem Signal), keine Verzerrung/Aliasing (Klirr −78 bis −148 dBc),
  pegelunabhängig von 0 bis −30 dBFS.
- **Hi-Hats/Übersprechen:** Smart ignoriert sie auf Vokalen fast vollständig (Relativ/Absolut fallen darauf rein).

**Wo man den Klang hörbar verbessern kann:**
1. **Das S wird im Split-Modus dunkler statt nur leiser.** Das offene High-Shelf nimmt oberhalb des S (Luft des
   S) 1,4-mal so viel weg wie im S selbst. Der Klangschwerpunkt des S rutscht im Mittel um 600 Hz nach unten
   (Pro-DS: 145 Hz). Das ist der Weg Richtung „sch“/Lispeln. Ein **Glocken-/Bandfilter um das S** (Prototyp gebaut)
   nimmt bei gleicher S-Absenkung nur ein Drittel der Luft weg und lässt das S wie ein S klingen.
2. **AUTO ist unzuverlässig bei der Frequenz** (je nach 12-s-Ausschnitt 5,0 oder 8,7 kHz bei derselben Sängerin),
   landet in 3 von 15 Takes am Anschlag Threshold −60 dB (dann regelt EMBER nicht mehr dynamisch, sondern senkt jedes
   Zischen gleich stark ab, auch leise „f“/„sch“), und verfehlt sein eigenes Ziel im Mittel um 0,6 dB.
3. **Die Stimmlage-Knöpfe wirken verkehrt herum:** „HOCH“ (8 kHz) de-esst auch hohe Stimmen am *wenigsten*
   (S-Kern −2,1 dB statt −2,9 dB mit MITTEL und −3,5 dB mit TIEF), weil der Threshold beim Verschieben der Frequenz
   nicht mitgeht.
4. **Ohne AUTO greift EMBER bei tiefen Männerstimmen kaum** (S-Kern −0,5 dB, die Hälfte der S unberührt).

---

## 1. Wie getestet wurde

**Material (15 echte Gesangsaufnahmen, 120–220 s):**

| Kürzel | Herkunft | Grundton (Median) | S-Spitze |
|---|---|---|---|
| J1 Chemicals, J3 Lonely | deine beiden Dry-Takes im Ordner „01 EMBER De-Esser“ | 294 / 311 Hz | 7,1 / 10,2 kHz |
| J2 Kissed | aus dem Hörtest 13.11. (Original = EMBER + Delta) | 332 Hz | 8,2 kHz |
| F01, F08, F22, M36, M40, M42 | Originale aus „Hoertest/2026-11-10_subtil“ (Sonovox) | 175–332 Hz | 4,6–9,4 kHz |
| F_wings, F_runwild, F_badboy, M_emerald, M_allittakes, M_notme | sechs weitere Sonovox-Packs (ausgepackt nach TEMP) | 208–350 Hz | 4,8–9,0 kHz |

Vergleich mit **FabFilter Pro-DS**: Pro-DS ist nicht mehr installiert, aber die Pro-DS-Renderings (Standard-Preset)
vom 13.11. für J1, J2, J3 liegen in „Hoertest/2026-11-13_vs_ProDS“ und sind sample-genau auf die Originale
ausgerichtet (geprüft). Der Vergleich mit Pro-DS gilt also nur für diese drei Takes.

**Messverfahren:** Der echte EMBER-Engine-Code läuft über ein Kommandozeilen-Werkzeug (`EmberTests --process`, nur im
Audit-Ordner). Danach wird jede Aufnahme in 20-ms-Stücke zerlegt und nach Klang sortiert:

| Klasse | Erkennung (Faustregel) | Beispiel |
|---|---|---|
| **S** | Höhen (4–16 kHz) lauter als der Stimmkörper, mindestens 40 ms lang, kräftig | s, z, ß, scharfes sch |
| **leise Reibelaute** | wie S, aber über 30 dB leiser als die lauteste Stelle | f, weiches s, h |
| **t/k-Knackser** | Höhen-Burst kürzer als 30 ms | t, k, p, ch-Ansatz |
| **Vokal** | Höhen mindestens 15 dB unter dem Körper, laut | a, o, u, e |
| **heller Vokal** | Höhen 5–15 dB unter dem Körper, nicht neben einem S | i, ä, gehauchte Töne |
| **Atmer** | leise, rauschig, nicht neben Vokal/S | Einatmen |
| **Vokal neben S** | Vokal bis 40 ms vor/nach einem S | „S**a**“ in „Sand“ |

Gemessen wird, wie viel Energie jede Klasse in bestimmten Frequenzbereichen verliert (in dB, positiv = leiser):
- **S 4–12 kHz:** das klassische De-Esser-Band.
- **S-Kern:** eine Oktave rund um die S-Spitze der jeweiligen Stimme (dort sitzt die Schärfe).
- **Luft über dem S:** von der Oberkante des S-Kerns bis 16 kHz (die „Seide“ des S).
- **S-Klangschwerpunkt:** wo die Energie des S im Mittel liegt (Hz). Sinkt er stark, klingt das S dunkler →
  Richtung „sch“ / Lispeln.
- **unberührte S:** Anteil der S-Laute, die weniger als 0,5 dB verlieren (= EMBER hat sie verpasst).

Einstellungen: **Standard** (6,5 kHz, −30 dB, Range 5 dB, Smart), **TIEF/HOCH** (5 / 8 kHz), **Relativ/Absolut**,
**AUTO** (genau wie im Plugin: die ersten 12 s Gesang, danach wird der ganze Take gerendert), jeweils **Split** und
**Breitband**.

---

## 2. S-Absenkung: Nimmt EMBER das S und lässt den Rest?

### 2.1 Mittelwerte über alle 15 Takes

| Einstellung | S 4–12 kHz | S-Kern | Luft über S | S-Schwerpunkt | unberührte S | f/leise | t/k | Vokal-Luft | Vokal-Präsenz | Vokal neben S |
|---|---|---|---|---|---|---|---|---|---|---|
| Standard Split | −1,9 dB | −1,9 | −2,7 | −426 Hz | 27 % | −0,3 | −1,1 | 0,00 | 0,00 | −0,02 |
| Standard Breitband | −2,8 | −2,9 | −2,9 | −159 Hz | 21 % | −0,4 | −1,6 | 0,00 | 0,00 | −0,02 |
| TIEF (5 kHz) | −2,7 | −2,7 | −3,1 | −364 Hz | 17 % | −0,5 | −1,5 | 0,00 | 0,00 | −0,03 |
| HOCH (8 kHz) | −1,1 | −1,2 | −2,1 | −367 Hz | 46 % | −0,1 | −0,7 | 0,00 | 0,00 | −0,02 |
| **AUTO Split** | **−3,2** | **−3,3** | **−4,5** | **−605 Hz** | 9 % | −1,2 | −1,8 | 0,00 | 0,00 | −0,03 |
| AUTO + Breitband | −4,7 | −4,7 | −4,7 | −106 Hz | 6 % | −1,8 | −2,6 | 0,00 | 0,00 | −0,03 |

**Gut:** Vokale, Atmer und helle Vokale verlieren praktisch nichts (0,00–0,01 dB) – egal ob Split oder Breitband.
Das ist besser als bei vielen De-Essern, die im Breitband-Modus die Vokalansätze mit runterziehen.

**Auffällig:** Im Split-Modus verliert die **Luft über dem S immer mehr als der S-Kern** (AUTO: −4,5 dB gegen −3,3 dB).
Der S-Schwerpunkt sinkt um 600 Hz. Das S wird also eher **dunkler** als leiser. Im Breitband-Modus bleibt die Farbe
des S erhalten (−106 Hz).

### 2.2 Vergleich mit Pro-DS (nur J1, J2, J3 – dort gibt es Pro-DS-Renderings)

| | S 4–12 kHz | S-Kern | Luft über S | S-Schwerpunkt | unberührte S | t/k | Vokal neben S (Luft) |
|---|---|---|---|---|---|---|---|
| EMBER Standard Split | −2,7 dB | −2,9 | −3,6 | −563 Hz | 9 % | −2,2 | −0,01 |
| **EMBER AUTO Split** | **−3,9** | **−4,4** | **−5,4** | **−850 Hz** | 6 % | −3,3 | −0,01 |
| EMBER AUTO Breitband | −5,5 | −5,6 | −5,5 | −162 Hz | 3 % | −4,5 | −0,01 |
| **Pro-DS Standard** | **−3,6** | **−3,7** | **−3,6** | **−145 Hz** | 10 % | −2,9 | −0,02 |

Ergebnis: Bei der **Menge** ist EMBER-AUTO mindestens so stark wie Pro-DS (S-Kern −4,4 gegen −3,7 dB), und die
Vokale bleiben genauso sauber. Der Unterschied liegt in der **Klangfarbe**: Pro-DS senkt das S gleichmäßig über alle
Frequenzen ab (das S bleibt ein S, nur leiser), EMBER nimmt 1,8 dB mehr Luft vom S und verschiebt es um 850 Hz nach
unten. Das hört man im A/B als „EMBER macht das S stumpfer“ (Hörbeispiele 01 und 02).

### 2.3 Die drei Stimmlagen-Knöpfe

Die Takes nach Lage ihrer S-Spitze gruppiert (S-Kern-Absenkung / unberührte S, Standard-Threshold −30 dB):

| Stimme (S-Spitze) | TIEF 5 kHz | MITTEL 6,5 kHz | HOCH 8 kHz | AUTO |
|---|---|---|---|---|
| S tief (unter 5,5 kHz), 5 Takes | −1,4 dB / 27 % | −0,5 / 48 % | −0,1 / 76 % | −2,3 / 9 % |
| S mittel (5,5–8 kHz), 4 Takes | −3,3 / 12 % | −2,3 / 16 % | −1,2 / 37 % | −3,3 / 11 % |
| S hoch (über 8 kHz), 6 Takes | −3,5 / 12 % | −2,9 / 17 % | −2,1 / 28 % | −4,1 / 7 % |

- **HOCH ist für jede Stimme die schwächste Einstellung – auch für die hohen.** Grund: Wird die Frequenz nach oben
  geschoben, sieht der Detektor weniger S-Energie, der Threshold bleibt aber bei −30 dB stehen. Dazu senkt das Shelf
  an seiner Eckfrequenz nur die Hälfte ab. Die Knöpfe wirken deshalb vor allem wie „mehr/weniger“ statt wie „richtig
  zielen“. Im Handbuch steht „HOCH: für hohe und weibliche Stimmen“ – das stimmt so nicht.
- **Tiefe Stimmen ohne AUTO:** Mit Standard (MITTEL) wird bei tiefem S fast nichts gemacht (−0,5 dB, die Hälfte der S
  unberührt; M40: 64 %, M42: 73 %). Selbst TIEF lässt 27 % liegen. Hörbeispiel 05.
- Sehr tiefe S (M36: Spitze 4,0–4,5 kHz) liegen unter der kleinsten AUTO-Frequenz (5 kHz). Das Shelf erwischt dann
  vor allem die Luft darüber: S-Kern −2,3 dB, Luft −4,4 dB, S-Schwerpunkt −565 Hz (Hörbeispiel 04).

---

## 3. Artefakte

### 3.1 Lispeln (Überabsenkung)

Mit dem Standard und mit AUTO (Split) wird **kein S um mehr als 6 dB abgesenkt** (größter Einzelwert 6,0 dB; AUTO + Breitband 7,0 dB). Lispeln
entsteht erst bei starken Handeinstellungen:

| 8 Takes, Mittel | S-Kern | Luft über S | S-Schwerpunkt | S über 8 dB abgesenkt |
|---|---|---|---|---|
| Preset „Kräftig“ (−40 dB, Range 9) Split | −4,9 dB | −6,9 | −985 Hz | 0 % |
| „Kräftig“ Breitband | −7,4 | −7,4 | −176 Hz | 39 % |
| „Kräftig“ + Amount 100 % Split | −8,9 | −12,2 | **−1.709 Hz** | 51 % |
| „Kräftig“ + Amount 100 % Breitband | −13,3 | −13,1 | −418 Hz | 84 % |

- Amount 100 % verdoppelt die Range (hier 18 dB). Dann werden 50–90 % der S um mehr als 8 dB abgesenkt – das ist
  hörbar gelispelt (Hörbeispiel 03). Im Split-Modus wird das S dabei zusätzlich 1,7 kHz dunkler („sch“-artig),
  im Breitband-Modus wird es eher „zu leise“, behält aber seine Farbe.
- Die Sprechblase „Klingt gelispelt!“ ab 9 dB passt zu diesen Zahlen. Kein Fehler, aber Amount über 50 % ist ein
  großer Sprung (doppelte Range).

### 3.2 Pumpen / Regelbewegung auf Vokalen

| Erkennung | Vokal-Stücke mit Regelung > 0,5 dB | helle Vokale mit Regelung | Vokal-Luft verloren |
|---|---|---|---|
| **Smart** (Standard) | **0,3 %** | **0,1 %** | **0,00 dB** |
| Relativ | 7,9 % | 14,7 % | −0,14 dB |
| Absolut (−30 dB) | 26,4 % | 36,7 % | −0,63 dB |

Smart lässt die Vokale praktisch in Ruhe. Im Breitband-Modus gibt es deshalb auch kein Pumpen auf Vokalen.
Achtung bei **Absolut**: Mit dem Standard-Threshold −30 dB regelt EMBER bei lauten Takes ständig (J1: 87 % aller
Vokal-Stücke). Wer auf Absolut umschaltet, ohne den Threshold anzupassen, bekommt im Breitband-Modus Pumpen.

**Smart-Tor schließt schnell genug?** Getestet: Abfallzeit 1 ms (heute) gegen 5 ms. Mit 5 ms verliert der Vokal nach
dem S 4-mal so viel Luft (−0,13 statt −0,03 dB). **Die 1 ms sind richtig gewählt.**

**Flattern im S:** Pro S fällt die Regelung im Mittel 0,4-mal kurz ab und setzt wieder ein (Relativ: 0,3-mal). Das
Smart-Tor macht also etwas mehr Bewegung, aber auf echtem Material unauffällig.

### 3.3 Klicks / Zipper

Das Split-Shelf wird alle 16 Samples neu berechnet (Treppen statt glatter Kurve). Gemessen mit einer Nachbildung
(die exakt mit EMBER übereinstimmt, Abweichung −93 dB) gegen eine Version mit Berechnung bei jedem Sample:

| Fall | Treppen-Störung relativ zum Signal | relativ zum entfernten S |
|---|---|---|
| F22, AUTO | −68 dB | −40 dB |
| F22, kräftig (−45 dB, Range 10, Amount 100 %) | −64 dB | −47 dB |

Unhörbar, zumal sie mitten im S-Rauschen liegt. **Keine Änderung nötig.**

### 3.4 Null-Test, Latenz, Verzerrung

| Test | Ergebnis |
|---|---|
| Null-Test (Threshold 0 dB, Range 0, Amount 0, Mix 0 / 50 %, Split und Breitband) | **bit-genau** in allen 7 Fällen |
| Latenz (Impuls) bei 44,1 / 48 / 96 kHz, Lookahead 0 / 2 / 5 ms | **0 Samples Abweichung** in allen 9 Fällen |
| Klirr + Rauschen, Sinus 1 / 7 / 10 / 15 kHz bei 10 dB Absenkung | −139 bis −148 dBc; Ausnahme 7 kHz Breitband −78 dBc (immer noch unhörbar) |
| Aliasing | nicht vorhanden – EMBER hat nur Filter und langsame Lautstärke-Regelung, keine Verzerrer |

Weil Split ein Shelf (kein Frequenzweichen-Paar) ist, gibt es auch **keine Phasen-/Kammfilter-Probleme an einer
Trennfrequenz** – ohne Absenkung wird das Shelf übersprungen.

### 3.5 S-Anfang und Lookahead (künstliches S nach einem Vokal, Breitband, 10 dB Range)

| Erkennung, Lookahead 2 ms | erste 1 ms des S | 1–3 ms | 3–10 ms | Vokal nach dem S, erste 5 ms | 5–20 ms |
|---|---|---|---|---|---|
| Smart | 0,0 dB | 0,0 | −3,7 | −2,8 | −0,03 |
| Relativ | −9,8 | −9,9 | −10,0 | −9,3 | **−7,8** |
| Absolut | −9,9 | −10,0 | −10,0 | −9,3 | **−7,9** |

Smart lässt den Vokal danach in Ruhe (Relativ/Absolut drücken ihn 8 dB runter = Pumpen), verpasst aber die ersten
3–5 ms eines **abrupt** einsetzenden S – der Lookahead bringt dort fast nichts, weil das Smart-Tor mit 5-ms-Mittelung
selbst etwas hinterherhinkt. Prototyp „Tor 3 ms früher lesen“: künstliches S 3–10 ms −8,3 statt −3,7 dB, Vokal danach
−0,7 statt −2,8 dB. **Auf den echten 15 Takes ändert das aber fast nichts** (S +0,02 dB, Vokal neben S −0,008 statt
−0,03 dB), weil echte S weich einsetzen. Kleiner Feinschliff, keine Baustelle.

---

## 4. Erkennung

### 4.1 Hi-Hats unter dem Gesang (8tel-Hats ab 7 kHz, 60 s je Take)

Anteil der Vokal-Stücke, auf denen EMBER fälschlich regelt:

| Hat-Pegel (unter Gesangsspitze) | Smart | Relativ | Absolut |
|---|---|---|---|
| keine Hats | 0,0–0,4 % | 1,8–14 % | 16–87 % |
| −30 dB (Kopfhörer-Übersprechen) | 0,0–0,5 % | 3–14 % | 17–87 % |
| −20 dB | 0,2–0,7 % | 9–16 % | 49–91 % |
| −12 dB (sehr laut) | 2–4 % | 34–43 % | 75–96 % |

In den Pausen zwischen den Phrasen regelt Smart auf den Hats (40–60 % der Pausen-Stücke) – das ist harmlos, es
senkt dann nur Hat-Übersprechen ab. **Smart ist hier klar die richtige Standardwahl.**
Einziger Haken: **AUTO lässt sich von Hats verwirren** (M40 mit Hats −30 dB: Absenkung auf den S springt von 4,5 auf
8 dB; mit −12 dB werden 26 % der S verpasst). AUTO also möglichst auf der reinen Gesangsspur laufen lassen.

### 4.2 Eingangspegel −40 bis 0 dB (Take auf 0 dBFS Spitze normiert, dann leiser gemacht)

| Take / Erkennung | 0 dB | −10 | −20 | −30 | −40 dB |
|---|---|---|---|---|---|
| F22 Smart (S 4–12 kHz) | −2,44 | −2,44 | −2,44 | −2,43 | −2,34 |
| F22 Relativ | −2,44 | −2,44 | −2,44 | −2,43 | −2,34 |
| F22 Absolut | −2,71 | −0,83 | 0,00 | 0,00 | 0,00 |
| M40 Smart | −0,62 | −0,62 | −0,62 | −0,62 | −0,32 |

Smart und Relativ verhalten sich über 30 dB Pegelunterschied **identisch** – genau wie gewollt. Erst wenn die
Gesangsspitze unter −40 dBFS liegt, werden leise S verpasst (Ruhe-Schwelle −60 dBFS). Absolut ist erwartungsgemäß
pegelabhängig.

### 4.3 Leise S, S nach einer Pause

- Leise S in leisen Phrasen werden genauso behandelt wie laute (relativ zum Phrasenpegel) – gut.
- S nach einer Pause werden etwas stärker abgesenkt als S nach einem Vokal (Standard: −2,4 gegen −1,6 dB im Mittel;
  wenige Fälle pro Take). Grund: Die Bezugs-Hüllkurve (250 ms) hängt nach einem Vokal noch hoch. Klein, unkritisch.
- **Leise Reibelaute (f, weiches s, h):** Mit AUTO verlieren sie bei einigen Männerstimmen 1,5–3,3 dB (M36, M40,
  M_notme), Pro-DS lässt sie bei J1 unberührt (0,00 dB). Ursache: AUTO stellt dort den Threshold auf −60 dB (siehe 5.2),
  dann wird jedes Zischen gleich stark abgesenkt. Das kann ein Wort wie „Schiff“ leicht „zahnlos“ machen.

---

## 5. AUTO

### 5.1 Folgt AUTO der Stimme? (Takes per Umsampeln tiefer/höher gemacht, Faktor 0,84 / 1,19)

| Take | S-Spitze laut AUTO: original | tiefer (×0,84) | höher (×1,19) | passt? |
|---|---|---|---|---|
| F22 | 7,3 kHz | 6,1 | 9,2 | ja |
| J3 Lonely | 9,7 | 8,1 | 11,5 | ja |
| M_notme | 8,5 | 6,9 | 10,1 | ja |
| M40 | 5,7 | 4,5 | 6,8 | ja |
| F_wings | 5,3 | 4,5 | 6,2 | ja |
| F01 | 10,9 | 8,8 | 12,0 (Suchgrenze) | ja |
| **J1 Chemicals** | 6,8 | **7,3** | **4,2** | **nein** (zwei S-Spitzen, AUTO springt) |

Heller/dumpfer gemacht (±6 dB Shelf bei 6 kHz): AUTO nimmt bei hellerer Stimme mehr weg (Ziel z. B. F01 3,1 → 3,4 dB,
J3 3,8 → 4,3 dB), bei dumpferer weniger (2,7 / 3,2 dB). **Das ist sinnvoll.**

### 5.2 Wo AUTO wackelt

**a) Die Frequenz hängt vom Ausschnitt ab** (AUTO auf drei verschiedenen 12-s-Stellen desselben Takes):

| Take | Ausschnitt 0 s | 25 s | 50 s |
|---|---|---|---|
| F01 | 8.700 Hz | **5.000 Hz** | 6.950 Hz |
| J1 Chemicals | 5.450 Hz | 7.050 Hz | 5.800 Hz |
| F22 | 5.800 Hz | 5.250 Hz | 5.150 Hz |
| J3 Lonely | 7.750 Hz | 8.150 Hz | 8.250 Hz |
| M40 (Threshold) | **−60 dB** | −37,5 dB | −31 dB |

Bei F_badboy findet AUTO 4,5 kHz, die S-Spitze über den ganzen Take liegt bei 9 kHz. Ursache: AUTO nimmt das
Maximum eines gemittelten S-Spektrums aus oft nur 30–170 S-Stücken; bei S mit zwei Buckeln (s und sch) gewinnt mal
der eine, mal der andere.

**b) Threshold am Anschlag −60 dB** bei 3 von 15 Takes (M40, M_allittakes, M_notme), in den Varianten noch öfter.
Dann ist EMBER kein dynamischer De-Esser mehr, sondern ein „S-geschaltetes festes Shelf“: leise und scharfe S werden
gleich stark abgesenkt, auch leise f/sch (siehe 4.3). Ursache: AUTO misst den Erfolg an der Energie des ganzen
S-Stücks, das Shelf wirkt aber vor allem oberhalb des S – das Ziel ist mit dem Shelf schwer erreichbar, also dreht
die Suche den Threshold ganz runter.

**c) AUTO verfehlt sein eigenes Ziel** in allen 15 Takes, im Mittel um 0,6 dB (bis 1,3 dB, z. B. M_emerald:
Ziel 3,5, erreicht 2,2 dB). Ursache: Die Range wird **nach** der Threshold-Suche kleiner gesetzt
(90-%-Wert + 1 dB), das nimmt wieder etwas weg.

**d)** AUTO wählt immer Split, obwohl Breitband die S-Farbe besser erhält (2.1).

---

## 6. Was die guten kommerziellen De-Esser anders machen

| Merkmal | Pro-DS (aus deinem Screenshot) / übliche Praxis | EMBER heute |
|---|---|---|
| Breitband als Standard | Pro-DS startet in „Wide Band“: die ganze Stimme taucht kurz ab, das S bleibt ein S | Split (offenes Shelf) |
| Split-Band mit **Ober- und Untergrenze** | Pro-DS: Band 5,7–10,4 kHz, darüber bleibt die Luft | Shelf ohne Obergrenze: alles über der Frequenz bis 20 kHz |
| Lookahead | bis 15 ms | bis 5 ms (reicht, da echte S weich einsetzen) |
| Erkennung „nur die Stimme“ | Pro-DS „Single Vocal“ unterscheidet S von Vokalen | Smart – auf Vokalen messbar mindestens so gut |
| Frequenzabhängige Absenkung | dynamische EQs / spektrale De-Esser (z. B. Soothe-Art) senken nur die Frequenzen ab, die gerade zu laut sind, mehrere Bänder oder viele kleine | ein Shelf, ein Regelwert |
| Oversampling | Pro-DS 2× | nicht nötig (keine Verzerrer, gemessen) |

Konkret hörbar wäre:
1. **Band statt Shelf im Split-Modus (dynamischer EQ um das S).** Prototyp in EMBER gebaut (Glocke an der von AUTO
   gefundenen S-Spitze, sonst alles gleich wie AUTO):

   | 15 Takes, Mittel | S-Kern | Luft über S | Verhältnis Luft/Kern | S-Schwerpunkt (Betrag) | Vokal neben S |
   |---|---|---|---|---|---|
   | Shelf (heute, AUTO) | −3,3 dB | −4,5 | 1,43 | 605 Hz | −0,03 |
   | Glocke, Güte 1,0 | −3,8 | −1,6 | 0,42 | 362 Hz | −0,01 |
   | Glocke, Güte 0,7 | −4,1 | −2,3 | 0,55 | 321 Hz | −0,02 |
   | Glocke, Güte 0,5 (breit) | −4,3 | −2,9 | 0,67 | 255 Hz | −0,02 |
   | Pro-DS (nur J1–J3) | −3,7 | −3,6 | 0,98 | 145 Hz | −0,02 |

   Bei J1–J3 mit gleicher S-Kern-Absenkung (Shelf −4,4 / Glocke −4,5 dB) bleiben mit der Glocke 3,9 dB mehr Luft auf
   dem S. **Achtung:** Bei schmaler Glocke steigt der S-Schwerpunkt bei manchen Stimmen um 500–800 Hz (M36, M42,
   J1): Das S kann dann dünn/pfeifig klingen, weil nur noch die Luft übrig ist. Und wenn AUTO die S-Spitze falsch
   findet (F_badboy: 4,5 statt 9 kHz), trifft die Glocke daneben (S-Kern nur −1,4 dB). Die breite Glocke (Güte 0,5–0,7)
   ist der gute Mittelweg; noch besser wäre ein Band mit Ober- und Untergrenze wie bei Pro-DS oder 2–3 kleine Bänder,
   die jeweils nur so weit absenken, wie sie über dem Stimmkörper liegen.
2. **Breitband als Option in AUTO** (oder ein Schalter „S-Klang behalten“): erhält die Farbe des S heute schon
   (−106 Hz Schwerpunkt), müsste aber eigene Ziel-Werte bekommen (mit den Split-Werten senkt es 1,5 dB mehr ab).
3. Die Smart-Erkennung selbst ist **nicht** verbesserungsbedürftig – sie ist der Teil, in dem EMBER Pro-DS ebenbürtig
   ist (Vokale unberührt, Hats ignoriert, pegelunabhängig).

---

## 7. Hörbeispiele

Ordner `EMBER_KLANG_TEST_Hoerbeispiele` (14 s je Datei, 24 Bit, gleicher Ausschnitt mit den meisten S, Pegel nicht
verändert). Zu jeder bearbeiteten Datei gibt es ein `_DELTA` (= was entfernt wurde).

| Nr. | Was vergleichen | Worauf hören |
|---|---|---|
| 01 J3 Lonely | Original – EMBER AUTO Split – **Pro-DS** – **EMBER Glocke (Prototyp)** – EMBER AUTO Breitband | Klingt das S bei EMBER Split stumpfer als bei Pro-DS? Die Glocke sollte gleich viel Schärfe nehmen, aber das „Glitzern“ auf dem S lassen. |
| 02 J1 Chemicals | Original – EMBER AUTO Split – Pro-DS – Glocke | wie 01, an deiner zweiten Stimme (dort hat AUTO eine eher tiefe Frequenz gewählt) |
| 03 F22 Lispeln | Original – Kräftig Split – **Amount 100 % Split (lispelt)** – Amount 100 % Breitband | Ab wann wird aus „s“ ein „sch“/„th“? Split 100 % wird dunkel, Breitband 100 % eher zu leise. |
| 04 M36 tiefe Männerstimme | Original – Standard 6,5 kHz – TIEF 5 kHz – AUTO – AUTO Glocke | Standard nimmt fast nur Luft weg; TIEF und AUTO treffen besser; Glocke trifft den Kern (S-Kern −3,7 statt −2,3 dB). |
| 05 M40 Standard greift kaum | Original – Standard ohne AUTO – AUTO | Ohne AUTO bleiben 64 % der S unberührt. |

Hinweis: Die Sonovox-Ausschnitte (F/M) nur zum Abhören, nicht weitergeben (Lizenz der Demo-Daten).

---

## 8. Priorisierte Verbesserungsliste

| # | Verbesserung | Hörbarer Gewinn (gemessen) | Aufwand | Risiko |
|---|---|---|---|---|
| 1 | **Split: Band/Glocke um das S statt offenem High-Shelf** (breit, Güte ca. 0,5–0,7, oder Band mit Obergrenze; Mitte von AUTO bzw. aus der Stimmlage) | **mittel–groß**: S bleibt „S“ statt dunkler. Luft auf dem S −2,3…−2,9 statt −4,5 dB, S-Schwerpunkt-Verschiebung 255–321 statt 605 Hz, bei mehr S-Kern-Absenkung (−4,1…−4,3 statt −3,3 dB). Nähert EMBER dem Pro-DS-Klang an | mittel (Filter tauschen, Spektrum-Anzeige, AUTO-Ziel neu einmessen, Tests) | mittel: hängt an der richtigen S-Frequenz → nur zusammen mit #2; bei schmaler Glocke Gefahr „pfeifiges S“ |
| 2 | **AUTO robuster:** S-Frequenz aus dem *Überschuss* des S-Spektrums über dem Vokalspektrum (Schwerpunkt statt Maximum), Threshold-Untergrenze ca. −45 dB, Range vor der Suche festlegen, Erfolg im S-Band statt Vollband messen | **mittel**: gleiche Stimme → gleiches Ergebnis (heute 5,0–8,7 kHz je nach Ausschnitt), kein „festes Shelf“ bei −60 dB (leise f/sch verlieren dann nicht mehr 1,5–3,3 dB), Ziel wird getroffen (heute 0,6 dB zu wenig) | klein–mittel (nur `EmberAuto.cpp`, Tests mit den 15 Takes) | gering |
| 3 | **Stimmlage-Knöpfe gleichen den Threshold aus** (oder Detektor normiert auf die Bandbreite), damit HOCH bei hohen Stimmen nicht die schwächste Wahl ist | **mittel** für alle, die AUTO nicht drücken: hohe Stimmen mit HOCH heute −2,1 dB statt −3,5 dB möglich, tiefe Stimmen mit MITTEL nur −0,5 dB | klein | gering |
| 4 | **Standard-Einstellung für tiefe Stimmen** (z. B. Threshold −34 dB oder Stimmlage beim ersten Abspielen grob raten) | klein–mittel: heute bleiben ohne AUTO bei tiefen S 48 % der S unberührt | klein | gering (vorher an hohen Stimmen gegenprüfen) |
| 5 | **AUTO darf Breitband wählen** bzw. Schalter „S-Klang behalten“ | mittel für Leute, die das S lieber natürlich leiser mögen (Schwerpunkt −106 statt −605 Hz) – entfällt weitgehend, wenn #1 kommt | klein–mittel (eigene Ziel-Werte) | gering |
| 6 | **Amount über 50 % sanfter** (z. B. Range ×1,5 statt ×2 bei 100 %) oder Lispel-Warnung schon ab 8 dB | klein: verhindert versehentliches Lispeln (heute bei „Kräftig“ + 100 %: im Mittel 51 % (Split) bzw. 84 % (Breitband) der S über 8 dB) | klein | gering, ändert aber das Gefühl des Reglers |
| 7 | Smart-Tor 3 ms früher auslesen | sehr klein auf echtem Gesang (S +0,02 dB; Vokal neben S −0,008 statt −0,03 dB), nur bei abrupten S hörbar | klein (Prototyp existiert) | gering |
| – | *Nicht nötig:* Zipper-Glättung (−68 dB), Oversampling (kein Aliasing), Latenz/Null-Test (perfekt), Gate-Abfallzeit (1 ms ist besser als 5 ms), Pegelabhängigkeit (keine) | – | – | – |

**Empfehlung:** #2 und #3 zuerst (wenig Aufwand, macht EMBER verlässlicher), dann #1 als eigentliche
Klangverbesserung – am besten mit Hörvergleich gegen die Pro-DS-Dateien aus `Hoertest/2026-11-13_vs_ProDS`.

---

## Anhang: Nachmachen

- Audit-Ordner: `C:\Users\Shadow\dev\velvox-wt\ember-sound-audit` (Branch `audit/ember-sound`, nicht committed).
  Änderungen dort nur für den Test: `EmberTests --process <in.wav> <out.wav> key=wert …` (freq, thr, range, amount,
  attack, release, la, mode, det, auto=1, autostart, autosec, trace=<wav>) und Versuchs-Schalter in `EmberEngine`
  (`shape=1 bellhz= bellq=` Glocke, `gatefall=`, `gatelead=`). Mit den Standardwerten verhält sich die Engine wie
  ausgeliefert (alle 246 Tests grün).
- Skripte und Rohzahlen: `EMBER_KLANG_TEST_Skripte` (`ana.py` = Messungen, `batch.py` = alle Takes × Einstellungen,
  `results.json`/`exp.json`/`heavy.json`/`hats.json`/`autovar.json`/`synth.json` = Ergebnisse).
- Die Testaufnahmen lagen in TEMP (`…\scratchpad\data\takes`); Sonovox-Packs: `Testvocals\Sonovox-…` (Wings to Fly,
  Run Wild, bad-boy, Emerald City, All it takes, Not Me).
