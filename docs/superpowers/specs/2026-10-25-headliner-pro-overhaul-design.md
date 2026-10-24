# Headliner Pro-Overhaul – Design

Status: approved by the user in conversation on 2026-10-25 ("wenn du noch genauere Fragen hast gerne jetzt, sonst starte").
Repo: `C:\Users\jakob\dev\velvox`, branch `feature/automix`. Plugin: "Headliner" by Lind Audio (CMake target `Velvox`, JUCE 8.0.9, VST3).
Mockups the user reacted to: `.superpowers/brainstorm/1362-1792876699/content/*.html` (git-ignored; `modes.html`, `spectrum-variants.html` A3, `modules-all.html`).

## 1. Intent and why

The user mixes their own songs (party/pop/EDM vocals) and wants Headliner to feel and sound like a top commercial vocal plugin. After the
UI redesign of 0.5 the plugin works, but in the user's words it "lacks polish": every module is "flooded with knobs", there is too much
text, the window is too big, the chain row carries settings, some behaviour is wrong (Delay at 100 % wet still passes the dry vocal),
and the DSP quality is not yet at the level of the reference plugins the user owns. Reference plugins named by the user:
FabFilter Pro-Q 4, Pro-C 2, oeksound soothe2, Valhalla VintageVerb, iZotope Ozone 11, Cymatics Voxity; for single modules also Waves
Doubler (Doubler) and Nicky Romero Kickstart (Pulse).

UI and sound are **equally important** (user). Success = the user loads the finished plugin, every module looks clean and uniform, is
quick to use, and every module sounds at least as good as its reference on dry vocals.

## 2. Goals

1. **Window and shell.** Base size about **1000 × 600** (like Pro-Q 4), freely scalable (keep the stored `uiScale` mechanism). Header:
   logo, preset selector, **SIMPEL | ADVANCED** switch (replaces the A/B buttons), tabs **AUTO MIX** and **VOCAL CHECK**, Bypass, `?`.
2. **Remove** the A/B compare feature (UI and the state behind it) and the **Vocal Control bar** at the bottom.
3. **Simple mode (variant A3, chosen by the user):** left a live spectrum of the vocal (before dashed / after filled) with a
   **"Vocal analysieren"** button under it (runs the existing auto-mix analysis and sets the modules); right a column of the 6 macros as
   horizontal sliders with value text: PRESENCE, SPACE, WIDTH, PUNCH (= `macroAggression`), WARMTH, VOCAL LEVEL (= `outputGain`).
   Moving a macro adjusts the modules in the background (existing `Macros` mapping). Same window size as Advanced. IN/OUT meters and
   loudness small at the bottom.
