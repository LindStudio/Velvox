# HEADLINER VOCAL – Design-Übergabe für JUCE

Dieses Paket bringt das Design aus dem Claude-Canvas in dein JUCE-Plugin.
Die Canvas-Dateien selbst (`.dc.html`) kann JUCE **nicht direkt** laden – das ist ein Design-Format.
Darum steckt hier alles drin, was du für eine native Umsetzung brauchst:

```
Source/UI/
  HeadlinerColours.h          Alle Farben (Flächen, Text, 8 Modulfarben, Makros)
  HeadlinerLookAndFeel.h/.cpp Knob, Switch, Segment-Buttons – fertig gezeichnet
  HeadlinerKnob.h             Regler + Wert + Name, direkt an APVTS-Parameter gebunden
  NeonIcon.h                  Leuchtende Modul-Icons (färbt die SVGs automatisch ein)
Resources/icons/              8 Modul-Icons als SVG (weiß, werden umgefärbt)
Resources/fonts/              hier Montserrat + Roboto Mono ablegen (siehe Schritt 2)
```

Der Code ist gegen die aktuellen JUCE-Header geprüft (kompiliert ohne Fehler) und so geschrieben,
dass er auch mit JUCE 7 und 8 funktioniert.

---

## Schnellstart (so gibst du es Claude am PC)

1. Kopiere den Ordner `Source/UI` und `Resources` in dein Plugin-Projekt.
2. Öffne Claude Code in deinem Projektordner und gib ihm diesen Prompt:

> Ich habe in `Source/UI` ein fertiges Design-System (LookAndFeel, Farben, Knob-Komponente, Icons) und in
> `README_JUCE_HANDOFF.md` die komplette Design-Spezifikation meines Plugins „HEADLINER VOCAL“.
> Bitte:
> 1. Füge die Dateien aus `Source/UI` zum Build hinzu (CMake `target_sources` bzw. Projucer) und lege
>    `Resources/icons/*.svg` und `Resources/fonts/*.ttf` als BinaryData an.
> 2. Setze im PluginEditor `HeadlinerLookAndFeel` als LookAndFeel (im Destruktor wieder `nullptr` setzen)
>    und lade die Fonts über `setTypefaces`.
> 3. Baue die Oberfläche nach dem Abschnitt „Layout“ dieser README auf. Verwende für jeden Regler
>    `HeadlinerKnob` und verbinde ihn mit den **vorhandenen** Parametern meines Prozessors.
>    Wenn ein Parameter aus der Spezifikation im Prozessor fehlt, liste ihn mir auf, statt ihn zu erfinden.
> 4. Ändere nichts an der DSP, nur an der Oberfläche.

---

## Schritt 1 – LookAndFeel einbinden

```cpp
// PluginEditor.h
#include "UI/HeadlinerLookAndFeel.h"
#include "UI/HeadlinerKnob.h"
#include "UI/NeonIcon.h"

class HeadlinerAudioProcessorEditor : public juce::AudioProcessorEditor
{
    ...
    HeadlinerLookAndFeel lnf;                       // muss VOR den Komponenten stehen
    std::unique_ptr<HeadlinerKnob> reverbDecay;
    NeonIcon reverbIcon;
};

// PluginEditor.cpp
HeadlinerAudioProcessorEditor::HeadlinerAudioProcessorEditor (HeadlinerAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lnf);

    lnf.setTypefaces (
        juce::Typeface::createSystemTypefaceFor (BinaryData::MontserratMedium_ttf,   BinaryData::MontserratMedium_ttfSize),
        juce::Typeface::createSystemTypefaceFor (BinaryData::MontserratSemiBold_ttf, BinaryData::MontserratSemiBold_ttfSize),
        juce::Typeface::createSystemTypefaceFor (BinaryData::RobotoMonoRegular_ttf,  BinaryData::RobotoMonoRegular_ttfSize));

    reverbDecay = std::make_unique<HeadlinerKnob> (p.apvts, "reverb_decay", "DECAY",
                                                   hv::colours::reverb, " s", 1);
    addAndMakeVisible (*reverbDecay);

    reverbIcon.setSvg (BinaryData::reverb_svg, BinaryData::reverb_svgSize, hv::colours::reverb);
    addAndMakeVisible (reverbIcon);

    setSize (1384, 1040);
}

HeadlinerAudioProcessorEditor::~HeadlinerAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}
```

