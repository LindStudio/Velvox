# Ledger – overhaul/voicespanel (Doubler and Choir panels)

Baseline (0cca7f0, feature/automix after the time-panel merge): full suite 29660 checks passed, 0 failed.

Task 1: complete (`doubleredit` in Source/UI/VoicesPanels.*: mode / voice / Edit / setValue / toggleOn / resetVoice /
resetPosition / leaveClassic / valueText / parseValue; tests "Headliner voices panels" – Doubler edit blocks)
Task 2: complete (DoublerStage strip + rows, DoublerPanel; old DoublerView / DoublerPanel removed; PanelFrameTests doubler
exemption removed (smoothW kept))
Task 3: complete (ChoirStage half circle, ChoirPanel moved; old ChoirView / ChoirPanel removed)
TDD note: the tests were written before the implementation, but the first red run was a build failure (no VoicesPanels.cpp), not
a runtime failure. First runtime run: 2 failures (rows never shown because the first readParameters() saw "no change" – fixed by
starting from 0 shown voices; a test that assumed a linear delay range – the parameter is skewed, test now uses the parameter's
normalisation). Mutation run (leaveClassic call removed + pan drag direction inverted): 13 checks failed as expected, reverted.

## Rulings

- Ruling: both panels and their views live in one new unit Source/UI/VoicesPanels.{h,cpp}, registered in an own CMake block at
  the end; DetailPanels.cpp / ModuleViews.cpp only lose the old classes and route the two cases to createVoicesModulePanel / View.
  — Same pattern as TimePanels; small merge hunks. — Cost if wrong: none.
- Ruling: the stage strip's x axis is the voice's Pan parameter (−100 … +100 %), y its Gain (−24 … +6 dB); Width is drawn as the
  band the voices are heard in, and the engine's live position (voicePan = pan × Width, voiceCents, voiceDelayMs) as a faint
  ghost ring with a dashed link when it differs from the dot. — The dot must be what the drag and the Pan field edit (dragging
  at Width 0 would otherwise do nothing); the ghost shows what is heard. — Cost if wrong: users may expect the dot at the heard
  position at Width < 100 %.
- Ruling: Classic shows its two voices at Pan ∓100 %, 0 dB with the Classic delays (D, 1.37 D) in the rows (values greyed as
  "linked") and a bracket over the pair; any voice edit (drag, wheel, field, on/off, reset) converts first: V1/V2 = Classic
  values, Mod Depth = Classic Detune (both 0 – 25 ct), doublerVoices = "2", all in the same undo step. — Voices ledger ruling;
  copying the pan as ∓100 % (not ∓Width) keeps the heard pans identical because Width scales the per-voice pans too. — Cost if
  wrong: none measured (test: engine's resolveVoices after the conversion = Classic pans and delays).
- Ruling: Dry is a main-row knob (not a drag on the lead dot); the lead is drawn at the Dry level (a ring when Dry is off).
  — Fewer hidden gestures; the spec lists Dry as a global control. — Cost if wrong: one more drag target later.
- Ruling: voice gestures: drag = pan + gain (Shift/Ctrl 1/10), double-click / Alt+click = pan + gain defaults, wheel = detune
  ±1 ct (Shift 0.1), right-click = Stimme an/aus, Stimme zurücksetzen + panel items, hover card = delay · detune · gain · pan.
  A click without movement records nothing (the Edit and its transaction start on the first movement).
- Ruling: the per-voice rows are own value fields (DoublerStage::ValueField), not Knob/Slider attachments: drag (220 px = whole
  range, skew as the parameter), click = type after the double-click time (so a double-click stays a reset), double-click /
  Alt+click = default, wheel = step (0.5 ms, 1 ct, 0.5 dB, 5 %), right-click = Standardwert / Wert eingeben + voice items. Each
  names its parameter in the "paramId" property (panel reach test). — An attachment would write V1/V2 directly and bypass the
  Classic conversion, and in Classic it would show V-parameters that are not heard. — Cost if wrong: no copy/paste items in the
  field menu (the knobs have them).
- Ruling: main row = Stimmen (Classic / 2 / 4), Mix (setMixKnob), the wobble (Classic: "Detune" = doublerDetune, 2 / 4: "Mod
  Depth" = doublerModDepth), Dry; Mehr = Width, Mono Safe, Tone, Low Cut, Mod Rate, Delay + the inactive wobble knob. The
  inactive wobble and Delay outside Classic are disabled (alpha 0.45) but stay reachable. — Spec "Mod depth (Classic: Detune)",
  same swap pattern as the Delay panel's note value / ms. — Cost if wrong: two knob labels for one concept.
- Ruling: Choir positions = seats on the half circle: each side's voices in the order of their heard pans, the outermost at its
  pan (sin angle = pan), the others evenly spaced towards the centre (0.75 of the angle over the side's voices); radius 0.8 – 1.0
  by the entry delay relative to the latest voice. Width 0 puts every voice behind the lead. — With x = heard pan the stage pan
  curve (all voices at ±0.8 … 1) piles the voices on the two sides on top of each other; the seats keep the user's picked
  half-circle look, honest order and sides, and exact values in the hover card. — Cost if wrong: inner voices look less wide than
  they are (the MONO read-out and the card are exact).
- Ruling: dragging the choir stage sideways sets Width (220 px = 0 … 100 %, one undo step), double-click / Alt = default, wheel
  ±2 % (Shift 0.2). — The stage is otherwise only a picture; Width is the spatial control. — Cost if wrong: none.
- Ruling: formatValue shows Hz values below 10 Hz with two decimals ("0.31 Hz"). — Doubler Mod Rate (0.05 – 3 Hz) read "0 Hz" on
  its knob and in hosts. Display only, no ID or value change. — Cost if wrong: none (no other Hz parameter goes below 20 Hz).
- Ruling: snapshot overlay for Mehr open with parameters: "set:<id>=<v>,…|more" (the existing parser; ",more" at the end of a
  set: list is dropped by the first parse). No change to TestMain.cpp.

Task 4: complete (help pages doubler / choir, screenshots in C:/Users/jakob/dev/velvox-wt/shots-voicespanel, full suite
29965 checks passed, 0 failed)

Merge: feature/automix 9cd3823 (smooth + pitch panels) merged. Conflicts DetailPanels.cpp / ModuleViews.cpp (overlapping deletions):
resolved from their side + my deletions and factory lines; PanelFrameTests pendingPanel block removed entirely (no pending
parameters remain). Full suite after the merge: 30150 checks passed, 0 failed.
