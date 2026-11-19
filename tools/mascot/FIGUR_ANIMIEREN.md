# Eine Figur zum Leben erwecken – Ablauf

Stand 17.11.2026, entwickelt an **Ember** (EMBER De-Esser), freigegebene Version **v17**. Mit diesem Ablauf lässt sich
jede 2D-Figur so animieren, dass sie „im Plugin lebt“. Die Figur wird aus der eigenen Zeichnung in Teile zerlegt, die
sich live und ohne Fugen bewegen. Augen und Brauen werden als Vektorformen dazugezeichnet. Ein Verhalten reagiert auf
Maus, Regler, Song-Tempo, S-Laute, AUTO und Bypass und läuft nie zweimal gleich ab.

- Online-Vorschau: https://claude.ai/artifact/NG6n8QpUn6TRxxdkb25X29
- Gesicherter Stand v17 (Drive): `Products/Ember/Mascot/live_v17/` mit `ember_live_v17.html` (im Browser öffnen),
  `rig/` (Teile, `rig.json`, `eyes.json`) und der Renderer-Vorlage.

---

## Kurzfassung (neue Figur)

1. **Zwei Bilder** derselben Figur in exakt derselben Pose malen lassen: eines **ohne Augen und Brauen**, eines **mit**.
2. **Gelenkpunkte eintragen** (Hals, Schultern, Ohren, Schwanz, Füße) in `puppet_ember.py` / `rig_ember.py`
   (Abschnitt 2, Tabelle).
3. **Augen nachzeichnen** in `eye_design.py` (`DESIGN`) und **Varianten** anlegen (`VARIANTS`).
4. **Bauen:**
   `python tools/mascot/build_live_preview.py <ohne_augen.png> <ziel.html> --ref <mit_augen.png>`
5. **Prüfen** auf weißem Hintergrund mit Extremposen (Abschnitt 5). Gefällt es, sichern (Abschnitt 7).
6. **Ins Plugin** übertragen (Abschnitt 6).

Ember neu bauen (ergibt exakt v17):

```
cd "G:\Meine Ablage\MUSIC\Plugin"
python tools/mascot/build_live_preview.py "Plugins/01 EMBER De-Esser/emberNoEyes.png" ember.html --ref "Plugins/01 EMBER De-Esser/Embernew.png"
```

Benötigt Python 3 mit `numpy`, `scipy`, `pillow` (`python -m pip install --user numpy scipy pillow`).

---

## 1. Grafik vorbereiten

| Datei | Wofür | Hinweise |
|---|---|---|
| Figur **ohne** Augen und Brauen | daraus wird animiert | Weißer Hintergrund, quadratisch, mindestens 2048 px, Stelle der Augen ist glatte Haut |
| Figur **mit** Augen und Brauen | nur Vorlage zum Nachzeichnen | exakt gleiche Pose, Größe und Position (Umrisse deckungsgleich prüfen) |

Bei Ember: `Plugins/01 EMBER De-Esser/emberNoEyes.png` und `Embernew.png`.

**Warum ohne Augen?** In einer flachen Zeichnung haben Pupille, Lidstrich und Lidschatten dieselbe Farbe und laufen
ineinander. Ausgeschnittene Augen hatten deshalb immer Reste und Kanten (viele Versuche bei Ember). Selbst gezeichnete
Augen über echter Haut sind dagegen fehlerfrei animierbar.

Weitere Teile, die sich stark bewegen sollen (Mund-Innenraum, Hände für große Gesten, Hinsetzen), am besten ebenfalls
getrennt zeichnen lassen.

## 2. Zerlegen (`rig_ember.py --no-eyes`)

Arbeitet im 1024-px-Raum und schreibt `<teil>.png` und `rig.json`:

- **Jedes Pixel hat genau einen Besitzer.** Die Farbflächen (Haut, Horn, Schwanz, Ohr, Arm) werden beschriftet, jeder
  Umriss-Pixel gehört zur nächsten Fläche.