Einen Switch und eine Segment-Gruppe stylen:

```cpp
juce::ToggleButton pingPong { "Ping-Pong" };
pingPong.setColour (juce::ToggleButton::tickColourId, hv::colours::delay);

// Segment-Gruppe (z. B. Delay TIME): TextButtons in einer Radio-Gruppe
for (auto* b : { &t14, &t14d, &t18, &t18d, &t18t, &t116 })
{
    b->setClickingTogglesState (true);
    b->setRadioGroupId (1001);
    b->setColour (juce::TextButton::buttonOnColourId, hv::colours::delay);
}
// im paint() des Moduls unter die Buttons:
HeadlinerLookAndFeel::drawSegmentGroupBackground (g, timeGroupBounds.toFloat());
```

## Schritt 2 – Fonts

Lade von Google Fonts (freie OFL-Lizenz, darf ins Plugin) herunter und lege sie in `Resources/fonts`:
`Montserrat-Medium.ttf`, `Montserrat-SemiBold.ttf`, `RobotoMono-Regular.ttf`.

CMake:
```cmake
juce_add_binary_data(HeadlinerAssets SOURCES
    Resources/fonts/Montserrat-Medium.ttf
    Resources/fonts/Montserrat-SemiBold.ttf
    Resources/fonts/RobotoMono-Regular.ttf
    Resources/icons/levelride.svg  Resources/icons/smooth.svg
    Resources/icons/vocalsmash.svg Resources/icons/choir.svg
    Resources/icons/pulse.svg      Resources/icons/stutter.svg
    Resources/icons/delay.svg      Resources/icons/reverb.svg)
target_link_libraries(DeinPlugin PRIVATE HeadlinerAssets)
```
Ohne Fonts läuft alles auch – dann mit der System-Schrift.

---

## Design-Spezifikation

### Farben (alle in `HeadlinerColours.h`)

| Rolle | Hex |
|---|---|
| Fenster-Hintergrund | `#05060A` |
| Panels / Spalten | `#07080D`, Rahmen `#161925`, Radius 12 |
| Visualizer-Boxen | `#040509`, Rahmen `#13161E`, Radius 8 |
| Segment-Hintergrund | `#0D0F15`, Radius 7 |
| Text stark / normal / gedimmt | `#F1F5F9` / `#E8EBF2` / `#5B6478` |

| Modul | Farbe | Icon |
|---|---|---|
| LEVEL RIDE | `#00D1FF` | `levelride.svg` |
| SMOOTH | `#22B8FF` | `smooth.svg` |
| VOCAL SMASH | `#3B82F6` | `vocalsmash.svg` |
| CHOIR | `#6366FF` | `choir.svg` |
| PULSE | `#8B5CF6` | `pulse.svg` |
| STUTTER | `#A855F7` | `stutter.svg` |
| DELAY | `#D946EF` | `delay.svg` |
| REVERB | `#F472B6` | `reverb.svg` |

### Schrift
- Überschriften: Montserrat SemiBold, Modulname 15 px, Letter-Spacing 2.5 px, Großbuchstaben
- Sektions-Labels (RATE, MODE …): Montserrat SemiBold 10 px, `#5B6478`
- Werte: Roboto Mono 11.5 px, `#E8EBF2`
- Reglername: Montserrat SemiBold 9 px, `#6B7489`

### Knob (in `drawRotarySlider` umgesetzt)
Maße in Einheiten eines 100 × 100 Knopfs:
- Strichkranz: 57 Striche über 270°, Radius 40.5–47.5, Strich 1.3; bis zum Wert in Modulfarbe + Glow, Rest `#30343D`
- Schatten: Kreis r 37, 7 Einheiten nach unten, weich auslaufend
- Körper: r 35, Verlauf `#34373F` → `#121418`, feine helle Kante oben
- Kappe: r 26, Verlauf `#17191D` → `#2A2D34` (wirkt vertieft)
- Zeiger: leuchtender Punkt r 3.4 auf Radius 27
- Drehbereich 270° (7:30 bis 4:30 Uhr), Doppelklick = Default
- Größen: Modul-Regler 50–56 px, Spalten-Regler 50 px, Vocal Control 54 px, SMASH 150 px

### Layout Hauptfenster (1384 × 1040)
Empfehlung: `setResizable (true, true)` mit festem Seitenverhältnis und alles über einen Skalierungsfaktor zeichnen.

