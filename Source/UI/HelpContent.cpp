#include "HelpContent.h"

namespace velvox::ui::help
{
    namespace
    {
        using i18n::Language;

        // Every text of a page in both languages; the language is picked when the table is built.
        // Both tables are built once (on first use) and kept, so Page pointers from find() stay valid.
        struct Pick
        {
            bool german;
            juce::String operator() (const char* de, const char* en) const { return juce::String::fromUTF8 (german ? de : en); }
        };

        // Builds a module page from its seven answers
        Page modulePage (Language l, const juce::String& group, const char* key, const juce::String& title,
                         std::initializer_list<juce::String> answers)
        {
            const auto& h = moduleSectionHeadings (l);
            juce::String body;
            int i = 0;
            for (const auto& part : answers)
                body << "## " << h[i++] << "\n" << part << "\n\n";
            jassert (i == 7);
            return { key, title, group, body.trim() };
        }

        std::vector<Page> build (Language l)
        {
            const Pick t { l == Language::german };
            const auto gStart = t ("Erste Schritte", "Getting started");
            const auto gWork = t ("Arbeiten mit Headliner", "Working with Headliner");
            const auto gModules = t ("Module", "Modules");
            std::vector<Page> p;

            //==================================================================
            p.push_back ({ "start", t ("Willkommen bei Headliner", "Welcome to Headliner"), gStart, t (R"~(Headliner ist ein automatischer Vocal-Mixer für EDM, Hypertechno, Hard Dance, Party- und Pop-Vocals. Er hört einen Abschnitt deiner Vocal an, misst ihn und stellt seine Module ein – Aufräumen, Dynamik, S-Laute, Klang, Breite und Raum – für eine fertig klingende Vocal. Jedes Modul bleibt danach frei einstellbar.

## Deine erste Minute
- Lade Headliner auf die Mixer-Spur deiner Vocal. Eine neue Instanz bietet dir gleich die Analyse an.
- Klick auf „Vocal analysieren“ und starte die Wiedergabe der Vocal in deiner DAW. Headliner nimmt einen begrenzten Abschnitt auf (Pausen werden übersprungen) und hört von selbst auf.
- Analyse und Mix laufen im Hintergrund; die Wiedergabe wird nie unterbrochen.
- Vergleiche mit Vorher / Nachher neben dem Knopf und stell mit den sechs Fadern fein nach.

## SIMPEL und ADVANCED
- SIMPEL: das Live-Spektrum deiner Vocal (vorher = gestrichelte Linie, nachher = gefüllte Kurve), „Vocal analysieren“ und sechs Makro-Fader: Presence, Space, Width, Punch, Warmth, Vocal Level. Unten kleine IN- und OUT-Meter.
- ADVANCED: oben die Modul-Kette, darunter das Panel des gewählten Moduls – ein großes Live-Bild, die Hauptregler und alles Weitere unter „Mehr“.

## Die Tabs
- AUTO MIX (in SIMPEL) bzw. MODULE (in ADVANCED): dein Mix – die Makros oder die Modul-Kette. Der Tab heißt wie das, was du siehst.
- VOCAL CHECK: eine Diagnose der Aufnahme mit Vorschlägen.
Ein Wechsel von Modus oder Tab ändert nie einen Wert und setzt nichts zurück.

## Einstellungen
Über das Zahnrad oben rechts wählst du die Sprache (Automatisch, Deutsch, English), die Ansicht beim Öffnen und die Fenstergröße. Nichts davon ändert den Klang.

## Hilfe ist immer da
Der ?-Knopf oben rechts öffnet diese Anleitung. In einem Modul springt der ?-Knopf direkt zur Seite dieses Moduls. Die Tour startest du jederzeit unten in der Navigation neu.)~",
R"~(Headliner is an automatic vocal mixer for EDM, hypertechno, hard dance, party and pop vocals. It listens to a section of your vocal, measures it, and sets up its modules - cleaning, dynamics, S sounds, tone, width and space - for a finished-sounding vocal. Every module stays fully editable afterwards.

## Your first minute
- Load Headliner on the mixer track of your vocal. A new instance offers the analysis right away.
- Click "Analyze vocal" and start playback of the vocal in your DAW. Headliner captures a limited section (pauses are skipped) and stops by itself.
- The analysis and the mix run in the background; playback is never interrupted.
- Compare with Before / After next to the button, and fine-tune with the six faders.

## SIMPLE and ADVANCED
- SIMPLE: the live spectrum of your vocal (before = dashed line, after = filled curve), "Analyze vocal", and six macro faders: Presence, Space, Width, Punch, Warmth, Vocal Level. Small IN and OUT meters at the bottom.
- ADVANCED: the module chain on top and the panel of the selected module below - a large live picture, the main controls, and everything else behind "More".

## The tabs
- AUTO MIX (in SIMPLE) or MODULES (in ADVANCED): your mix - the macros, or the module chain. The tab names what you see.
- VOCAL CHECK: a diagnosis of the recording with suggestions.
Switching mode or tab never changes or resets a value.

## Settings
The gear at the top right sets the language (Automatic, Deutsch, English), the start view and the window size. None of it changes the sound.

## Help is always here
The ? button at the top right opens this guide. Inside a module, the ? button jumps straight to the page for that module. You can restart the tour any time from the bottom of the navigation.)~") });

            p.push_back ({ "chain", t ("Die Signalkette", "The signal chain"), gStart, t (R"~(In ADVANCED ist die Reihe unter der Kopfleiste die Signalkette. Das Audio läuft von links nach rechts durch die Module und verlässt das Plugin bei OUTPUT (klick darauf für die Ausgangsstufe).

## Eine Kachel lesen
- Jede Kachel zeigt den Namen des Moduls, sein Symbol und seinen Ein-/Aus-Schalter. Eine ausgeschaltete Kachel ist abgedunkelt, das Modul ist auf Bypass.
- Eine dünne Linie am unteren Rand bewegt sich, während das Modul arbeitet (zum Beispiel, während ein Compressor den Pegel senkt).
- Delay und Reverb können als paralleler Send laufen: Dann sitzen sie hinter der gestrichelten Linie und tragen ein SEND-Schild; ihr Mix-Regler heißt dann SEND.

## Warum die Reihenfolge zählt
Jedes Modul hört, was die Module davor getan haben. Typische Folgen:
- Level Ride vor dem Compressor gibt dem Compressor ein gleichmäßiges Signal, so kann er sanft arbeiten.
- Saturation nach dem De-Esser übertreibt keine S-Laute; davor bändigt der De-Esser auch die Obertöne, die die Saturation erzeugt hat.
- Reverb und Delay am Ende sorgen dafür, dass die Effekte nicht komprimiert und gesättigt werden.
- Vocal Check analysiert das Signal an seiner eigenen Position. Ganz hinten beurteilt er das Ergebnis, ganz vorne die Aufnahme.
Die Standard-Reihenfolge folgt diesen Regeln und ist ein sicherer Startpunkt.)~",
R"~(In ADVANCED the row under the header is the signal chain. Audio runs through the modules from left to right and leaves at OUTPUT (click it for the output stage).

## Reading a tile
- Each tile shows the module's name, its icon and its on/off switch. A switched-off tile is dimmed and the module is bypassed.
- A thin line along the bottom moves while the module is working (for example while a compressor reduces gain).
- Delay and Reverb can run as a parallel send: they then sit behind the dashed line and carry a SEND badge; their Mix knob reads SEND.

## Why order matters
Every module hears what the modules before it did. Typical consequences:
- Level Ride before the Compressor gives the compressor an even input, so it can work gently.
- Saturation after the De-Esser does not exaggerate S sounds; before it, the de-esser also tames the harmonics the saturation created.
- Reverb and Delay at the end keep the effects from being compressed and saturated.
- Vocal Check analyzes the signal at its own position. Put it last to judge the result, first to judge the recording.
The default order follows these rules and is a safe place to start.)~") });

            p.push_back ({ "modules", t ("Module organisieren", "Organizing modules"), gWork, t (R"~(## Ein Modul öffnen
Klick auf eine Kachel: Ihr Panel erscheint unter der Kette.

## Ein- und ausschalten
Klick auf den Schalter einer Kachel oder auf den im Panel-Kopf. Ein ausgeschaltetes Modul ist auf Bypass: Es ist aus der aktiven Bearbeitung genommen und kostet keine Rechenleistung.

## Solo
Klick auf das S einer Kachel (es erscheint, wenn die Maus über der Kachel ist), Alt-Klick auf die Kachel oder Klick auf das S neben dem Schalter im Panel-Kopf: Nur dieses Modul spielt, alle anderen sind auf Bypass. Die Solo-Kachel hat einen gelben Rahmen, die anderen sind abgedunkelt, und OUTPUT zeigt SOLO. Ein weiterer Klick (oder ein Klick auf SOLO) hebt es auf. Solo ist eine Abhörhilfe: Es wird nicht gespeichert, ist kein Rückgängig-Schritt, behält die Latenz und endet, wenn du das Fenster schließt.

## Mehr Module, als hineinpassen
Die Reihe scrollt: mit dem Mausrad (auch mit Shift), dem Kipprad oder einem Wisch mit zwei Fingern, den Pfeilen an ihren Rändern, der dünnen Leiste unter den Kacheln oder durch Ziehen der Reihe mit gedrückter Shift-Taste (oder mit der mittleren Maustaste). Ein gewähltes Modul rutscht immer ganz ins Bild.

## Die Reihenfolge ändern
Zieh eine Kachel: Eine leuchtende Markierung zeigt, wo sie landet, und die Audio-Reihenfolge ändert sich im selben Moment (mit einer kurzen, klickfreien Überblendung). Das Ziehen beginnt erst, wenn sich die Maus ein paar Pixel bewegt hat, ein Klick verschiebt also nie ein Modul.

## Rechtsklick auf eine Kachel
Bypass, Duplizieren (eine Kopie mit denselben Einstellungen), Entfernen, Nach links / Nach rechts, Zurücksetzen (jeder Wert zurück auf den Standard), Als Send / Als Insert (Delay und Reverb), Rückgängig / Wiederholen.

## Module hinzufügen
Die +-Kachel am Ende der Reihe oder ein + in der Lücke zwischen zwei Kacheln öffnet den Modul-Browser: Karten, nach Aufgabe gruppiert, mit einem Suchfeld. Enter fügt die markierte Karte hinzu, Esc schließt. Dasselbe Modul kannst du mehrmals verwenden; jede Instanz hat ihre eigenen Einstellungen.

## Ketten-Layouts
Rechtsklick auf den leeren Teil der Reihe öffnet das Ketten-Menü: in die übliche Reihenfolge sortieren, diese Kette als deinen Standard speichern (für alle Projekte), deine Standard-Kette laden.)~",
R"~(## Open a module
Click a tile: its panel appears below the chain.

## Switch on and off
Click the switch of a tile, or the one in the panel header. A switched-off module is bypassed: it is removed from the active processing and costs no CPU.

## Solo
Click the S on a tile (it appears when the mouse is over the tile), Alt-click the tile, or click the S next to the switch in the panel header: only this module plays, every other one is bypassed. The soloed tile is framed in yellow, the others are dimmed and OUTPUT shows SOLO. Click again (or on SOLO) to release. Solo is a listening aid: it is not saved, not an undo step, keeps the latency and ends when you close the window.

## More modules than fit
The row scrolls: mouse wheel (also with Shift), tilt wheel or a two-finger swipe, the arrows at its edges, the thin bar under the tiles, or drag the row with Shift held (or with the middle mouse button). A selected module always slides fully into view.

## Change the order
Drag a tile: a glowing marker shows where it will land, and the audio order changes at the same moment (with a short, click-free crossfade). Dragging starts only after the mouse has moved a few pixels, so a click never moves a module.

## Right-click a tile
Bypass, Duplicate (a copy with the same settings), Remove, Move left / Move right, Reset (every value back to its default), As send / As insert (Delay and Reverb), Undo / Redo.

## Add modules
The + tile at the end of the row, or a + on the gap between two tiles, opens the module browser: cards grouped by purpose, with a search field. Enter adds the highlighted card, Esc closes. The same module can be used several times; each instance has its own settings.

## Chain layouts
Right-click the empty part of the row for the chain menu: sort into the usual order, save this chain as your default (for all projects), load your default chain.)~") });

            p.push_back ({ "controls", t ("Regler und Werte", "Controls and values"), gWork, t (R"~(Jeder Drehregler, Fader und jede Segment-Gruppe funktioniert gleich:
- Ziehen ändert den Wert (nach oben oder rechts wird er größer).
- Halte beim Ziehen Shift oder Strg für feine Schritte (1/10 der Geschwindigkeit).
- Doppelklick oder Alt-Klick setzt auf den Standardwert zurück.
- Das Mausrad verstellt den Wert in Schritten, solange der Zeiger über dem Regler ist (Listen nutzen das Rad zum Scrollen).
- Klick auf den Wert, um ihn einzutippen. Einheiten werden verstanden: „2k“ = 2000 Hz, „-3 dB“, „1,5 s“, „50 %“, Notenwerte wie „1/8“.
- Rechtsklick öffnet ein Menü: Standardwert, Wert eingeben, Wert kopieren, Wert einfügen – und in einem Modul-Panel auch Modul zurücksetzen und Modul-Preset speichern.
- Lass die Maus kurz auf einem Regler ruhen: Name, Wirkung und aktueller Wert erscheinen.

## Rückgängig
Jede Änderung ist ein Schritt in einem gemeinsamen Verlauf: Strg+Z macht rückgängig, Strg+Y oder Strg+Shift+Z wiederholt. Ein Modul verschieben, ein Preset laden und der automatische Mix sind jeweils ein Schritt.

## Automation
Jeder Regler, Schalter und jedes Menü ist ein echter Plugin-Parameter: Deine DAW kann ihn automatisieren, Presets speichern ihn. Headliner meldet Änderungen dem Host als saubere Gesten, aufgenommene Automation wird also sauber.)~",
R"~(Every knob, fader and segment group works the same way:
- Drag to change the value (up or right raises it).
- Hold Shift or Ctrl while dragging for fine adjustment (1/10 of the speed).
- Double-click, or Alt-click, to return to the default.
- The mouse wheel steps the value while the pointer is over the control (lists keep the wheel for scrolling).
- Click the value to type it. Units are understood: "2k" = 2000 Hz, "-3 dB", "1.5 s", "50 %", note values such as "1/8".
- Right-click for a menu: Default value, Enter value, Copy value, Paste value - and inside a module panel also Reset module and Save module preset.
- Rest the mouse on a control for a moment: its name, what it does and its current value appear.

## Undo
Every change is one step of one history: Ctrl+Z undoes, Ctrl+Y or Ctrl+Shift+Z redoes. Moving a module, loading a preset and the automatic mix are one step each.

## Automation
Every knob, switch and menu is a real plugin parameter: your DAW can automate it and presets store it. Headliner reports changes to the host as proper gestures, so recorded automation is clean.)~") });

            p.push_back ({ "modes", t ("Modi, Tabs und die Makros", "Modes, tabs and the macros"), gWork, t (R"~(## SIMPEL
- Das Spektrum zeigt deine Vocal vor (gestrichelt) und nach (gefüllt) Headliner.
- „Vocal analysieren“ startet den automatischen Mix. Danach vergleicht ein kleiner Schalter Vorher / Nachher mit dem Zustand davor; Details zeigt, was eingestellt wurde und warum, dazu Density und den fairen Vergleich.
- Die sechs Fader: Presence (Höhen und De-Essing), Space (Reverb und Delay mit Ducking), Width (Doubler und Stereobreite), Punch (schnellere, festere Kompression und Drive), Warmth (Körper, weichere Höhen, Saturation), Vocal Level (der finale Ausgangspegel). Jeder stellt die echten Modul-Parameter ein (ein Rückgängig-Schritt pro Bewegung): ADVANCED zeigt genau das, was du hörst, und ein Modul, das du dort ausschaltest, bleibt stumm.

## ADVANCED
Die Modul-Kette mit dem Panel des gewählten Moduls. Was die SIMPEL-Fader getan haben, erscheint hier als ganz normale Einstellungen.

## VOCAL CHECK
Eine Diagnose der Aufnahme an der Position des Vocal-Check-Moduls: eine Punktzahl (0 – 100) und eine Kachel pro Befund mit dem vorgeschlagenen Fix. „Anwenden“ setzt ihn um, „Alles anwenden“ setzt alle Vorschläge auf einmal um – jeder ist ein Rückgängig-Schritt (Strg+Z). „Ignorieren“ blendet einen Befund aus (zurückholen unter „x ignoriert“). Das Genre (Party, Pop, Akustik) bestimmt die Zielbereiche.

## Wechseln
Alle Ansichten zeigen dieselben Einstellungen. Ein Wechsel ändert nie den Klang und setzt nichts zurück.)~",
R"~(## SIMPLE
- The spectrum shows your vocal before (dashed) and after (filled) Headliner.
- "Analyze vocal" starts the automatic mix. Afterwards a small Before / After switch compares with the state before; Details shows what was set up and why, Density and the fair compare.
- The six faders: Presence (highs and de-essing), Space (ducked reverb and delay), Width (doubler and stereo width), Punch (faster, firmer compression and drive), Warmth (body, softer highs, saturation), Vocal Level (the final output level). Each one sets the real module parameters (one undo step per move): ADVANCED shows exactly what you hear, and a module you switch off there stays silent.

## ADVANCED
The module chain with the panel of the selected module. What the SIMPLE faders did shows up here as ordinary settings.

## VOCAL CHECK
A diagnosis of the recording at the position of the Vocal Check module: a score (0 - 100) and one tile per finding with the suggested fix. "Apply" applies it, "Apply all" applies every suggestion at once - each is one undo step (Ctrl+Z). "Ignore" hides a finding (restorable under "x ignored"). The genre (Party, Pop, Acoustic) sets the target ranges.

## Switching
All views show the same settings. Switching never changes the sound or resets anything.)~") });

            p.push_back ({ "automix", t ("Automatischer Mix", "Automatic mix"), gWork, t (R"~(## Was er tut
Headliner nimmt einen begrenzten Abschnitt der Vocal auf, während deine DAW sie abspielt (etwa 14 Sekunden Gesang, höchstens 24 Sekunden mit kurzen Lücken), analysiert ihn und stellt die Module ein. Alles läuft im Plugin: kein Internet, keine Cloud, kein KI-Dienst. Ein Plugin hört nur, was die DAW durch es abspielt – Headliner liest nie die Audiodatei selbst, analysiert wird also nur der aufgenommene Abschnitt.

## Was gemessen wird
- Lautheit (ITU-R BS.1770), Spitzen, Clipping, der Pegelunterschied zwischen lauten und leisen Wörtern, Crest-Faktor und Grundrauschen (wenn es Pausen gibt).
- Tonumfang der Stimme, das Langzeit-Spektrum (Rumpeln, Körper, Tiefmitten, Präsenz, S-Bereich, Luft), S-Laute im Verhältnis zu Vokalen, schmale Resonanzen (Teiltöne der gesungenen Noten werden ausgenommen), Raum- oder Effektfahnen und Stereobreite.
- Mit einem Beat auf dem Sidechain: sein Pegel und wie stark er den Präsenzbereich der Stimme überlappt. Das ist nur ein Hinweis, nie ein Beweis für Verdeckung – der Beat ist durch Headliner nie zu hören.

## Was eingestellt wird
Die Entscheidungen folgen den Messungen: Der Low Cut sitzt deutlich unter den tiefsten gesungenen Noten, Höhen werden nur bei dunklen Aufnahmen angehoben, der Threshold des De-Essers liegt zwischen Vokal- und S-Pegel, die Resonanz-Kontrolle läuft nur, wenn etwas heraussticht, Reverb und Delay werden bei halligen Aufnahmen reduziert oder bleiben aus.

## Geprüft, bevor er angewendet wird
Headliner rechnet den aufgenommenen Abschnitt durch die komplette Kette, misst das Ergebnis und korrigiert es dort, wo die Messung verlässlich ist: zu starke oder zu schwache Kompression, S-Laute, die die Kette lauter gemacht hat, verlorener Körper oder zu viel Helligkeit, Mono-Kompatibilität. Zum Schluss wird der Ausgang an die Lautheit des Originals angeglichen (damit der Vergleich fair ist), mit 1 dB Headroom.

## Vergleichen und rückgängig machen
- Vorher / Nachher (SIMPEL, neben „Vocal analysieren“) schaltet die echten Parameter zwischen dem Zustand vor der Analyse und dem Mix um (einschließlich deiner Änderungen daran).
- Bypass in der Kopfleiste spielt die unbearbeitete Vocal; mit dem fairen Vergleich auf Wie Bypass gleich laut.
- „Vorherigen Zustand wiederherstellen“ (Details) verwirft den Mix; Strg+Z macht den Mix als einen Schritt rückgängig.
- „Vocal analysieren“ ist immer verfügbar; der aktuelle Zustand wird zum neuen „Vorher“.

## Grenzen
Eine kurze oder leise Aufnahme ergibt vorsichtige Einstellungen (und Headliner sagt das). Die Analyse beurteilt die Aufnahme, nicht deinen Geschmack – nimm das Ergebnis als gut vorbereiteten Startpunkt und hör hin. Ein perfekter Mix für jede Aufnahme ist nicht garantiert.)~",
R"~(## What it does
Headliner records a limited section of the vocal while your DAW plays it (about 14 seconds of singing, at most 24 seconds including short gaps), analyzes it and sets up the modules. Everything runs inside the plugin: no internet, no cloud, no AI service. A plugin only hears what the DAW plays through it - Headliner never reads the audio file itself, so only the captured section is analyzed.

## What is measured
- Loudness (ITU-R BS.1770), peaks, clipping, the level difference between loud and quiet words, crest factor and noise floor (when there are pauses).
- Pitch range of the voice, the long-term spectrum (rumble, body, low mids, presence, S range, air), S sounds against vowels, narrow resonances (partials of the sung notes are excluded), room or effect tails, and stereo width.
- With a beat on the sidechain: its level and how much it overlaps the voice's presence range. This is only a hint, never proof of masking - the beat is never heard through Headliner.

## What is set up
The decisions follow the measurements: the low cut sits well below the lowest sung notes, highs are only raised on dark recordings, the de-esser threshold sits between vowel and S level, resonance control only runs when something sticks out, reverb and delay are reduced or left off on roomy recordings.

## Checked before it is applied
Headliner renders the captured section through the complete chain, measures the result and corrects it where the measurement is reliable: compression that is too strong or too weak, S sounds that the chain made louder, lost body or too much brightness, mono compatibility. Finally the output is matched to the loudness of the original (so the comparison is fair) with 1 dB of headroom.

## Compare and undo
- Before / After (SIMPLE, next to "Analyze vocal") switches the real parameters between the state before the analysis and the mix (including your changes to it).
- Bypass in the header plays the unprocessed vocal; with the fair compare set to Like bypass at the same loudness.
- "Restore previous state" (Details) discards the mix; Ctrl+Z undoes the mix as one step.
- "Analyze vocal" is always available; the current state is kept as the new "Before".

## Limits
A short or quiet capture gives conservative settings (and says so). The analysis judges the recording, not your taste - treat the result as a well-prepared starting point and listen. It is not a guarantee of a perfect mix for every recording.)~") });

            p.push_back ({ "presets", t ("Presets und Speichern", "Presets and saving"), gWork, t (R"~(## Die Preset-Liste
Die Kopfleiste zeigt ‹ Name ›. Die Pfeile blättern durch alle Presets; ein Klick auf den Namen öffnet die Liste:
- Links die Kategorien: Alle, die Werks-Kategorien (Clean, Punchy, Aggressive, Wide, Creative, ...), Eigene (deine Presets) und Favoriten.
- Tipp ins Suchfeld, um nach Name, Kategorie oder Beschreibung zu filtern.
- Klick auf den Stern eines Presets macht es zum Favoriten (gilt für alle Projekte).
- Lass die Maus auf einem Preset ruhen, um seine Beschreibung zu lesen; Doppelklick oder Enter lädt es (ein Rückgängig-Schritt).

## Deine eigenen Presets
- Speichern überschreibt das geladene eigene Preset; bei einem Werks-Preset fragt es nach einem Namen.
- Speichern als, Umbenennen und Löschen gelten für deine eigenen Presets. Werks-Presets werden nie verändert.
- Eigene Presets sind Dateien in deinem Dokumente-Ordner (Lind Audio/Headliner/Presets) – du kannst sie sichern oder weitergeben.
- Ein * hinter dem Namen in der Kopfleiste heißt: Das geladene Preset wurde verändert.

## Modul-Presets
Der Presets-Knopf in einem Modul-Panel lädt Startpunkte nur für dieses Modul; der Rest der Kette bleibt, wie er ist. Rechtsklick im Panel und Modul-Preset speichern sichert deine eigenen (sie erscheinen in diesem Menü unter Eigene).

## Was Presets nicht anfassen
Input Gain, Bypass und der faire Vergleich (Level Match) gehören zu deiner Aufnahme und deinem Abhören, deshalb ändern Presets sie nie.

## Projekte
Dein DAW-Projekt speichert den kompletten Zustand jeder Headliner-Instanz: alle Parameter, die Modul-Reihenfolge, Modus und Tab, die Fenstergröße, den Bericht des automatischen Mixes und den Zustand davor.)~",
R"~(## The preset list
The header shows ‹ name ›. The arrows step through all presets; click the name for the list:
- Categories on the left: All, the factory categories (Clean, Punchy, Aggressive, Wide, Creative, ...), User (your presets) and Favorites.
- Type in the search field to filter by name, category or description.
- Click the star of a preset to make it a favorite (kept for all projects).
- Rest the mouse on a preset to read its description; double-click or Enter loads it (one undo step).

## Your own presets
- Save overwrites the loaded preset of your own; with a factory preset it asks for a name.
- Save as, Rename and Delete work on your own presets. Factory presets are never changed.
- User presets are files in your Documents folder (Lind Audio/Headliner/Presets), so you can back them up or share them.
- A * after the name in the header means the loaded preset was changed.

## Module presets
The Presets button inside a module panel loads starting points for that module only; the rest of the chain stays as it is. Right-click in the panel and choose Save module preset to keep your own (they appear under User in that menu).

## What presets do not touch
Input gain, Bypass and the fair compare (Level Match) belong to your recording and monitoring, so presets never change them.

## Projects
Your DAW project stores the complete state of every Headliner instance: all parameters, the module order, the mode and tab, the window size, the automatic-mix report and the state before it.)~") });

            p.push_back ({ "compare", t ("Bypass und Vorher / Nachher", "Compare: Bypass and Before / After"), gWork, t (R"~(## Bypass
Bypass (oben rechts) spielt die unbearbeitete Vocal, latenzkompensiert, damit sie im Takt bleibt. Solange Bypass an ist, zeigt Headliner einen bernsteinfarbenen Rahmen und einen Hinweis, damit du es nie vergisst. Es ist auch der Bypass-Parameter des Hosts.

## Vorher / Nachher
Nach einer Analyse zeigt SIMPEL neben „Vocal analysieren“ den Schalter Vorher / Nachher: Er wechselt zwischen den Einstellungen vor der Analyse und dem Mix. Umschalten ist Hören, nicht Bearbeiten – deshalb ist es kein Rückgängig-Schritt.

## Fairer Vergleich
Lauter klingt fast immer besser. Stell den fairen Vergleich (Details) oder Level Match (OUTPUT) auf:
- Wie Bypass: Bypass spielt so laut wie die bearbeitete Vocal, du vergleichst also den Klang, nicht die Lautheit.
- Auto-Gain: Die bearbeitete Vocal wird laufend so laut gehalten wie der Eingang.
- Aus: keine Pegelanpassung.
Der Pegel wird nie heimlich angeglichen: nur, wenn du es wählst.)~",
R"~(## Bypass
Bypass (top right) plays the unprocessed vocal, delay-compensated so it stays in time. While bypass is on, Headliner shows an amber frame and a label so you never forget it. It is also the host's bypass parameter.

## Before / After
After an analysis, SIMPLE shows Before / After next to "Analyze vocal": it switches between the settings before the analysis and the mix. Switching is listening, not editing, so it is not an undo step.

## Fair comparison
A louder sound almost always seems better. Set the fair compare (Details) or Level Match (OUTPUT) to:
- Like bypass: Bypass plays as loud as the processed vocal, so you compare tone, not loudness.
- Auto gain: the processed vocal is continuously kept as loud as the input.
- Off: no level adjustment.
Level matching never happens silently: it only works when you choose it.)~") });

            p.push_back ({ "meters", t ("Meter und Anzeigen", "Meters and displays"), gWork, t (R"~(## IN und OUT
- SIMPEL zeigt sie unten, ADVANCED am rechten Ende der Ketten-Reihe.
- Die Balken zeigen den Spitzenpegel, die Zahl ist die Lautheit in LUFS (Short Term).
- Die rote Lampe an OUT leuchtet, wenn das Signal über 0 dBFS ging. Klick auf die Meter, um sie zurückzusetzen.

## Im OUTPUT-Panel
- Clip Guard (Mehr): ein weicher Clipper ab -1 dBFS, der den Ausgang unter 0 dBFS hält.
- Mono (Mehr): in Mono abhören, um die Kompatibilität zu prüfen (wird nicht im Projekt gespeichert).
- Korrelation: +1 ist mono-sicher, um 0 sehr breit, unter 0 löschen sich Teile des Klangs in Mono aus.

## Modul-Anzeigen
Jedes Modul zeigt echte Daten aus dem Audio oder aus seinen eigenen Einstellungen: die EQ-Kurve über dem Live-Spektrum, die Pegelreduktion des Compressors, das De-Esser-Band, die Stimmen des Doublers, den Verlauf von Level Ride und mehr. Nichts auf dem Bildschirm ist simuliert.)~",
R"~(## IN and OUT
- SIMPLE shows them at the bottom, ADVANCED at the right end of the chain row.
- The bars show the peak level, the number is the loudness in LUFS (short term).
- The red lamp on OUT lights when the signal went above 0 dBFS. Click the meters to reset it.

## In the OUTPUT panel
- Clip guard (More): a soft clipper from -1 dBFS that keeps the output below 0 dBFS.
- Mono (More): listen in mono to check compatibility (not saved with the project).
- Correlation: +1 is mono-safe, around 0 is very wide, below 0 parts of the sound cancel in mono.

## Module displays
Each module shows real data from the audio or from its own settings: the EQ curve over the live spectrum, the compressor's gain reduction, the de-esser band, the doubler voices, the level ride history and more. Nothing on screen is simulated.)~") });

            p.push_back ({ "problems", t ("Typische Vocal-Probleme lösen", "Fixing common vocal problems"), gWork, t (R"~(- Rumpeln, Plosive, matschiger Bass: EQ Low Cut bei etwa 80 bis 120 Hz. Wenn Plosive den Compressor auslösen, heb das SC-Filter des Compressors an (Mehr).
- Hohl oder näselnd: EQ, eine Absenkung von 2 bis 4 dB zwischen 300 und 800 Hz mit mittlerem Q.
- Harte, klingelnde Resonanzen: Smooth mit moderater Depth; schmale, spitze Stellen: eine EQ-Absenkung mit hohem Q.
- Scharfe S- und T-Laute: De-Esser – such das Band mit Listen, dann stell den Threshold so, dass nur die S-Laute auslösen.
- Ungleichmäßige Wörter, manche gehen unter: Level Ride vor dem Compressor.
- Nicht laut oder dicht genug: Vocal Smash unter die Vocal mischen, dann den Compressor für die Kontrolle.
- Zu trocken oder zu weit hinten: Reverb und Delay mit Ducking (Mehr), damit die Wörter klar bleiben.
- Zu schmal: Doubler, dann Mono im OUTPUT-Panel prüfen.
- Clipping am Ausgang: Vocal Level senken und Clip Guard anlassen.
- Kein Plan, wo du anfangen sollst: Öffne VOCAL CHECK, spiel die Vocal eine Weile ab und nutz dann „Anwenden“ bei den Befunden.)~",
R"~(- Rumble, plosives, muddy low end: EQ Low cut around 80 to 120 Hz. For plosives that trigger the compressor, raise the Compressor's SC filter (More).
- Boxy or honky: EQ, cut 2 to 4 dB between 300 and 800 Hz with a moderate Q.
- Harsh, ringing resonances: Smooth with moderate Depth; narrow, sharp spots: an EQ cut with a high Q.
- Sharp S and T sounds: De-Esser, find the band with Listen, then set Threshold so only the S sounds trigger.
- Uneven words, some disappear: Level Ride before the Compressor.
- Not loud or dense enough: Vocal Smash blended under the vocal, then the Compressor for control.
- Too dry or too far back: Reverb and Delay with ducking (More), so the words stay clear.
- Too narrow: Doubler, then check Mono in the OUTPUT panel.
- Clipping output: lower Vocal Level and keep Clip guard on.
- Not sure where to start: open VOCAL CHECK and play the vocal for a while, then use "Apply" on the findings.)~") });

            //==================================================================
            p.push_back (modulePage (l, gModules, "eq", "Equalizer", {
                t ("Formt den Klang der Stimme mit bis zu 16 eigenen Bändern: Low Cut gegen Rumpeln, Bells gegen Hohlklang oder für Präsenz, Shelves für Körper und Luft, ein High Cut für dunklere oder Lo-Fi-Sounds. Die Kurve liegt über dem Live-Spektrum (vorher blass, nachher gefüllt).",
                   "Shapes the tone of the voice with up to 16 bands of your own: low cut against rumble, bells for boxiness or presence, shelves for body and air, a high cut for darker or lo-fi sounds. The curve is drawn over the live spectrum (before faint, after filled)."),
                t ("Fast immer, als erster Aufräum-Schritt. Damit entfernst du, was du nicht willst, und gibst der Vocal ihre Grundfarbe.",
                   "Almost always, as the first cleaning step. Use it to remove what you do not want and to give the vocal its basic color."),
                t ("- Doppelklick auf eine freie Stelle fügt dort eine Bell mit dieser Frequenz und diesem Gain hinzu.\n- Zieh einen Punkt: Frequenz und Gain (Shift oder Strg: fein). Mausrad über einem Punkt: Q (bei einem Cut: die Steilheit).\n- Doppelklick auf einen Punkt schaltet ihn ein oder aus, Alt-Klick setzt Gain und Q zurück.\n- Rechtsklick auf einen Punkt: Typ (Bell, Low Shelf, High Shelf, Low Cut, High Cut, Notch, Band Pass, Tilt), Steilheit eines Cuts (6 bis 48 dB/Okt.), ein/aus, löschen. Die Entf-Taste entfernt das gewählte Band.\n- Kopfhörer (neben einem Punkt unter der Maus und neben dem Namen des gewählten Bands): nur den Bereich dieses Bands hören – einen Band Pass um eine Bell oder Notch, alles unterhalb eines Low Shelf oder Low Cut, alles oberhalb eines High Shelf oder High Cut – etwa gleich laut, ohne den Gain des Bands. Klick = an, bis du wieder klickst; gedrückt halten = nur solange du hältst. Das ist eine Abhörhilfe: nicht gespeichert, kein Rückgängig-Schritt, es endet, wenn du das Panel schließt.\n- Unter der Grafik: An, Typ, Freq, Gain und Q (bei Cuts die Steilheit) des gewählten Bands, um genaue Werte einzutippen.\n- Bänder mit einem kleinen Schild (AIR, WARM, LC, AUTO ...) gehören zu den Makros oder zum automatischen Mix. Du kannst sie bearbeiten oder löschen; ein Makro holt sein Band zurück, wenn du es bewegst.\n- Mehr: Mix mischt das bearbeitete und das Original-Signal.",
                   "- Double-click on empty space adds a Bell at that frequency and gain.\n- Drag a point: frequency and gain (Shift or Ctrl: fine). Mouse wheel over a point: Q (on a cut: the slope).\n- Double-click a point switches it on or off, Alt-click resets its gain and Q.\n- Right-click a point: type (Bell, Low Shelf, High Shelf, Low Cut, High Cut, Notch, Band Pass, Tilt), slope of a cut (6 to 48 dB/oct), on/off, delete. The Delete key removes the selected band.\n- Headphones (beside a point you hover, and next to the selected band's name): hear only that band's region - a band pass around a bell or notch, everything below a low shelf or low cut, everything above a high shelf or high cut - at about the same loudness, without the band's gain. Click = on until you click again, press and hold = only while held. It is a listening aid: not saved, not an undo step, it ends when you close the panel.\n- Under the graph: the selected band's On, Type, Freq, Gain and Q (slope for cuts), to type exact values.\n- Bands with a small tag (AIR, WARM, LC, AUTO ...) belong to the macros or the automatic mix. You can edit or delete them; a macro brings its band back when you move it.\n- More: Mix blends the equalized and the original signal."),
                t ("- Gain: positive Werte heben an, negative senken ab (die Anzeige zeigt +/-18 dB, das Wertefeld nimmt +/-30 dB).\n- Q: höher = schmalere Bell, niedriger = breit und musikalisch.\n- Frequenz des Low Cuts: höher entfernt mehr Bass; zu hoch macht die Stimme dünn.\n- Steilheit: 24 dB/Okt. schneidet steiler als 12 dB/Okt.",
                   "- Gain: positive values boost, negative values cut (the display shows +/-18 dB, the value field takes +/-30 dB).\n- Q: higher = narrower bell, lower = broad and musical.\n- Low cut frequency: higher removes more low end; too high makes the voice thin.\n- Slope: 24 dB/oct cuts more steeply than 12 dB/oct."),
                t ("- Low Cut 80 bis 120 Hz, 24 dB/Okt.\n- Eine Bell mit -2 bis -4 dB um 400 Hz, wenn die Stimme hohl klingt.\n- Ein High Shelf mit +2 bis +4 dB bei 10 kHz für Luft.",
                   "- Low cut 80 to 120 Hz, 24 dB/oct.\n- A Bell -2 to -4 dB around 400 Hz if the voice sounds boxy.\n- A High Shelf +2 to +4 dB at 10 kHz for air."),
                t ("- Viele Bänder anheben, statt das Problem abzusenken.\n- Sehr schmale, große Anhebungen, die zu klingeln beginnen.\n- Den Low Cut so hoch setzen, dass die Stimme ihren Körper verliert.\n- Nur nach der Kurve urteilen – immer hinhören.",
                   "- Boosting many bands instead of cutting the problem.\n- Very narrow, large boosts that start to ring.\n- Setting the low cut so high that the voice loses its body.\n- Judging only by the curve - always listen."),
                t ("Setz den EQ vor den Compressor, damit der Compressor nicht auf Rumpeln reagiert. Große Höhen-Anhebungen können S-Laute schärfer machen; lass den De-Esser folgen. Smooth fängt Resonanzen, die wandern – das kann ein statischer EQ nicht. Projekte aus Headliner 0.5 behalten ihre alten Filterkurven; unter Mehr stellt „Alte EQ-Kurve – umstellen“ auf die neuen, analog angepassten Kurven um.",
                   "Put the EQ before the Compressor so the compressor does not react to rumble. Large high boosts can make S sounds sharper; follow with the De-Esser. Smooth catches resonances that move, which a static EQ cannot. Projects from Headliner 0.5 keep their old filter curves; More offers \"Old EQ curve - switch\" to change to the new analog-matched curves.") }));

            p.push_back (modulePage (l, gModules, "comp", "Compressor", {
                t ("Senkt den Pegel lauter Passagen, damit die Vocal gleichmäßig bleibt und jedes Wort im Mix sitzt. Die Anzeige scrollt den Eingang (grau), den Ausgang und die Pegelreduktion (pink, von oben); die Zahl ist die Reduktion in diesem Moment. Rechts daneben die Kennlinie: Eingangspegel nach rechts, Ausgangspegel nach oben.",
                   "Reduces the level of loud passages so that the vocal stays steady and every word sits in the mix. The display scrolls the input (gray), the output and the gain reduction (pink, from the top); the number is the gain reduction right now. Right of it the transfer curve: input level across, output level up."),
                t ("Auf fast jeder Lead-Vocal. Nimm ihn für Kontrolle und Gleichmäßigkeit; für zusätzliche Dichte nimm Vocal Smash.",
                   "On nearly every lead vocal. Use it for control and consistency; use Vocal Smash for extra density."),
                t ("- Threshold legt fest, wo die Kompression beginnt, Ratio, wie stark oberhalb davon komprimiert wird. Auf der Kennlinie: Zieh den Knickpunkt für Threshold, den oberen Teil der Kurve nach oben oder unten für Ratio; das Mausrad über der Kurve stellt Knee ein. Shift = fein, Doppelklick = Standard, Rechtsklick = Wert eingeben.\n- Attack ist, wie schnell er reagiert, Release, wie schnell er wieder loslässt.\n- Mehr: Knee macht den Übergang um den Threshold weicher. Makeup gibt nach der Kompression Pegel zurück; Auto Makeup gleicht aus, was eine typische Vocal verliert. Mix mischt das unkomprimierte Signal dazu (Parallelkompression).\n- Detector: Peak reagiert auf jeden Transienten, RMS folgt dem durchschnittlichen Pegel. Auto Release lässt nach kurzen Spitzen schnell und nach langer Kompression langsam los.\n- Das SC-Filter verhindert, dass tiefe Frequenzen (Plosive, Bass) den Compressor auslösen.",
                   "- Threshold sets where compression starts; Ratio how strongly it compresses above that level. On the curve: drag the knee point for Threshold, drag the upper part of the curve up or down for Ratio, the mouse wheel over the curve sets Knee. Shift = fine, double-click = default, right-click = enter a value.\n- Attack is how fast it reacts, Release how fast it lets go.\n- More: Knee softens the transition around the threshold. Makeup adds level back after compression; Auto Makeup adds what a typical vocal level loses. Mix blends in the uncompressed signal (parallel compression).\n- Detector: Peak reacts to every transient, RMS follows the average level. Auto Release lets go fast after short peaks and slowly after long compression.\n- SC filter keeps low frequencies (plosives, bass) from triggering the compressor."),
                t ("- Niedrigerer Threshold oder höhere Ratio = mehr Pegelreduktion.\n- Attack: Kürzere Zeiten fangen Transienten schneller, können Konsonanten aber stumpf machen; längere lassen den Anfang der Wörter durch.\n- Release: kurz = dichter und hörbareres Pumpen, lang = weicher, kann den Pegel aber unten halten.\n- Mix unter 100 % lässt etwas natürliche Dynamik übrig.",
                   "- Lower Threshold or higher Ratio = more gain reduction.\n- Attack: shorter times catch transients faster but can dull consonants; longer times let the attack of words through.\n- Release: short = denser and more audible pumping, long = smoother but can hold the level down.\n- Mix below 100 % keeps some natural dynamics."),
                t ("- Ratio 3:1 bis 4:1, Attack 5 bis 15 ms, Release 80 bis 150 ms.\n- Senk den Threshold, bis die Anzeige bei lauten Wörtern etwa 3 bis 6 dB Reduktion zeigt.\n- Mit Makeup gleichst du die Lautheit an Bypass an.",
                   "- Ratio 3:1 to 4:1, Attack 5 to 15 ms, Release 80 to 150 ms.\n- Lower the Threshold until the meter shows about 3 to 6 dB reduction on loud words.\n- Use Makeup to match the bypassed loudness."),
                t ("- Zu viel Reduktion (dauerhaft über 10 dB) – die Vocal klingt flach und hauchig.\n- Sehr kurzer Attack auf einer ohnehin harten Vocal.\n- Mit Bypass bei unterschiedlicher Lautheit vergleichen – nutz den fairen Vergleich.",
                   "- Too much reduction (over 10 dB permanently) - the vocal sounds flat and breathy.\n- Very short attack on an already hard vocal.\n- Comparing with Bypass at different loudness - use the fair compare."),
                t ("Level Ride vor dem Compressor gleicht ganze Wörter aus, so kann der Compressor sanft arbeiten. Der De-Esser danach fängt S-Laute, die der Compressor lauter gemacht hat. Vocal Smash legt eine parallele, stark gequetschte Schicht obendrauf.",
                   "Level Ride before the Compressor evens out whole words so the compressor can work gently. The De-Esser after it catches S sounds the compressor made louder. Vocal Smash adds a parallel squashed layer on top.") }));

            p.push_back (modulePage (l, gModules, "deess", "De-Esser", {
                t ("Senkt scharfe S-, T- und Sch-Laute. Er erkennt Energie oberhalb einer eingestellten Frequenz und senkt sie nur, solange sie zu laut ist.",
                   "Reduces sharp S, T and SH sounds. It detects energy above a set frequency and lowers it only while it is too loud."),
                t ("Wenn S-Laute zischen oder schneiden, besonders nach EQ-Anhebungen, Kompression oder Saturation.",
                   "When S sounds hiss or cut, especially after EQ boosts, compression or saturation."),
                t ("- Die Anzeige zeigt das Spektrum der Vocal (graue Linie: Eingang, gefüllt: Ausgang) und das S-Band. Der Balken, der oben im Band hängt, ist die Reduktion in diesem Moment, die gestrichelte Linie ihre Grenze (Range).\n- Zieh das Band seitlich für seine untere Kante (Frequency), nach oben oder unten für Range; das Mausrad verschiebt die Frequenz. Doppelklick setzt beides zurück.\n- Threshold legt den Pegel fest, ab dem die Reduktion beginnt.\n- Range begrenzt, wie stark das Band abgesenkt werden darf.\n- Mode: Split senkt nur die S-Frequenzen; Wide senkt bei jedem S die ganze Stimme.\n- Listen spielt nur das erkannte Band, damit du die richtige Frequenz findest.\n- Mehr: Detection Relative vergleicht das S-Band mit dem Rest der Stimme, so werden laute und leise Phrasen gleich behandelt (neue De-Esser starten auf Relative); Absolute reagiert allein auf den Pegel des S-Bands (wie in älteren Projekten).",
                   "- The display shows the spectrum of the vocal (gray line: in, filled: out) and the S band. The bar hanging from the top of the band is the reduction right now, the dashed line its limit (Range).\n- Drag the band sideways for its lower edge (Frequency), up or down for Range; the mouse wheel moves the frequency. Double-click resets both.\n- Threshold sets the level where the reduction starts.\n- Range limits how much the band may be reduced.\n- Mode: Split only reduces the S frequencies; Wide dips the whole voice on each S.\n- Listen plays only the detected band, to find the right Frequency.\n- More: Detection Relative compares the S band with the rest of the voice, so loud and quiet phrases are de-essed alike (new De-Essers start on Relative); Absolute reacts to the level of the S band alone (as in older projects)."),
                t ("- Niedrigerer Threshold = reagiert auf mehr und leisere S-Laute.\n- Höhere Range = stärkere Absenkung.\n- Frequency: Niedrigere Werte fangen auch tieferes Zischen, können die Stimme aber lispeln lassen.",
                   "- Lower Threshold = reacts to more and quieter S sounds.\n- Higher Range = stronger reduction.\n- Frequency: lower values also catch lower hiss but can make the voice lisp."),
                t ("- Frequency 5 bis 7 kHz, Range 6 dB.\n- Schalt Listen ein, verschieb Frequency, bis du vor allem S-Laute hörst, schalt Listen wieder aus.\n- Senk den Threshold, bis nur die S-Laute abgesenkt werden.",
                   "- Frequency 5 to 7 kHz, Range 6 dB.\n- Turn on Listen, move Frequency until you hear mostly S sounds, turn Listen off.\n- Lower Threshold until only the S sounds are reduced."),
                t ("- Zu viel Range: Der Sänger klingt, als würde er lispeln.\n- Listen eingeschaltet lassen (du hörst nur das S-Band).\n- Wide in einem dichten Mix verwenden, in dem dann die ganze Vocal hörbar wegtaucht.",
                   "- Too much Range: the singer sounds as if they had a lisp.\n- Leaving Listen switched on (you only hear the S band).\n- Using Wide on a dense mix where the whole vocal then ducks audibly."),
                t ("Setz ihn hinter EQ-Anhebungen und Kompression, die beide S-Laute lauter machen. Presence (Makro-Fader in SIMPEL) erhöht das De-Essing automatisch zusammen mit den Höhen. Smooth kümmert sich um klingelnde Resonanzen, der De-Esser um S-Laute.",
                   "Place it after EQ boosts and compression, which both make S sounds louder. Presence (macro fader in SIMPLE) raises the de-essing automatically together with the highs. Smooth handles ringing resonances, the de-esser handles sibilance.") }));

            p.push_back (modulePage (l, gModules, "sat", "Saturation", {
                t ("Fügt Obertöne hinzu: Wärme, Biss, Dichte und mehr Präsenz in einem lauten Mix. Der Drive ist pegelkompensiert, er ändert also den Charakter, nicht die Lautstärke.",
                   "Adds harmonics: warmth, grit, density and a stronger presence in a loud mix. The drive is level compensated, so it changes character rather than volume."),
                t ("Wenn die Vocal für einen energiegeladenen Track zu sauber, dünn oder brav klingt.",
                   "When the vocal sounds too clean, thin or polite for an energetic track."),
                t ("- Character: Warm (weich, leicht asymmetrisch), Tape (sanft, symmetrisch), Tube (reiche geradzahlige Obertöne), Hard (festes Clipping).\n- Drive bestimmt die Menge der Sättigung (der große Regler; du kannst auch ein Band nach oben oder unten ziehen).\n- Multiband teilt die Stimme in Low, Mid und High: Klick auf ein Band, um es zu wählen, dann wähl seinen Character und Drive; zieh die Griffe zwischen den Bändern, um die Übergänge zu verschieben; jedes Band hat einen eigenen Ein-/Aus-Schalter. Der Kopfhörer oben rechts in einem Band spielt nur dieses Band, so wie es das Modul verlässt (Klick = an, bis du wieder klickst; halten = nur solange du hältst; eine Abhörhilfe, nicht gespeichert).\n- Link bewegt den Drive aller drei Bänder gemeinsam.\n- Mix (Hauptreihe) mischt das gesättigte und das saubere Signal.\n- Unter Mehr: Output gleicht den Pegel nach der Stufe an, dazu die beiden Übergangsfrequenzen.",
                   "- Character: Warm (smooth, slightly asymmetric), Tape (gentle, symmetric), Tube (rich even harmonics), Hard (firm clipping).\n- Drive sets the amount of saturation (the big control; you can also drag a band up or down).\n- Multiband splits the voice into Low, Mid and High: click a band to choose it, then pick its character and Drive; drag the handles between the bands to move the crossovers; each band has its own on/off switch. The headphones at the top right of a band play only that band as it leaves the module (click = on until you click again, hold = only while held; a listening aid, not saved).\n- Link moves the Drive of all three bands together.\n- Mix (main row) blends the saturated and the clean signal.\n- In More: Output trims the level after the stage, and the two crossover frequencies."),
                t ("- Mehr Drive = mehr Obertöne und hörbarere Verzerrung.\n- Weniger Mix = dezenter, die sauberen Transienten bleiben.\n- Hard verzerrt bei gleichem Drive am stärksten.",
                   "- More Drive = more harmonics and more audible distortion.\n- Lower Mix = subtler, keeps the clean transients.\n- Hard character distorts most at the same Drive."),
                t ("- Warm oder Tape, Drive 4 bis 8 dB, Mix 100 %.\n- Für aggressive Leads: Hard mit Mix 30 bis 50 %.",
                   "- Warm or Tape, Drive 4 to 8 dB, Mix 100 %.\n- For aggressive leads: Hard with Mix 30 to 50 %."),
                t ("- Zu viel Drive auf S-Lauten – setz den De-Esser davor oder senk den Drive.\n- Verzerrung nur im Solo beurteilen; im Mix ist oft mehr in Ordnung, aber prüf es.",
                   "- Too much drive on S sounds - put the De-Esser before it or lower Drive.\n- Judging distortion only on solo; in the mix more is often fine, but check."),
                t ("Saturation erhöht die Dichte ähnlich wie Kompression; mit Vocal Smash oder starker Kompression brauchst du weniger. Warmth, Punch und Density (Makro-Fader in SIMPEL) fügen automatisch Saturation hinzu.",
                   "Saturation increases density similar to compression; with Vocal Smash or heavy compression use less. Warmth, Punch and Density (macro faders in SIMPLE) add saturation automatically.") }));

            p.push_back (modulePage (l, gModules, "doubler", "Doubler", {
                t ("Erzeugt zusätzliche Takes der Vocal mit kleinen Unterschieden in Timing und Tonhöhe, wie eine gedoppelte Aufnahme: zwei oder vier Stimmen, jede mit eigenem Delay, Detune, Pegel und Panorama, während das Original in der Mitte bleibt.",
                   "Creates extra takes of the vocal with small timing and pitch differences, like a double-tracked recording: two or four voices, each with its own delay, detune, level and pan, while the original stays in the center."),
                t ("Für Refrains, Hooks und Adlibs, die größer und breiter wirken sollen.",
                   "For choruses, hooks and adlibs that should feel bigger and wider."),
                t ("- Stimmen: Classic (zwei Stimmen, eingestellt über Delay und Detune, wie in älteren Projekten), 2 oder 4 Stimmen mit eigenen Werten.\n- Die Bühne zeigt jede Stimme an ihrer Panorama-Position (links / rechts) und ihrem Pegel (oben = lauter); zieh eine Stimme, um sie zu verschieben, das Mausrad ändert ihr Detune, Rechtsklick schaltet sie aus. Bearbeitest du in Classic eine Stimme, wechselt der Doubler auf 2 Stimmen, die gleich klingen.\n- Die Zeilen unter der Bühne enthalten für jede Stimme Schalter, Delay, Detune, Gain und Pan (ziehen oder zum Eintippen klicken).\n- Mix bestimmt den Pegel der Doppelungen, Dry den des Originals.\n- Mod Depth (in Classic: Detune) und Mod Rate stellen das langsame Schwanken von Tonhöhe und Timing der Stimmen ein.\n- Unter Mehr: Width skaliert das Panorama der Stimmen, Mono safe hält sie aus der Mono-Summe heraus, Tone und Low Cut filtern die Doppelungen, Delay stellt das Classic-Paar ein.",
                   "- Voices: Classic (two voices set by Delay and Detune, as in older projects), 2 or 4 voices with their own values.\n- The stage shows every voice at its pan (left / right) and level (up = louder); drag a voice to move it, the mouse wheel changes its detune, right-click switches it off. Editing a voice in Classic switches to 2 voices that sound the same.\n- The rows under the stage hold each voice's on switch, delay, detune, gain and pan (drag, or click to type).\n- Mix sets the level of the doubled voices, Dry the level of the original.\n- Mod Depth (in Classic: Detune) and Mod Rate set the slow pitch and timing wobble of the voices.\n- In More: Width scales the voices' pans, Mono safe keeps them out of the mono sum, Tone and Low Cut filter the doubles, Delay sets the Classic pair."),
                t ("- Höherer Mix = deutlichere Doppelung.\n- Mehr Detune und Timing = lockerer, eher wie ein Chorus; zu viel klingt verstimmt oder nach Echo.\n- Mono safe 100 % = in Mono klingt es exakt wie das Original.",
                   "- Higher Mix = more obvious doubling.\n- More Detune and Timing = looser, more chorus-like; too much sounds out of tune or echoey.\n- Mono safe 100 % = mono playback sounds exactly like the original."),
                t ("- Mix 40 bis 50 %, Width 80 %, Delay 12 bis 18 ms, Detune / Mod Depth 6 bis 10 ct, Mono safe 50 bis 100 %.\n- 4 Stimmen: zwei enge Stimmen (10 bis 20 ms, etwa 5 ct) weit außen, zwei lockerere (25 bis 35 ms) weiter innen und 3 bis 6 dB leiser.",
                   "- Mix 40 to 50 %, Width 80 %, Delay 12 to 18 ms, Detune / Mod Depth 6 to 10 ct, Mono safe 50 to 100 %.\n- 4 voices: two tight voices (10 to 20 ms, about 5 ct) wide, two looser ones (25 to 35 ms) further in and 3 to 6 dB lower."),
                t ("- Zu viel Detune auf einer gestimmten Lead-Vocal.\n- Den Club vergessen: prüf mit Mono im OUTPUT-Panel.",
                   "- Too much Detune on a tuned lead.\n- Forgetting the club: check with Mono in the OUTPUT panel."),
                t ("Width (Makro-Fader in SIMPEL) steuert den Doubler. Setz den Reverb hinter den Doubler, damit beide Doppelungen denselben Raum bekommen.",
                   "Width (macro fader in SIMPLE) drives the doubler. Put reverb after the doubler so both doubles get the same room.") }));



            p.push_back (modulePage (l, gModules, "delay", "Delay", {
                t ("Echos der Vocal, mit Filtern auf den Wiederholungen und Ducking, das die Echos absenkt, solange die Vocal singt.",
                   "Echoes of the vocal, with filters on the repeats and ducking that pushes the echoes down while the vocal is active."),
                t ("Für Tiefe, Breite und Rhythmus, besonders an Phrasen-Enden.",
                   "For depth, width and rhythm, especially at the ends of phrases."),
                t ("- Sync koppelt die Zeit über Note an das Host-Tempo; ohne Sync (oder ohne Host-Tempo) gilt Time.\n- Feedback bestimmt die Zahl der Wiederholungen, Mix ihren Pegel.\n- Low Cut und High Cut filtern die Wiederholungen.\n- Ping-Pong verteilt die Wiederholungen abwechselnd links und rechts.\n- Into reverb schickt die Echos in den Reverb (wenn beide als Send laufen).\n- Ducking: Amount, Threshold, Attack und Release; Duck source wählt die Vocal oder den Sidechain-Eingang.\n- Rechtsklick auf die Kachel: Als Send / Als Insert entscheidet parallel oder seriell. Als Send heißt Mix SEND (der Send-Pegel).",
                   "- Sync locks the time to the host tempo using Note; without Sync (or without a host tempo) Time is used.\n- Feedback sets the number of repeats, Mix their level.\n- Low cut and High cut filter the repeats.\n- Ping-pong alternates repeats left and right.\n- Into reverb sends the echoes into the reverb (when both run as sends).\n- Ducking: Amount, Threshold, Attack and Release; Duck source chooses the vocal or the sidechain input.\n- Right-click the tile: As send / As insert decides parallel or series. As a send, Mix reads SEND (the send level)."),
                t ("- Mehr Feedback = längere Fahne. Sehr hohe Werte schaukeln sich auf.\n- Höherer Mix = lautere Echos.\n- Mehr Duck-Amount = klarere Wörter beim Singen, die Echos blühen in den Lücken auf.",
                   "- More Feedback = longer tail. Very high values build up.\n- Higher Mix = louder echoes.\n- More duck Amount = clearer words while singing, echoes bloom in the gaps."),
                t ("- Send, Sync an, 1/8 oder 1/4 punktiert, Feedback 25 bis 35 %, Mix 15 bis 25 %.\n- Low Cut 300 Hz, High Cut 6 kHz, Duck-Amount 6 bis 10 dB.",
                   "- Send, Sync on, 1/8 or 1/4 dotted, Feedback 25 to 35 %, Mix 15 to 25 %.\n- Low cut 300 Hz, High cut 6 kHz, duck Amount 6 to 10 dB."),
                t ("- Zu viel Mix ohne Ducking: Die Vocal verschwimmt.\n- Ungefilterte Wiederholungen, die in den Höhen mit dem Lead kämpfen.",
                   "- Too much Mix without ducking: the vocal gets blurry.\n- Unfiltered repeats that fight with the lead in the high end."),
                t ("Als Send wird es von der Kette nicht komprimiert oder gesättigt. Space (Makro-Fader in SIMPEL) steuert Delay und Reverb gemeinsam. Mit Into reverb bekommst du eine weiche, verwaschene Fahne.",
                   "As a send it is not compressed or saturated by the chain. Space (macro fader in SIMPLE) drives Delay and Reverb together. Use Into reverb for a smooth, washed tail.") }));

            p.push_back (modulePage (l, gModules, "reverb", "Reverb", {
                t ("Raum und Tiefe: acht Räume vom kleinen Room bis Atmosphere, mit der Vocal als Punkt im Raum – zieh ihn nach hinten für mehr Raum. Ducking schafft Platz, solange die Vocal singt.",
                   "Room and depth: eight spaces from a small Room to Atmosphere, with the vocal as a dot in the room - drag it back for more space. Ducking clears space while the vocal is singing."),
                t ("Für fast jeden Lead in einem Club-Track – meist als Send mit Ducking.",
                   "For almost every lead in a club track - usually as a send with ducking."),
                t ("- Das Bild: der Raum des gewählten Typs und der gewählten Größe, die Vocal als Punkt. Zieh den Punkt nach hinten (oder zieh irgendwo im Raum, nach oben = nach hinten): Mix, Pre-Delay und Size bewegen sich gemeinsam – vorne eine lange Lücke und wenig Hall, weit hinten viel Hall und ein großer Raum. Shift = fein, Doppelklick = Standard, Mausrad = kleine Schritte, Rechtsklick = Vorne / Mitte / Hinten. Die blasse Kurve an der Rückwand zeigt, wie lange die Fahne von tief (links) bis hoch (rechts) nachklingt.\n- Room: Room, Plate, Hall, Large Hall, Ambience, Atmosphere, Cinematic, Ethereal.\n- Size, Decay (die Länge der ganzen Fahne) und Mix (als Send: SEND) sitzen unter dem Bild.\n- Mehr: Pre-Delay, Low Cut, High Cut, Damping, Bass Decay, Width, Early, Diffusion, Movement und Ducking (wie beim Delay).",
                   "- The picture: the room of the chosen type and size, the vocal as a dot. Drag the dot back (or drag anywhere in the room, up = back): Mix, Pre-delay and Size move together - in front a long gap and little reverb, far back a lot of reverb and a big room. Shift = fine, double-click = default, wheel = small steps, right-click = Front / Middle / Back. The faint curve on the back wall shows how long the tail rings from low (left) to high (right).\n- Room: Room, Plate, Hall, Large Hall, Ambience, Atmosphere, Cinematic, Ethereal.\n- Size, Decay (the length of the whole tail) and Mix (SEND as a send) sit under the picture.\n- More: Pre-delay, Low cut, High cut, Damping, Bass decay, Width, Early, Diffusion, Movement and ducking (as in the Delay)."),
                t ("- Längeres Decay = längere Fahne. Lange Fahnen bei schnellem Tempo machen den Mix matschig.\n- Längeres Pre-Delay = klarere Trennung zwischen Wort und Raum.\n- Weniger Damping = dunklere Fahne, die hinter der Vocal sitzt.",
                   "- Longer Decay = longer tail. Long tails at fast tempos make the mix muddy.\n- Longer Pre-delay = clearer separation between word and room.\n- Lower Damping = darker tail that sits behind the vocal."),
                t ("- Send, Plate, Decay 1,5 bis 2,5 s, Pre-Delay 20 bis 40 ms, Mix 15 bis 20 %.\n- Low Cut 200 bis 300 Hz, Duck-Amount 6 bis 10 dB.",
                   "- Send, Plate, Decay 1.5 to 2.5 s, Pre-delay 20 to 40 ms, Mix 15 to 20 %.\n- Low cut 200 to 300 Hz, duck Amount 6 to 10 dB."),
                t ("- Zu viel Bass im Hall (lass den Low Cut oben).\n- Hall-Mengen im Solo vergleichen; entscheide im Mix.",
                   "- Too much low end in the reverb (keep the Low cut up).\n- Comparing reverb amounts in solo; decide in the mix."),
                t ("Setz den Reverb ans Ende oder als Send ein. Läuft das Delay als Send, schickt Into reverb die Echos in den Raum. Ducking über den Sidechain-Eingang (z. B. eine Kick) erzeugt pumpende Hallräume.",
                   "Put Reverb last or as a send. With Delay as a send, Into reverb feeds the echoes into the room. Ducking from the sidechain input (e.g. a kick) creates pumping reverbs.") }));


            p.push_back (modulePage (l, gModules, "smash", "Vocal Smash", {
                t ("Parallelkompression: Eine stark gequetschte Kopie der Vocal wird unter das Original gemischt. Dichte und Lautheit steigen, das Original behält seinen Punch.",
                   "Parallel compression: a heavily squashed copy of the vocal is blended under the original. Density and loudness increase while the original keeps its punch."),
                t ("Wenn die Vocal in einem vollen, lauten Track lauter und dichter werden muss, ohne ihren Attack zu verlieren.",
                   "When the vocal needs to be louder and denser in a busy, loud track without losing its attack."),
                t ("- Der große SMASH-Regler bestimmt, wie hart der parallele Weg gequetscht wird (niedrigerer Threshold, höhere Ratio). Die Säule daneben zeigt die Pegelreduktion des gequetschten Wegs mit Spitzenhaltung, das Bild links ihren Verlauf.\n- Blend bestimmt, wie viel vom gequetschten Weg dazukommt.\n- Tone kippt seine Farbe: negativ wärmer, positiv heller.\n- Output gleicht das Ergebnis an.\n- Mehr: Attack und Release stellen das Tempo des gequetschten Wegs ein.",
                   "- The big SMASH control sets how hard the parallel path is squashed (lower threshold, higher ratio). The column next to it shows the gain reduction of the squashed path with a peak hold, the picture on the left its history.\n- Blend sets how much of the squashed path is added.\n- Tone tilts its color: negative warmer, positive brighter.\n- Output trims the result.\n- More: Attack and Release set the speed of the squashed path."),
                t ("- Mehr Amount = flacherer gequetschter Weg.\n- Mehr Blend = dichtere, lautere Vocal; zu viel kostet die natürliche Dynamik.\n- Kurzes Release = dichter und körniger, langes = weicher.",
                   "- Higher Amount = flatter squashed path.\n- Higher Blend = denser, louder vocal; too high loses the natural dynamics.\n- Short Release = denser and grainier, long = smoother."),
                t ("- Amount 50 %, Blend 30 bis 40 %, Attack 2 ms, Release 80 ms, Tone +20 %.",
                   "- Amount 50 %, Blend 30 to 40 %, Attack 2 ms, Release 80 ms, Tone +20 %."),
                t ("- Blend so weit aufdrehen, bis die Vocal nur besser wirkt, weil sie lauter ist – nutz den fairen Vergleich.\n- Ein sehr heller Tone, der S-Laute übertreibt.",
                   "- Raising Blend until the vocal only seems better because it is louder - use the fair compare.\n- Very bright Tone that exaggerates S sounds."),
                t ("Meist nach dem Compressor (erst Kontrolle, dann Dichte) und vor dem De-Esser. Mit Vocal Smash können Compressor und Saturation oft weniger tun.",
                   "Usually after the Compressor (control first, then density) and before the De-Esser. With Smash in use, the Compressor and Saturation can often do less.") }));


            p.push_back (modulePage (l, gModules, "smooth", "Smooth", {
                t ("Adaptive Resonanz-Kontrolle: Findet Frequenzen, die klingeln oder aus ihrer Umgebung herausstechen, und senkt sie nur, solange sie herausstechen. Die Anzeige zeigt das Live-Spektrum der Vocal; was gerade entfernt wird, ist pink gezeichnet.",
                   "Adaptive resonance control: finds frequencies that ring or stick out of their neighbors and lowers them only while they stick out. The display shows the live spectrum of the vocal; what is removed right now is drawn in pink."),
                t ("Für harte, näselnde oder klingelnde Vocals, günstige Mikrofone und helle, komprimierte Leads.",
                   "For harsh, nasal or ringing vocals, cheap microphones and bright, compressed leads."),
                t ("- Depth bestimmt, wie stark Resonanzen abgesenkt werden.\n- Sharpness bestimmt, wie weit eine Frequenz herausstechen muss, bevor sie zählt.\n- Speed bestimmt, wie schnell Smooth reagiert und loslässt (Attack und Release zusammen; beide einzeln unter Mehr).\n- Die Kurve mit sechs Punkten gewichtet die Bearbeitung pro Frequenz: Zieh einen Punkt nach oben für mehr Absenkung dort, nach unten für weniger (x = Frequenz). Doppelklick setzt einen Punkt zurück; Rechtsklick für „Alle flach“.\n- Die schattierten Bereiche links und rechts werden nicht bearbeitet: Zieh ihre Kanten (Low / High).\n- Mehr: Mode Soft (breit, sanft) oder Hard (schmal, stärker), Attack, Release, Output, Low, High.\n- Delta spielt nur das, was entfernt wird.",
                   "- Depth sets how strongly resonances are reduced.\n- Sharpness sets how far a frequency must stick out before it counts.\n- Speed sets how fast it reacts and lets go (Attack and Release together; both are in More separately).\n- The curve with six points weights the treatment per frequency: drag a point up for more reduction there, down for less (x = frequency). Double-click a point to reset it; right-click for \"All flat\".\n- The shaded areas left and right are not treated: drag their edges (Low / High).\n- More: Mode Soft (wide, gentle) or Hard (narrow, stronger), Attack, Release, Output, Low, High.\n- Delta plays only what is being removed."),
                t ("- Mehr Depth oder Sharpness = mehr Absenkung; zu viel macht die Stimme stumpf.\n- Ein breiterer Low-/High-Bereich bearbeitet mehr von der Stimme.\n- Ein Punkt bei -12 dB stoppt die Bearbeitung um seine Frequenz fast ganz.",
                   "- More Depth or Sharpness = more reduction; too much makes the voice dull.\n- A wider Low / High range treats more of the voice.\n- A point at -12 dB nearly stops the treatment around its frequency."),
                t ("- Depth 40 bis 60 %, Sharpness 50 %, Low 800 Hz, High 12 kHz, Mode Soft.\n- Schalt Delta kurz ein: Du solltest nur Härte hören, nicht die Stimme selbst.",
                   "- Depth 40 to 60 %, Sharpness 50 %, Low 800 Hz, High 12 kHz, Mode Soft.\n- Turn on Delta briefly: you should hear only harshness, not the voice itself."),
                t ("- Delta eingeschaltet lassen.\n- Smooth gegen S-Laute einsetzen – dafür ist der De-Esser gemacht.",
                   "- Leaving Delta on.\n- Using Smooth to remove sibilance - the De-Esser is made for that."),
                t ("Setz es hinter Kompression und Saturation, die Resonanzen hervorholen. Statische Probleme löst du oft besser zuerst mit einer EQ-Absenkung.",
                   "Place it after compression and saturation, which bring resonances out. Static problems are often better fixed with an EQ cut first.") }));

            p.push_back (modulePage (l, gModules, "check", "Vocal Check", {
                t ("Analysiert die Vocal an seiner Position in der Kette und erklärt, was sich verbessern lässt: Balance zwischen Tiefen, Körper, Kern, Präsenz, S und Luft, Pegelumfang, S-Laute und Clipping. Er hört nur zu – das Audio läuft unverändert durch.",
                   "Analyzes the vocal at its position in the chain and explains what could be improved: balance between low end, body, core, presence, S and air, loudness range, sibilance and clipping. It only listens - the audio passes through unchanged."),
                t ("Wenn du unsicher bist, was die Vocal braucht, und als letzter Check deiner Einstellungen.",
                   "When you are unsure what the vocal needs, and as a final check of your settings."),
                t ("- Schalt ihn ein und spiel die Vocal ab; Pausen werden ignoriert. Er braucht etwa 4 Sekunden Stimme, bevor er Ergebnisse zeigt.\n- Mode: Continuous analysiert ungefähr die letzten 30 Sekunden; Capture analysiert alles zwischen Start und Stop.\n- Reset löscht die Analyse.\n- Klick auf einen Befund öffnet das Modul, das er empfiehlt.",
                   "- Switch it on and play the vocal; pauses are ignored. It needs about 4 seconds of voice before it shows results.\n- Mode: Continuous analyzes the last 30 seconds or so; Capture analyzes everything between Start and Stop.\n- Reset clears the analysis.\n- Click a finding to open the module it recommends."),
                t ("Es gibt keine Klangregler – nur den Analyse-Modus. Mehr Material ergibt verlässlichere Befunde.",
                   "There are no sound controls - only the analysis mode. More material gives more reliable findings."),
                t ("- Setz Vocal Check ans Ende und spiel einen typischen Abschnitt des Songs.\n- Um die Aufnahme selbst zu beurteilen, zieh ihn an den Anfang der Kette.",
                   "- Put Vocal Check last and play a typical section of the song.\n- To judge the recording itself, drag it to the front of the chain."),
                t ("- Jedem Befund blind folgen. Er beschreibt die Messung; deine Ohren und der Track entscheiden.\n- Einen Abschnitt mit viel Stille oder anderen Instrumenten analysieren.",
                   "- Following every finding blindly. It describes the measurement; your ears and the track decide.\n- Analyzing a section with a lot of silence or other instruments."),
                t ("Er sieht genau, was die Module davor getan haben. Weil er das Audio nie verändert, ändert seine Position nie den Klang.",
                   "It sees exactly what the modules before it did. Because it never changes audio, its position never changes the sound.") }));

            p.push_back (modulePage (l, gModules, "ride", "Level Ride", {
                t ("Gleicht Lautstärke-Unterschiede in deiner Vocal aus: Leise Wörter werden angehoben, laute abgesenkt, weich und ohne das Pumpen eines Compressors. Die Anzeige zeigt einen Balken pro Wort: grau der Pegel davor, farbig der Pegel nach dem Ausgleich; die Zahl ist der Gain in diesem Moment.",
                   "Evens out loudness differences inside your vocal: quiet words are lifted and loud words are lowered, smoothly and without the pumping of a compressor. The display shows one bar per word: gray is the level before, the colored bar the level after the ride; the number is the gain right now."),
                t ("Für Vocals mit großen Pegelsprüngen zwischen Wörtern oder Phrasen, vor der Kompression.",
                   "For vocals with big level jumps between words or phrases, before compression."),
                t ("- Ziel legt den Pegel fest, den Level Ride anstrebt: Zieh die gestrichelte Linie nach oben oder unten (Mausrad: Schritte von 0,5 dB, Doppelklick: Standard) oder nutz den Ziel-Regler.\n- Ausgleich legt fest, wie viel vom Abstand zum Ziel korrigiert wird.\n- Tempo legt fest, wie schnell er folgt: langsam und weich oder Wort für Wort.\n- Ignore below behandelt leisere Signale als Pausen und hebt sie nie an.\n- Mehr: Max Boost und Max Cut begrenzen die größte Anhebung und Absenkung; Phrase mittelt über längere Phrasen, damit der natürliche Fluss bleibt; Transients ignoriert kurze Spitzen wie Konsonanten und Plosive.",
                   "- Target sets the level the rider aims for: drag the dashed line up or down (mouse wheel: 0.5 dB steps, double-click: default) or use the Target control.\n- Leveling sets how much of the distance to the target is corrected.\n- Speed sets how fast it follows: slow and smooth, or word by word.\n- Ignore below treats quieter signals as pauses and never boosts them.\n- More: Max boost and Max cut limit the largest lift and reduction; Phrase averages over longer phrases to keep the natural flow; Transients ignores short peaks such as consonants and plosives."),
                t ("- Mehr Ausgleich = gleichmäßiger; 100 % zieht alles zum Ziel.\n- Höherer Max Boost hebt leise Wörter stärker an, aber auch Atmer und Raumgeräusche.\n- Höheres Tempo = schnellere, hörbarere Bewegungen.",
                   "- Higher Leveling = more even; 100 % pulls everything towards the target.\n- Higher Max boost lifts quiet words more, but also breaths and room noise.\n- Higher Speed = faster, more audible moves."),
                t ("- Ausgleich 60 bis 70 %, Max Boost und Max Cut 6 bis 8 dB, Tempo 50 %.\n- Setz das Ziel in die Nähe des durchschnittlichen Pegels der Vocal.\n- Fang moderat an und erhöh die Intensität Schritt für Schritt.",
                   "- Leveling 60 to 70 %, Max boost and Max cut 6 to 8 dB, Speed 50 %.\n- Set Target near the average level of the vocal.\n- Start moderate and raise the intensity step by step."),
                t ("- Zu viel Boost macht Atmer und Raumgeräusche lauter – heb Ignore below an.\n- Sehr hohes Tempo: Die Vocal beginnt zu wackeln.",
                   "- Too much boost makes breaths and room noise louder - raise Ignore below.\n- Very fast Speed: the vocal starts to wobble."),
                t ("Setz ihn vor den Compressor: Der muss dann nur noch kurze Spitzen fangen.",
                   "Place it before the Compressor: the compressor then only has to catch short peaks.") }));

            p.push_back (modulePage (l, gModules, "output", "Output", {
                t ("Die letzte Stufe des Plugins: die Stereobreite des ganzen Ausgangs, Mono-Sicherheit im Bass, der Lautheits-Vergleich und der Clip Guard.",
                   "The last stage of the plugin: the stereo width of the whole output, mono safety in the low end, the loudness comparison and the clip guard."),
                t ("Immer aktiv. Du öffnest sie mit einem Klick auf OUTPUT am Ende der Kette.",
                   "Always active. Open it by clicking OUTPUT at the end of the chain."),
                t ("- Width ändert nur das Seitensignal, Mitte und Mono-Summe bleiben also unberührt.\n- Mono below hält alles unterhalb der Frequenz in Mono (20 Hz = aus).\n- Level Match: Aus, Wie Bypass (fairer Bypass-Vergleich) oder Auto-Gain (bearbeiteter Klang so laut wie der Eingang). Measure over und Max correction legen fest, wie gemessen wird und wie weit korrigiert werden darf.\n- Clip Guard: weicher Clipper ab -1 dBFS, der den Ausgang unter 0 dBFS hält.\n- Goniometer und Korrelation zeigen das Stereobild.",
                   "- Width changes only the side signal, so the center and the mono sum stay intact.\n- Mono below keeps everything under the frequency in mono (20 Hz = off).\n- Level Match: Off, Like bypass (fair bypass comparison) or Auto gain (processed sound as loud as the input). Measure over and Max correction set how it measures and how far it may correct.\n- Clip guard: soft clipper from -1 dBFS that keeps the output below 0 dBFS.\n- The goniometer and correlation show the stereo image."),
                t ("- Width über 100 % macht breiter, darunter schmaler, 0 % ist Mono.\n- Höheres Mono below = mehr vom Bass in Mono.",
                   "- Width above 100 % widens, below narrows, 0 % is mono.\n- Higher Mono below = more of the low end in mono."),
                t ("- Width 100 bis 120 %, Mono below 120 Hz für den Club.\n- Level Match: Wie Bypass, während du arbeitest.",
                   "- Width 100 to 120 %, Mono below 120 Hz for club playback.\n- Level Match: Like bypass while you work."),
                t ("- Extreme Width, die die Korrelation unter 0 drückt.\n- Vergessen, dass Auto-Gain den Ausgangspegel laufend verändert.",
                   "- Extreme Width that pushes correlation below 0.\n- Forgetting that Auto gain changes the output level continuously."),
                t ("Width (Makro-Fader in SIMPEL) steuert die Ausgangsbreite zusammen mit dem Doubler. Vocal Level (der Makro-Fader, auch ein Hauptregler dieses Panels) stellt den finalen Pegel nach dieser Stufe ein; auch Input Gain sitzt hier.",
                   "Width (macro fader in SIMPLE) drives the output width together with the Doubler. Vocal Level (the macro fader, also a main control of this panel) sets the final level after this stage; Input Gain sits here too.") }));

            return p;
        }
    }

    const juce::StringArray& moduleSectionHeadings (Language l)
    {
        static const juce::StringArray de { juce::String::fromUTF8 ("Was es tut"), "Wann du es brauchst", "So bedienst du es",
                                            juce::String::fromUTF8 ("Höhere und niedrigere Werte"), "Gute Startpunkte",
                                            "Typische Fehler", "Zusammenspiel mit anderen Modulen" };
        static const juce::StringArray en { "What it does", "When to use it", "How to use it", "Higher and lower values",
                                            "Good starting points", "Common mistakes", "Working with other modules" };
        return l == Language::german ? de : en;
    }

    const juce::StringArray& moduleSectionHeadings()
    {
        return moduleSectionHeadings (i18n::current());
    }

    const std::vector<Page>& pages (Language l)
    {
        // Built on first use per language (never during static initialisation) and kept for the
        // lifetime of the plugin, so find() pointers stay valid across a language switch
        if (l == Language::german)
        {
            static const std::vector<Page> de = build (Language::german);
            return de;
        }
        static const std::vector<Page> en = build (Language::english);
        return en;
    }

    const std::vector<Page>& pages()
    {
        return pages (i18n::current());
    }

    const Page* find (const juce::String& key)
    {
        for (auto& page : pages())
            if (page.key == key)
                return &page;
        return nullptr;
    }

    juce::String keyForModule (int id)
    {
        if (id < 0 || id >= dsp::numModules)
            return "output";
        return dsp::moduleKey ((dsp::ModuleId) id);
    }
}

namespace velvox::ui::help
{
    juce::String shortDescription (int id)
    {
        using dsp::ModuleId;
        using i18n::tr;
        if (id < 0 || id >= dsp::numModules)
            return tr ("Stereobild, Mono-Sicherheit und fairer Lautheits-Vergleich",
                       "Stereo image, mono safety and fair loudness comparison");

        switch ((ModuleId) id)
        {
            case ModuleId::eq:      return tr ("Rumpeln entfernen, Körper, Präsenz und Luft formen", "Remove rumble, shape body, presence and air");
            case ModuleId::comp:    return tr ("Gleicht den Pegel aus, damit jedes Wort im Mix sitzt", "Evens out the level so every word sits in the mix");
            case ModuleId::deess:   return tr ("Zähmt scharfe S- und T-Laute, ohne die Stimme stumpf zu machen", "Tames sharp S and T sounds without dulling the voice");
            case ModuleId::sat:     return tr ("Fügt Obertöne hinzu: Wärme, Biss und Dichte", "Adds harmonics for warmth, grit and density");
            case ModuleId::doubler: return tr ("Zusätzliche Takes links und rechts für eine breite, dichte Vocal", "Extra takes left and right for a wide, thick vocal");
            case ModuleId::delay:   return tr ("Echos neben der Stimme; Ducking hält die Wörter klar", "Echoes beside the voice; ducking keeps the words clear");
            case ModuleId::reverb:  return tr ("Raum und Tiefe; Ducking schafft Platz, solange du singst", "Room and depth; ducking clears space while you sing");
            case ModuleId::smash:   return tr ("Parallelkompression: Dichte und Lautheit, das Original behält seinen Punch", "Parallel compression: density and loudness, the original keeps its punch");
            case ModuleId::smooth:  return tr ("Findet klingelnde, harte Frequenzen und zähmt sie nur, solange sie herausstechen", "Finds ringing, harsh frequencies and tames them only while they stick out");
            case ModuleId::check:   return tr ("Analysiert die Vocal an dieser Stelle der Kette und erklärt, was sich verbessern lässt", "Analyzes the vocal at this point of the chain and explains what could be improved");
            case ModuleId::ride:    return tr ("Hebt leise Wörter an, senkt laute ab – natürlich, ohne Pumpen", "Lifts quiet words, lowers loud ones - natural, without pumping");
        }
        return {};
    }
}