4. **Advanced mode:** chain row on top, module panel below (whole remaining area).
   - Chain tiles show **only name, then icon, then on/off** (this order, user's correction). No knobs or values in the chain.
     Drag = reorder; click = select; right-click = menu (bypass, duplicate, remove, move, reset, insert/send for delay/reverb);
     a `+` tile opens the module browser. Undo/redo live in the right-click menu and via Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z.
   - Module panels follow the user's chosen style **A "Grafik zuerst"** (Pro-C/Pro-Q): one large interactive visual, **3–5 main
     controls**, everything else in a **"Mehr" (more) drawer**. Per-module designs: section 5.
5. **Text only on hover.** No permanent explanatory sentences or hint lines in the panels. Labels stay (short control names, values).
   Explanations appear as tooltips after hovering ~600 ms, and in the `?` help.
6. **Uniform control gestures** (all knobs, sliders, graph handles, segment controls):
   - drag = change (vertical for knobs, along the axis for sliders); **Shift or Ctrl + drag = fine** (1/10 speed);
   - **double-click = reset to default**; **mouse wheel = step value** (only when the pointer is over the control; scrollable lists keep
     the wheel for scrolling); **Alt/Option + click = reset** as well (Pro-Q convention);
   - **click on the value text = type a value** (units accepted, e.g. "2k", "-3 dB", "1/8");
   - **right-click = context menu**: reset, enter value, copy/paste value, and for module panels "reset module", "save module preset";
   - hover shows the tooltip (name, short description, current value with unit).
   Graph handles (EQ points, comp curve, de-ess band, smooth weighting points, delay echoes, reverb depth dot, pulse curve) use the same
   gestures; in addition: EQ double-click on empty space adds a point, wheel over a point changes Q, right-click on a point offers
   type/slope/delete.
7. **Modern module browser** (the `+` tile): a popup grid of module cards grouped by category (Clean-up: EQ, De-Esser, Smooth;
   Dynamics: Compressor, Vocal Smash, Level Ride; Colour: Saturation; Width/Voices: Doubler, Choir, Pitch; Space: Delay, Reverb;
   Rhythm: Stutter, Pulse; Analysis: Vocal Check). Each card: icon, name, one-line description on hover, greyed out with reason when
   no more instances are possible. Search field on top, Esc closes, Enter adds the highlighted card.
8. **Modern preset list:** header shows `‹ name ▾ ›` (prev/next arrows). Clicking opens a panel with search, categories (factory
   categories + "Eigene" + "Favoriten"), star to favourite, hover = preview description, double-click/Enter = load, save / save as /
   rename / delete for user presets. Module presets (per module) stay reachable via the panel's right-click menu.
9. **Dry/Wet correctness (bug).** Every module with a Mix control must output **only the wet signal at 100 %** and only the dry signal
   at 0 %. The reported bug: Delay is placed as a *send* by default, so the dry vocal is always present. Required behaviour:
   - Delay and Reverb are added as **inserts** by default; "Mix" means true dry/wet.
   - The send option stays (right-click on the tile, "Als Send"), and while a module runs as a send its knob is **labelled "SEND"**
     and the panel says so in the header; there 100 % = full send level.
   - Audit all 15 modules with a null/correlation test (at Mix 100 % the dry vocal must be ≥ 60 dB down in the output for modules
     where that is physically meaningful – e.g. delay with zero-feedback, reverb with predelay; pitch, doubler, choir, smash, sat,
     comp, eq, smooth, pulse, stutter where mix exists). Old projects that already have a send keep it.
10. **Top-level DSP quality** for every module, measured on **dry vocals** (VocalSet in `C:\Users\jakob\dev\velvox-data`, the MYLK dry
    chorus) against the installed reference plugins (Pro-Q 4, Pro-C 2, Pro-DS, soothe2, Saturn 2, Pro-R 2/VintageVerb, Timeless 3,
    Ozone 11 modules, Waves Doubler, Kickstart if installed). Section 6.
11. **Vocal Check** more accurate and shown as tiles (section 5.15), with **Ignorieren / Anwenden** per finding and **Alles anwenden**
    above the list. Anwenden inserts/adjusts modules and is a single undo step.

## 3. Non-goals

- No new module types beyond the existing 15 (Doubler gets a new engine, but stays "Doubler").
- No changes to the auto-mix objective/calibration from 0.5 (except feeding it the new EQ band model, see 5.1).
- No AAX/AU builds, no licensing/copy protection, no cloud features.
- Assistant (chat) view is not part of this overhaul; it stays hidden as today.

## 4. Anti-goals (would be a failure even if it technically works)

- Any panel that again shows a grid of 8+ equal knobs.
- Old projects/presets that load with a different sound (all parameter IDs stay; new behaviour defaults keep old sessions identical,
  except the Delay/Reverb *default for newly added* modules).
- Clicks, zipper noise, denormal CPU spikes, or latency that changes while playing.
- Explanatory text blocks permanently on screen.
- A "quality" improvement that only shows on synthetic sine tests but not on real dry vocals.

## 5. Module panels (user's picks; take the direction, not the mockup 1:1)

Common frame: header with icon, name, instance number, on/off, module preset menu; visual takes most of the panel; main controls in one
row under or beside it; "Mehr" drawer slides in from the right/bottom and holds the remaining parameters as small knobs. Every module
has a live visual (no static pictures).

1. **EQ – Pro-Q style (EQ 1).** Large curve over a live spectrum (pre faint, post filled), **user-added points** (double-click empty
   space adds a Bell; drag; wheel = Q; right-click = type: Bell, Low Shelf, High Shelf, Low Cut, High Cut, Notch, Band Pass, Tilt;
   cut slopes 6/12/18/24/36/48 dB/oct; delete). Up to **16 bands**. Under the graph only the selected band's values (Freq, Gain, Q,
   Type, on/off). Mix in "Mehr". Optional per band: dynamic mode is **not** required. DSP: minimum-phase biquads with
   **Pro-Q-like analog-matched (de-cramped) response near Nyquist** (e.g. Vicanek matched-z or 2× oversampling for the top octave),
   smoothed coefficient changes without zipper. Parameters: new IDs `eqBand{1..16}{On,Type,Freq,Gain,Q,Slope}`; the legacy fixed
   bands (HPF, low shelf, mid bell, high shelf, LPF) are **migrated** on load into bands 1–5 when a state has no `eqBand*` values,
   and the legacy IDs stay registered (hidden from UI, not automatable) so old automation does not break loading. Auto-mix and macros
   write the new bands.