1. **Header** (Höhe 56): links Preset-Box (290 × 36) + ‹ ›, Mitte Logo „HEADLINER“ (Montserrat ExtraBold 20) + „VOCAL“ (Light, `#A78BFA`) + „BY LIND AUDIO“, rechts Tabs CHAIN / AUTO MIX / VOCAL CHECK (aktiv = Unterstrich in Farbe), A/B-Umschalter, A→B-Kopieren, kleiner Dry/Wet-Regler.
2. **Modul-Chain** (Höhe 330): eine Spalte pro Modul, gleich breit, Trennlinie `#13151E`.
   Spalte = Power-Button + Name (44) · Neon-Icon 64 px (96) · 2 Regler untereinander · Fußleiste mit ‹ Duplizieren Entfernen › (34).
   Ausgewählte Spalte: Verlauf von Modulfarbe 14 % (oben) nach 2 %. Bypass: Inhalt 30 % Deckkraft.
   Am Ende: runder „+“-Button (gestrichelt) → Menü mit allen 8 Modulen.
3. **Detail-Bereich** (Höhe 460): links das Bedienfeld des ausgewählten Moduls (1180 × 460), rechts IN/OUT-Meter (2 × 5 px Balken, Verlauf Cyan → Violett → Pink).
4. **Vocal Control** (Rest): 6 Makro-Regler PRESENCE, SPACE, WIDTH, PUNCH, WARMTH, VOCAL LEVEL (Farben siehe Makros) + GR-Anzeige.

Spalten-Regler je Modul: LEVEL RIDE Target/Speed · SMOOTH Amount/De-Ess · VOCAL SMASH Smash/Drive · CHOIR Blend/Width · PULSE Depth/Rate · STUTTER Gate/Mix · DELAY Feedback/Mix · REVERB Decay/Mix.
Diese zwei Regler sollen dieselben Parameter steuern wie im Detail-Bedienfeld.

### Modul-Bedienfelder (je 1180 × 460, Innenabstand 22/28)
Kopfzeile: Neon-Icon 30 px + Name + Slot-Info rechts daneben, darunter die 4 Funktionen als grauer Text (11 px). Rechts oben Live-Werte bzw. Switches.

**STUTTER** – Visualizer: 16-Step-Sequencer (Pads anklickbar, Gruppen à 4, Lauflicht) · Buttons Random / Invert / Clear
- Segmente: Rate 1/4 · 1/8 · **1/16** · 1/32 · 1/64 — Mode **Stutter** · Cut · Gate · Reverse — Übergang Aus · **Build-Up** · Tape Stop · Riser
- Switch: Beat-Sync (an)
- Regler: Repeats 1–16 (4) · Gate 5–100 % (70) · Swing 0–75 % (12) · Pitch Drift −12…+12 st (0) · Decay 0–100 % (30) · Smooth 0–20 ms (4) · Chance 0–100 % (85) · Mix (100)

**DELAY** – Visualizer: Echo-Karte L/R (Ping-Pong wechselt die Seite, Höhe = Feedback)
- Segmente: Time 1/4 · 1/4D · 1/8 · **1/8D** · 1/8T · 1/16 — Charakter Digital · **Tape** · Analog
- Switches: Tempo-Sync (an) · Ping-Pong (an) · Freeze (aus)
- Regler: Time 20–1500 ms (320, nur ohne Sync) · Feedback 0–95 % (52) · Fahne 0–100 % (40) · Width (80) · Offset 0–50 ms (12) · Low Cut 20–1000 Hz (180) · High Cut 1–20 kHz (9) · Ducking (45) · Wobble (18) · Mix (28)

**REVERB** – Visualizer: Hallfahne (Pre-Delay, Early Reflections, Decay) + Tiefenstaffelung (Punkt wandert nach hinten)
- Hallraum-Liste: Room · Plate · Hall · **Festival** · Cathedral (setzt Size/Decay/Pre-Delay/Damping)
- Regler: Size (85) · Decay 0.2–12 s (4.2) · Pre-Delay 0–200 ms (35) · Damping (40) · Tiefe (45) · Width (75) · Low Cut 20–800 Hz (220) · Modulation (20) · Mix (24)

