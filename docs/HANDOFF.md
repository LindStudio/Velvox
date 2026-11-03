# HEADLINER – Übergabe für die nächste Session

## STAND 2026-11-03 NACHT (Haupt-PC) – HIER WEITERMACHEN: offene Punkte abgearbeitet
Branch **`feature/vocalypse-rebrand`**. Jakob: „mach alle Punkte als Info … erledige, was du kannst“ (er war weg).
**Die Liste für Jakob: `docs/OFFENE_PUNKTE.md`** (Preise, Empfehlungen, was er tun muss). Details: CHANGELOG oben.
- Jakobs Antworten: Lizenz **vorbereiten** (aus), Hörbeispiele **liefert er später**, GitHub **hochladen +
  umbenennen** (Umbenennen geht hier nicht: kein `gh` installiert → Jakob im Browser, dann `git remote set-url`),
  Installer **später**.
- Lizenz: `Source/Licensing/License.*`, `docs/LICENSING.md`, CMake-Option `VOCALYPSE_LICENSING` (OFF). Einschalten:
  IDs + buyUrl in `ProductInfo.h`, neu konfigurieren.
- Klang: SPACE-Regler 0,7 statt 0,3 pro Weg (`dsp::spaceReverbPerTravel`), Stil-Zuschlag im EQ-Limit (`limitsOf`),
  Raum-Charakter pro Vibe (Planer), breiterer Ride in `tuneDynamics`. Bench `velvox-bench\2026-11-03\sonovox\r5_*`
  (Genre/Vibe beim `--bench` als **Zahlen**: party 0, rap 1, pop 2, edm 3, ballad 4 / punchy 0, aggressive 1,
  floaty 2, warm 3, natural 4 – r4 war mit Wörtern gestartet = alles Party · Druckvoll).
- Offen: Rap·Aggressiv-Dynamikziel (1–3 dB) vermutlich zu streng → nach Jakobs Hörtest; Floaty-Luft bleibt klein.

## STAND 2026-11-03 SPÄT (Haupt-PC) – HIER WEITERMACHEN: Rebranding VOCALYPSE + Website
Branch **`feature/vocalypse-rebrand`** (von `feature/automix-0.8`), Commit `5ecb55e`. Details: CHANGELOG oben.
- **Name:** Headliner und Velvox gibt es nicht mehr → **VOCALYPSE** (Jakob). Code: Namespace `vocalypse`, Ziele
  `Vocalypse`/`VocalypseTests`/`VocalypseRefHost`, Build-Ordner `build/Vocalypse_artefacts`, Env `VOCALYPSE_*`.
  Plugin-Codes `Lnda`/`Vlvx` NIE ändern (VST3-Kennung `e632afc1`, alte Projekte). Alte Kennungen werden gelesen:
  `product::legacyStateType` (`VelvoxState`), `ParameterSnapshot::legacyTreeType` (`VelvoxPreset`),
  `legacySettingsName` (`Velvox.settings`), `legacyNames` (Preset-Ordner Headliner/Velvox).
- Nicht umbenannt (bewusst): Repo-Ordner `dev\velvox`, `velvox-data`, `velvox-bench`, Drive-Bundle `velvox.bundle`,
  GitHub `LindStudio/Velvox` → Jakob fragen.
- **Logo:** Jakob wählte Schriftzug **W3** (Metal Mania, Feuerverlauf, ohne Hörner/Schwanz). Dateien:
  Drive `VOCALYPSE_Logo\Schriftzug` (SVG + PNG), Icons-Vorschläge `VOCALYPSE_Logo\Icon_Vorschlaege` (N01–N12,
  Jakob: „Icon schauen wir später“; die erste Runde I1–I15 mochte er nicht). Generatoren lagen im Scratchpad
  (wm_build.py / icon_build.py) – bei Bedarf neu schreiben: fontTools-Outline von Metal Mania + resvg.
  Im Plugin: Kopfzeile zeigt W3 (`Resources/Images/VocalypseWordmark.png`), Wellen-Icon bleibt Platzhalter.
