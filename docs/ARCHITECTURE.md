# Velvox – Architektur (ab v0.2, Auto-Mix ab 0.3)

Entscheidungen vom 14.10.2026 (mit dem Produktinhaber abgestimmt).

## Schichten

```
UI (Beginner / Pro)  ──>  Parameter (APVTS, stabile IDs)  +  Routing-State (kein Parameter)
                                   │
                   MacroMapper (0.6.1): Simpel-Regler -> schreibt Modul-Parameter (Message-Thread)
                                   │
DSP: VocalChain ── Routing (Slots) ── Module (EQ, Comp, De-Ess, Sat, Doubler, Pitch, Delay, Reverb)
                └─ Output-Stufe: Stereo-Width/Mono-Check -> Output-Gain -> Level-Matcher -> Clip Guard -> Bypass
Presets: ParameterSnapshot (Werte + Routing), Factory + User, versioniert, Migration
Mixing (später): SuggestionProvider -> ParameterSnapshot (nicht implementiert)
```

## Makros (Beginner-Modus)

- Makros (Presence, Warmth, Aggression, Width, Space, Density) sind eigene, automatisierbare Parameter.
- **Ab 0.6.1 halten sie nur die Stellung der Simpel-Regler** und erreichen den Audio-Pfad nicht. `MacroMapper`
  (`Parameters/MacroMapper.*`, Message-Thread) schreibt jede Bewegung direkt in die Modul-Parameter: relativ
  (`dsp::applyMacroMove (base, macro, from, to)`, aus 0 identisch mit der alten Abbildung), verankert am Modulzustand beim
  Start der Bewegung (zurück auf den Startwert = exakt der alte Zustand). UI-Gesten schreiben Host-Gesten (ein Undo-Schritt
  pro Geste, Mausrad-Rasten zusammengefasst), Host-Automation schreibt ohne Gesten (kein Undo). Laden, Undo/Redo,
  Transaktionen und Snapshots setzen nur Stellungen. SPACE macht ein frisches, unbenutztes Delay/Reverb-Insert zum Send;
  ein Mix über 50 % bei additiven Inserts behält das Original (`macrobake::keepDry`: alte Mix-Kurve, beim Doubler + Dry).
- Bis 0.6.0 waren die Makros eine **Offset-Schicht** (`dsp::applyMacros`, unverändert erhalten): States mit
  `stateVersion` < 4 und Snapshots mit Format < 3 werden beim Laden einmal eingerechnet (`macrobake::bake`), Stellungen bleiben.
- Der Moduswechsel Simpel/Advanced ändert **nur die Ansicht**, nie den Klang.

## EQ-Bänder (0.6)