2. **Compressor – Pro-C style (Comp 1).** Scrolling level/output/gain-reduction display + draggable transfer curve (threshold point,
   ratio by dragging the upper segment). 4 main controls: Threshold, Ratio, Attack, Release. Mehr: Knee, Makeup (with Auto), Mix,
   Detector/Style, Sidechain HPF. DSP: program-dependent release option, feed-forward with proper log-domain smoothing, lookahead 0 ms
   (no latency) – quality target: Pro-C 2 "Vocal" style on dry vocals.
3. **De-Esser – Pro-DS style (De-Esser 1).** Spectrum with draggable S band (frequency range), live reduction shown. Main: Threshold,
   Range, Listen, mode (wide-band / split-band). DSP: split-band with linear-phase-free band split that nulls when inactive; detection
   relative to the full-band level so loud and quiet phrases are treated alike (Pro-DS "Single Vocal").
4. **Smooth – soothe style (Smooth 1).** Live spectrum with the removed energy shown in pink, a weighting curve with draggable points
   under it, 3 main controls: Depth (Amount), Sharpness/Selectivity (Sensitivity), Speed (Attack/Release linked), plus **Delta** listen.
   Mehr: separate attack/release, mode (soft/hard), output, low/high limits. DSP target: **comparable to soothe2** on dry vocals:
   STFT-based (or high-resolution filterbank) dynamic resonance suppression with spectral-peak detection relative to a smoothed
   spectral envelope, per-bin gain smoothing in time and frequency, phase-coherent overlap-add, transparent when nothing resonates.
   Latency allowed and reported to the host (constant).
5. **Level Ride – word bars (Ride 2).** Bars per word/phrase showing level before (grey) and after (cyan) and the target line; one big
   **"Ausgleich"** control (Amount) plus Target; Mehr: max boost/cut, response, phrase, transient, silence.
6. **Saturation – character tiles + Drive (Sat 2) with 3 bands.** Character tiles (e.g. Tape, Tube, Warm, Crunch/Clip) and a big Drive;
   output is auto-compensated. **Multiband (user addition):** Low / Mid / High with two draggable crossovers; each band has its own
   Drive and Character (and on/off); a "Link" switch moves all bands together. Mehr: Mix, Output trim, crossover freqs. DSP:
   oversampled (≥ 4×) antialiased waveshaping, phase-compensated Linkwitz-Riley crossovers that sum flat when drive is 0.
7. **Vocal Smash – one big control (Smash 1) with settings.** Big SMASH control, gain-reduction column, and visible small controls
   Blend, Tone, Output; Mehr: Attack, Release. ("jedoch mit Einstellungsmöglichkeiten" – the user wants the settings reachable.)
8. **Doubler – like Waves Doubler (none of the 3 mockups).** 2 or 4 voice mode; per voice: on, delay (ms), detune (cents), gain, pan;
   global: modulation (pitch + delay wobble) depth/rate, low/high cut for the doubles, dry level, mono-safe. Visual: a stage strip
   showing each voice's pan position and level, voices draggable (x = pan, y = gain); the per-voice values as a compact row each.
   New parameters as needed (`doublerVoices`, `doublerV{1..4}{On,Delay,Detune,Gain,Pan}`, `doublerModDepth`, `doublerModRate`,
   `doublerLowCut`, `doublerHighCut`, `doublerDry`); the old 0.2 doubler params map onto the new engine so old projects sound close
   (exact identity not required for this module – mark as accepted change in CHANGELOG). DSP: high-quality fractional delay
   (Lagrange/allpass), smooth random-walk modulation, formant-neutral small detune (pitch shifter with low artefacts for ±30 cents).
9. **Choir – half-circle stage (Choir 1).** Voices on a half circle around the lead, voice count as segments, 3 main controls
   (Detune, Width, Mix); Mehr: Timing, Humanize, Density.
