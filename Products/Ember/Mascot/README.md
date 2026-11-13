# Ember – Bilder für das Maskottchen (2D)

**Stand 2026-11-13:** Ember ist eine 2D-Puppe aus Jakobs einem Standbild `Embernew.png` (Drive
`Plugins/01 EMBER De-Esser/Embernew.png`). KI-Videos blieben nicht „on model“; jetzt wird das eine Bild pro Frame
weich verbogen (Kopf, Schwanz als eigene Ebene, Ohren, Arme, Atmen), die Augen blinzeln wie im Zeichentrick
(zusammendrücken → schwarzer Strich), die Pupillen wandern, der Mund kann sich öffnen; Effekte (Noten, Z, Schweiß,
Rauch, Schallwellen, Funkeln) werden dazugezeichnet.

Neu erzeugen (löscht die alten PNGs hier und schreibt `../mascot.json` mit fps und Mundposition):

    python tools/mascot/puppet_ember.py "<Drive>/Plugins/01 EMBER De-Esser/Embernew.png" Products/Ember/Mascot/frames --plugin

Danach CMake neu konfigurieren (die PNGs werden per GLOB eingebunden). Vorschau ohne Plugin:
`python tools/mascot/puppet_ember.py <png> <ordner> --preview` (GIF pro Stimmung).

| Präfix | Wann | Bewegung | Länge |
|---|---|---|---|
| `idle` | kein Signal | atmet, Kopf wiegt, Schwanz schwingt, blinzelt, Ohr zuckt | 4 s |
| `singing` | Gesang, kein S | wippt (120 BPM), Mund bewegt sich, Augen wandern, Noten | 2 s |
| `snapping` | leichtes S | Ohren zucken, Kopf ruckt, Blick zum S | 1 s |
| `fire` | De-Essing | Kopf zurück, Mund weit offen; **die Flammen zeichnet das Plugin live** aus dem Mund (`mascot.json` `mouth`), so groß wie die Reduktion | 1 s |
| `overheated` | über 9 dB | zittert, Schweißtropfen, hustet Rauch (Sprechblase vom Plugin) | 2 s |
| `sleeping` | Bypass | Augen zu, tiefes Atmen, nickt, „Z“ | 4 s |
| `listening` | AUTO hört zu | Kopf zur Seite, Ohr gespitzt, Blick, Schallwellen | 3 s |
| `proud` | AUTO fertig | Hüpfer, Brust raus, Funkeln | 2 s |

Format: 600 × 600 PNG, 256 Farben mit Transparenz, 12 Bilder/s (`mascot.json`). Clips `<state>_in_###` /
`<state>_out_###` werden weiter unterstützt, die Puppe braucht sie nicht (alle Stimmungen teilen dieselbe Grundpose,
das Plugin blendet über). Ein Klick auf Ember startet AUTO.
