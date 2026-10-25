# Velvox – Architektur (ab v0.2, Auto-Mix ab 0.3)

Entscheidungen vom 14.10.2026 (mit dem Produktinhaber abgestimmt).

## Schichten

```
UI (Beginner / Pro)  ──>  Parameter (APVTS, stabile IDs)  +  Routing-State (kein Parameter)
                                   │
                   MacroMapper: Basiswerte + Makros  ->  effektive ChainSettings (pro Block)
                                   │
DSP: VocalChain ── Routing (Slots) ── Module (EQ, Comp, De-Ess, Sat, Doubler, Pitch, Delay, Reverb)
                └─ Output-Stufe: Stereo-Width/Mono-Check -> Output-Gain -> Level-Matcher -> Clip Guard -> Bypass
Presets: ParameterSnapshot (Werte + Routing), Factory + User, versioniert, Migration
Mixing (später): SuggestionProvider -> ParameterSnapshot (nicht implementiert)
```

## Makros (Beginner-Modus)

- Makros (Presence, Warmth, Aggression, Width, Space, Density) sind eigene, automatisierbare Parameter.
- Sie wirken als **Offset** auf die Basiswerte der Modulparameter. Die Basiswerte (Pro-Modus) werden nie überschrieben.
- Die Abbildung steckt in einer reinen Funktion `applyMacros (base, macros) -> effective`, die getestet und später auch von einer Mixing-Engine nutzbar ist.
- Ein Makro darf ein ausgeschaltetes Modul anteilig zuschalten (z. B. Space → Reverb-Send). Die Pro-Ansicht zeigt das an.
- Der Moduswechsel Beginner/Pro ändert **nur die Ansicht**, nie den Klang.

## EQ-Bänder (0.6)

- Bis zu **16 Bänder pro EQ-Instanz** (EQ 1-4): Parameter `eqBand{1..16}{On,Type,Freq,Gain,Q,Slope,Role}` (Instanz k >= 1: Suffix `_k+1`, z. B. `eqBand3Gain_2`) und `eqLegacyResponse`. Typen: Bell, Low Shelf, High Shelf, Low Cut, High Cut, Notch, Band Pass, Tilt; Cut-Flanken 6/12/18/24/36/48 dB/Okt. (Butterworth). Bänder sind standardmäßig aus.
- DSP (`DSP/EqDesign.*`, `DSP/EqualizerModule.*`): minimalphasige Biquad-Kaskaden, **analog-gematcht** nach Vicanek (Pole impulsinvariant, Zähler über Betrag bei DC, Matchpunkt und Steigung) – kein Bilinear-Cramping nahe Nyquist (Bell 16 kHz @ 48 kHz <= 0,5 dB bis 20 kHz). Freq/Q multiplikativ, Gain linear geglättet (50 ms, Neuberechnung alle 16 Samples); Ein/Aus, Typ-, Flanken- und Designwechsel über 30-ms-Überblendung.
- `Role` (versteckt, nicht automatisierbar): none (Nutzer), presence, warmth, air, lowcut, mud, auto, highcut. Makros (Presence -> air, Warmth -> warmth + air) und Auto-Mix (lowcut, warmth, auto, air, highcut) schreiben nur Bänder ihrer Rolle und legen fehlende in einem freien Slot an; Nutzerbänder werden nie verändert. Freier Slot = Band mit allen Werten auf Default (das "Löschen" im Panel setzt ein Band auf Default zurück).
- Migration: Ein State/Preset ohne `eqBand*`-Werte wird beim Laden umgesetzt: HPF/Low/Mid/High/LPF -> Bänder 1-5 mit Rollen lowcut/warmth/auto/air/highcut und `eqLegacyResponse = 1` (alte RBJ-Bilinear-Designs, klingt identisch; Null-Test <= -120 dB). Die 17 alten Festband-IDs bleiben registriert, versteckt und nicht automatisierbar. Eine `LegacyBridge` leitet Änderungen dieser IDs im Message-Thread (altes EQ-Panel, Assistent) in die Rollenbänder.
- APIs (`Parameters/EqBands.h`): `findOrCreateEqBand (apvts, role, type, freqHz, gainDb, q, instance = 0)` (Vocal Check, Message-Thread, gibt Band 0..15 oder -1), `eqbands::clearLegacyResponse (apvts, instance)`, `eqbands::accessFor/readBand/writeBand/findBand/findFreeBand`. Kurven fürs Panel: `EqualizerModule::responseDb / bandResponseDb / responseCurveDb` auf einer Kopie von `processor.readEffectiveSettings().instance (k).eq` (atomare Reads, thread-safe; enthält die von Makros erzeugten Bänder).

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
