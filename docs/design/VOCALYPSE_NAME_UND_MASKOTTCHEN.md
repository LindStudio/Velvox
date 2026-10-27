# VOCALYPSE – neuer Name und Maskottchen (Stand 2026-10-27)

Notiz für die nächste Sitzung. Noch **nichts davon ist im Code umgesetzt**. Erst Mockups, dann Jakobs Freigabe, dann Umsetzung.

## Name
- **Entscheidung:** Das Plugin heißt künftig **VOCALYPSE** (ersetzt „Headliner“).
- **Warum der alte Name weg muss:** „Headliner“ ist als Marke von Mixware LLC eingetragen (DJ- bzw. Musik-Hardware), dazu kommen headliner.app und die Big-Fish-Audio-Library „Headliner“.
  Zusätze wie „Suite“, „Vocal Suite“ oder „Vocal Hub“ lösen den Konflikt nicht, weil sie nur beschreiben und „Headliner“ der prägende Teil bleibt.
- **Vorprüfung der Kandidaten** (Websuche, USA und Plugin-Shops, keine Rechtsberatung):

  | Name | Ergebnis | Risiko |
  |---|---|---|
  | Vocalypse | keine Treffer | niedrig → **gewählt** |
  | Stagedive | US-Anmeldung 98325735, StageDive LLC, Musik-Streaming-App (läuft noch) | mittel |
  | Loudmouth | US-Marke 3309550, verstärkte Lautsprecher, eingetragen und verlängert | eher hoch |
  | Anthem | Universal-Audio-Synth-Plugin „Anthem“ (2025) | raus |
- **Noch offen:** Prüfung der EU-Register (TMview/EUIPO, z. B. über den Browser), Prüfung durch einen Markenanwalt, Domain prüfen.
  Umbenennung in Code, UI, Website und Installer erst nach Jakobs Go.
- **Herstellername bleibt „Lind Audio“** (Entscheidung Jakob, 2026-10-27), obwohl er der US-Marke „LINDELL AUDIO“ (Reg. 5839289) ähnelt. Nicht erneut zur Diskussion stellen.

## Maskottchen – Anforderungen (Jakob)
- Soll im Kopf bleiben; Party- und Bühnen-Thema ist erwünscht.
- **Anzeige:** reagiert auf das Signal bzw. die Gesangsqualität.
- **Tutorial-Guide:** führt durch die Tour.
- Danach **sitzt oder steht es seitlich**; ein Klick darauf hilft dem Nutzer.
- **Der Vocal-Check-Button entfällt:** Ein Klick aufs Maskottchen startet den Vocal Check.
  Empfehlung: beim ersten Mal eine Sprechblase („Klick mich für den Vocal Check!“), damit niemand die Funktion übersieht.
- In der **SIMPEL-Ansicht** ist viel Platz für eine große Animation.

## Ideen
1. **Stickman-Frontmann** (Jakobs Idee): singt auf der Bühne, die Crowd im Hintergrund wächst und springt mit, je besser der Gesang ist.
   Gag: Bei einem Top-Ergebnis im Vocal Check macht er einen Stagedive in die Crowd. Viel Show, aber großer Platz- und Animationsaufwand.
2. **Mund** (Jakobs Loudmouth-Idee): öffnet sich weiter, je besser es klingt, und verzieht bei Problemen das Gesicht (Zischlaute, Übersteuerung).
   Spricht in Sprechblasen. Sehr einprägsam, kann gleichzeitig das Logo sein, klein genug für die Seitenleiste.
3. **Mikrofon mit Gesicht:** glüht, wackelt und schwitzt je nach Pegel. Klar und niedlich, aber wenig eigenständig.
4. **„Apokalypse“-Figur** (kleines Monster oder Rock-Teufelchen): dreht bei gutem Gesang durch, mit Flammen und Blitzen.
   Passt am besten zum Wortspiel, braucht aber am meisten Design, und der Stil muss zum edlen Plugin-Look passen.

**Empfehlung (Claude): Kombination aus 1 und 2.** Ein kleines Maskottchen (Mund bzw. Kopf mit großem Mund) als Guide und Vocal-Check-Knopf am Rand,
dazu in SIMPEL die große Crowd-Animation als Anzeige.