- Bis zu **16 Bänder pro EQ-Instanz** (EQ 1-4): Parameter `eqBand{1..16}{On,Type,Freq,Gain,Q,Slope,Role}` (Instanz k >= 1: Suffix `_k+1`, z. B. `eqBand3Gain_2`) und `eqLegacyResponse`. Typen: Bell, Low Shelf, High Shelf, Low Cut, High Cut, Notch, Band Pass, Tilt; Cut-Flanken 6/12/18/24/36/48 dB/Okt. (Butterworth). Bänder sind standardmäßig aus.
- DSP (`DSP/EqDesign.*`, `DSP/EqualizerModule.*`): minimalphasige Biquad-Kaskaden, **analog-gematcht** nach Vicanek (Pole impulsinvariant, Zähler über Betrag bei DC, Matchpunkt und Steigung) – kein Bilinear-Cramping nahe Nyquist (Bell 16 kHz @ 48 kHz <= 0,5 dB bis 20 kHz). Freq/Q multiplikativ, Gain linear geglättet (50 ms, Neuberechnung alle 16 Samples); Ein/Aus, Typ-, Flanken- und Designwechsel über 30-ms-Überblendung.
- `Role` (versteckt, nicht automatisierbar): none (Nutzer), presence, warmth, air, lowcut, mud, auto, highcut, seit 0.6.1 angehängt autoTone, autoNotch1-3 (Auto-Mix: breite 2-5-kHz-Bell, schmale Resonanz-Kerben; `AutoMix/AutoEq.*`: Grenzen, Tonal-Modell, Planung, `correct` für die Prüf-Runden; `autoeq::ownedRole` = alle Rollen, die der Auto-Mix neu schreibt). Makros (Presence -> air, Warmth -> warmth + air) und Auto-Mix (lowcut, warmth, auto, air, highcut) schreiben nur Bänder ihrer Rolle und legen fehlende in einem freien Slot an; Nutzerbänder werden nie verändert. Freier Slot = Band mit allen Werten auf Default (das "Löschen" im Panel setzt ein Band auf Default zurück).
- Migration: Ein State/Preset ohne `eqBand*`-Werte wird beim Laden umgesetzt: HPF/Low/Mid/High/LPF -> Bänder 1-5 mit Rollen lowcut/warmth/auto/air/highcut und `eqLegacyResponse = 1` (alte RBJ-Bilinear-Designs, klingt identisch; Null-Test <= -120 dB). Die 17 alten Festband-IDs bleiben registriert, versteckt und nicht automatisierbar. Eine `LegacyBridge` leitet Änderungen dieser IDs im Message-Thread (altes EQ-Panel, Assistent) in die Rollenbänder.
- APIs (`Parameters/EqBands.h`): `findOrCreateEqBand (apvts, role, type, freqHz, gainDb, q, instance = 0)` (Vocal Check, Message-Thread, gibt Band 0..15 oder -1), `eqbands::clearLegacyResponse (apvts, instance)`, `eqbands::accessFor/readBand/writeBand/findBand/findFreeBand`. Kurven fürs Panel: `EqualizerModule::responseDb / bandResponseDb / responseCurveDb` auf einer Kopie von `processor.readEffectiveSettings().instance (k).eq` (atomare Reads, thread-safe; enthält die von Makros erzeugten Bänder).
- Panel (`UI/EqPanel.*`): `eqedit` (Band-Operationen, je ein Undo-Schritt; `materialise` schreibt ein nur von einem Makro erzeugtes Band mit 0 dB Basis als Parameter, damit der Makro-Anteil nicht doppelt wirkt), `EqGraph` (Spektrum, Bandkurven, Summenkurve, Punkte, alle Gesten, Hover-Karte), `EqPanel` (Hauptzeile = gewähltes Band: Ein, Typ, Freq, Gain, Q bzw. Slope; Mehr: Mix, „Alte EQ-Kurve – umstellen“). Die 17 alten Festband-IDs werden nicht angezeigt.

## Routing

- Serielle Module (EQ, Comp, De-Ess, Sat, Doubler, Pitch) sind per Drag-and-drop sortierbar.
- Delay und Reverb laufen entweder als **Send** (parallel, nur Wet-Rückführung, `out = dry + wet * mix`, 100 % = volle Send-Menge) oder als **Insert** an einer beliebigen Stelle der seriellen Kette. Ein neues Plugin und Module aus dem Browser setzen Delay/Reverb als Insert ein (0.6); Automation/Makros (SPACE) und der Auto-Mix nutzen Sends.
- **Mix-Kurve (0.6, `DSP/MixLaw.h`)** pro Modul-Instanz, versteckte State-Eigenschaft `mixLaw` (`"v1"` + Liste der Instanzen auf alter Kurve; fehlt sie = Projekt ≤ 0.5 = alle alt). *Alt:* Delay/Reverb-Insert, Doubler und Smash addieren den Wet-Anteil zum Dry bei Einheitsverstärkung. *Neu (echtes Dry/Wet):* Dry bleibt bis 50 % auf 1 und fällt darüber linear auf 0 bei 100 % (Wet = Mix wie bisher) – bis 50 % bitgleich mit der alten Kurve. Makros erhöhen nur den Wet-Anteil; die Dry-Dämpfung folgt dem eigenen Mix des Moduls. Frisch ist eine Instanz, die ausgeschaltet in die Kette kommt (Browser). Presets ohne Kurve setzen nur dort „alt“, wo ein additiver Mix über 50 % liegt. API: `usesLegacyMixLaw`, `switchToTrueMixLaw`, `setLegacyMixLaw`, `getMixLaws` am Prozessor.
- Delay → Reverb: einstellbarer Anteil des Delay-Signals, der in den Reverb geht.
- Parallele Kompression und Sättigung laufen über den Modul-Mix; der Dry-Anteil ist latenzrichtig ausgerichtet.
- Die Reihenfolge ist **kein** automatisierbarer Parameter. Sie liegt im Plugin-State und in Presets und wird lock-free an den Audio-Thread übergeben. Ein Umbau erfolgt mit einer kurzen Ausblendung (etwa 2 × 8 ms), damit es nicht knackt.

