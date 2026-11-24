# EMBER – GUI-Runde (Branch `r3/ember-gui`, 24.11.2026)

Jakobs Rückmeldung zur Oberfläche, Punkt für Punkt umgesetzt. Nur GUI: Engine, AUTO, Presets und DSP-Verhalten sind
nicht angefasst (parallel läuft `r3/ember-evenness`). Basis: `feature/ember-deesser` (3a9bbe3).

Bilder: `C:\Users\Shadow\dev\velvox-bench\2026-11-24\gui_round\before` und `…\after` (gleiche Dateinamen, DE und EN,
`_150` = Fenster 150 %). Erzeugt mit dem neuen Aufruf `EmberTests --gui-shots <ordner>` (vorher-Bilder mit derselben
Funktion auf dem Stand 3a9bbe3 gebaut).

| Datei | zeigt |
|---|---|
| `simple_de/en(.png, _150)` | SIMPLE mit Embers Tipp auf der Tafel |
| `advanced_de/en(.png, _150)` | ADVANCED |
| `plate_de/en`, `plate_typing_de/en` | Tafel mit langer Meldung, fertig und halb getippt |
| `menu_preset_*`, `menu_main_*`, `menu_sub1…8_*` (nur nachher) | Preset-Menü, ≡-Menü, alle Untermenüs |
| `dialog_rename_*`, `dialog_delete_*`, `dialog_warning_*`, `dialog_licence_*` | Umbenennen, Löschen-Rückfrage, Warnung ohne Elternfenster, Lizenzschlüssel |
| `info_*` | Infokarte (Release-Regler) |
| `locked_*` (nur vorher) | Lizenzkarte |

## Die Punkte

1. **Punkt im Stern weg.** Der Funke, der über die Linien des Pentagramms lief (`drawFloorLive`), ist entfernt. Auf
   dem Knopf gab es keinen eigenen Funken; dort war es das sich drehende Siegel (Punkt 2).
   Bilder: `plate_de.png` vorher (Funke unten am Boden) / nachher.
2. **Knopf dreht sich nicht mehr von selbst.** Das kleine Siegel auf dem Amount-Knopf folgt nur noch dem Wert:
   eine Spitze des Sterns sitzt immer auf dem Wertpunkt des Knopfs (`drawMiniSeal (…, angle, …)`, Winkel = gezeigte
   Knopfstellung). Neu gezeichnet wird es nur, wenn sich der Knopf oder sein Licht ändert (vorher 15× pro Sekunde).
   Das Leuchten folgt weiter leicht der Absenkung. Der große Bodenkreis dreht sich weiter langsam (war nicht gefragt).
3. **Tafel statt Sprechblase.** Feste Tafel oben mittig in der Szene, zwischen den Ketten (312 × 40 px, zwei kurze
   Kettenglieder), wie die Panels: dunkles Glas, dünner pinker Rand, Schrift der Infozeile. Der Text tippt sich wie
   bisher ein, aber in seinem fertigen Layout (nichts springt beim Tippen). Lange Texte nehmen zwei Zeilen in kleinerer
   Schrift. Leer: die Tafel blendet in ¼ s aus (ruhiger als ein leerer Rahmen); Animation „Aus“: ohne Blende.
   Gleiche Meldungen wie vorher (AUTO-Zustände, „Klick mich für AUTO!“, „Uff, zu viel! …“). Damit die Tafel Platz
   hat, steht Ember etwas kleiner auf dem Siegel (Figurhöhe 80 → 74 % der Bühne, Füße an derselben Stelle).
   Bilder: `simple_*`, `plate_*`, `plate_typing_*`.
4. **Frequenz und Breite im Spektrum** (wie Pro-DS/Pro-Q, Marker und Absenkung bleiben in einer Anzeige):
   pinke Linie ziehen = Frequenz; Kante des S-Bereichs ziehen = Breite (die Kante folgt der Maus); Mausrad über dem
   Spektrum = Breite (¼ Oktave pro Raste; im Breitband-Modus, der keine Breite hat: Frequenz); Shift/Strg = fein;
   Doppelklick auf Linie bzw. Kante = Standardwert. Neben der Linie steht „6,00 kHz · 3,00 Okt.“ (Breitband nur
   „6,00 kHz“), bei hohen Frequenzen links der Linie. Mauszeiger ↔ über Linie und Kanten, die gefasste Stelle leuchtet
   auf. Der Hinweis „Spiel Audio ab …“ liegt jetzt auf einer dunklen Unterlage. Bilder: `advanced_*`.