- **Untere Teile reichen 4 px unter die oberen.** So gibt es keine Fugen.
- **Ohren, Arme und Schwanz sind Netze mit Gewichten.** Der Ansatz bleibt (Gewicht 0), das Ende bewegt sich
  (Gewicht 1). Sie biegen sich, statt zu reißen. Die Arme reichen bis in die Schulter.
- **Der Hals ist ein biegsames Band** zwischen Kopf und Oberkörper. Oberhalb des Mundes dreht der Kopf voll mit,
  darunter blendet es weich aus. So wird nie etwas aufgedeckt.
- **Die Flanke unter den Armen wird neu aufgebaut:** Reste vom Arm-Umriss am Körper werden durch eine saubere Hautkante
  ersetzt, sonst bleiben dunkle Spitzen stehen, wenn der Arm abspreizt.
- **Der Außenumriss wird aus allen Teilen entfernt** (dunkles Band vom Hintergrund aus, bis 9 px tief) und die
  Teilkanten werden hart gemacht. Der Renderer legt dann **einen** gleich dicken, runden Umriss um die ganze Figur
  (Abschnitt 4). Bei Ember gewählt: 6 px im 1024er-Raum, Farbe `rgb(52,6,22)`.
- Prüfbild `_rest_check.png`: Teile in Ruhe zusammengesetzt neben dem Original.

### Figur-spezifische Werte (bei einer neuen Figur anpassen)

| Wert | Datei | Bedeutung bei Ember |
|---|---|---|
| `NECK`, `TAIL_BASE`, `EAR_L/R`, `ARM_CUT`, `SHOULDER_*`, `HAND_*`, `FEET_Y` | `puppet_ember.py` oben | Gelenkpunkte im 1024-px-Bild |
| Startpunkte Kopf `(512, 300)`, Hörner `(400, 200)` / `(630, 220)` | `rig_ember.py` Schritt 2 | ein Pixel mitten in der Fläche |
| Ohr-Schnitt `band`, `px < 357` / `px > 667` | `rig_ember.py` Schritt 2 | wo die Ohren am Gesicht ansetzen |
| Kinnkurve `548 - 38 * dxn²`, Halskerben x 412 / 612 | `rig_ember.py` Schritt 2 | Grenze Kopf / Hals |
| Schwanz-Erkennung (Farbe lila, rechts unten) | `rig_ember.py` Schritt 2 | wie der Schwanz gefunden wird |
| Halsband `NECK0 = 492`, `NECK1 = 582` | Renderer-Vorlage | wo sich der Hals biegt |
| `OUTLINE_MAX = 9`, `OUTLINE_W = 6` | `rig_ember.py` / Vorlage | Umriss entfernen / neu zeichnen |

Werte finden: einen Ausschnitt mit Koordinatenraster rendern (alle 10 px eine beschriftete Linie) und ablesen.

## 3. Augen und Brauen (`eye_design.py`)

`DESIGN` enthält die Grundaugen, nachgezeichnet über die Vorlage (kubische Bézierkurven):

- `top` / `bot`: obere und untere Kurve der Öffnung, vom äußeren zum inneren Augenwinkel
- `lid` / `low`: Dicke des oberen / unteren Lidstrichs (außen, Mitte, innen), `tail`: Ausläufer des unteren Lids
- `pupil`: Mitte, halbe Breite, halbe Höhe, Schräge (Schlitzpupille), `glint`: Glanzpunkt
- `brow`: Mittellinie, Haken am inneren Ende, Dicke (Anfang, Maximum, Ende)
- `colors`: Linie, Pupille, drei Gelbtöne, Schatten unter dem Lid, Glanzpunkt (aus der Zeichnung gemessen)

`VARIANTS` leitet daraus Varianten ab (Lidstärke, Pupillenbreite `pupil_w`, Brauenform `brow_shape`: geschwungen /
`straight` / `flame`, Brauenhöhe `brow_dy`, Neigung `brow_tilt`). In der Vorschau lässt sich unter „Augen“ umschalten;
`default` legt die Startvariante fest. **Ember: Variante T4** (gerade, steile Brauen, breitere Schlitzpupille).