## Latenz

- Jedes Modul meldet seine Latenz. Die Gesamtlatenz wird dem Host gemeldet (PDC), so wie es professionelle Plugins machen.
- Der Bypass-Pfad ist um die Gesamtlatenz verzögert.
- **Pitch (0.6):** konstante Latenz ceil(fs/70) + 16 Samples (14,6 ms bei 48 kHz), nur solange das Modul an ist; sonst 0. Unabhängig von allen Pitch-Einstellungen.
  - Beim Ein- und Ausschalten wird die neue Latenz auf dem Message-Thread gemeldet (`setLatencySamples`). Der Wechsel in der Kette erfolgt unter derselben Ausblendung wie Routing-Änderungen, und der Bypass-Pfad wird mitgezogen.
  - Engine (0.6, ersetzt Signalsmith Stretch mit 120 ms und 2-7 ct Fehler je nach FFT-Bin-Lage, `Tests/PitchDiagnostics.cpp`): `PitchTracker` (YIN auf ~8 kHz, Verfeinerung in voller Rate), `SpliceShifter` (zwei Leseköpfe mit Tempo = Verhältnis, Sprünge um ganze Perioden per Kreuzkorrelation, korrelationsnormierte Blende, Anti-Alias-Tiefpass bei Hochtransponieren), `FormantCorrector` (cepstrale Hüllkurve des Eingangs, Korrektur E(f/Ziel)/E(f/rho) als minimalphasiges FIR, alle 10 ms überblendet).

## Sidechain (Ducking)

- Wahlweise interne Quelle (trockenes Vocal am Send-Abgriff) oder ein externer Sidechain-Bus (VST3-Aux-Eingang, standardmäßig deaktiviert).
- Ist „extern“ gewählt, aber kein Signal angeschlossen, wird die interne Quelle verwendet; die UI zeigt das an.

## Level-Matcher

- Lautheit nach ITU-R BS.1770 (K-Filter), gemessen an Plugin-Eingang und -Ausgang. Messfenster wählbar: 0,4 s, 3 s oder 10 s.
- Modus **Bypass Match**: Nur das Bypass-Signal wird auf die Lautheit des bearbeiteten Signals gebracht (fairer A/B-Vergleich).
- Modus **Auto Gain**: Das bearbeitete Signal wird dauerhaft auf die Eingangslautheit geregelt.
- Grenzen ±12 dB, langsame Glättung, Einfrieren bei Stille (Gate), Clip Guard danach.

## State und Kompatibilität

- `stateVersion` als Property im State. v0.1-Projekte haben keine.
- Beim Laden werden alle Parameter, die im gespeicherten State fehlen, explizit auf ihren Standardwert gesetzt. JUCE behält sonst den aktuellen Wert.
- Alle neuen Parameter sind standardmäßig neutral bzw. aus. Ein v0.1-Projekt klingt dadurch identisch; das ist per Test abgesichert.
- Parameter-IDs bleiben stabil, Plugin-Codes `Lnda`/`Vlvx` ändern sich nie.
- Preset-Dateien: Version 2 mit optionalem Routing. Version-1-Dateien laden mit Standard-Routing.

## Neue Module (v0.2)

| Modul | Typ | Latenz | Kernprinzip |
|---|---|---|---|
| Doubler | seriell, addiert Stimmen | 0 | zwei modulierte Delay-Stimmen, Mid-Anteil per „Mono safe“ reduzierbar |
| Pitch | seriell | 14,6 ms bei 48 kHz (nur wenn an) | Splice-Resampling + Formantkorrektur (0.6), latenzrichtiger Dry-Mix |
| Chop | seriell | 0 | 16-Step-Pattern, synchron zur Host-PPQ-Position, Repeat = verzögerter Slice |
| Ducker | auf Delay-/Reverb-Return | 0 | Gain-Kurve aus Vocal oder Sidechain |
| StereoStage | Ausgang | 0 | Mid/Side; nur das Seitensignal wird skaliert bzw. gefiltert |
| LevelMatcher | Ausgang | 0 | Lautheitsdifferenz Eingang ↔ bearbeitet, mit Slew-Begrenzung, Gate und Range |

## UI-Struktur

