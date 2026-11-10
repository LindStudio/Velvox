# Ember – Bilder für das Maskottchen (2D)

Ember zeigt im Plugin acht Stimmungen. Für jede Stimmung kannst du eine Bildfolge (Animation) liefern.
Solange eine Stimmung keine Bilder hat, zeichnet das Plugin dafür den Platzhalter-Teufel. Du kannst also
eine Stimmung nach der anderen liefern.

## So kommen die Bilder ins Plugin

1. PNG-Dateien in den Ordner `Products/Ember/Mascot/frames/` legen.
2. Dateiname: `<stimmung>_<nummer>.png`, zum Beispiel `fire_001.png`, `fire_002.png` und so weiter.
   Die Nummern bestimmen die Reihenfolge, also bitte immer dreistellig (`001`, `002` …).
3. Neu bauen (`scripts/build.ps1` oder CMake neu konfigurieren). Die Bilder landen dann automatisch im Plugin.

## Die acht Stimmungen

| Datei-Präfix | Wann | Idee (vom Plan 2026-11-10) | Schleife |
|---|---|---|---|
| `idle` | kein Signal | sitzt da, raucht Kringel aus der Nase, Schwanz wippt | ja |
| `singing` | Gesang, kein S | wippt im Takt mit, Augen wandern | ja |
| `snapping` | leichtes S (bis 1,5 dB) | Ohren zucken, schnappt das „S“ aus der Luft | ja |
| `fire` | De-Essing | spuckt Feuer nach rechts | ja |
| `overheated` | zu viel (über 9 dB) | hustet, schwitzt (Sprechblase macht das Plugin) | ja |
| `sleeping` | Bypass | schläft, „Zzz“ | ja |
| `listening` | AUTO hört zu | Hand am Ohr, lauscht | ja |
| `proud` | AUTO ist fertig (2,5 s) | Daumen hoch | ja |

Bei `fire` zeigt das Plugin derzeit eine Bildfolge. Die Stärke (wie viel reduziert wird) steht als Wert 0–1 bereit.
Wenn du für `fire` drei Stärken zeichnen willst (klein / mittel / groß), sag Bescheid, dann wähle ich sie danach aus.

## Format

- PNG mit transparentem Hintergrund, Ember mittig.
- Seitenverhältnis 5 : 4 (Querformat), empfohlen **1000 × 800 px**. Im SIMPLE-Fenster ist Ember etwa 500 × 400 groß,
  bei 200 % Zoom 1000 × 800.
- Platz nach rechts lassen: Feuer und Sprechblase gehen nach rechts oben.
- 12–30 Bilder pro Sekunde. Standard sind 24. Pro Stimmung kann man das in `mascot.json` ändern
  (`"states": { "fire": 30 }`).
- Größe im Blick behalten: 8 Stimmungen × 24 Bilder × etwa 150 KB sind schon etwa 30 MB. Kurze Schleifen
  (1–2 Sekunden) reichen.

## Klick auf Ember

Ein Klick auf Ember startet AUTO, genau wie der AUTO-Knopf.
