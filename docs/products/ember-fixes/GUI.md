# EMBER – GUI-CPU (Zweig fix/ember-gui, 23.11.2026)

Ziel: deutlich weniger CPU für Ember und das Siegel, ohne sichtbaren Unterschied, plus eine Einstellung „Animation“.

## Was geändert wurde

1. **Runenrand (SealOverlay):** Die ~200 Runen wurden bei *jedem* Neuzeichnen im Fenster (Ember 60×/s, Meter 30×/s)
   gezeichnet, und jede Rune war ein Speicherbild, das Direct2D bei jedem Zeichnen neu auf die Grafikkarte lud. Jetzt:
   Runenbilder einmal als Direct2D-Bilder (gleiche Pixel), Runen außerhalb des neu gezeichneten Bereichs werden
   übersprungen (Clip-Test), Mini-Siegel nur, wenn der Knopf betroffen ist. Ein festes Bild des Randes ist nicht
   nötig (er bewegt sich; mit Clip-Test kostet er nur noch in seinen eigenen Streifen, 15×/s).
2. **Ember-Bild:** wird in *ein* Bildschirm-Bild kopiert und dort aktualisiert, statt jedes Bild als neue Textur
   hochzuladen. Rendermaßstab auf 2× begrenzt (darüber nicht sichtbar). Bodenkreis-Ebene nur so groß wie der Boden,
   kein Löschen unter der deckenden Wand, Figur nur neu, wenn sich die Pose ändert.
3. **S-Glyphe** einmal als Pfad (100 px), pro Bild nur skaliert; Sprechblasen-Layout nur bei neuem Text.
4. **paint() wartet nie** auf den Zeichen-Thread: zeigt das letzte fertige Bild (gestreckt, wenn sich die Größe gerade
   geändert hat); nur das allererste Bild wird mit Try-Lock direkt gezeichnet. Die Wand (Backdrop) wird ohne
   Render-Sperre übergeben.
5. **Pause:** wenn das Host-Fenster minimiert oder das Plugin-Fenster versteckt ist (`EmberWindowState.cpp`, fragt das
   oberste Fenster), und unter der Lizenzkarte (Ember und Runenrand stehen). Überdeckung durch andere Fenster wird
   *nicht* erkannt (offen).
6. **Menü ≡ → Animation: Voll / Reduziert / Aus** (gespeichert in `EMBER.settings`, Schlüssel `animation`, gilt für
   alle Fenster). Reduziert = 30 Bilder/s, Runen, Kreis, Rand und Mini-Siegel stehen, weniger Krümel. Aus = Ember in
   Ruhepose auf stillem Siegel, neu gezeichnet nur bei Änderung (Bypass, Blase, Größe), 4 Prüfungen/s.
7. **Rig** (`MascotRig`) nicht mehr als `static`, sondern über `MascotRig::acquire()` geteilt (weak_ptr) und mit dem
   letzten Fenster freigegeben. Test: „the rig is freed with the last window“.
8. **Größe ändern:** Kammer-Wand wird erst neu gebaut, wenn die Größe 250 ms steht (bis dahin gestreckt); Ember wechselt
   die Pixeldichte erst nach 200 ms.
9. **Mehrere Fenster:** Rig geteilt; je Fenster ein Zeichen-Thread (bewusst: verteilt die Arbeit auf Kerne, untätige
   Threads kosten nichts). Zusätzlich: Reduktionsmeter zeichnet nur bei Änderung neu (Stille = keine Neuzeichnungen).

## Messung (`EmberTests --bench`, 6 s, Median aus 3 Läufen)

Achtung: Der Rechner (8 Threads, VM) war durch parallele Agenten stark ausgelastet, die Werte streuen um ±20 %.
UI = Message-Thread, Prozess = ganzer Prozess, jeweils % eines Kerns.

| Fall | vorher UI / Prozess | nachher UI / Prozess |
|---|---|---|
| 1 Fenster 100 %, Voll, 60 fps | 34,9 / 93,8 | 15,1 / 73,7 |
| 1 Fenster 125 %, Voll, 60 fps | 32,8 / 87,8 | 14,8 / 68,5 |
| 1 Fenster 150 %, Voll | 20,1 / 62,8 (fiel auf 30 fps) | 8,3 / 47,7 bei 30 fps; 17,4 / 89,1 bei 60 fps |
| 1 Fenster 125 %, Maus woanders (30 fps) | 18,2 / 44,5 | 9,6 / 35,7 |
| Reduziert 100 / 125 / 150 % | – | 7,6 / 29,2 · 8,6 / 36,7 · 9,1 / 45,0 |
| Aus 100 / 125 / 150 % | – | 4,4 / 7,8 · 4,4 / 7,0 · 5,2 / 7,0 |
| 4 Fenster 125 %, Voll | 62,6 / 190,4 | 29,4 / 148,3 |
| eigenes Fenster minimiert | 0,5 / 1,6 | 0,5 / 1,6 |
| im Host-Fenster, Host minimiert (`hosted=1 min=1`) | 6,3 / 24,6 (lief mit ~22 fps weiter) | 3,4 / 5,3 (steht) |

Fazit: UI-Thread etwa halbiert; der Rest des Prozesses ist der Zeichen-Thread (Figur ~3,5 ms, Boden ~1,5 ms, Runen und
Feuer ~1 ms pro Bild bei 125 %). Für weniger geht es nur über weniger Bilder („Reduziert“) oder die GPU.

## Gleiches Aussehen

- `EmberTests --frames <dir>` (neu): 21 feste Bilder der SIMPLE-Ansicht (Ember mit fliegenden S, Welle, Runen) bei 100 %
  und 125 % über den Direct2D-Renderer: vorher/nachher **0 von 42 Bildern verschieden** (pixelgleich).
- `EmberTests --snapshots`: **0 von 112 Bildern verschieden**.

## Tests

EmberTests 298 bestanden, 0 fehlgeschlagen. pluginval 1.0.4 Strictness 10 (mit GUI-Tests): SUCCESS.

## Offen

- Überdeckung durch andere Fenster (Host im Hintergrund, aber nicht minimiert) wird nicht erkannt.
- Figur (Teile-Raster + Umriss) ist der größte Rest; weitere Einsparung nur mit GPU-Zeichnen oder geringerer Bildrate.
- Menü-Hilfetext zu ≡ („Sprache, Handbuch, Lizenz …“) nennt die Animation noch nicht (Texte gehören zu fix-ux).
- Bench-Werte bei ruhigem Rechner wiederholen (`--bench 6 scales=1,1.25,1.5 counts=1 front=yes anim=full|reduced|off`,
  `hz=60` erzwingt die Bildrate, `hosted=1 min=1` simuliert den minimierten Host).