**Was den Ausdruck macht** (gelernt an Ember):
- Die **Augenform aus der Originalzeichnung** (groß, rund, „Glubsch“) gehört zur Figur. Schmale, schräge Augen
  veränderten den Charakter komplett.
- **Dicke Lidstriche** wirken wie Kajal („Emo“). Dünn bis mittel halten.
- **Gemein / teuflisch** wird es über die Brauen (zur Nase hin tief und steil) und Schlitzpupillen, nicht über die
  Augenform. Zu dünne Pupillen wirken leer.

Vorgehen: Vorlage-Augen vergrößert mit Raster rendern, Punkte ablesen, eintragen, mit `--preview` vergleichen,
wiederholen. Dann 3–5 Varianten bauen und die Person entscheiden lassen.

**Animation der Augen:** Blinzeln = obere Kurve wandert zu einer Treffpunkt-Linie (72 % der Öffnung), Lidstrich wandert
mit. Zusammenkneifen = untere Kurve kommt hoch. Blick = Pupille ±12 / ±8 px, Glanzpunkt ein Drittel davon, beide an der
Öffnung abgeschnitten. Brauen: `browY` (hoch / runter) und `browTilt` (positiv = wütend).

## 4. Renderer und Verhalten (`ember_live_prototype_template.html`)

**Zeichnen pro Bild:** Schatten → Figur in eine eigene Leinwand (Schwanz, Ohren, Körper mit Halsband, Arme, Kopf,
Augen, Mund) → Umriss: Silhouette per SVG-Filter weichzeichnen und per Schwelle hart machen (= runde Ausdehnung um
`OUTLINE_W`), in Linienfarbe darunterlegen → Figur darüber → Effekte (S, Noten, Z, Dampf, Krümel).

**Bewegung:** Alles läuft über gedämpfte Federn (`Spring`). Ein „Gehirn“ setzt pro Bild die Ziele:

| Zustand | Was die Figur tut |
|---|---|
| Wartet | atmet, blinzelt (manchmal doppelt), schaut zur Maus, zufällige Kleinigkeiten (herumschauen, Ohr zucken, gähnen, strecken, hüpfen) |
| Müde (~15 s ohne Aktion) | Lider halb, Kopf sinkt; nach ~30 s schläft sie |
| Song läuft | wippt im DAW-Tempo, nickt, singt mit, Noten; stoppt sofort mit dem Song |
| S-Laut | ein „S“ fliegt im Bogen heran, sie schaut es an, reißt den Mund auf und schnappt zu (Kopf nickt, Ohren zucken, Krümel, zufriedener Blick) |
| Über 9 dB | schüttelt den Kopf, Dampf aus den Ohren, wütende Brauen, sagt „Uff, zu viel!“ (höchstens alle 12 s) |
| AUTO | beugt sich vor, lauscht (Schallwellen am Ohr), danach stolzer Hüpfer und ein Satz |
| Bypass | schläft |
| Sprechen | Text tippt in eine Sprechblase; Mund formt Silben (Öffnung + Breite), ruhiges Nicken, eine Handgeste |
| Regler drehen | schaut zum Regler |

**Regeln, die wir gelernt haben:**
- Bei einem Fehler **zuerst die Teile als Bild ansehen** (Teil allein, vergrößert), erst dann am Code drehen.
- Zum Prüfen von Rändern die Bühne **weiß** machen.
- Beim Sprechen keine Federstöße auf Kopf / Hals und **keine Leerlauf-Aktionen** (sonst Gummiball-Hüpfen).
- Stauchen / Strecken klein halten (-0,05 … +0,06), Federn gut dämpfen.
- Mund öffnet nur im mittleren Teil der Lachlinie; Mund-Feder steif (Silben dauern ~0,1 s).
- Funken / Feuer passten nicht; das gefressene S schon (zeigt, was der De-Esser tut).
- Nicht jede „Verbesserung“ ist besser: Bei Ember wurde eine weichere S-Animation und ein feiner geschnittener Umriss
  (v16) wieder verworfen. Vor großen Umbauten einen Stand sichern und vergleichen lassen.