5. **Feuerschalen weg.** Flammen und Schalen auf den Säulen entfernt, die dunklen Säulen bleiben. Bilder: `simple_*`.
6. **Menüs im Plugin-Stil.** Preset-Menü (mit Untermenüs, Umbenennen/Löschen) und ≡-Menü (Sprache, Größe,
   Animation, Lizenz): dunkles Glas mit runden Ecken und dünnem pinkem Rand, pinke Markierung beim Überfahren,
   gewählter Eintrag mit pinker Raute statt grauem Haken, Überschriften in Pink-Versalien, Trennlinien pink. Ursache des
   grauen Menüs: Menüs sind eigene Fenster und bekamen JUCEs Standard-Look. Bilder: `menu_*`.
7. **ADVANCED-Regler.** FREQUENZ und BREITE sind keine Knöpfe mehr (Punkt 4). Sieben Knöpfe in drei Gruppen mit
   feinen Trennlinien: Threshold · Range · Amount | Attack · Release | Mix · Output, etwas größer (56 statt 50 px).
   LOOKAHEAD und STEREO-LINK sind kleine Schieberegler rechts (Name + Wert in einer Zeile, schmale Spur; gleiche Gesten
   wie Knöpfe), STEREO-LINK direkt über dem STEREO-Schalter und abgeblendet bei Mid/Side (dort wirkt er nicht).
   Untere Reihe neu verteilt, damit „S-Bereich“/„Allround“ nicht anstoßen. Threshold bleibt an der S-Pegel-Linie
   ziehbar. Parameter unverändert (Automation, Presets). Bilder: `advanced_*`.
8. **Kein Blau mehr.** `EmberLookAndFeel` überschreibt jetzt alles, was der geteilte VOCALYPSE-Look blau/türkis/violett
   malt: AlertWindows (eigene Box: dunkles Glas, pinker Rand und Strich oben, Symbol als kleines pinkes Zeichen statt
   türkisem Kreis), Textfelder (pinker Fokusrand, pinke Markierung), Tooltips, Scrollbalken, Combo-/Listenfarben,
   eingeschaltete Knöpfe (DELTA/BYPASS in ihrer eigenen Farbe statt Blau-Violett-Neon), „primary“-/Link-Knöpfe,
   Infokarte (pinkes Leuchten, Wert und Strich statt Türkis/Aurora), Spektrumkurve (pink statt Türkis→Blau),
   Eckgriff. Normale Knöpfe haben einen feinen pinken Rand und warmgraue Schrift, nicht gewählte Segmente warmgrau.
   Solange ein EMBER-Fenster offen ist, ist sein Look auch der Standard-Look der Plugin-DLL (`EmberDefaultLook`):
   Meldungen ohne Elternfenster (z. B. nach dem Aktivieren) erscheinen damit auch im EMBER-Stil; mit dem letzten Fenster
   kommt JUCEs Standard zurück. Der Lizenzschlüssel-Dialog nimmt über `setActivationDialogLook` den EMBER-Look.
   Bilder: `dialog_*`, `info_*`.
9. **Texte geprüft** (DE/EN, siehe Liste unten), Handbücher angepasst.

## Geänderte Texte