- **Website** (`%USERPROFILE%\dev\lind-audio-web`, Commit `5bbde28`, Drive-Bundle + Kopie `Website\` aktualisiert):
  Bühnen-Design, Umschalter Roh/VOCALYPSE mit Memphy, 5 Stile, Newsletter statt Kaufen (Jakobs TODO), Impressum
  (aus lind-studio.com, angepasst), Datenschutz. Vorschau: https://claude.ai/artifact/VRK5xLn9XWcaRfQLsx6zFt
  Offen: Newsletter-Dienst (`NEWSLETTER_ACTION` in site.js, Double-Opt-in), Platzhalter in datenschutz.html,
  Jakob klärt Gewerbeumfang (Software-Verkauf) und ob „Lind Audio“ im Impressum so passt; Hosting/Domain.
- 40.111 Checks grün, pluginval 10 SUCCESS, Beenden ohne Hänger. **NICHT installiert** (UAC abgebrochen),
  nicht gepusht. Bekannt: SIMPEL-Regler SPACE steht bei 42 % Hall am Anschlag (Anzeige reverbMix/0,3).

## STAND 2026-11-03 ABEND (Haupt-PC) – HIER WEITERMACHEN: FL-Hänger behoben, Raum + Stile kalibriert
Branch **`feature/automix-0.8`**, Commits `005f611` (FL-Hänger) und `b2eb8e5` (Auto Mix). Details: CHANGELOG oben.
- Jakobs Punkte: FL hing beim Schließen · Stile kaum verschieden · Hall/Delay unhörbar · klingt nicht professionell.
- **FL-Hänger:** `static juce::Image` (Memphy-Sticker) → Direct2D-Freigabe beim DLL-Entladen. Prüf-Werkzeug:
  `HeadlinerRefHost --editor --plugin <vst3> [--tail s]` (Hänger = Prozess endet nie; vorher 3/3, jetzt 4 s).
  Regel: **nie** `juce::Image`/Grafik-Ressourcen in `static`-Variablen.
- **SONOVOX-Daten** (Jakob, Drive `Testvocals\Sonovox-Free-Demo-Vocal-Dataset`, nur Evaluation, nicht weitergeben):
  entpackt nach `%USERPROFILE%\dev\velvox-data\sonovox` (F01–M50). 11 Lead-Paare trocken/fertig:
  `python tools/bench/sonovox_pairs.py <sonovox> <out>` → `velvox-bench\2026-11-03\sonovox\{dry,wet,dry_short,wet_short}`.
  Messen: `tools/bench/pair_metrics.py <dry> <proc> [--suffix _velvox]`, Stile: `tools/bench/style_compare.py <dry> <wet> <bench-dirs…>`.
- Profis (Median): ab 2 kHz +4…5 dB, Phrasen-Spanne −4 dB, Fahnen −25/−29/−41 dB, Seite −20 dB. Auto Mix vorher: EQ und
  Dynamik ok, Fahnen 10–12 dB zu leise, Stile 0,7–2,7 dB auseinander. Jetzt (r3): Druckvoll −26/−31/−41, Stile 1,5–8 dB.
- Voller Lauf **40.104 grün**, pluginval 10 **SUCCESS**. **NICHT installiert** (UAC abgebrochen; FL war zu),
  nicht gepusht. Hör-Kit für Jakob: `%USERPROFILE%\dev\velvox-bench\2026-11-03\HOERTEST_stile\` (4 Takes × Original,
  Profi, alt, 5 neue Stile, alle −18 LUFS).
- Offen / nächste Schritte: EQ-Stilanhebungen kommen erst zu ~1/3 an (Aggressiv Präsenz, Floaty Luft) – Grenzen in
  `AutoEq.cpp limitsOf` (Rauschen/Zischen) und Kandidaten-Objective misst mit Hall; Rap · Aggressiv erreicht sein
  Dynamik-Ziel (1–3,5 dB) nicht ganz (4,6); SIMPEL-Regler SPACE steht bei 42 % Hall am Anschlag (Anzeige /0,3);
  Floaty und Ballade · Warm liegen beim Raum gleich (unterscheiden sich in Klang/Sättigung); Zielkurve mit SONOVOX-Wet
  per `--target-curve` neu rechnen (Maß des Plugins verwenden, nicht pair_metrics).

## STAND 2026-11-03 NACHT (Haupt-PC) – HIER WEITERMACHEN: Auto Mix 0.8 fertig, Jakob muss hören
Branch **`feature/automix-0.8`** (von `feature/modules-0.7`). Spec `docs/superpowers/specs/2026-11-02-automix-08-tailored-design.md`,
Plan `docs/superpowers/plans/2026-11-02-automix-08-tailored.md`, Entscheidungen („Ruling“) im Ledger
`.superpowers/sdd/2026-11-02-automix-08-tailored/progress.md` (nur auf diesem PC; Zusammenfassung im CHANGELOG 0.8).
- Jakob (nach Hörtest): „klingt random, nicht auf die Stimme angepasst, nur lauter“; Ziel Partysongs (Tream/FiNCH):
  voller, Brillanz, jedes Wort klar, sitzt im Beat. Er sagte „mach selbst“, testet später.
- Gebaut: Genre/Vibe-Abfrage + Beat-Sidechain-Hinweis auf der Startkarte, keine vorzeitige Analyse, Zielkurve aus
  7 Profi-Party-Stems (`VelvoxTests --target-curve`), EQ-Kurvenfit + Regelkreis, Kompressor-Suche, alle Module
  stimmabhängig. Bench: echte Takes 2,77 → 2,46 dB Abstand zum Ziel (Median), alle im Mittel gleich (4,98 → 4,95),
  Median aller 5,05 → 5,30 (verrauschte VocalSet-Übungen: Höhen-Anhebung bewusst auf 2 dB begrenzt).
- Voller Lauf **40.097 Checks grün**, pluginval 10 **SUCCESS** (VST3 vom 2026-11-03 00:05), **installiert** (2026-11-03). **Nicht
  gepusht.** Drive-Bundle aktualisiert.
- Renders für Jakob: `%USERPROFILE%\dev\velvox-bench\2026-11-02\jakob_vergleich_08\` (Original, alter Auto Mix,
  Stand Mittag, neu Party · Druckvoll / Aggressiv / Floaty + Berichte).
- Werkzeuge: `VelvoxTests --bench <dry> <aus> [genre vibe]`, `--thirds <dir> <csv>`, `python tools/bench/target_distance.py`,
  `summarise_reports.py` (zeigt jetzt „distinct settings“ je Modul).
- Offen: Jakobs Hörtest in FL; Acapellas von Jakob → Zielkurve neu (`--target-curve`); die 25-s-Grenze der Analyse
  hielt nur mit FL im Leerlauf (16 s), mit laufendem FL 35–42 s; Self-Review statt frischem Reviewer (siehe Ledger).

## STAND 2026-11-02 (Haupt-PC)
Stand a551512 vom Shadow PC übernommen (Goldens neu gesetzt, Room Noise stereo-gekoppelt + digitale Stille,
Vocal-Check-Befund „wenig Luft“). Damit sind Punkt 1 und 2 unten erledigt:
- Voller Lauf **39.051 Checks grün**, pluginval Strictness 10 **SUCCESS** (Release-Build).
- Vorher/Nachher-Renders für Jakob: `%USERPROFILE%\dev\velvox-bench\2026-11-02\jakob_vergleich\`
  (1_original, 2_vorher_alter_automix = base-Lauf vom 29.10., 3_nachher_neuer_automix + Berichte).
  Unterschied: Präsenz −4 dB bei 2,7 kHz → +1,5 dB bei 3 kHz, Room Noise 67 % → 40 %, Rest gleich.
- Jakobs Reihenfolge-Fragen (Punkt 3) im Chat beantwortet.
- Offen: **Installation** (UAC-Abfrage wurde abgebrochen, FL ist zu → `scripts/install-vst3.ps1` erneut),
  Jakobs Hörtest in FL, Push nach GitHub nur nach Rückfrage.

## STAND 2026-10-29 NACHT (dieser PC) – HIER WEITERMACHEN: Auto Mix „trainieren“
Jakob 2026-10-28 abends: „Auto Mix nimmt immer Höhen raus, Profis boosten eher; nimmt nie Breath Control/Smooth;
für jede Stimmart passen, keine dummen Entscheidungen, wir trainieren jetzt das Plugin (ohne meine Hilfe)“.
Erledigt und committet vorher: Score-Abzeichen bei Memphy statt über dem Tab, Spektrum bleibt bei Pause stehen,
Memphy ruhiger (Stimmung durch den Gesang höchstens alle 4 s, kleinere Pops/Hüpfer).

**Letzter Commit = WIP, Tests NICHT grün** (voller Lauf: 23 Fehler, Liste unten). Bench-Daten und Werkzeuge:
- Trainingssatz `%USERPROFILE%\dev\velvox-bench\2026-10-29\train\`: `dry\` (23 trockene Stimmen: 5 Jakob-Aufnahmen
  aus FL „Recorded“, seine Vergleichs-Vocal, Splice MYLK dry, 16 VocalSet-Excerpts ohne Held-out f9/m10/m11), `pro\`
  (8 fertige Profi-Vocals: Song-Stems aus FL „Rendered“ + Taylor Swift Acapella). Läufe: `base_*` (vorher), `r1_*`, `r2_*`.
- `python tools/bench/vocal_profile.py <ordner>` (Bänder ggü. Kern), `python tools/bench/summarise_reports.py <bench-aus>`
  (Module pro Mix, EQ-Züge, markiert „DUMB“ = breite Präsenz/Höhen-Absenkung auf dunkler Stimme).
- Bench: `VelvoxTests --bench <dry> <aus>` (~1 min/Datei). Nie zwei VelvoxTests gleichzeitig (gemeinsame Settings-Datei).

**Befund:** Die Zielfenster waren ~5 dB zu dunkel. Profi-Vocals (Plugin-Einheiten, mittlere Hälfte): Präsenz 2–5 kHz
−11,5…−5,2, über 2 kHz −8,7…−3,3, Luft −17,3…−14,6 dB ggü. dem Kern. Alt: top −16…−7, air −32…−16, Präsenz ohne
Untergrenze → der Auto Mix senkte 2–5 kHz bei 9/23 dunklen Stimmen um bis zu 4 dB.

**Geändert (WIP-Commit):**
- `AutoEq.h` Fenster: top −11…−3, air −22…−13, presence −12…−5 (auch im Objective `AutoMixObjective.cpp`, neu: Präsenz
  unter −12 kostet; harshness-Grenze −5); Vocal-Check-Ziele (`CheckTargets.*`) gleich gezogen (Pop/Akustik etwas dunkler).
- `AutoEq.cpp rangeOf`: 2–5 kHz nur absenken, wenn heller als Profi-Niveau (oder hart); Präsenz-Anhebung auch bei
  Rauschen/S-Lauten (dann max. 2 dB).
- Planer: Smooth immer (genug Material), 20 % als Grundpflege, 30/45 % bei Schärfe/Resonanz; Room Noise nur < 35 dB SNR,
  15…50 %, nicht bei halligen oder stark komprimierten (fertigen) Takes; Breath Control ab 1 % Atem-Anteil (gemessen
  0…3,6 %). Variante „Höhen sanfter“ an `topWindow.hi` gekoppelt. Bericht zeigt „breaths x %“.
- Ergebnis Runde 2 (dry, Median nach Mix): Präsenz −12,3 → −10,4, Höhen −11,8 → −9,1 (jetzt im Profi-Bereich), keine
  „DUMB“-Entscheidung mehr; Smooth 22/23, Breath 9/23. Luft bleibt bei −26 (Profi −17): Anhebung ist auf 4 dB
  (2 dB bei Rauschen) begrenzt – bewusst, sonst wird Rauschen/Zischeln lauter.

**Offen (in dieser Reihenfolge):**
1. Die 23 Testfehler: fast alles Goldens des alten Auto Mix bzw. der alten Fenster → bewusst neu setzen, nicht wegdrücken
   (`.superpowers/sdd/open-failures.txt`): Vocal Check 0.6 „Targets: party = the auto-mix objective windows“ (5),
   Velvox EQ auto-mix „result unchanged … dark low / dark 44.1 / bright sibilant / clean dry / stereo with rumble“ (14),
   Velvox auto EQ „broad moves follow the target curve“ (3) und „a resonance gets a notch … without SMOOTH“ (1 – Smooth
   läuft jetzt immer; Test anpassen: Notch ja, SMOOTH nur sanft 20 %).
2. Voller Lauf grün, pluginval 10, installieren (FL schließen), Jakob die Vorher/Nachher-Renders seiner Vocal geben
   (`VelvoxTests --automix` auf `train\dry\jakob_vergleich.wav`).
3. Jakobs Fragen beantworten (noch nicht geschehen): Reihenfolge – Level Ride VOR dem Kompressor ist Profi-Praxis (Rider
   gleicht Phrasen aus, der Kompressor arbeitet dann gleichmäßig); De-Esser nach Kompressor, Höhen-Anhebung idealerweise
   nach der Dynamik (Variante EQ 2 existiert). Smooth/Breath kamen selten, weil die Schwellen zu streng waren (s. o.).
4. Bekannte Grenze: Auf fertigen Profi-Vocals setzt der Auto Mix trotzdem Hall/Doubler/Delay (er erkennt „schon nass“
   nicht sicher) – Eingabe sollte die trockene Vocal sein.
- Alles NICHT gepusht. Installiert ist der Stand 963cde1 (vor Badge/Spektrum/Memphy/Auto-Mix).

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