## 5. Testen

In der Vorschau (Browser-Konsole):

```js
window.__emberPose = true;                 // Gehirn anhalten
const S = window.__ember;                  // alle Federn
for (const k in S) { S[k].v = 0; S[k].vel = 0; }
S.pupil.v = 1; S.mouthW.v = 1;
S.head.v = 13; S.headX.v = 10; S.armL.v = -45; S.tail.v = 30;   // Pose
S.lid.v = 1;                               // Augen zu
window.__emberPose = false;                // weiter leben lassen
```

Prüfen (vergrößert, auf Weiß): Kopf ±13° und verschoben; Arme ganz raus / rein; Ohren ±30°; Schwanz ±30°; Strecken /
Stauchen; Augen offen, halb, zu, gekniffen; Blick in alle Ecken; Mund weit offen / schmal; Brauen hoch / wütend.
Bewegungen messen statt schätzen: Federwerte pro Bild aufzeichnen (Minimum / Maximum, Änderung pro Bild).

## 6. Ins Plugin: Animations-Engine (kein Video)

Ember wird im Plugin **live berechnet**, genau wie in der Vorschau. Es werden keine Bildfolgen oder Videos
abgespielt. Das Plugin enthält nur die Teile (wenige PNGs, zusammen ~0,4 MB), die Daten (`rig.json`, `eyes.json`)
und den Code, der daraus jedes Bild neu zusammensetzt.

### Aufbau

```
 Audio-Thread (EmberProcessor)            GUI-Thread (EmberEditor, 60 Hz Timer)
 ─────────────────────────────            ──────────────────────────────────────
 Pegel, Reduktion (dB), Bypass,  ──►  MascotInput (atomics)  ──►  MascotBrain
 AUTO-Status, S-Ereignisse       ──►  lock-free FIFO          ──►  (Zustände, Federn, Aktionen)
 Tempo + Wiedergabe der DAW      ──►  (AudioPlayHead)                    │ Pose: ~25 Werte
                                                                         ▼
                                     MascotRig (einmal geladen) ──►  MascotRenderer ──► Bildschirm
                                     Teile, Netze, Augenkurven       (zeichnet die Pose)
```

**1. `MascotRig` – die Daten (einmal beim Start):**
Teile-PNGs, `rig.json` und `eyes.json` kommen als `BinaryData` ins Plugin. Der Rig lädt sie einmal und hält Bilder,
Netze (Gitterpunkte, Gewichte, Dreiecke), Gelenke und Augenkurven. Alle Fenster teilen ihn.

**2. `MascotBrain` – das Verhalten:**
Eine 1:1-Übertragung der JavaScript-Logik nach C++: Federn (`v`, `vel`, Steifigkeit, Dämpfung), Zustände (wartet,
müde, schläft, Song, S, zu viel, AUTO, stolz, spricht), Leerlauf-Aktionen, Blinzeln, das fliegende S. Eingänge:
- Pegel / Reduktion / Bypass / AUTO über `std::atomic` aus dem Prozessor
- jedes erkannte **S** als Ereignis (Zeit, dB) über einen lock-freien FIFO (`juce::AbstractFifo`), damit kein S verloren
  geht und der Audio-Thread nie wartet
- **Tempo und Wiedergabe** aus `AudioPlayHead::getPosition()` (BPM, `isPlaying`, ppq für den Takt)
- Maus und gedrehte Regler aus dem Editor
Ausgang: eine **Pose** (Kopfwinkel, Arme, Ohren, Schwanz, Lider, Pupillen, Mund, Brauen, Stauchung …).
Das Gehirn ist reiner Code ohne Zeichnen, also mit Unit-Tests prüfbar (z. B. „nach Bypass schläft er“, „ein S führt
zum Zuschnappen“).