10. **Pitch – two big controls (Pitch 2).** Big semitone control (snaps to semitones) and Formant; Mehr: Cents, Preserve formants, Mix.
    DSP target: clean ±12 st on dry vocals (Graillon/Little AlterBoy class), no phasiness on sustained notes.
11. **Delay – echo map (Delay 1).** Echoes drawn as bars (L/R, height = level) that can be dragged (x = time, y = feedback). Main:
    note value (sync) / ms, Feedback, Mix, Ping-Pong. Mehr: filters, ducking, drive, diffusion, modulation, spread, into reverb.
    Target: Timeless 3 / Pro-quality tape/analog characters, no zipper when changing time (crossfade or tape-style).
12. **Reverb – depth (Reverb 3).** The vocal as a dot in a room drawing; dragging it back = more space (moves Mix, Pre-delay and Size
    together along a curated path); 3 sliders Size, Decay, Mix; room type dropdown; Mehr: everything else incl. ducking.
    Target: Valhalla VintageVerb / Pro-R 2 on dry vocals (dense, no metallic ringing, smooth decay).
13. **Stutter – cuts on the waveform (Stutter 3).** Scrolling vocal waveform with the cut/muted slices shaded; Rate, Depth, Smooth,
    Pattern dropdown. (Internal name stays `chop`; UI name "Stutter" everywhere.)
14. **Pulse – like Kickstart.** One big **Amount** control, **4 curve shape tiles** (different pump shapes), rate buttons (1/4 default,
    1/2, 1/8, 1/16, 1 bar), sync to host tempo. The existing 6-point curve editor and Attack/Release/Smooth/BPM stay in "Mehr".
15. **Vocal Check – score + tiles (Check 1).** Score ring on top, **"Alles anwenden"** button, under it one tile per finding: what is
    wrong (short, e.g. "Mulm um 300 Hz (+4 dB)"), the suggested fix ("→ EQ −3 dB bei 300 Hz"), buttons **Ignorieren** and **Anwenden**.
    Ignored findings collapse into "x ignoriert" (restorable). Algorithm: compare the analysed vocal against target ranges per genre
    from the auto-mix profile data (tonal balance per band with frequency-specific resonance detection, sibilance level, dynamics/
    crest, LRA, loudness, plosives/low rumble, harshness 2.5–5 kHz, mud 200–500 Hz, stereo/phase). Each finding carries a confidence;
    only findings above a threshold are shown. Anwenden = concrete parameter change (adds a module if missing), one undo step.

## 6. DSP quality process (how "top level" is proven)

- Build a small **reference host** tool (JUCE, `tools/`) that renders the same dry vocal through a Headliner module and through an
  installed reference VST3 at matched settings and level, and writes WAVs + metrics. Metrics per module type: THD/aliasing (sat,
  pitch), transfer/timing curves (comp, de-ess), resonance reduction vs. artefact (Smooth vs. soothe2: spectral flatness gain,
  transient preservation, delta energy), decay density/modal ringing (reverb), modulation smoothness (delay/doubler), null tests for
  mix and bypass, CPU per block.
