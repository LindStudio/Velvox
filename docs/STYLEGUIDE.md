# HEADLINER – UI-Stil (0.5, Shell 0.6)

Vorlage: Claude-Design-Paket „HEADLINER VOCAL“ (`docs/design/HEADLINER_DESIGN_HANDOFF.md`,
Referenzcode in `docs/design/reference/`). Was davon mangels Parametern nicht gebaut ist: `docs/DESIGN_MAPPING.md`.
Alle Werte stehen als Tokens in `Source/UI/Theme.h`; neue Komponenten nutzen nur diese Tokens.

## Fenster (ab 0.6)

Design-Canvas **1000 × 600**, gleichmäßig skaliert (fester Seitenverhältnis-Constrainer, 8/9 … 200 %, gespeichert als
`uiScale`). Unter 8/9 würde 9-px-Text kleiner als 8 px.

| Bereich | Höhe | Inhalt |
|---|---|---|
| Header (`TopBar`) | 44 | Logo · Presets ‹ Name ▾ › · SIMPEL \| ADVANCED · Tabs AUTO MIX \| VOCAL CHECK (aktiv = 2-px-Unterstrich) · Bypass · ? |
| SIMPEL (`SimpleView`) | Rest | links Spektrum (vorher gestrichelt, nachher gefüllt) + „Vocal analysieren“, Vorher \| Nachher, Details; rechts 6 Makro-Fader (`HSlider`); unten IN/OUT (`CompactMeters`) |
| ADVANCED Kette (`ChainView`) | 88 | Kacheln Name → Icon → Ein/Aus, „+“-Kachel, Sends hinter gestrichelter Linie (SEND-Badge), OUTPUT; rechts IN/OUT |
| ADVANCED Panel (`DetailPanel`) | Rest | volle Breite, siehe Modul-Panel |

VOCAL CHECK zeigt das Check-Panel über dem ganzen Körper (beide Modi). Ansicht: `uiMode` ("beginner" = SIMPEL,
"pro" = ADVANCED) + `uiTab` ("automix"/"check").

## Farben

| Token | Wert | Rolle |
|---|---|---|
| `windowBg` | #05060A | Fenster |
| `panel` | #07080D | Spalten, Panels (Rahmen `border` #161925, Radius 12) |
| `panelInset` | #040509 | Visualizer-Boxen (Rahmen `borderInset` #13161E, Radius 8) |
| `control` | #0D0F15 | Segment-Gruppen (Radius 7) |
| `borderSoft` | #13151E | Trennlinien |
| `textStrong` / `text` | #F1F5F9 / #E8EBF2 | Titel / Werte |
| `textMuted` (= `textDim`) | #8A93A6 | Zweittext, Funktionszeile |
| `textFaint` | #5B6478 | Sektions-Labels, Skalen |
| `knobName` | #6B7489 | Reglernamen |

Modulfarben (`moduleColour`): Level Ride #00D1FF · Smooth #22B8FF · Vocal Smash #3B82F6 · Choir #6366FF ·
Pulse #8B5CF6 · Stutter (Chop) #A855F7 · Delay #D946EF · Reverb #F472B6 – dazu für die Module ohne
Design-Vorlage: EQ #2DD4BF · Vocal Check #38BDF8 · De-Esser #0EA5E9 · Compressor #2563EB ·
Doubler #818CF8 · Pitch #C084FC · Saturation #EC4899.
Makros: Presence #00D1FF · Space #3B82F6 · Width #6366FF · Punch #8B5CF6 · Warmth #D946EF · Vocal Level #F472B6.
Rot (`warning`) nur für echte Probleme, Amber (`bypass`) nur für den globalen Bypass.

## Schrift

Montserrat und Roboto Mono (SIL OFL, eingebettet). Design-Größen sind exakte Pixel (`semiBold()`, `monoFont()`):
Modulname SemiBold 15, Tracking 2,5, Großbuchstaben · Sektions-Labels SemiBold 10 `textFaint` ·
Werte Roboto Mono 11,5 `text` · Reglernamen SemiBold 9 `knobName`.

## Bedienelemente

- **Regler** (`VelvoxLookAndFeel::drawRotarySlider`, `Knob`): 57 Striche über 270°, bis zum Wert in der
  Modulfarbe mit Glow, Rest #30343D; Körper r35 #34373F → #121418, vertiefte Kappe r26, Leuchtpunkt r3,4.
  Bipolare Parameter leuchten von der Mitte aus. Darunter Wert (Mono) und Name. Größen: Hauptregler 50,
  Mehr 32, SMASH 120.
- **Fader** (`HSlider`, `drawLinearSlider`): Spur 4 px #30343D, bis zum Wert in der Akzentfarbe (bipolar ab 0),
  Daumen 14 px mit Akzentring; Name links (Farbpunkt + Großbuchstaben), Wert rechts.
- **Schalter** (`Toggle`): Spur 26 × 14 in der Akzentfarbe, Knopf 10 px, Label rechts.
- **Segmente** (`Segments`): Radio-Buttons auf #0D0F15, aktiv = Akzent 16 %, Text Akzent → Weiß 55 %.
  An Choice- oder Bool-Parameter gebunden; Labels werden über den Namen der Choices zugeordnet.
- **Neon-Icons**: `Resources/Icons/*.svg` (weiße Linien, 48 × 48, Strich 1,6) – werden in die Modulfarbe
  umgefärbt (`NeonIcon`, `moduleIconSvg`).

## Gesten (alle Regler, Fader, Segmente – zentral in `FineSlider`, `Segments`, `ValueText`)

Ziehen · Shift/Strg + Ziehen = fein (1/10) · Doppelklick oder Alt+Klick = Standardwert · Mausrad = Schritt (nur über
dem Regler; Listen behalten das Rad) · Klick auf den Wert = eintippen (Einheiten: „2k“, „-3 dB“, „1.5 s“, „1/8“) ·
Rechtsklick = Menü (Standardwert, Wert eingeben, kopieren, einfügen; im Panel zusätzlich Modul zurücksetzen,
Modul-Preset speichern). Hover nach 600 ms: Name, Kurzbeschreibung, Wert mit Einheit (`HoverInfo`).
Kein dauerhafter Erklärtext in Panels – Erklärungen stehen im Hover und in der Hilfe.

## Modul-Panel (`DetailPanel`, Stil A „Grafik zuerst“)

Kopf 44: Power · Icon 24 · NAME n · Badge (SEND / AUS) · Presets · Mehr · ?.
Darunter die Grafik (so groß wie möglich), darunter **eine** Reihe mit 3–5 Hauptreglern (`setMainControls`).
Alles andere liegt in der „Mehr“-Schublade (rechts, 430 breit): Segmente und Schalter oben, kleine Regler im Raster.
Extras in der Grafik: `placeInVisual` + `layoutVisual`, Meter mit `placeMeter`. Delay/Reverb als Send: Mix heißt
„SEND“ (`setMixKnob`). Ausgeschaltet: Inhalt gedimmt, Power bleibt voll bedienbar.