| Wo | vorher | nachher |
|---|---|---|
| Infokarte Spektrum | „Die pinke Kante ziehen = Frequenz (Shift = fein, Mausrad = Schritt) …“ | erklärt Linie = Frequenz (mit Stimm-Richtwerten, aus der alten Frequenz-Karte), Kanten/Mausrad = Breite (aus der alten Breite-Karte), Breitband-Mausrad, Doppelklick; Wert „6,00 kHz · 3,00 Okt.“ |
| Infokarten Frequenz, Breite | eigene Karten | entfallen (im Spektrum) |
| Lookahead, Stereo-Link | Knopf „LINK“ | kleine Regler „LOOKAHEAD“, „STEREO-LINK“ / „STEREO LINK“; Link-Text nennt „bei L/R und M/S“ |
| Stereo-Karte | „L/R … (mit Stereo Link). M/S: Mitte und Seiten getrennt.“ | beide „(mit Stereo-Link)“, einheitliche Schreibweise |
| Abhören-Karte | „(Split: das S-Band, Breitband: ab der Frequenz)“ | „(Split: der pinke S-Bereich im Spektrum, Breitband: alles ab der Frequenz)“ – ein Begriff (S-Bereich) |
| Statuszeile S-Bereich | „… den Bereich, in dem nach S-Lauten gesucht wird“ (falsch seit dem DSP-Fix) | „… den Bereich, der bei S-Lauten abgesenkt wird“ |
| Statuszeile ADVANCED | „Doppelklick setzt ihn zurück.“ | + „Im Spektrum: Linie ziehen = Frequenz, Kanten oder Mausrad = Breite. Doppelklick = Standard.“ |
| ADVANCED-Knopf | „Alle Regler, das Spektrum mit dem S-Bereich …“ | „Alle Einstellungen: das Spektrum mit Frequenz und Breite des S-Bereichs, …“ |
| AUTO-Karte | „Er stellt nur Frequenz, Threshold, Range und Release ein“ | + „(und Amount auf 50 %, wenn es unter 25 % stand)“ – stimmt mit dem Code und Handbuch überein |
| ≡-Karte | ohne Animation | „… Sprache, Animation, Handbuch, Ember-Tipp, Lizenz …“ (offener Punkt aus GUI.md) |
| A/B | „Vergleich A/B“, „in den anderen Speicher“ | „Slot A/B“, „in den anderen Slot“ (wie in der AUTO-Karte und den AUTO-Meldungen) |
| Ember-Karte | – | + „Was er sagt, steht auf der Tafel über ihm.“ |
| Stimmlage | „S-Bereich ab 5 kHz“ | „Frequenz 5 kHz“ (der S-Bereich beginnt nicht dort) |
| Menü Sprache | „Wie das System“ / „Same as the system“ | „Systemsprache“ / „System language“ |
| Menü Handbuch (EN) | „Open the manual“ | „Open manual“ |
| Handbuch DE/EN | Regler Frequenz/Breite/Link, Funke, „Klingt gelispelt!“, Kante ziehen | Spektrum-Bedienung, kleine Regler, Tafel, „frisst das S“, „Uff, zu viel! …“, Schlafen auch nach ½ min Stille, Mausrad, Systemsprache, Slot, AUTO setzt auch Release |

Geprüft und gelassen: alle übrigen Infokarten, Statuszeilen, Tafel-Texte, Dialoge, Lizenztexte, Segmente (beide
Fenstergrößen in den Bildern, nichts abgeschnitten). `tools/bench/ember_presets/gen_manual.py` erzeugt nur die
Preset-Tabelle – nicht betroffen.

## Tests, pluginval, CPU

- `EmberTests` Release (ohne `EMBER_VOCALS`): **1900 bestanden, 0 fehlgeschlagen** (vorher 1844; neu: Breite aus den
  Kanten ist die Umkehrung von `splitBand`, ADVANCED hat 7 Knöpfe und keinen für Frequenz/Breite, Lookahead und
  Stereo-Link als kleine Regler, Marker zeigt Frequenz und Breite, Linie und Kante werden gefasst, daneben nichts).
  QA-Infokarten: jedes Control beider Ansichten hat DE/EN-Text.
  Ein Lauf unter Last (parallel Build des anderen Agenten) hatte 1 Fehler im Zeit-Test „rig: 2x screen: under 16 ms a
  frame“ (19 ms); Wiederholung ohne Last: 1900/0. Der Test misst die Figur auf fester Bühne, unabhängig von dieser Runde.
- pluginval 1.0.4, Strenge 10, `--validate-in-process`, mit GUI-Tests, auf der gebauten VST3: **SUCCESS**.
- VOCALYPSE (geteilte UI-Dateien nur ergänzt, Standard unverändert): `VocalypseTests` baut.
- GUI-CPU (`EmberTests --bench 6 scales=1,1.25 counts=1 front=yes anim=full hz=60`, vorher/nachher abwechselnd je 3×,
  Median, Rechner geteilt, ±20 %):

  | | vorher UI / Prozess | nachher UI / Prozess |
  |---|---|---|
  | 100 % | 15,4 / 66,9 % | 14,3 / 64,6 % |
  | 125 % | 19,3 / 74,5 % | 18,5 / 64,8 % |
  | pro Bild Figur / Runen+Feuer | 3,95 / 1,07 ms | 3,47 / 0,93 ms |

  Nicht schlechter, eher etwas besser (keine Feuerschalen, kleinere Figur, Knopf-Siegel nur bei Änderung; in Stille
  zeichnet der Knopf gar nicht mehr nach).

## Für den Merge

- Nur GUI-Dateien und Tests; `EmberEngine.*`, `EmberAuto.*`, `EmberPresets.*`, `EmberProcessor.*` unverändert.
- Geteilter Code, nur ergänzt: `InfoPopup::ColourIds`, `SpectrumView::curveColourId`, `setActivationDialogLook`.
- `EmberPanel::showPresetMenu/showMainMenu/buildPresetMenu/buildMainMenu/renamePreset/deletePreset` sind öffentlich
  (für `--gui-shots`).

## Offen

