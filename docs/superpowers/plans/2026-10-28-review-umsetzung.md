# Review-Umsetzung – Plan (NOCH NICHT BEGONNEN)

> Erst anfangen, wenn Jakob es sagt („mach die Review-Punkte“). Dann Punkt für Punkt, je Punkt: Test zuerst (rot),
> umsetzen, Screenshot ansehen (`VelvoxTests --snapshot …`), committen. Am Ende voller Lauf `scripts\build.ps1` +
> pluginval 10, installieren, Standalone mit zurückgesetzten Einstellungen starten (siehe „Prüfen“ unten).

**Quelle:** Review-Website https://claude.ai/artifact/NwwJ1YbUGzf2B3VyNftHae (15 Befunde, links Ist, rechts Vorschlag),
Jakobs Antworten vom 2026-10-28 (Chat). Punkt 10 hat Jakob abgelehnt („wir bauen später jedes Modul final“).

**Regeln wie immer:** Parameter-IDs nie ändern, alte Projekte klingen gleich (Migrationen), Layout nur so wie hier
beschrieben, Texte in beiden Sprachen (`docs/I18N.md`), vor Push fragen.

---

## 1. Makros: bipolare Fader mit Erklärzeile (Jakob: „so umsetzen“)
- SIMPEL rechts: Fader mit **Mittelstellung = unverändert**, links = weniger, rechts = mehr. Absenkungen des Auto Mix
  (z. B. Präsenz −4 dB) werden sichtbar statt 0 %.
- Unter jedem Fader eine graue Zeile, **was der Auto Mix dort gemacht hat** (z. B. „Auto Mix: 2–5 kHz etwas zurück (zu
  scharf)“); leer ohne Mix. Quelle: die Entscheidungen im Mix-Bericht.
- **Gruppiert** mit kleinen Überschriften: Klang (Presence, Warmth) · Raum (Space, Width) · Dynamik & Pegel (Punch,
  Vocal Level). Füllt den heutigen Leerraum.
- Technik: `Parameters/MacroMapper.h` und `AutoMixPlanner::sliderPositions` arbeiten heute nur 0..100 % nach oben.
  Bipolar heißt: Bereich −100..+100 % mit 0 in der Mitte, für Presence/Warmth auch Absenkung über die Rollen-Bänder.
  **Achtung Parameter-IDs:** Makro-Parameter nicht umbenennen; falls der Wertebereich sich ändert, Migration in
  `setStateInformation` (alte 0..100 % → neue Mitte + Wert), alte Projekte müssen gleich klingen.
- Tests: Fader zeigt nach Mix mit Presence-Cut einen negativen Wert; Ziehen auf die Mitte nimmt die Mix-Änderung zurück;
  altes Projekt lädt mit gleichem Klang.

## 2. Leiser nach dem Laden (Jakob: „auch“)
- Init und **alle Werks-Presets** bringen eine −18-LUFS-Vocal auf Eingangslautheit (±1 LU). Heute Init −5 LU,
  „Hard Dance Lead“ −8 LU.
- Weg: Kompressor-Makeup / Output-Kalibrierung je Preset (wie 0.6.1 „Werks-Presets gleich laut“, Test in
  `Tests/PresetTests.cpp`, der offenbar nicht mehr alles abdeckt – prüfen, warum Hard Dance Lead durchrutscht).
  Default-Änderung beim Init-Kompressor (`compMakeup`) braucht Migration für alte Projekte ohne gespeicherten Wert.
- OUT-Meter zeigt zusätzlich den **Unterschied zum Eingang** („±0“, „−5 LU“) – siehe auch Punkt 15 (Meter).

## 3. Vocal Check raus aus der Kette, Memphy sagt, was nicht passt (Jakob: „so umsetzen, gibt's ja Memphy dafür“)
- Vocal-Check-Kachel aus der Modul-Kette entfernen (kein Power-Knopf zwischen Effekten). Die Diagnose läuft weiter,
  wenn der Tab sie braucht; `checkOn`-Parameter bleibt (ID nie ändern), wird nur nicht mehr als Kette angezeigt.
  Routing-Migration: `check` aus alten Routings beim Laden ignorieren, ohne den Klang zu ändern (Check ist klangneutral).
