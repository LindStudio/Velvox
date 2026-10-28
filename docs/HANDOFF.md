# HEADLINER – Übergabe für die nächste Session

## STAND 2026-10-28 ABEND (dieser PC) – HIER WEITERMACHEN
Branch **`feature/modules-0.7`**, Version **0.7.0**. Die Review-Punkte aus
`docs/superpowers/plans/2026-10-28-review-umsetzung.md` sind **alle umgesetzt** (Punkt 10 hatte Jakob abgelehnt).
Was sich geändert hat, steht im CHANGELOG unter „Review-Umsetzung“. Entscheidungen, die ich dabei selbst getroffen
habe („Ruling“), stehen im Ledger `.superpowers/sdd/2026-10-28-review-umsetzung/progress.md` (nur auf diesem PC).
- Voller Lauf 37.889 Checks grün. Eigenes Abschluss-Review gemacht, ein Fund behoben: Der Analyzer wertet nur noch
  bei offenem Editor aus.
- **Nicht gepusht, nicht installiert.** Vorher Jakob fragen. Zum Installieren FL Studio schließen.
- Offen für Jakob: Plugin in FL testen. Die wichtigsten Rulings, die er absegnen sollte: Vocal-Check-Abzeichen am
  Tab statt eines langen Chips in der Kopfzeile, keine Gruppen-Zusammenfassung (Stufe 3) in der Kette, keine
  Gruppenbeschriftung unter der Kette, Saturation-Kennlinien unverändert (nur Drive/Mix begrenzt), Legende
  „unbearbeitet / bearbeitet“ statt „vorher / nachher“.
- Tests nie zweimal gleichzeitig starten: Sie teilen sich `%TEMP%\velvox-tests.settings`, dann schlagen
  Sprach-Tests fehl.

## STAND 2026-10-28 (Shadow-PC)
Branch **`feature/modules-0.7`**, am PC „Shadow“ weitergemacht (alle Commits des Tages noch nicht gepusht).
- Punkte 1, 2 und der Changelog-Teil von 4 unten sind **erledigt**: voller Lauf 37.527 Checks grün, pluginval 10 SUCCESS.
  Das Review (neu gemacht, die `.superpowers`-Ledger liegen nur am anderen PC) fand einen echten Fehler, der jetzt behoben ist:
  Room Noise / Breath Control klickten beim Einfügen während der Wiedergabe → jetzt `reset (fadeIn)` wie Smooth, Test in
  `Tests/RoutingTests.cpp`. Bekannt, nicht behoben: unter 44,1 kHz (22,05/24 kHz) senkt Breath Control die ersten ~15 ms
  eines Atmers nicht ab, weil die Look-ahead-Zeit dort kürzer ist als Erkennung + halbes Fenster.
- Punkt 3 entschieden: Modulnamen bleiben Englisch. Jakob 2026-10-28: noch nicht pushen, noch nicht installieren.
- Punkt 5 **erledigt**: „Der Auto Mix hört den Beat“, Plan `docs/superpowers/plans/2026-10-28-automix-hears-the-beat.md`
  (Jakob wählte „Auto Mix hört den Beat“ und „Zum Beat einpegeln“: Stimme 1 LU über dem Beat). Tests: `Tests/BeatTests.cpp`,
  synthetische Beats `Tests/SyntheticBeat.h`. Ohne Beat bleibt alles gleich (AutoMix-/AutoEq-Suites unverändert grün).
  Der Assistent (`Source/Assistant`, `AssistantView`) ist nicht mehr im Editor eingebunden und wurde nicht angefasst.
  Noch nicht mit echten Beats geprüft (keine `velvox-data` auf diesem PC).
- Punkt 6 **erledigt**: neue Figur **Memphy** (Flame Hoodie Imp), alte Figur gelöscht. Alles dazu (Animationen je
  Situation, Gesichter, Tour, Pipeline mit Blender auf diesem PC): `docs/design/VOCALYPSE_NAME_UND_MASKOTTCHEN.md`,
  Abschnitt „Memphy“. Renders: `%USERPROFILE%\dev\velvox-bench\2026-10-28\mascot-imp`.
  **Danach entschieden:** im Plugin Comic-Sticker statt 3D-Animation (Plugin wieder ~10 MB statt 53 MB).
