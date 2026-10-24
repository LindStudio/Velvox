# Design „HEADLINER VOCAL“ (Claude Design) ↔ vorhandene Parameter

Quelle: `G:\Meine Ablage\MUSIC\Plugin\headliner-juce\README_JUCE_HANDOFF.md` (Stand 2026-10-24).
✓ = vorhanden (Parameter-ID), ≈ = ähnlicher Parameter vorhanden (Bereich/Bedeutung weicht ab), ✗ = fehlt in der DSP.

## Module, die das Design NICHT enthält (sind aber im Plugin und im Auto-Mix)
EQ, Compressor, De-Esser, Saturation, Doubler, Pitch, Vocal Check. Der Auto-Mix stellt EQ, Comp, De-Esser,
Sättigung und Doubler bei fast jedem Mix ein.

## Die 8 Design-Module

**LEVEL RIDE** (`ride*`)
- ✓ Target (`rideTarget`, −40…−6 statt −30…−6) · Speed (`rideResponse`) · Gate Floor (`rideSilence`, −80…−30 statt −70…−20)
- ≈ Range 0–18 dB → zwei Parameter `rideMaxBoost` / `rideMaxCut`
- ✗ Vocal-Typ (Vocal/Rap/Spoken/Backing) · Atmer ignorieren + Atmer-Schwelle · Look-Ahead (Modul ist latenzfrei) · Sensitivity · Clarity · Output
- vorhanden, im Design nicht vorgesehen: `rideAmount`, `ridePhrase`, `rideTransient`

**SMOOTH** (`smooth*`)
- ✓ Amount · Delta · Fokus Low (`smoothLow`) · Fokus High (`smoothHigh`) · Speed (`smoothAttack`)
- ≈ Sharpness → `smoothSensitivity` · Charakter Gentle/Natural/Silk → `smoothMode` hat nur 2 Stufen
- ✗ De-Ess (eigenes Modul De-Esser) · Air Tame · Mix
- vorhanden, nicht im Design: `smoothRelease`, `smoothOutput`

**VOCAL SMASH** (`smash*`)
- ✓ Smash (`smashAmount`) · Attack · Release · Output · Mix (`smashBlend`)
- ✗ Charakter Clean/Tape/Tube/Clip/Fuzz (gibt es nur im Modul Saturation, `satCharacter` 4 Stufen) · Parallel- und Auto-Gain-Switch · Threshold · Ratio · Drive · Density
- vorhanden, nicht im Design: `smashTone`

**CHOIR** (`choir*`)
- ✓ Stimmen (`choirVoices`, 4 Stufen) · Detune (0–40 statt 0–50 ct) · Timing · Humanize · Mix
- ≈ Spread / Width (0–200 %) → nur `choirWidth` 0–100 · Blend → `choirMix`
- ✗ Intervall (Unison/Oktaven/Quinte/Terz/Stack) · Formant Dark/Natural/Bright · Tone
- vorhanden, nicht im Design: `choirDensity`

**PULSE** (`pulse*`)
- ✓ Rate (`pulseRate`) · Depth · Smooth · Mix
- ≈ Wellenform → `pulseInterp` + `pulseTension` (Form), keine Sine/Square/Saw-Auswahl
- ✗ Ziel Filter/Pan/Pitch/Formant (nur Lautstärke) · Retrigger · Triole · Phase · Swing · Stereo Offset · Texture · Resonance
- vorhanden, nicht im Design: `pulseAttack`, `pulseRelease`, `pulseBpm`

**STUTTER** (entspricht am ehesten CHOP, `chop*`)
- ✓ Smooth (`chopSmooth`)
- ≈ Rate (`chopRate`, 4 Stufen statt 5) · Gate → `chopDepth`
- ✗ 16-Step-Sequencer · Mode Stutter/Cut/Gate/Reverse · Übergang Build-Up/Tape Stop/Riser · Beat-Sync · Repeats · Swing · Pitch Drift · Decay · Chance · Mix
- → STUTTER ist im Design ein neues Effektmodul; CHOP kann davon nur einen kleinen Teil.

**DELAY** (`delay*`)
- ✓ Time-Teilungen (`delayDivision`, 14 Stufen) · Tempo-Sync · Ping-Pong · Time · Feedback (0–90 statt 0–95) · Low Cut · High Cut · Ducking (`delayDuckAmount`) · Wobble (`delayMod`) · Mix
- ≈ Offset 0–50 ms → `delaySpread` · Charakter Digital/Tape/Analog → nur `delayDrive`
- ✗ Freeze · Fahne · Width
- vorhanden, nicht im Design: `delayDiffusion`, Ducking-Threshold/Attack/Release, `delayToReverb`

**REVERB** (`reverb*`)
- ✓ Size · Decay (0,2–20 s) · Pre-Delay · Damping (`reverbDamping`, in Hz) · Width · Low Cut · Modulation (`reverbMod`) · Mix
- ≈ Hallraum-Liste Room/Plate/Hall/Festival/Cathedral → `reverbType` hat Room, Plate, Hall, Large Hall, Ambience, Atmosphere, Cinematic, Ethereal
- ✗ Tiefe (Tiefenstaffelung)
- vorhanden, nicht im Design: `reverbEarly`, `reverbDiffusion`, `reverbBass`, `reverbHighCut`, Ducking

## Vocal Control (Makros)
- ✓ PRESENCE, SPACE, WIDTH, WARMTH
- ≈ PUNCH → `macroAggression`? · VOCAL LEVEL → `outputGain`?
- vorhanden, nicht im Design: `macroDensity`

## Sonstiges aus dem Design
- Header: Dry/Wet-Regler → kein globaler Mix-Parameter vorhanden (✗).
- Auto Mix und Vocal Check: im Canvas noch alter Stil; „Zielstil“ (Party Rap, Hypertechno …), Tempo- und Tonart-Erkennung,
  „Trefferquote“ gibt es im Auto-Mix nicht (✗).