- **Memphy sagt die Tipps:** In SIMPEL meldet er sich in der Sprechblase, wenn der Vocal Check etwas findet
  („Unter 90 Hz rumpelt's – soll ich einen Low Cut setzen?“ mit Knopf „Ja, mach“ / „Zeig mir“), Stimmung passend
  (genervt/traurig/nachdenkend). Ein Tipp nach dem anderen, nicht während er schon redet.
- Kopfzeile: kleiner Chip „Vocal Check 68 · 3 Tipps“, Klick öffnet den Tab.

## 4. Tour: Karte nie über dem Ziel + Memphy redet nicht gleichzeitig + kein Blinzeln beim Wechsel
- **Karte neben das Ziel**, nie überlappend (heute Schritt 4 über den Makros). Regel in `TutorialOverlay::cardBounds`:
  passt sie weder darunter noch darüber, dann links oder rechts daneben mit kleinem Pfeil; Test: für jeden Schritt
  schneidet die Karte den Lichtkegel nicht.
- **Memphy redet nicht, während die Tour läuft** (Jakob: der Nutzer fokussiert sich nur auf eins): Seine Vorstellung
  gehört nur zu Schritt 1. Sobald die Tour weitergeht, hört er sofort auf (Sätze abbrechen, Blase zurück auf normalen
  Text) und hört nur zu. Ebenso: Text der Tour-Karte in Schritt 1 kurz halten, weil Memphy dort spricht – oder in
  Schritt 1 die Karte erst einblenden, wenn er fertig ist (Entscheidung beim Umsetzen: Karte erst nach seinen Sätzen,
  „Weiter“ geht aber sofort).
- **Kein Blinzeln beim Stimmungswechsel:** Blinzeln nur, wenn kein Überblenden läuft UND der Wechsel mind. ~0,5 s her
  ist; ein angesetztes Blinzeln wird beim Wechsel abgebrochen (`MascotBrain::switchTo` setzt `blinkT` zurück /
  verschiebt `blinkIn`). Test: über viele Wechsel nie `blink() > 0` innerhalb von 0,5 s nach `switchTo`.

## 5. Modul-Kette: zusammenfassen, wenn zu wenig Platz (Jakob: „ab einer gewissen Anzahl zusammenfassen“)
- Solange alle Kacheln in normaler Größe passen: wie heute.
- Wird es zu eng (ab einer Breite pro Kachel unter ~X px, beim Umsetzen festlegen): **kompakte Kacheln** (Symbol + kurzer
  Name, Power als Punkt), die sich die Breite teilen – alle Module bleiben sichtbar, kein seitliches Scrollen.
- Noch enger: nach **Gruppe zusammenfassen** (Aufräumen · Dynamik · Klang · Raum) als je eine Kachel mit Anzahl;
  Klick klappt die Gruppe auf. Ausgewähltes Modul bleibt immer einzeln sichtbar.
- **Farbe nach Gruppe** statt heute zufällig (Saturation/Delay/Reverb pink, Rest blau): Aufräumen = Teal (Room Noise,
  Breath, EQ), Dynamik = Blau (Ride, Comp, Smash, De-Esser), Klang = Violett (Smooth, Saturation, Doubler),
  Raum = Pink (Delay, Reverb). Gruppenbeschriftung unter der Kette.
- Drag & Drop, Rechtsklick-Menü, „+“-Kachel und Send-Trenner müssen in allen drei Stufen gehen.

## 6. Vocal-Check-Tab aufräumen (Jakob: „so umsetzen“)
- Statt „Laufend / Capture / Start / Reset“ **ein Knopf „Neu messen“** (Zustand im Text: „misst gerade …“).
- Score mit **Wort**: schwach / okay / gut / sehr gut + Satz „3 Dinge kannst du noch verbessern“.
- „Alle anwenden“ als Hauptknopf neben dem Score, Genre-Wahl bleibt (kleiner).
- Freie untere Fläche: **Spektrum der Stimme gegen das Ziel des Genres** (woher die Tipps kommen), mit Legende (Punkt 12).
- Zusammen mit Punkt 3: Memphy liest die Tipps auch hier vor bzw. reagiert auf den Score.

## 7. Bericht: Zahlen erklären, fairer Vergleich nach vorn (Jakob: „ja umsetzen“)
- Zahlen mit Herkunft im Klartext: „Gesang im gemessenen Abschnitt −14,4 LUFS“ vs. „Gerade jetzt (live) −25,3 LUFS“.
- **„Gleich laut vergleichen“ (fairer Vergleich) direkt neben Vorher/Nachher in SIMPEL**, Standard an
  (`levelMatchMode`, Default-Änderung → nur für neue Instanzen, alte Projekte behalten ihren Wert).
- „Vorherigen Zustand wiederherstellen“ als Link unter der Liste, nicht mehr über der Liste schwebend.
- Feinschliff-Bereich: Density bleibt, aber nicht mehr allein in einer großen Box.

## 8. (siehe 2 – Lautheit) Jakob: „ja machen“ – gleicher Punkt wie 2 auf der Website (08). Zusammen umsetzen.

## 9. Saturation: voller statt verzerrt (Jakob: „Saturation soll die Vocal voller machen und nicht distorten“)
- Presets und Auto Mix bleiben im Bereich **„Wärme“: unter ~3 % THD** bei normalem Pegel. Heute „Hard Dance Lead“
  Tube +8 dB = THD 13 %. Alle Werks-Presets und `AutoMixPlanner` (Sättigung) prüfen und absenken; Test: THD jedes
  Werks-Presets bei −18 LUFS Eingang < 3 %.
- Klangziel „voller“: mehr gerade Obertöne / Low-Mid-Fülle, weniger Hochton-Zerre (z. B. Tube/Warm mit sanfterer
  Kennlinie, Drive-Bereich der Makros begrenzen, ggf. Vor-/Nach-Filter, damit Zischlaute nicht zerren). Messung über
  `Tests/` (THD + Spektrum), kein Hörtest nötig laut Jakob – trotzdem Renders in `velvox-bench` ablegen.
- Panel: oben eine **Skala Wärme → Farbe → Zerre** mit Marker (statt der leeren Bandfläche ohne Multiband),
  THD-Chip in Farbe (grün/gelb/rot). Bandfläche nur bei Multiband.
- Alte Projekte: Klang bleibt (nur Presets/Auto-Mix-Vorschläge ändern sich, keine Parameter-Umdeutung).

## 10. Panels einheitlich – NICHT umsetzen (Jakob: „passt derzeit so, wir bauen später jedes Modul final“)

## 11. Output-Panel: Kürzel ausschreiben (Jakob: „ja mach“)
- „IN / FX / MATCH“ → „Eingang / Nach den Modulen / Lautheits-Ausgleich“ (EN: Input / After the modules / Loudness match).
- Level-Match-Schalter: „Wie Bypass“ bekommt genug Platz (heute gequetscht).

## 12. Legenden überall, wo sie Sinn machen – immer sichtbar (Jakob: „nicht ausblenden“)
- SIMPEL-Spektrum: „vorher (gestrichelt) / nachher (gefüllt)“, oben rechts, dezent, **bleibt stehen**.
- Weitere Stellen prüfen und dort ebenfalls: EQ (Kurve gesamt / einzelne Bänder / Spektrum vorher-nachher), De-Esser
  (Spektrum / Bereich), Smooth (Kurve / Absenkung pink), Output (Eingang / Ausgang), Vocal-Check-Spektrum (Stimme /
  Ziel), Saturation-Obertöne. Einheitliche kleine Legenden-Komponente in `Widgets.h`.

## 13. Sprechblase kompakter, Klick-Hinweis an Memphy (Jakob: „ja machen“)
- Blase passt sich dem Text an (keine halbleere Fläche), Knöpfe direkt darunter.
- „Klick auf mich = Vocal Check“ als **Chip direkt unter Memphy** statt 10 px grau rechts; verschwindet, nachdem man
  einmal geklickt hat (in den User-Settings merken).

## 14. Ein Wort pro Sache + Analyse-Popup in drei Schritten (Jakob: „ja machen“)
- Überall **„Vocal analysieren“** (Popup-Knopf heute „Audio analysieren“; „Jetzt analysieren“ während des Hörens darf
  bleiben, „Neu analysieren“ nach einem Mix auch – prüfen, ob das reicht).
- **Solo** überall gleich: Kopfhörer-Symbol + Tooltip „Solo“; De-Esser „Abhören“ → „Solo“ (bzw. „S-Laute solo“),
  Modul-„S“-Knopf bekommt dasselbe Symbol oder bleibt „S“ mit Tooltip „Solo“ – einheitlich entscheiden beim Umsetzen.
- Analyse-Popup: Text nicht doppelt („Headliner analysiert …“ steht zweimal), stattdessen **drei nummerierte Schritte**:
  1 Song abspielen (am besten die unbearbeitete Vocal) · 2 Headliner hört zu (ca. 15 s Gesang, Pausen zählen nicht) ·
  3 Fertig – vergleiche Vorher / Nachher.
- Alle Texte DE + EN, Glossar in `docs/I18N.md` ergänzen.

## 15. Kleinere Punkte (Jakob: „ja machen“) + Preset-Liste
- **Undo / Redo** als ↶ ↷ neben dem Preset-Namen (ausgegraut, wenn nichts da) – war schon Mockup-Punkt.
- **Version** an einer Stelle im Code (CMake-Version) und in den Einstellungen anzeigen („… 0.7“); bei der Umbenennung
  auf Vocalypse mit anpassen.
- Beschriftungen unter Reglern: **mind. 11 px** und mehr Kontrast (Farbtoken `textFaint` anheben, Design-Tests anpassen).
- **IN/OUT-Meter unten** in SIMPEL: richtige Meter mit Spitzenhalter, Zahl direkt am Balken, OUT mit Differenz zum
  Eingang (Punkt 2).
- **EQ-Punkte** nicht überlappen: nahe Punkte leicht auseinander, Rollen-Beschriftung (LC, WARM, …) nur am gewählten /
  überfahrenen Punkt.
- **NEU (Jakob): Preset-Auswahlliste schließt, sobald man auf ein Preset geklickt hat** (heute bleibt sie offen).
  Test: Klick auf ein Preset in der Liste → Preset geladen, Liste zu. Tastatur (Pfeile/Enter) gleich behandeln.

---

## Reihenfolge beim Umsetzen (Vorschlag)
1. Schnelle, sichere Sachen: 15 (Preset-Liste, Version, Schriftgröße), 11, 12, 13, 14, 4
2. Klang: 9 (Saturation), 2/8 (Lautheit) – mit Messungen
3. Struktur: 3 (Vocal Check raus + Memphy-Tipps), 6, 7
4. Größer: 1 (bipolare Makros, Migration!), 5 (Kette zusammenfassen)

## Prüfen am Ende
- `scripts\build.ps1` (alle Tests) + pluginval 10.
- Screenshots aller Ansichten wie in der Review (`VelvoxTests --snapshot …`, siehe Liste in `Tests/TestMain.cpp`) und
  die Review-Website um eine „Umgesetzt“-Spalte ergänzen.
- Installieren (`scripts\install-vst3.ps1`), Einstellungen zurücksetzen (`%APPDATA%\Velvox\Velvox.settings`,
  `%APPDATA%\Headliner\Headliner.settings` vorher sichern), Standalone starten, Jakob zeigen.