**PULSE** – Visualizer: animierte LFO-Kurve L (durchgezogen) / R (gestrichelt) + Textur-Leiste
- Segmente: Wellenform Sine · Triangle · **Square** · Saw · Random — Rate 1/2 · 1/4 · **1/8** · 1/16 · 1/32 — Ziel **Volume** · Filter · Pan · Pitch · Formant
- Switches: Retrigger (an) · Triole (aus)
- Regler: Depth (70) · Smooth (35) · Phase 0–360° (0) · Swing 0–75 % (10) · Stereo Offset 0–180° (90) · Texture (40) · Resonance (30) · Mix (100)

**VOCAL SMASH** – großer SMASH-Regler (150 px) · GR-Meter · Kennlinie (Threshold/Ratio) · Obertöne-Balken je Charakter
- Segmente: Charakter Clean · Tape · **Tube** · Clip · Fuzz
- Switches: Parallel (an) · Auto Gain (an)
- Regler: Smash 0–100 (72) · Threshold −40…0 dB (−22) · Ratio 1–20 (8) · Attack 0.1–50 ms (3) · Release 10–500 ms (80) · Drive (55) · Density (60) · Output −12…+12 dB (0) · Mix (85)

**CHOIR** – Visualizer: Stereo-Bühne mit LEAD in der Mitte und den Stimmen im Halbkreis (Intervall + Verstimmung in Cent)
- Segmente: Stimmen 2 · 4 · **6** · 8 — Intervall **Unison** · Okt ↓ · Okt ↑ · Quinte · Terz · Stack — Formant Dark · **Natural** · Bright
- Regler (2 Spalten, Name links neben dem Wert): Detune 0–50 ct (14) · Spread (80) · Timing 0–60 ms (22) · Blend (55) · Humanize (40) · Tone −50…+50 (10) · Width 0–200 % (140) · Mix (45)

**SMOOTH** – Visualizer: Spektrum Original (gestrichelt) vs. geglättet, entfernte Bereiche rosa, Fokusband markiert
- Charakter: Gentle · **Natural** · Silk (große Buttons mit Untertitel)
- Switch: Delta (nur Entferntes hören)
- Regler: Amount (50) · Sharpness (60) · Fokus Low 1–8 kHz (2) · Fokus High 4–20 kHz (12) · De-Ess (45) · Air Tame (25) · Speed 1–50 ms (12) · Mix (100)

**LEVEL RIDE** – Visualizer: Pegelverlauf Input (grau) / Output (Farbe) + Gain-Linie + Target-Linie, daneben Rider-Fader
- Segmente: Vocal-Typ Vocal · **Rap** · Spoken · Backing
- Switches: Atmer ignorieren (an) · Look-Ahead (an)
- Regler: Target −30…−6 dB (−18) · Range 0–18 dB (9) · Speed (55) · Sensitivity (60) · Clarity (40) · Atmer −24…0 dB (−12) · Gate Floor −70…−20 dB (−45) · Output −12…+12 dB (0)

(Werte in Klammern = Standardwert, fett = Standardauswahl, ohne Einheit = 0–100 %.)

### Auto Mix & Vocal Check
Diese beiden Ansichten sind im Canvas noch im älteren Stil. Inhaltlich:
- **Auto Mix:** Wellenform mit Analyse-Fortschritt, Button „Vocal analysieren“, erkannte Werte (Tempo, Tonart, Lautheit, Vocal-Typ, Sibilanz, Dynamik), Zielstil (Party Rap, Hypertechno, Festival Pop, Trap, R&B Smooth), Intensität, 3 Switches, vorgeschlagene Chain mit Trefferquote, „Auf Chain anwenden“.
- **Vocal Check:** Vocal Score (Ring), Diagnose-Sicherheit, 8 Messwerte mit Bereichs-Balken, Status (OK / Hinweis / Problem), Vorschlag und FIX-Button pro Zeile.

---

## Alternative: Web-Oberfläche in JUCE
Ab JUCE 8 kann ein Plugin seine Oberfläche als HTML/JavaScript in einem eingebetteten Browser zeigen
(`WebBrowserComponent` mit Native-Integration und Parameter-Relays). Dann ließe sich das Canvas-Design
fast 1:1 übernehmen – es müsste aber erst in normales HTML/JS umgeschrieben werden, und unter Windows
braucht es WebView2. Für den Anfang ist der native Weg mit diesem Paket einfacher und stabiler.