- `ChainView`: Kacheln in Routing-Reihenfolge und parallele Send-Spur. Drag & Drop ruft nur `Routing::placeBefore`/`placeAtEnd` auf, die Logik liegt also testbar im Modell.
- `BeginnerView`: Makros und Hinweise; die Hinweise berechnet der Editor aus den Metern.
- `DetailPanel`: eine Klasse pro Modul plus Output, erzeugt über `createDetailPanel`.
- Der Ansichtszustand (`uiMode`, `uiSelection`, `uiWidth`) liegt als State-Property im Projekt und beeinflusst nie das Audio.

## Automatischer Mix (0.3)

```
Audio-Thread:  processBlock ─ CaptureBuffer.push (nur Kopie, vorab reserviert) ─ VocalChain
                                   │ complete
Message-Thread: AutoMixController (Timer 15 Hz, nur solange aktiv)
                                   │ copyOut
Worker-Thread:  VocalProfiler ─> AutoMixPlanner ─> AutoMixEngine (Render + Messung, max. 4 Runden, Pegel)
                                   │ AutoMixResult
Message-Thread: applyResult: Snapshot "Vorher", Parameter mit Gesten setzen, Routing, Bericht
```

- **CaptureBuffer:** feste Kapazität (24 s, oberhalb 96 kHz entsprechend kürzer), Start bei Signal (RMS > −50 dBFS), digitale Stille wird nicht gespeichert, Stopp bei 14 s Gesang. Neu-Armieren/Abbrechen nutzt einen Handshake (Zustand + "writing"-Flag, seq_cst), damit der Puffer nie während eines Kopiervorgangs neu angelegt wird. Der Audio-Thread wartet nie.
- **VocalProfiler:** reine Funktion der Audiodaten. Messungen haben Gültigkeitsflags; Heuristiken erscheinen nur als `Issue` mit Konfidenz (gemessen / wahrscheinlich / möglich).
- **AutoMixPlanner:** reine Funktion (Profil + aktuelle Werte -> `MixPlan`). Jede Entscheidung trägt eine Begründung und ob sie auf einer Messung beruht. Nur vorhandene Parameter, Werte auf ihre Bereiche geklemmt.
- **AutoMixEngine:** rendert den Abschnitt durch eine private `VocalChain`; die Parameterabbildung kommt aus `SettingsReader`, also dieselbe wie im Plugin (per Test bitgenau abgesichert). S-Laute werden nach der Kette auf exakt denselben Frames gemessen wie vorher.
- **Ausgangspegel:** an die Lautheit des Originals angeglichen (−24 bis −14 LUFS), Spitze ≤ −1 dBFS. Lauter klingt nicht automatisch besser; der Vergleich bleibt fair.
- **Zustand:** Kind-Knoten `AutoMix` im Plugin-State (Popup-Status `pending/done/dismissed`, Bericht, Snapshots "Vorher" und "Mix"). State-Version 3; Projekte < 3 ohne Knoten gelten als bereits gemischt (kein Popup).
- **Vergleich:** "Vorher / Velvox Mix" tauscht vollständige Parameter-Snapshots (Änderungen am Mix bleiben erhalten); "Original" ist der latenzkompensierte, auf Wunsch pegelangeglichene Bypass.
- **Sidechain:** wird mit erfasst und nur analysiert. Sie erreicht den Ausgang nicht (Ducking-Quelle ausgenommen, wie bisher).

## UI (0.3)

- `AutoMixView` ersetzt die Beginner-Ansicht; `AnalysisOverlay` ist das Popup (Intro, Warten, Erfassen, Analyse, Fertig, Fehler).
- Der `uiMode`-Wert bleibt aus Kompatibilitätsgründen "beginner"/"pro" und steht für "Auto Mix"/"Module".
- Der Hintergrund der Leinwand wird einmal pro Bildschirmskalierung gerendert und danach nur kopiert (keine Dauer-Animation).

## 0.6 – Pro-Overhaul (Einheiten)

