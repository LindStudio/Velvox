# HEADLINER VOCAL – UI-Stil (ab 0.5)

Vorlage: Claude-Design-Paket „HEADLINER VOCAL“ (`docs/design/HEADLINER_DESIGN_HANDOFF.md`,
Referenzcode in `docs/design/reference/`). Was davon mangels Parametern nicht gebaut ist: `docs/DESIGN_MAPPING.md`.
Alle Werte stehen als Tokens in `Source/UI/Theme.h`; neue Komponenten nutzen nur diese Tokens.

## Fenster

Design-Canvas 1384 × 1040, gleichmäßig skaliert (fester Seitenverhältnis-Constrainer, 70 … 140 %).
Unter 70 % werden die 9-px-Reglernamen unlesbar.

| Bereich | Höhe | Inhalt |
|---|---|---|
| Header (`TopBar`) | 56 | Preset-Box 290 × 36 mit ‹ › + Speichern/Menü, Logo HEADLINER VOCAL, Tabs CHAIN / AUTO MIX / VOCAL CHECK (aktiv = 2-px-Unterstrich), A/B, Kopieren, Bypass, ? |
| Kette (`ChainView`) | 330 | eine Spalte pro Modulinstanz (≥ 104 px, sonst scrollen), Sends hinter gestrichelter Linie, runder „+“ |
| Detail | 460 | Modul-Panel 1180 breit (`DetailPanel`), rechts IN/OUT-Meter |
| Vocal Control | Rest | 6 Makros + Gain Reduction (`VocalControlBar`), in allen Tabs |

AUTO MIX und VOCAL CHECK belegen Kette + Detail + Meterspalte.

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
  Bipolare Parameter leuchten von der Mitte aus. Darunter Wert (Mono) und Name. Größen: Modul 50–56,
  Spalte 48, Vocal Control 54, SMASH 150. Doppelklick = Standardwert, Shift = fein.
- **Schalter** (`Toggle`): Spur 26 × 14 in der Akzentfarbe, Knopf 10 px, Label rechts.
- **Segmente** (`Segments`): Radio-Buttons auf #0D0F15, aktiv = Akzent 16 %, Text Akzent → Weiß 55 %.
  An Choice- oder Bool-Parameter gebunden; Labels werden über den Namen der Choices zugeordnet.
- **Neon-Icons**: `Resources/Icons/*.svg` (weiße Linien, 48 × 48, Strich 1,6) – werden in die Modulfarbe
  umgefärbt (`NeonIcon`, `moduleIconSvg`).

## Modul-Panel

Kopf: Power · Neon-Icon 30 · NAME + „SLOT n · INSERT/SEND“ · Funktionszeile (11 px).
Darunter links der Visualizer, rechts Segmente und Schalter (`hasSideControls`), unten das Regler-Raster
(`knobGrid`). Ausgeschaltet: Inhalt gedimmt, Power bleibt voll bedienbar.
