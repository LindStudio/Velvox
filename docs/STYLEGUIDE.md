# HEADLINER – UI-Stil (ab 0.5)

Vorlage: „HEADLINER Audioplugin UI-Stilguide“ (Gesamtansicht, Regler, Buttons, Meter, Farbpalette,
Typografie) und für die Modulfarben der Voxity-Look (eine Neonfarbe pro Modul).
Alle Werte stehen als Tokens in `Source/UI/Theme.h`; neue Komponenten nutzen nur diese Tokens.

## Farben

| Token | Wert | Verwendung |
|---|---|---|
| `background` | #0B1020 | Fensterhintergrund (Dark BG) |
| `panel` | #111827 | Hauptpanels (Panel BG) |
| `raised` / `raisedHi` | #18202F / #1F2937 | Felder, Karten (Card BG) |
| `line` | #334155 | Rahmen (Border) |
| `text` | #EEF0F5 | Text (Text Primary, leicht heller für Kontrast) |
| `teal` | #00D1FF | Cyan Primary – Meter, Anfang des Verlaufs |
| `blue` | #6366FF | Blue Primary – aktive Buttons |
| `violet` | #A855F7 | Violet Primary – Ende des Regler-Verlaufs |
| `pink` | #F472B6 | Pink Accent – Farbe/Drive-Module, Hintergrundlicht |

- `aurora(t)`: Cyan → Blau → Violett (Regler-Ringe, Kurven, Meter).
- `neon(t)`: ganzes Spektrum bis Pink. `moduleColour(id)` setzt jedes Modul auf einen festen Ton:
  Reinigen (EQ, Check, De-Esser, Smooth) cyan · Dynamik (Comp, Ride, Smash) blau ·
  Stimmen/Raum (Doubler, Choir, Pitch, Delay, Reverb) violett · Charakter (Chop, Pulse, Sättigung) pink.
  Die Farbe erscheint an Kachel-Icon, Power-Licht, Auswahlrahmen, Aktivitätsbalken und im Panel-Kopf.
- Rot (`warning`) nur für echte Probleme, Amber (`bypass`) nur für den globalen Bypass.

## Typografie

Montserrat (SIL OFL, eingebettet). Größen aus `typesize`, intern ×0,93 (Montserrat läuft breiter).

| Rolle | Schnitt |
|---|---|
| Modul-/Panel-Titel | Bold, Großbuchstaben, leicht gesperrt |
| Abschnitts- und Reglernamen | SemiBold, Großbuchstaben, gesperrt (`caps`) |
| Werte | SemiBold (`display`), größer als der Name |
| Fließtext | Regular / Medium |

## Bausteine

- **Regler:** matter dunkler Knopf, dünner dunkler Ring, Wertebogen im Aurora-Verlauf mit weichem
  Glühen, weißer Zeiger. Hover verstärkt das Glühen. Name oben, Wert darunter.
- **Buttons:** dunkel mit #334155-Rahmen; aktiv = Indigo-Fläche mit Blau→Violett-Rahmen und Glow
  (`VelvoxLookAndFeel::drawNeonFill`). Primäraktion = gefüllte Blau→Violett-Pille.
- **Meter:** Cyan-Verlauf, Wert darunter in großer Zahl.
- **Hintergrund:** statisches Licht (blau links, violett rechts oben, pink rechts unten), einmal pro
  Größe gerendert.

## Prüfen

`VelvoxTests --snapshot out.png x 0 <beginner|pro> <selection> [none|mixed|…]` rendert den echten Editor.