- Die grauen Texte der Beschriftungen (Knopfnamen, MODUS …, Skalen) und der Segment-Hintergrund kommen aus dem
  geteilten Theme und sind ganz leicht bläulich-grau; fast neutral, aber nicht warm. Ändern hieße das Theme/Knob-Label
  anfassen.
- Menüs sind eigene Fenster mit durchsichtigen Ecken; in den Bildern sind die Ecken schwarz (Snapshot). Im Host (DPI,
  Schatten) einmal ansehen.
- Bei Breite 3 (Standard) liegt die obere Kante über 20 kHz und die untere an ihrer Untergrenze (0,45 × Frequenz):
  Breite dann am einfachsten mit dem Mausrad. Zieht man die untere Kante unter ihre Grenze, wird das Band nach oben
  breiter, die Kante bleibt stehen.
- AUTO-Meldungen liegen in `EmberProcessor.cpp` (nicht angefasst wegen des parallelen Branches). Vorschlag EN: „Before
  is on A.“ → „Your previous setting is on A.“ (DE „Vorher liegt auf A.“ ist ok).
- Der Speichern-Dialog ist der Windows-Dateidialog (nicht stylbar).
- Hörurteil/Sichtprüfung im DAW (FL/Ableton/Reaper) steht aus.

## Nachbesserung (nach Jakobs Test im Standalone, 24.11.2026)

Bilder: `C:\Users\Shadow\dev\velvox-bench\2026-11-24\gui_round\after2`. Neu darin: `real_*` (Editor wirklich auf dem
Desktop, Timer laufen, nichts erzwungen, Bild nach 0,6 s und 3 s; mit und ohne benutztes AUTO; `real_bypass_auto_*` /
`real_bypass_after_*` = BYPASS an, AUTO an bzw. abgebrochen), `card_option_*` (Karte einer Segment-Option, 100/150 %).

1. **Keine Infokarte über dem Spektrum.** Das Spektrum hat keine schwebende Karte mehr; solange die Maus darüber ist,
   steht seine Erklärung in der Infozeile unten (Split und Breitband je eigener Text). Allgemein erscheint keine Karte,
   solange irgendwo eine Maustaste gedrückt ist, und 1,2 s nach dem Loslassen (globaler Maus-Beobachter im Panel) –
   gilt für Knöpfe, kleine Regler, S-Pegel-Linie, Segmente.
2. **Leere Tafel – Ursache:** Mit BYPASS an schläft Ember (`Mode::sleep`), und im Schlaf lief `talkStep` nicht. Sagte er
   dann etwas (z. B. AUTO: „Ich hör zu …“), blieb der getippte Text bei 0 Zeichen stehen und wurde nie beendet –
   `isTalking()` blieb für immer wahr, die Tafel stand als leerer Rahmen da. Jakobs Einstellung hatte BYPASS an
   (im Standalone nachgestellt und abfotografiert). Die alte Sprechblase hatte denselben Fehler, zeigte bei leerem Text
   aber einfach nichts. In den Snapshots fiel es nicht auf, weil dort BYPASS aus war.
   **Fix:** Im Schlaf tippt der Text weiter (ohne Mund und Geste) und geht nach seiner Zeit; die Tafel zeigt sich nur,
   wenn es etwas zu lesen gibt, sonst blendet sie ganz aus. Test „text plate: with BYPASS on …“.
3. **Segmente MODUS, ERKENNUNG, ABHÖREN, STEREO:** Jede der 13 Optionen hat einen eigenen Text (DE/EN). Über einer
   Option zeigt die Karte genau diese („ERKENNUNG · Absolut“, ihr Text, und „Gewählt ist gerade: Smart.“, wenn eine
   andere gewählt ist); wandert die Maus zur Nachbaroption, wechselt die Karte sofort. Über dem Gruppennamen: die
   gewählte Option. Nach einem Klick steht der Text der neu gewählten Option in der Infozeile. Texte in
   `EmberPanel::optionTexts`; Test: jede Option hat ihren eigenen Text, DE ≠ EN, > 40 Zeichen. In 100 % und 150 %
   nichts abgeschnitten (`card_option_*`).

Außerdem: `--gui-shots` legt sein Test-Preset nur noch mit einem Aufräum-Objekt an (wird auch bei frühem Abbruch
gelöscht); geprüft, dass im echten Preset-Ordner nichts liegen bleibt.

Tests: **1951 bestanden, 0 fehlgeschlagen** (ohne `EMBER_VOCALS`). pluginval 1.0.4, Strenge 10, mit GUI: **SUCCESS**.

Offen: Die Karte einer Segment-Option steht über der Knopfreihe und kann den unteren Rand des Spektrums überdecken –
nur beim Überfahren der Segmente, nicht beim Arbeiten im Spektrum.
