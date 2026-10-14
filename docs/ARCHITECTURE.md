# Velvox – Architektur (ab v0.2)

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

## Routing

- Serielle Module (EQ, Comp, De-Ess, Sat, Doubler, Pitch) sind per Drag-and-drop sortierbar.
- Delay und Reverb laufen entweder als **Send** (parallel, nur Wet-Rückführung) oder als **Insert** an einer beliebigen Stelle der seriellen Kette. In beiden Fällen gilt `out = dry + wet * mix`, damit es beim Moduswechsel keinen Pegelsprung gibt.
- Delay → Reverb: einstellbarer Anteil des Delay-Signals, der in den Reverb geht.
- Parallele Kompression und Sättigung laufen über den Modul-Mix; der Dry-Anteil ist latenzrichtig ausgerichtet.
- Die Reihenfolge ist **kein** automatisierbarer Parameter. Sie liegt im Plugin-State und in Presets und wird lock-free an den Audio-Thread übergeben. Ein Umbau erfolgt mit einer kurzen Ausblendung (etwa 2 × 8 ms), damit es nicht knackt.

## Latenz

- Jedes Modul meldet seine Latenz. Die Gesamtlatenz wird dem Host gemeldet (PDC), so wie es professionelle Plugins machen.
- Der Bypass-Pfad ist um die Gesamtlatenz verzögert.
- **Pitch (umgesetzt):** etwa 120 ms Latenz, aber nur solange das Modul an ist; sonst bleibt es bei 3 Samples.
  - Beim Ein- und Ausschalten wird die neue Latenz auf dem Message-Thread gemeldet (`setLatencySamples`). Der Wechsel in der Kette erfolgt unter derselben Ausblendung wie Routing-Änderungen, und der Bypass-Pfad wird mitgezogen.
  - Kürzere Analysefenster wurden gemessen (`Tests/PitchDiagnostics.cpp`) und wegen bis zu 70 Cent Verstimmung verworfen.

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
| Pitch | seriell | ~120 ms (nur wenn an) | Signalsmith Stretch, latenzrichtiger Dry-Mix |
| Chop | seriell | 0 | 16-Step-Pattern, synchron zur Host-PPQ-Position, Repeat = verzögerter Slice |
| Ducker | auf Delay-/Reverb-Return | 0 | Gain-Kurve aus Vocal oder Sidechain |
| StereoStage | Ausgang | 0 | Mid/Side; nur das Seitensignal wird skaliert bzw. gefiltert |
| LevelMatcher | Ausgang | 0 | Lautheitsdifferenz Eingang ↔ bearbeitet, mit Slew-Begrenzung, Gate und Range |

## UI-Struktur

- `ChainView`: Kacheln in Routing-Reihenfolge und parallele Send-Spur. Drag & Drop ruft nur `Routing::placeBefore`/`placeAtEnd` auf, die Logik liegt also testbar im Modell.
- `BeginnerView`: Makros und Hinweise; die Hinweise berechnet der Editor aus den Metern.
- `DetailPanel`: eine Klasse pro Modul plus Output, erzeugt über `createDetailPanel`.
- Der Ansichtszustand (`uiMode`, `uiSelection`, `uiWidth`) liegt als State-Property im Projekt und beeinflusst nie das Audio.