- Each module gets automated quality tests (in `VelvoxTests`) for the measurable targets, and its rendered comparison files are left in
  `C:\Users\jakob\dev\velvox-bench\<date>\modules\` for the user's final listening test.
- The user does the final listening of the whole plugin; my job is to have every module at the best quality I can reach and measure.

## 7. Constraints

- Keep every existing parameter ID and plugin code (IDs are persisted). New IDs only added. Old projects sound identical except where
  this document says otherwise (EQ migration must be sound-identical; Doubler close; Delay/Reverb newly added default = insert).
- Real-time safe audio thread (no allocation/locks), constant latency reported to the host.
- Tests: `VelvoxTests` must stay green; pluginval strictness 10 must pass; build via `scripts\build.ps1`, install via
  `scripts\install-vst3.ps1`.
- Never edit files through PowerShell Get/Set-Content (UTF-8 issues).
- Fonts/colours: keep the design tokens of 0.5 (`colours::*`, module colours). Module colours: EQ #2dd4bf, Comp #2563eb, De-Esser
  #0ea5e9, Smooth #22b8ff, Ride #00d1ff, Sat #ec4899, Smash #3b82f6, Doubler #818cf8, Choir #6366ff, Pitch #c084fc, Delay #d946ef,
  Reverb #f472b6, Stutter #a855f7, Pulse #8b5cf6, Check #38bdf8.
- UI language as today (German labels where the user used German: "Mehr", "Vocal analysieren", "Ignorieren", "Anwenden",
  "Alles anwenden", "SIMPEL/ADVANCED"); parameter names stay English as today.

## 8. Decided by me (builder may refine)

- Sub-projects, executed in this order, each with its own plan and tests, run back-to-back without stopping:
  1. Shell: window size, header (Simple/Advanced, no A/B), remove Vocal Control bar, chain tiles, gestures + tooltips, module browser,
     preset list, Simple mode A3.
  2. Dry/wet audit and the insert default for Delay/Reverb.
  3. EQ with user points (DSP + parameters + migration + panel).
  4. Remaining module panels (sections 5.2–5.14) in the common frame.
  5. DSP quality per module with the reference host (Smooth, Doubler engine, Saturation multiband, Pulse shapes first; then the rest).
  6. Vocal Check algorithm + tile UI.
  7. Final review, full test suite, pluginval, install, benchmark files, docs (HANDOFF, CHANGELOG 0.6.0).
- **Mix law and old sessions (builder Q1):** today inserts use `out = in + wet*mix` (VocalChain.cpp ~560). The new true dry/wet law
  applies to every instance whose state has the new hidden per-instance flag `mixLaw = 1`. Fresh instances get 1; states loaded
  without the flag get legacy 0, so old projects sound identical. The same goes for the amount crossfades of Doubler/Choir/Pitch/Sat/Pulse,
  which are only changed under that flag. The Mehr drawer of an instance on legacy law shows a small "Alte Mix-Kurve – umstellen" action.
- **Insert default (Q2):** a fresh plugin instance (`Routing::defaults()`) and modules added through the browser place Delay/Reverb
  as **inserts**. The auto-mix planner and the SPACE macro keep sends, because their calibration depends on it. In the UI those
  tiles/panels say **SEND**, which removes the confusion the user reported. Factory presets are not converted.
- **A/B removal (Q3):** the auto-mix before/after compare becomes a hidden internal snapshot. In Simple mode, after an analysis, a
  small **Vorher/Nachher** toggle appears next to "Vocal analysieren". Sessions saved with slot B active load the active slot.
- **EQ migration (Q4):** migrated legacy bands keep the old bilinear response through a hidden state flag `eqLegacyResponse`, which is
  set only by migration, so they stay sound-identical. New states use the matched response. The flag can be cleared from the right-click menu.
- **Macros/auto-mix on a user EQ (Q5):** bands carry a hidden role (`eqBand{n}Role`: none/presence/warmth/air/lowcut/mud/auto).
  Macros and auto-mix find the band with their role, or create it in a free slot if it is missing (also after the user deleted it).
  User-created bands have role none and are never touched by macros.
- **Quality gate (Q6):** for each module, measure the reference at matched settings first. Pass = within or better than the reference
  on the module's key metrics (tolerances set per metric in the plan). At most 3 improvement rounds per module. If a gap remains, it
  is documented in docs/BENCHMARK.md and work moves on. Nothing blocks the run.
- **Quality fixes vs. old projects:** pure quality fixes (less aliasing, de-cramped response, less ringing, correct pitch
  ratio, less distortion from ballistics, decay matching the set time) may change old projects slightly. They are listed in the
  CHANGELOG. Changes of *character* (a different intended sound for the same settings) are avoided, or apply only to fresh instances.
- **New parameters (Q7):** new IDs may be added wherever a listed control has none. Their default reproduces the old behaviour.
- **Undo (Q8):** one global history (JUCE `UndoManager` on the APVTS plus routing edits). It holds all parameter gestures, routing
  edits, preset loads and Vocal Check "Anwenden"/"Alles anwenden", each as one step.
- **Latency (Q9):** as today. Latency changes only when a latency-bearing module is added, removed or switched on/off, under the
  existing fade. While an instance exists its latency is constant.
- Window base 1000 × 600; min scale keeps 9 px text ≥ 8 px on screen.
- Tooltip delay 600 ms.
- Max 16 EQ bands.

## 9. Left to the builder

Exact pixel layout inside the common frame, the "Mehr" drawer animation, the four Pulse shapes, the Saturation character set, the exact
Vocal Check thresholds (derive from VocalSet/MYLK data and the auto-mix profiles), the reference-host implementation details, and the
choice of STFT size/overlap for Smooth (within constant latency ≤ 1024 samples at 48 kHz unless clearly better).