- EQ-Fehler aus der TODO behoben (Q sprang nach Mausrad während des Sweeps zurück), Test in `Tests/AuditionTests.cpp`.
- **Noch offen aus Jakobs `Character\TODO.txt` (Drive):** Saturation verzerrt die Vocal stark · Auto Mix klingt dumpf · Website: Kauf-Button weg („bald verfügbar“),
  Newsletter-Anmeldung (früher Zugang + Rabattcode per Mail), Impressum von lind-studio.com übernehmen, Datenschutz
  inkl. E-Mail-Erfassung · Idee für später: Belohnung für gute Rezensionen.
- **Review-Punkte geplant, NICHT begonnen:** `docs/superpowers/plans/2026-10-28-review-umsetzung.md` (14 Punkte mit Jakobs
  Entscheidungen, Punkt 10 abgelehnt). Erst anfangen, wenn Jakob es sagt. Review-Website:
  https://claude.ai/artifact/NwwJ1YbUGzf2B3VyNftHae
- Memphy ist heute fertig geworden als Comic-Figur im violetten Speedlines-Rahmen (Kopf schaut heraus), blinzelt, auch auf
  der Willkommenskarte. Der komplette Testlauf lief zuletzt vor den Memphy-Feinschliffen (Rahmen, Blinzeln, Willkommen) –
  vor dem nächsten Push `scripts\build.ps1` + pluginval laufen lassen.
- Pushen erst nach Jakobs OK.

## STAND 2026-10-27 ABEND
Branch **`feature/modules-0.7`** (baut auf `feature/i18n` auf). Jakob hat unterbrochen („ich mach morgen weiter“).
- **Fertig + committet:**
  - Plan A `docs/superpowers/plans/2026-10-27-remove-creative-modules.md`: Pitch, Stutter, Pulse, Choir komplett raus,
    6 Presets umgebaut (3 umbenannt: Tight Rap Vocal, Deep Double, Bright Hook). Gate war grün (36.896 Checks, pluginval 10).
  - Plan B `docs/superpowers/plans/2026-10-27-denoise-breath-modules.md`, alle 5 Tasks: Raumrauschen (`denoise`,
    „Room Noise“) und Atem-Absenker (`breath`, „Breath Control“) mit DSP, Panels, Hilfe, Auto Mix. Einzel-Suites grün.
  - Vorher (auch hier drin): Maskottchen-Teufel in SIMPEL, SIMPEL-Regler zeigen nach dem Auto Mix seine Einstellung.
- **Noch offen (in dieser Reihenfolge):**
  1. Voller Lauf `scripts\build.ps1` + pluginval 10 nach Plan B (wurde abgebrochen, bevor er fertig war).
  2. Finales Whole-Branch-Review (Paket: `.superpowers/sdd/2026-10-27-denoise-breath-modules/review-91e1fa1..181e15a.diff`,
     Rulings stehen in den `progress.md`-Ledgern beider Pläne unter `.superpowers/sdd/`).
  3. Jakob fragen: Modulnamen Englisch lassen (Regel docs/I18N.md) oder deutsch („Raumrauschen“, „Atem-Absenker“)?
     Latenz: Raumrauschen 1024 Samples, Atem-Absenker 90 ms – nur solange das Modul in der Kette ist.
  4. Installieren (FL schließen) + Changelog-Eintrag für Plan B ergänzen.
  5. Danach: Beat als Sidechain (eigener Plan; `CapturedAudio` hat schon ein `beat`-Feld).
  6. Maskottchen neu (Meteor / Ghettoblaster / Kassette, 3D-Comic) – Jakob erzeugt Bilder mit den Prompts.

Stand: **2026-10-26**, Branch `feature/automix`, Version **0.6.1** (Beta). Diese Datei zuerst lesen.

## Auf einen Blick
- **Produkt:** Headliner Vocal (Arbeitstitel Velvox, Firma Lind Audio), automatischer Vocal-Mixer als VST3 + Standalone für Windows,
  JUCE 8.0.9. Kommerziell geplant (JUCE-Lizenz für Closed Source nötig).
