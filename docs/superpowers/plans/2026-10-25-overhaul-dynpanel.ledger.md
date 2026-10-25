# Ledger – overhaul track "dynpanel" (Compressor, De-Esser, Vocal Smash, Level Ride panels)

Baseline (feature/automix 8ab2e13 incl. the dyn merge): build ok.

Task 1–4: complete (Source/UI/DynPanels.{h,cpp}: HandleGraph (shared graph-handle gestures), CompGraph, DeEssGraph, SmashStage,
RideBars, the four panels, createDynView / createDynPanel; old CompPanel / DeEssPanel / SmashPanel / RidePanel (DetailPanels.cpp)
and CompView / DeEssView / SmashView / RideView (ModuleViews.cpp) deleted; ChainView::freshInstanceValues; Tests/DynPanelTests.cpp
"Headliner dynamics panels" written first and watched failing (link errors: no DynPanels.cpp), then 137 checks green on the first
build; PanelFrameTests: the temporary dyn exemption (compAutoRelease / compAutoMakeup / deessDetection) removed, 1493 checks green)
- Ruling: one shared base `HandleGraph` for every dyn graph handle (drag relative with Shift/Ctrl 1/10, double-click / Alt+click =
  the handle's defaults, wheel = one complete gesture per event, right-click = gestures::buildMenu of the handle's first parameter +
  panel items + an inline value field, hover card after 600 ms / at once while dragging, one host gesture per parameter, ended in
  reverse = one undo step) — why: three graphs with the same gestures (Smooth's code pattern, written once). Cost if wrong: none known.
- Ruling: graphs draw the EFFECTIVE settings (macros applied: LiveData.settings / readEffectiveSettings) and drag the PARAMETER by the
  pointer delta — the handle sits where the sound is, the knob and the handle move together. Cost if wrong: with the PUNCH macro up
  the knee point is not at the Threshold knob's value (it is where the compressor really works).
- Ruling (Compressor): level display 4 s (800 frames of 5 ms) right to left; input grey, output in the module colour, gain reduction
  pink hanging from the top on the same dB scale (-60..0); the threshold as a dashed line across; big GR number top right. Transfer
  curve = CompressorModule::transferDb (incl. makeup / auto makeup), knee point = threshold handle (drag along the dominant axis,
  60 dB per curve width, right / up = higher), upper segment = ratio (log, 220 px = 1:1..20:1, down = more), wheel anywhere over the
  curve = Knee +-1 dB; signal dot at the newest input level. No separate GR meter (the display carries it). Mehr: Detector (segments),
  Auto Release, Auto Makeup (switches), Knee, Makeup, Mix, SC Filter.
- Ruling (De-Esser): the band is ONE handle: x = Frequency (log over 20 Hz–20 kHz), y = Range (down = more, 24 dB over 45 % of the
  plot = the scale of the reduction bar), wheel = Frequency 1/24 oct; double-click resets both. The band runs from deessFreq to
  DeEsserModule::bandHighHz (the module's shelf has no upper edge parameter). Spectrum = the module's own input tap through a
  private SpectrumSource (≤ 30 Hz); "after" = input − reduction × shelf share ((f/fc)^4 / (1 + (f/fc)^4), split) or − reduction
  (wide); band shade = 0.08 + 0.92 × reduction / max (3 dB, Range); the reduction bar hangs from the top of the band, Range is its
  dashed floor. Mode labels "Split" / "Wide". Main: Threshold, Range, Mode, Listen; Mehr: Detection, Frequency.
- Ruling (fresh instances, dyn Ruling 4): `ChainView::freshInstanceValues (id)` = values on top of the parameter defaults, applied by
  insertModule (the browser path: defaults, off, insert, on — inside the same undo step) and by resetInstance ("Modul zurücksetzen":
  = like a module fresh from the browser). De-esser: deessDetection = Relative. Not applied: a new plugin's default chain (its
  de-esser stays Absolute — the auto-mix calibration, factory presets and the EqRole goldens assume the default chain), loading
  (missing parameters are reset to their defaults = Absolute), duplicates (copy their original). Cost if wrong: a user who wants a
  new plugin's default de-esser on Relative picks it in Mehr.
- Ruling (Vocal Smash): SMASH knob (dial 120–200) in the visual between the GR history (left, from the top, 0–24 dB) and the GR
  column (right, peak hold 1.5 s). Main: Blend (label "Blend", was "Mix"), Tone, Output; Mehr: Attack, Release.
- Ruling (Level Ride): main row = Target, Speed (rideResponse), Ignore Below (rideSilence); Ausgleich (rideAmount) big in the visual
  left of the bars; Mehr: Max Boost, Max Cut, Phrase, Transients — why: the frame needs 3–5 main controls and the spec's "Ausgleich +
  Target in main" gives two; the user's mockup "Ride 2" shows AUSGLEICH big with "Ziel", "Tempo" and "Atmer ignorieren" beside it,
  i.e. exactly Target, Response and the silence floor (same precedent as the Pitch panel ruling). Cost if wrong: Response and Ignore
  Below sit one level more prominent than the spec text says (nothing hidden, nothing duplicated).
- Ruling (Level Ride bars): 8 s window placed by RideModule::samplePosition (x = right edge − age / 8 s), level axis -54..-6 dB, the
  "after" bar in the Ride colour over the whole segment, the level before as a grey bar behind it plus a grey cap line (visible above
  and inside the after bar); the dashed target line is handle 0 (drag up/down 48 dB per plot height, wheel 0.5 dB); live dot at the
  right edge while a voice is active; current ride gain as a number.
- Ruling (stale feed backlog): CompGraph and SmashStage drain AND DISCARD their feed on their first update — while no panel is open
  the DynamicsHistory queue fills up and then drops the newest frames, so what it holds is up to 10 s old and would be drawn as
  "live". Test "a full feed queue … drained and discarded on open" watched failing first (1024 / 1024 frames shown), then green.
  Ride segments are kept: they carry absolute sample positions, old ones simply lie left of the window. Consequence for
  `--snapshot`: audio pumped before the editor opens is not shown in the comp / smash history (shots taken earlier show it filled).
- Paint never allocates: scale labels built once (dbScaleLabel), read-outs / band frequency / target texts cached in update().
- Help pages comp / deess / smash / ride rewritten for the graphs, the gestures and the new controls (Auto Release / Auto Makeup,
  Detection, Ausgleich / Speed / Ignore Below in the main row).

Task 5: complete (snapshots in C:\Users\jakob\dev\velvox-wt\shots-dynpanel\: comp-1000, comp-mehr, comp-889, deess-1000,
deess-mehr-889 (Relative), smash-1000, smash-mehr, smash-889, ride-1000, ride-889, ride-mehr-889; reviewed; fixes after review:
Ride's Ausgleich dial shrinks with the visual width (Mehr open: the bars keep ~60 % of the width), Smash history fill lighter)
Final: full suite 30449 checks passed, 0 failed (b054a6a).