```
UI:   PluginEditor ─ TopBar (SIMPEL|ADVANCED, AUTO MIX bzw. MODULE | VOCAL CHECK) ─ SimpleView (Spektrum, Makros, Vorher|Nachher)
                   ─ ChainView + DetailPanel-Rahmen (DetailPanels.h) ─ Modul-Panels ─ ModuleBrowser ─ PresetBrowser ─ VocalCheckView
Undo: UndoHistory (Presets/) ◄─ GestureListener des Prozessors, Transaktionen (Preset, Auto-Mix, Anwenden, Kette)
DSP:  VocalChain ─ MixLaw (pro Instanz) ─ EqDesign/EqualizerModule (16 Bänder) ─ überarbeitete Module
Mess: tools/refhost (HeadlinerRefHost + bench.py/modules.py/qmetrics.py/signals.py) ─ docs/QUALITY_BASELINE.md
```

- **Panels** (`Source/UI/`, alle auf dem gemeinsamen Rahmen `DetailPanel`: Kopf, Visual, Hauptzeile, „Mehr“, Meter, Mix-Regler):
  `EqPanel` (EqGraph + eqedit), `SmoothPanel` (Gewichtungskurve), `PitchPanel`, `SatPanel` (Charakter-Kacheln, 3 Bänder),
  `TimePanels` (Delay-Echokarte, Pulse, Stutter-Wellenform), `VoicesPanels` (Doubler-Bühne, Choir-Halbkreis), `DynPanels`
  (CompGraph, DeEssGraph, SmashStage, RideBars; Live-Daten über `DSP/DynamicsFeed.h`), `ReverbPanel` (Raum mit Tiefe-Punkt,
  `DSP/ReverbDepth`), `VocalCheckView`. Gemeinsame Gesten in `ValueText` (`gestures::`), Werte eintippen über `parseValueText`.
  Malen alloziert nicht; Live-Daten ≤ 30 Hz.
- **UndoHistory** (`Presets/UndoHistory.*`, nur Message-Thread): eine `juce::UndoManager`-Historie. Gesten = `ValueAction`
  (normalisierte Vorher/Nachher-Werte + Routing/Mix-Kurve, falls Code im Gestenverlauf sie änderte, z. B. SPACE Insert → Send);
  Transaktionen = `SnapshotAction` (ganzer Zustand inkl. Routing und Mix-Kurve, plus Roh-Werte außerhalb des Snapshots).
  Schnelle Wiederholungen auf demselben Parameter (Mausrad, < 400 ms, ohne Schritt dazwischen) verschmelzen zu einem Schritt.
  `takeCompareBase`: eine Änderung bei „Vorher“ beendet den Auto-Mix-Vergleich und wird als Schritt vom „Nachher“-Zustand aus
  aufgezeichnet (`AutoMixController::endCompareForEdit`). Laden eines Projekts leert die Historie.
- **MixLaw** (`DSP/MixLaw.*`): siehe Routing oben; Umschalten per `switchToTrueMixLaw` ist ein Undo-Schritt.
- **EqDesign / EqBands**: siehe „EQ-Bänder“ oben. `eqbands::ScopedBridgeSuspend (apvts)` gilt pro Plugin-Instanz.
- **Check/** (`CheckRules`, `CheckTargets`, `CheckFixPlanner`, `VocalCheckController`, `VoiceTemplate`): Vocal-Check-Regeln
  gegen Zielbereiche pro Stimmtyp/Genre, Vorschläge als reine Pläne, „Anwenden“ / „Alles anwenden“ als eine Transaktion.
- **Routing-Übergabe:** `routingShared` / `mixLawShared` sind `SeqLockValue`s mit genau einem Schreiber (Message-Thread,
  `isStateWriterThread()` per jassert). Eine reine Insert↔Send-Umschaltung eines stillen Delay/Reverb (Return ≤ −30 dB)
  übernimmt die Kette sofort, ohne die Routing-Ausblendung; jede andere Änderung läuft weiter über die Ausblendung.
- **Latenz (0.6):** Sättigung 5 Samples/Instanz (48 kHz), Smooth = FFT-Größe (1024 bei 48 kHz), Level Ride 80 ms Vorausschau,
  Pitch ceil(fs/70)+16 – jeweils konstant, solange die Instanz in der Kette steht (Pitch: solange an). Test „Combined latency“
  prüft gemeldete = gemessene Verzögerung für alle zusammen bei 44,1 und 192 kHz.
- **refhost** (`tools/refhost`, CMake-Ziel `HeadlinerRefHost`): rendert ein Modul (echter Prozessor, Ein-Modul-Kette) oder ein
  VST3-Referenz-Plugin offline; `bench.py` misst die Fälle aus `modules.py` und schreibt die Tabellen in QUALITY_BASELINE.