- **Status:** alle 0.6.1-Punkte umgesetzt, 32.732 Tests grün, pluginval Strenge 10 SUCCESS, auf GitHub gepusht.
  Installiert ist ein Build vom 26.10. vor dem Sweep (`854743c`) – die Commits `f2362d0`/`4d2f786` sind noch **nicht installiert**
  (FL Studio schließen, dann `scripts\install-vst3.ps1`).
- **Offen beim Nutzer:** Test in FL Studio (Sweep im EQ, Presets, Auto Mix im echten Projekt), Hörtest der 0.6-Module.

## Wo was liegt
| Was | Ort |
|---|---|
| Arbeitskopie Plugin | `%USERPROFILE%\dev\velvox` (Branch `feature/automix`) |
| GitHub (Quelle der Wahrheit, privat) | https://github.com/LindStudio/Velvox – Push funktioniert auf diesem PC |
| Drive-Sicherung | `scripts\backup-to-drive.ps1` → `G:\Meine Ablage\MUSIC\Plugin\_git-backup\velvox.bundle` (Remote `drive-backup`) |
| Website (eigenes Git, Branch `master`) | `%USERPROFILE%\dev\lind-audio-web`; Drive: Bundle `_git-backup\lind-audio-web.bundle` (Remote `drive-backup`) + Kopie `G:\Meine Ablage\MUSIC\Plugin\Website\` (Anleitung `LIESMICH.md`). Sichern: `python backup_to_drive.py` |
| Website-Vorschau (privat) | https://claude.ai/artifact/Ma28n9mFG74Zqd3nb2KKQr |
| Website-Design des Nutzers (Canvas) | https://claude.ai/artifact/E4diYCZugWuH1BCQybhotF |
| Testdaten | `%USERPROFILE%\dev\velvox-data` (VocalSet CC BY 4.0, Sonovox-Demo nur intern) |
| Renders, Screenshots, Hörtest-Kits | `%USERPROFILE%\dev\velvox-bench\<datum>\` |
| pluginval | `%USERPROFILE%\dev\tools\pluginval-1.0.4\pluginval.exe` |
| Worktrees der 0.6-Spuren (alle gemergt) | `%USERPROFILE%\dev\velvox-wt\*` |

Anderer PC („Shadow"): zuerst `git fetch origin` bzw. `git fetch drive-backup feature/automix` und fast-forwarden.

## Arbeitsweise (vom Nutzer gewünscht)
- Antworten auf Deutsch, kurz und klar. Nutzer heißt Jakob, Produzent (Künstlername Jackyy), Steiermark.
- **Design/UI:** Jakob gibt die Richtung per Mockups vor (SVG-Varianten zeigen, nicht 1:1 umsetzen). Layout nicht eigenmächtig
  umbauen. Entscheidungen: `docs/superpowers/specs/2026-10-25-headliner-pro-overhaul-design.md`, `docs/design/`.
- TDD: erst Test rot, dann Fix. Vor „fertig": `scripts\build.ps1` (alle Tests) + pluginval Strenge 10.
- **Keine Rückwärtskompatibilität** (Jakob 2026-10-28): Entwicklungsphase, es gibt keine alten Projekte. Defaults, Wertebereiche,
  Routing und State direkt ändern, keine Migrationen und keine „alte Projekte klingen gleich“-Tests mehr. (Ersetzt die frühere
  Regel „Parameter-IDs nie ändern, alte Projekte müssen gleich klingen“.)
- Pro Schritt committen; **vor jedem Push fragen**, außer Jakob hat es ausdrücklich gesagt.
- Dateien nie mit PowerShell `Get-Content`/`Set-Content` bearbeiten (zerstört Umlaute) – Edit-Tool oder Python mit UTF-8.
  PowerShell 5.1 verschluckt leere `""`-Argumente an Programme.
- `juce::String (x, 0)` heißt in JUCE „alle Stellen", nicht „0 Nachkommastellen" → `roundToInt` nehmen.
- Website-Änderungen vor dem Abgeben selbst ansehen (Headless-Chrome-Screenshot von `dist/index.html`, Handy über einen
  390-px-iframe, siehe `.render/` im Website-Ordner) – die erste Version hatte gestreckte Bilder, weil das nicht passiert war.

## Bauen, testen, installieren
- Alles bauen + alle Tests: `scripts\build.ps1` (Log enthält `==== N checks passed, 0 failed ====`).
- Nur Tests: VS-CMake (`vswhere` → `...\CMake\bin\cmake.exe`) `--build build --config Release --target VelvoxTests`.
- Eine Suite: `build\VelvoxTests_artefacts\Release\VelvoxTests.exe --test "<Name>"`.
- pluginval (Ausgabe nur mit `--output-dir`): `pluginval.exe --strictness-level 10 --output-dir <dir> --validate build\Velvox_artefacts\Release\VST3\Headliner.vst3`.
  Einzelne Tests: `--disabled-tests <datei>` (Liste der übrigen Testnamen), `--random-seed`.
- Installieren: `scripts\install-vst3.ps1` (UAC). **Scheitert still, solange FL Studio läuft** (DLL gesperrt) → FL vorher schließen.
  Im `!`-Befehl absoluten Pfad nehmen (`C:\Users\jakob\...`), `$env:` wird dort nicht aufgelöst.
  Prüfen: Datum von `C:\Program Files\Common Files\VST3\Headliner.vst3\Contents\x86_64-win\Headliner.vst3`.
- Screenshots ohne DAW: `VelvoxTests --snapshot out.png "<Preset>" <breite> <beginner|pro> <modulId|-1> [overlay]` (Overlays in `Tests/TestMain.cpp`).
- Auto Mix auf Datei: `VelvoxTests --automix <wav> <ausgabeordner>` (Render + Bericht mit allen Entscheidungen).

## Was am 26.10. gemacht wurde
1. **pluginval-Blocker behoben** (`854743c`): Schalter behielten nach dem Laden einen rohen Host-Wert (0,37). Ursache war JUCEs
   `AudioParameterBool` + `replaceState`, nicht der MacroMapper. `setStateInformation` rundet jetzt alle Parameter. Test:
   `Tests/StateRestoreTests.cpp`. Das Problem bestand schon in 0.6.0 (isoliert 51 Fehler).
2. **Jakobs Entscheidungen** (in der 0.6.1-Spec festgehalten, alle = bisheriges Verhalten): Makro-Stellungen beim Einrechnen
   behalten · EQ-Band-Solo hat Vorrang vor Modul-Solo · PRESENCE darf das Auto-Mix-Air-Band verschieben.
3. **EQ-Sweep wie Pro-Q** (`f2362d0`): Kopfhörer am Punkt halten und ziehen = Frequenz (links/rechts) und Q (hoch/runter),
   Mausrad = Q, ein Undo-Schritt. Frischer Druck → Solo endet beim Loslassen; eingerastetes Solo bleibt. `Source/UI/EqPanel.cpp`.
4. **Werks-Presets gleich laut** (`4d2f786`): bei −18 LUFS Eingang waren sie 3–8 LU leiser; Output Gain kalibriert, jetzt ±1 LU.
   Test „Factory presets keep the loudness…" in `Tests/PresetTests.cpp`; echte Stimmen über
   `VELVOX_LOUDNESS_VOCALS=<wav;wav>` (Skript `%USERPROFILE%\dev\velvox-wt\pv\loud.ps1`).
   **Achtung:** Dabei wurde u. a. `m10_dona_straight` aus dem VocalSet-**Held-out** (f9, m10, m11) verwendet. Für den Auto-Mix
   nicht relevant (betrifft nur Preset-Pegel), aber für eine saubere Held-out-Prüfung m10 künftig meiden oder neu bewerten.
5. Deutsche Beschriftungen in Output/Vocal Check, Level Match „Aus / Wie Bypass / Auto-Gain" (war abgeschnitten),
   Ratio ≥ 10:1 und THD ≥ 10 % zeigten alle Nachkommastellen.

## Website und Verkauf
- Statische Seite in `%USERPROFILE%\dev\lind-audio-web`: `index.html` ist der Seiteninhalt (für die Claude-Vorschau ohne
  `<html>`-Gerüst), `python build.py` baut `dist/` mit Gerüst + Favicon + lemon.js zum Hochladen (z. B. Vercel).
  Vorschau aktualisieren: `index.html` nach `<scratchpad>\site\` kopieren und das Artifact dort neu veröffentlichen.
- Inhalt: echte Plugin-Screenshots (`assets/img`), Vorher/Nachher-Player mit drei VocalSet-Aufnahmen durch den Auto Mix,
  beide auf −16 LUFS angeglichen (`assets/audio`; Quellen/Renders in `velvox-bench\web-demos`). VocalSet-Nennung im Footer
  ist Pflicht (CC BY 4.0). **Keine Sonovox- oder Splice-Vocals öffentlich verwenden.**
- Texte: einfach, du-Form, in Jakobs Stimme, keine Werbefloskeln (Jakob merkt KI-Formulierungen sofort). Keine
  Konkurrenzprodukte auf der Seite nennen. Nur Fakten, die stimmen (nur Windows, kein AU/Mac, kein A/B mehr, 15 Module,
  19 Werks-Presets + Init).
- **Preis:** Beta 39 € (danach 89 €), Updates bis 1.x inklusive. Begründung: Nectar 4 Standard 199 $, Clarity Vx 199 $ (oft im Sale).
- **Zahlung: Lemon Squeezy** (Merchant of Record: EU-USt, Rechnung, Datei-Auslieferung, Lizenzschlüssel; 5 % + 0,50 €).
  Im Code eine Stelle: `const CHECKOUT_URL = ''` im Skript von `index.html`. Leer → Buttons zeigen „Shop noch nicht freigeschaltet".
  Risiko: Stripe baut ein Nachfolgeprodukt, Umstieg = Link tauschen.

## Offene Punkte (Reihenfolge = Vorschlag)
1. **Jakob:** Lemon-Squeezy-Account + Produkt anlegen, Checkout-Link schicken; Impressum, Datenschutz, AGB, Widerruf, E-Mail liefern
   (Platzhalter `[E-MAIL]` und `#top`-Links im Footer). Dann Website auf Vercel stellen (erst nach den Rechtstexten).