**3. `MascotRenderer` – das Zeichnen:**
- **Starre Teile** (Körper, Kopf): `Graphics::drawImageTransformed` mit `AffineTransform`
- **Netze** (Arme, Ohren, Schwanz, Halsband): pro Dreieck die Textur mit einer affinen Abbildung zeichnen (wie in der
  Vorschau), Gitterpunkte vorher pro Bild aus Gelenkwinkel × Gewicht berechnen
- **Augen, Brauen, Mund**: `juce::Path` mit Verläufen (`ColourGradient`), direkt aus den Kurven in `eyes.json`
- **Umriss**: Figur in ein eigenes `juce::Image` zeichnen, Alphakanal um 6 px rund ausdehnen (schneller Max-Filter auf
  dem kleinen Bild, Ember ist im Plugin nur ~300 px groß), in Linienfarbe darunterlegen
- **Effekte** (S, Noten, Z, Dampf): kleine Partikelliste, als Pfade / Text gezeichnet

Zwei Wege, die gleich aussehen:
- **Software (Start):** alles mit `juce::Graphics` in ein Bild, dann ins Fenster. Einfach, läuft überall gleich, bei
  ~300 px Größe schnell genug (Ziel: unter 2 ms pro Bild).
- **OpenGL / GPU (später, falls nötig):** Teile als Texturen, Netze als Vertex-Buffer mit Gewichten im Shader, Umriss
  als Shader-Pass. Nur, wenn Messungen zeigen, dass Software zu viel CPU braucht.

### Leistung und Sauberkeit
- Der Timer läuft nur, solange das Fenster sichtbar ist. Bei Bypass / Schlaf reichen 20 Bilder pro Sekunde.
- Das Zeichnen passiert nur im GUI-Thread, der Audio-Thread schreibt nur Zahlen. Keine Sperren, keine Speicher-
  anforderung im Audio-Thread.
- Mehrere Plugin-Fenster teilen Rig und Bilder (wie heute schon die Frame-Caches).

### Stand im Plugin

- **Schritt 1 erledigt (17.11.2026):** Zweig `feature/ember-live-mascot` (Repo velvox), Commit `be0d5d1`. `Products/Ember/Source/EmberRig.h/.cpp` (`MascotRig`, `RigRenderer`, `RigPose`), Daten in `Products/Ember/Mascot/live` (aus `export_plugin_rig.py`). `EmberTests --rig <ordner>` speichert die Testposen als PNG. 7 ms in Ruhe, 9 ms bewegt bei 300 px.
- Exportieren nach einer Änderung am Rig: `python tools/mascot/export_plugin_rig.py <rig-ordner> <eyes.json> <repo>/Products/Ember/Mascot/live`, danach CMake neu konfigurieren.

### Vorgehen in Schritten
1. `rig.json` / `eyes.json` / Teile ins Plugin (BinaryData), `MascotRig` laden und **eine Ruhepose** zeichnen,
   Screenshot-Vergleich mit der Vorschau.
2. Netze und Halsband; Test-Posen (wie Abschnitt 5) als Bilder in `EmberTests` vergleichen.
3. `MascotBrain` portieren (Federn, Zustände), Eingänge aus dem Prozessor anschließen (S-FIFO, Tempo, Bypass, AUTO).
4. Augen, Brauen, Mund, Umriss, Effekte, Sprechblase.
5. Messen (CPU pro Bild, mehrere Instanzen), pluginval, im Host testen (FL Studio, Ableton, Reaper).
6. Die alten Bildfolgen (`Mascot/frames`, `MascotFrames`) entfernen.

## 7. Sichern

- Stand ablegen unter `Products/<Plugin>/Mascot/live_vNN/`: gebaute HTML, `rig/` mit Teilen, `rig.json`, `eyes.json`,
  und die Renderer-Vorlage. So lässt sich jeder Stand auf jedem PC im Browser öffnen und vergleichen.
- Die Werkzeuge liegen unter `tools/mascot/` (Drive); mit den beiden Originalbildern ergibt
  `build_live_preview.py` exakt denselben Stand (für Ember v17 geprüft).