## Mockups (erledigt)
Canvas https://claude.ai/artifact/XbT1vrPjeR7K6SCmCyrBLe, Artboards 6 (4 Figuren × schlecht/okay/super, Neon-Linien) und
7 (SIMPEL mit Crowd-Bühne + Mund als Vocal-Check-Knopf). Jakob: „cool, aber ich will realistische Maskottchen, keine Striche“.

---

# 3D-Teufelchen (Stand 2026-10-27, Abend)

## Entscheidungen
- **Hauptmaskottchen = rotes 3D-Teufelchen** („Little Fire King“: Partyhütchen zwischen den Hörnern, Jeansweste mit
  Konfetti, Flammen-Hose, Nieten-Armbänder). Stil: glänzendes Designer-Vinyl-Toy, Pixar-artig.
- **Lippen-Figur** nicht in 3D (kein Mensch-Körperbau, Mund müsste sich öffnen). Eventuell später als 2D-Logo/App-Icon.
- **Kein KI-Video im Plugin** (reagiert nicht aufs Signal, kein Alphakanal, Figur nicht konstant). KI-Video nur für Werbung.
- **Kopf-Hände-Überschneidungen** (großer Kopf, Standard-Bewegungen): Jakob hat die bestmöglichen Bewegungen gewählt
  („besser geht's nicht“). Beim Rendern prüfen; wo es sichtbar ist, Arme in Blender per Skript nach außen drehen.

## Dateien
| Was | Ort |
|---|---|
| KI-Konzeptbilder (Higgsfield, Nano Banana; nur 896 × 1200) | `art/mascot/concepts/` (+ Drive `Character\`) |
| 3D-Modell mit Skelett + alle Bewegungen (Meshy) | `art/mascot/devil-3d/devil_rig_allmoves.zip` (+ Drive `Character\3d\…ALLMOVES.zip`) |

Inhalt des ZIPs: `Meshy_AI_Little_Fire_King_biped_Meshy_Merged_Animations.fbx` (Modell + Skelett + 12 Bewegungen) und
4 PBR-Texturen (`texture_0` = Farbe 2048², `_metallic`, `_roughness`, `_normal`).

**Rig:** Humanoid, 28 Knochen mit Mixamo-Namen (`mixamorig:Hips` … plus `headfront`) → Mixamo-Bewegungen passen direkt.
Schwanz und Hörner haben **keine** Knochen (Schwanz in Blender selbst riggen, damit er mitschwingt).
Mesh ca. 35.700 Punkte / 55.000 Dreiecke (Meshy-Einstellung 30k, Quad).
**Gesicht ist in die Farbtextur gemalt** (Augen + kleiner Mund, unten links in `texture_0`) → Mundformen und Blinzeln als
Texturwechsel lösen, kein 3D-Artist für Shape Keys nötig. Vorlage für die Ausdrücke: `concepts/RedDevilGesichter.png`
(neutral, „Ooh“/Zwinkern, Schreien mit Reißzähnen, Zähne zusammenbeißen, traurig).

## Bewegungen → Zustände im Plugin
| Zustand | Bewegung |
|---|---|
| Plugin öffnet sich | `Wake_Up_and_Look_Up` (wacht auf, schaut hoch) |
| Ruhe / kein Signal | `Short_Breathe_and_Look_Around` |
| **Schlecht** (Jakobs Wahl) | `Stand_and_Chat` (telefoniert, ignoriert den Gesang) + am Boden liegen (Liegepose aus dem Anfang von `Wake_Up_and_Look_Up`) |
| Tutorial-Guide | `Call_Gesture` (ruft/winkt: „Klick mich!“), Reinlaufen mit `Walking` |
| Okay | `Joyful_Dance_with_Hand_Sway`, `Arm_Circle_Shuffle` |
| Gut | `Hip_Hop_Dance` |
| Super | `All_Night_Dance`, `Cardio_Dance` |
| Top-Ergebnis Vocal Check | `_360_Power_Spin_Jump` |
| Übrig | `Running` |

Vor dem Rendern mit Jakob kurz bestätigen, dass `Stand_and_Chat` wirklich die Telefonier-Bewegung ist.

## Technischer Plan (Blender → Plugin)
1. **Blender** (auf Jakobs anderem PC installiert) per Python-Skript steuern, z. B. `blender -b -P tools/mascot/render_mascot.py`.
   Skript noch zu schreiben: FBX importieren, Texturen anhängen, Bühnenlicht (Lila/Pink, Cyan-Kante wie im Werbebild
   `concepts/RedDevilWerbung.png`), Kamera von vorne leicht unten, transparenter Hintergrund (Film → Transparent), EEVEE.
2. **Zuerst ein Vorschaubild + eine Tanzschleife** rendern und Jakob zeigen. Dabei Modellqualität prüfen (Gesicht, Hörner,
   Hände), und ob die Hände sichtbar durch den Kopf gehen.
3. **Schleifen pro Zustand** als PNG-Folgen mit Alpha, 30 fps. Jede Schleife in **3 Mund-Varianten (zu / halb / offen)**,
   Bild für Bild synchron → das Plugin wählt pro Bild die Variante nach dem Pegel = singt live mit, ohne Ruckeln.
   Mund-Varianten über Texturwechsel im Gesicht. Ziel: zusammen ca. 15–20 MB im Plugin, Größe etwa 400 px (2× für HiDPI).
4. **Schwanz** bekommt eigene Knochen mit leichtem Nachschwingen. Glühen (Augen, Flammen) als Emission in Blender,
   nicht in der Textur.
5. **Im Plugin (JUCE):** Sprite-Abspieler; Zustand aus Vocal-Check-Ergebnis bzw. Signalqualität, Mund-Variante aus dem
   Pegel; Klick aufs Teufelchen startet den Vocal Check (ersetzt den Vocal-Check-Button), beim ersten Mal Sprechblase
   „Klick mich für den Vocal Check!“. Vorher Mockup an Jakob, Layout erst nach Freigabe.

Alternative für später: echtes Echtzeit-3D (glTF + OpenGL in JUCE). Freier, aber viel Arbeit und in manchen DAWs fehleranfällig.

## Farben: Teufelchen rot, Plugin Cyan → Violett → Pink
Frage von Jakob: Plugin-Farben deshalb ändern? **Empfehlung: nein, Palette behalten.**
- Kalte Oberfläche + heißes Maskottchen = starker Kontrast; das Teufelchen springt sofort ins Auge. Das Werbebild zeigt,
  dass Rot im Lila/Pink-Bühnenlicht mit Cyan-Kante schon sehr gut zusammenpasst.
- Rot ist im Styleguide für echte Probleme reserviert. Eine rote Oberfläche würde dauernd nach Warnung aussehen.
- Die Palette ist freigegeben und überall im Code (`Source/UI/Theme.h`); Umfärben wäre viel Arbeit ohne Gewinn.
- Kleine Brücke möglich: Flammen-Orange als Akzent nur rund um das Maskottchen (Sprechblase, Vocal-Check-Ergebnis „super“).
Endgültige Entscheidung liegt bei Jakob.

## Assistent-Konzept (von Jakob freigegeben: „perfekt“)
Canvas-Artboards 8–12 (`Teufel.dc.html` = Komponente mit Prop `szene`, dazu 4 Hüllen per `dc-import`).
Freigestellte Figur + Köpfe liegen als Assets im Canvas (aus `concepts/RedDevil.png` und `RedDevilGesichter.png`).
- Das Teufelchen ist der **Assistent für alles**: Tour, Auto Mix, Ergebnis, Vocal Check, Fehlersuche.
- **Fester Platz in SIMPEL unten links**: Kopf (Ausdruck je nach Lage) + Sprechblase mit 1–2 Aktionsknöpfen.
  **Klick auf den Kopf = Vocal Check.** VOCAL-CHECK-Tab im Header fällt weg; Vorher/Nachher sitzt rechts über dem Spektrum;
  die alte Knopfreihe unter dem Spektrum wandert in die Sprechblase.
- **Tour:** Bildschirm abgedunkelt, die erklärte Stelle leuchtet (Cyan-Rahmen), daneben die ganze Figur groß mit
  Sprechblase, „TOUR · n / 6“, Weiter / Zurück / Tour beenden.
- Zustände → Gesicht: Tour/Ruhe = neutral · Analyse = „Ooh“ · Auto Mix fertig = schreit vor Freude ·
  Problem gefunden = Zähne zusammen · kein Signal = traurig.
- **Ton: frech und locker, duzt.** Beispiele: „Spiel den Song ab, ich hör mir deine Stimme an.“ ·
  „Autsch, bei 6,8 kHz zischt es ordentlich. Soll ich den De-Esser stärker machen?“ ·
  „Ich hör gar nichts. Ist die Spur scharf geschaltet?“

## Name des Teufelchens (offen, Jakob entscheidet am anderen PC)
Kandidaten von Jakob: **Cheeko**, **Memphy**, **Nando**. Websuche (nur USA/Web, EU-Register nicht geprüft):
- **Cheeko** – keine Treffer; von „cheeky“ = frech. Claudes Empfehlung.
- **Memphy** – keine Treffer; erinnert an Memphis (Elvis, Blues) + Mephisto. Rückfrage offen: wirklich „Memphy“ oder „Mephi“?
- **Nando** – riskant: Nando's (weltbekannte Hähnchenkette, Flammen, Rot) – berühmte Marke, falsche Assoziation.
Weitere Ideen aus der Liste: Mephi, Pepper, Ziggy, Puck, Nicky, Brimmy, Chili, Ember, Impy, Riffy.
Nicht nehmen: Vox (Verstärker-Marke), Sparky (Maskottchen Arizona State), Jinx/Fizz (League of Legends), Diablo, Hellboy, Lucifer.
Gewählten Namen vor Release mit „Vocalypse“ vom Markenanwalt prüfen lassen.

## Hut kommt weg (Jakob, 2026-10-27)
Jakob entfernt das Partyhütchen. Achtung: Das 3D-Modell in `devil-3d/` hat den Hut noch (ein einziges Mesh).
Zwei Wege: (a) Hut in Blender wegschneiden, Loch am Kopf schließen, Textur dort rot übermalen – günstig, Rig und
Bewegungen bleiben; (b) neues Bild ohne Hut → Meshy neu (Modell, Rig, alle 12 Bewegungen neu exportieren).
Empfehlung: zuerst (a) probieren. Falls Jakob schon ein neues Modell ohne Hut gemacht hat, das nehmen und die
Bewegungsliste oben gleich halten. Auch die Canvas-Freisteller (Artboards 8–12) zeigen noch den Hut.

## Stand 2026-10-27, später Abend (PC „jakob“)
- **Neues Modell „Ember Imp“ ohne Hut** ersetzt Little Fire King (Jakob bestätigt): `art/mascot/devil-3d/Ember_Imp_All_Animations.glb`
  (Modell + Rig + 11 Bewegungen in einer Datei; Drive `Character\3d\`). Gleiches Mixamo-Rig.
  Bewegungen: All_Night_Dance, Arm_Circle_Shuffle, Call_Gesture, Cardio_Dance, Running, Short_Breathe_and_Look_Around,
  **Stand_Talking_Angry (neu)**, Stand_and_Chat, Wake_Up_and_Look_Up, Walking, _360_Power_Spin_Jump.
  **Fehlen gegenüber der Tabelle oben:** Joyful_Dance_with_Hand_Sway, Hip_Hop_Dance → Zuordnung „Okay“/„Gut“ mit Jakob neu klären.
- Blender 5.2.3 LTS auf diesem PC installiert (`C:\Program Files\Blender Foundation\Blender 5.2\blender.exe`), RTX 4060 Ti.
- **Render-Skript fertig:** `tools/mascot/render_mascot.py` (Modi `list`, `sheet`, `loop`; EEVEE, Alpha, 30 fps, Bühnenlicht).
  800 px, 64 Samples ≈ 0,2 s/Bild. Kontaktbogen + `All_Night_Dance` (246 Bilder) in `velvox-bench\2026-10-27\mascot\`.
  Hände gehen in der Stichprobe nicht durch den Kopf. `_360_Power_Spin_Jump` verlässt das Bild → eigener Ausschnitt nötig.

## Nächste Schritte (am PC mit Blender)
1. `git fetch origin` und Branch `feature/i18n` holen (enthält diese Datei und `art/mascot/`).
2. ZIP `art/mascot/devil-3d/devil_rig_allmoves.zip` in einen eigenen leeren Ordner entpacken.
3. Prüfen, ob `blender` im PATH ist (sonst vollen Pfad nehmen, z. B. `C:\Program Files\Blender Foundation\Blender 4.x\blender.exe`).
4. Render-Skript schreiben (`tools/mascot/`), Vorschaubild + eine Tanzschleife rendern, Jakob zeigen.
5. „Schlecht“-Varianten bauen, Mund-Texturen malen bzw. aus `RedDevilGesichter.png` ableiten, alle Schleifen rendern.
6. Mockup des Teufelchens in SIMPEL (Canvas) → Freigabe → JUCE-Umsetzung.