2. **Lizenzprüfung im Plugin** (Lemon-Squeezy-License-API) – noch keinerlei Kopierschutz. Nur nach Rücksprache.
3. **Installer** (z. B. Inno Setup) für den Download statt nackter `.vst3`.
4. **Sprache:** Hilfe/Tutorial komplett Englisch, Bedienelemente teils Deutsch → Jakob entscheiden lassen (ganz DE, ganz EN oder Umschalter).
5. **Frische Instanz / Init** macht ein −18-LUFS-Vocal ~5 LU leiser (Standard-Kompressor ohne Makeup). Fix nur nach Rückfrage
   (Default-Änderung betrifft Presets ohne `compMakeup` und Auto-Mix-Goldens).
6. FL-Studio-Test (Pops, Latenz alter Projekte mit Pitch/Smooth/Ride, Undo im Host), Hörtest der 0.6-Module
   (`velvox-bench\2026-10-25\modules-after\<modul>\listen\`), zweiter Auto-Mix-Hörtest (`velvox-bench\2026-10-24\hoertest2_0.3_vs_neu`).
7. Website-Repo hat keinen GitHub-Remote (nur Drive-Bundle) – wenn Jakob ein privates GitHub-Repo anlegt, dort pushen.
8. Mac/AU-Version (auf der Website als „geplant" angekündigt).

## Weiterführende Doku im Repo
`CHANGELOG.md` (alle Änderungen inkl. „Alte Projekte"), `docs/ARCHITECTURE.md`, `docs/AUTOMIX_OBJECTIVE.md`, `docs/BENCHMARK.md`,
`docs/QUALITY_BASELINE.md` (Messungen gegen Referenz-Plugins), `docs/STYLEGUIDE.md`, `docs/design/UI_REDESIGN_DECISIONS.md`,
Pläne und Entscheidungen der 0.6-Spuren unter `docs/superpowers/plans/` (`*.ledger.md` = Rulings).