## Dateien

| Datei | Inhalt |
|---|---|
| `tools/mascot/rig_ember.py` | Figur zerlegen (Teile, Netze, Gewichte, Umriss entfernen); `--no-eyes` |
| `tools/mascot/puppet_ember.py` | Gelenkpunkte, Freistellen, Mund-Linie |
| `tools/mascot/eye_design.py` | Augen und Brauen als Kurven, Varianten, Vorschau |
| `tools/mascot/build_live_preview.py` | alles zusammen zu einer HTML-Vorschau |
| `tools/mascot/ember_live_prototype_template.html` | Renderer und Verhalten (Vorlage) |
| `tools/mascot/eyes_from_reference.py` | Versuch, Augen automatisch zu vermessen (bei Ember zu ungenau) |
| `Products/Ember/Mascot/live_v17/` | gesicherter, freigegebener Stand von Ember |

---

## Hier weitermachen (Stand 19.11.2026)

**Wo der Code liegt:** Repo `velvox`, Zweig **`feature/ember-live-mascot`** (Commit 223955b, "Merge feature/ember-ux").
Drive: `_git-backup/velvox.bundle`, Quellkopie, Testbuild `Plugins/01 EMBER De-Esser/Testbuild_Siegel_2026-11-19/`.
Stand vor dem Höllen-Design: Markierung `ember-vor-hoelle` (Testbuild `Testbuild_vor_Hoelle_2026-11-19/`).

**Erledigt am 19.11.:**
- Schritt 5: Ember zeichnet in eigenem Thread, Gesicht zwischengespeichert, 30 Bilder/s wenn nicht im Vordergrund;
  `EmberTests --bench` misst CPU mit 1/2/4/8 Fenstern (Audio, UI-Thread, Prozess, Zeit pro Bildteil); pluginval 10 ok.
- Design "Siegel" (Jakobs Wahl, Entwurf https://claude.ai/artifact/2DbiyZf8V7SpKonaiNKYNf): pinker Bodenkreis unter
  Ember, schwebende Runen, Feuerschalen, Gewölbe, Runenrand, Lichtwelle bei jedem geschluckten S, Mini-Siegel auf
  dem Amount-Regler, Leuchten am Mund. Code: `EmberSeal.h/.cpp`, `LiveMascotView::renderFrame`, `SealOverlay`.
- Usability-Test umgesetzt (Zweig feature/ember-ux, eingemischt): AUTO mit Vorher auf A, keine heimlichen Resets,
  Klick nur auf Ember, Abbrechen bleibt Abbrechen, Hinweis ohne Ton, Delta nicht gespeichert, Lizenzkarte,
  Spektrum 1-20 kHz, ABSENKUNG-Anzeige, Strg+Z. 296 Tests grün.
- Klang-Test: Bericht `~/dev/velvox-wt/ember-sound-audit/EMBER_KLANG_TEST.md` (Zweig audit/ember-sound, nicht committet).

**Offen:**
1. Jakobs Feedback zum Siegel im Plugin.
2. Klang: AUTO robuster (S-Frequenz, Schwelle nicht bis -60 dB), Stimmlage-Knöpfe ziehen die Schwelle mit; danach
   Glockenfilter im Split-Modus statt offenem Höhenregal (Hörvergleich gegen Pro-DS).
3. CPU: mit Siegel ~82 % eines Kerns bei 60 Bildern/s (1 Fenster, 125 %); nächster Schritt wäre Zeichnen über die GPU.
4. Schritt 6: alte Bildfolgen entfernen; Test in FL Studio / Ableton / Reaper; dann in `feature/ember-deesser`
   übernehmen und Installer neu bauen.

**Wichtig beim Sichern:** `scripts/backup-to-drive.ps1` schreibt das Bundle mit allen Zweigen *dieses* PCs. Vorher die
Zweige vom Drive-Bundle holen (`git fetch <bundle> "+refs/heads/*:refs/drive-backup-all/*"`), sonst gehen Zweige vom
anderen PC im Bundle verloren.
