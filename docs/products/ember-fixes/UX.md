# EMBER – Bedien-Fixes (Branch `fix/ember-ux`, 2026-11-23)

Rechtliches (EULA, Datenschutz, Computername an Polar, Signatur, Platzhalter für Support-Mail/URL) ist bewusst **nicht** angefasst.

| # | Was | Wie gelöst |
|---|---|---|
| 1 | Stimmlage = zwei Undo-Schritte | `EmberAudioProcessor::setVoice (int)`: ein `beginUndoStep (-1)` + `UndoGroup`, Frequenz und Threshold in einem Schritt. Panel ruft nur noch das auf. |
| 2 | Stimmlage überschreibt Threshold | Threshold geht nur mit, solange er auf einem Stimmlagen-Wert steht (−35 / −32 / −33, also auch Standard). Von Hand, Preset oder AUTO gesetzt → bleibt; Statuszeile sagt es kurz. AUTO-Schutz hängt nicht mehr an `lastAuto`, sondern an `fromAuto` der aktuellen Einstellung (kommt mit Preset-Laden, A/B, Undo und Projekt richtig mit). |
| 3 | Grenzen TIEF/MITTEL/HOCH = Preset-Frequenzen | Grenzen 5,75 kHz und 7 kHz (`voiceLowMidHz` / `voiceMidHighHz`), Rap (5,6 k) → TIEF, Pop-Gesang (6,8 k) → MITTEL. Test prüft: jedes Werks-Preset ≥ 100 Hz von einer Grenze weg, jede Stimmlage leuchtet nach dem Parameter-Rundweg selbst. |
| 4 | „rot/red“ statt pink | Infotexte (Spektrum, S-Pegel), Kommentar, beide Handbücher; Handbuch-Akzentfarbe auf Pink. |
| 5 | Undo ohne Presetname / AUTO / A/B | Undo-Schritte sind Snapshots (Werte + Presetname, *, AUTO-Kennung); AUTO-Schritte bringen auch A/B zurück. Normales Undo schaltet A/B nie um. |
| 6 | AUTO überschreibt A und B | Vorher bleibt auf dem aktiven Slot, Ergebnis auf den anderen (erstes Mal wie bisher: A = vorher, B = AUTO). Liegt auf dem anderen Slot schon eine andere Einstellung, bleibt sie; Ergebnis kommt auf den aktiven Slot, vorher per Strg+Z. Meldung sagt, wo „vorher“ liegt. In `applyAuto` nur die Slot-/Namenszeilen ersetzt (`setPresetMeta` + `assignAutoSlots`). |
| 7 | A/B nicht im Projekt | `EmberAB`-Block im State (beide Slots mit Werten, Preset, Name, AUTO-Kennung, aktiver Slot); wird vor `replaceState` aus dem XML genommen. |
| 8 | AUTO bei BYPASS | Statuszeile beim Zuhören und beim Ergebnis: „BYPASS ist an: Das Ergebnis hörst du erst, wenn du BYPASS ausschaltest.“ Ergebnis wird auch bei BYPASS gezeigt. |
| 9 | AUTO-Tipp weg nach Abbrechen | `setAutoUsed()` erst, wenn AUTO fertig ist (Phase → done). |
| 10 | Blättern ab AUTO-Einstellung überspringt erstes Preset | Nicht in der Liste (AUTO, gelöschtes Preset): vor = erstes, zurück = letztes. |
| 11 | Undo schwer zu finden | War schon im ≡-Menü; jetzt mit Strg+Y. Tasten auch, wenn der Editor selbst den Fokus hat. Handbücher: Undo/Redo, Lizenz-Untermenü, Mausrad, Alt+Klick, Shift/Strg fein, Doppelklick, Fenstergröße. |
| 12 | Lizenzkarte orange | `LicenceLock::accentColourId` im Look-and-Feel (EMBER setzt Pink, VOCALYPSE bleibt Feuer). Schlüssel-Dialog im Plugin-Look (eigenes Look-and-Feel, lebt so lange wie das Fenster). Karte zeigt „Wird aktiviert … einen Moment.“ |
| 13 | Einheiten / Meter | Threshold-Wert beim Ziehen mit „dB“, S-Pegel-Skala mit „dB“. Spitzenhalten (1 s, dann 4,5 dB/s) und S-Pegel-Abfall (18 dB/s) nach Uhr im Timer statt pro Paint. |
| 14 | Presets verwalten | Preset-Menü: Umbenennen (Dialog) und Löschen (Rückfrage, Papierkorb). Fehler bei Laden/Speichern/Umbenennen/Löschen als Meldung. Name in der Leiste und in A/B folgt dem Umbenennen. |
| 15 | Zoomstufen | ≡ › Fenstergröße 75/100/125/150/200 %, gemerkt wie gezogene Größe. |
| 16 | About / Stimmlage nach AUTO | About schließt mit Esc. Klick auf leuchtende Stimmlage nach AUTO: „AUTO hat in diesem Bereich … gefunden – die Frequenz bleibt.“ |
| 17 | Textabgleich | AUTO-Karte und Handbücher: 12 Sekunden Gesang, Pausen zählen nicht, fertig auch bei Stopp nach ≥ 3 s („bis 30 s“ entfernt). A/B-Abschnitt: Knopf wird zu „B → A“, wie AUTO die Slots füllt, A/B im Projekt. |
| 18 | S-Pegel nur über Farbe | „Kein S“ (Smart) ist grau **gestreift**. |
| 19 | `setStateInformation` threadsicher | Parameter sofort; Presetname/A/B/Undo-Reset außerhalb des Message-Threads als `pendingMeta` (unter `metaLock`), übernimmt der Processor-Timer. `getStateInformation` liest unter demselben Lock (und speichert ggf. die noch offene Meta). Keine `state.state`-Properties mehr für Presetname. Nichts davon im Audio-Thread. |
| 20 | `showPresetMenu` ohne SafePointer | SafePointer im Preset-Menü und im Speichern-Dialog. |

## Tests

`EmberTests`: neuer Block „ux fixes“ (Stimmlage = ein Schritt, Threshold-Regeln, Grenzen, Undo mit Name und A/B, AUTO neben geparktem Slot, A/B im Projekt, Blättern ab AUTO, Umbenennen/Löschen, State aus einem anderen Thread). 366 bestanden, 0 fehlgeschlagen. Snapshots (`--snapshots`) DE/EN angesehen. pluginval 1.0.4, Strenge 10, mit und ohne GUI-Tests: SUCCESS.

## Für die anderen Branches

- `fix-auto`: in `applyAuto` stehen statt der alten Slot-/Namenszeilen nur `setPresetMeta (-1, {}, true); assignAutoSlots (before);`. `getAutoMessage` (done) nutzt `autoBeforeSlot`. `isShowingAutoResult` prüft zusätzlich `fromAuto`. `timerCallback` ruft zuerst `applyPendingMeta()`.
- `fix-dsp`: Werks-Preset-Frequenzen nicht näher als 100 Hz an 5,75 / 7 kHz legen (Test schlägt sonst an), oder die Grenzen in `EmberProcessor.h` mitziehen.
