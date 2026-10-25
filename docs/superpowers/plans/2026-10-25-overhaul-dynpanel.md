# Dynamics panels (Headliner 0.6, track "dynpanel") Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The four dynamics panels of spec 5.2, 5.3, 5.7 and 5.5 on the common frame: Compressor "Comp 1 · Pro-C-Stil",
De-Esser "De-Esser 1 · Pro-DS-Stil", Vocal Smash "Smash 1 · ein Riesen-Regler", Level Ride "Ride 2 · Wörter-Balken"; plus the
fresh-instance default "Relative" for de-essers added from the browser.

**Architecture:** One new UI unit `Source/UI/DynPanels.{h,cpp}` like `SmoothPanel.*` / `TimePanels.*`: a small base `HandleGraph`
(a ModuleView whose graph handles share the spec 2.6 gestures: drag with Shift/Ctrl fine, double-click / Alt+click = defaults,
wheel, right-click value menu + panel items, inline value field, hover card after 600 ms, one undo step per gesture), the four
visuals `CompGraph`, `DeEssGraph`, `SmashStage`, `RideBars` (each drains its module's lock-free feed from the dyn track), and
the four panels. `DetailPanels.cpp` / `ModuleViews.cpp` only call `createDynPanel` / `createDynView`; the old CompPanel,
DeEssPanel, SmashPanel, RidePanel, CompView, DeEssView, SmashView, RideView are deleted. `ChainView::insertModule` (the browser's
fresh-instance path: defaults, off, insert, on) gets fresh-instance overrides (`deessDetection` = Relative).

**Tech Stack:** C++17, JUCE 8.0.9, VelvoxTests (juce::UnitTest).

**Spec:** `docs/superpowers/specs/2026-10-25-headliner-pro-overhaul-design.md` (2.4–2.6, 5.2, 5.3, 5.5, 5.7, 8); dyn DSP plan /
ledger `2026-10-25-overhaul-dyn.*` (feeds: `CompressorModule::history / currentGainReductionDb / transferDb`,
`DeEsserModule::spectrumTap / currentReductionDb / bandLowHz / bandHighHz`, `SmashModule::history / currentGainReductionDb`,
`RideModule::segments / samplePosition / currentGainDb`; params `compAutoRelease`, `compAutoMakeup`, `deessDetection`; dyn Ruling 4
of Task 5: fresh instances could start on Relative).

## Global Constraints

- No parameter IDs renamed / removed / added; parameter defaults unchanged (old projects keep Absolute detection: loading resets
  missing parameters to their defaults).
- Compressor: "Scrolling level/output/gain-reduction display + draggable transfer curve (threshold point, ratio by dragging the upper
  segment). 4 main controls: Threshold, Ratio, Attack, Release. Mehr: Knee, Makeup (with Auto), Mix, Detector/Style, Sidechain HPF."
- De-Esser: "Spectrum with draggable S band (frequency range), live reduction shown. Main: Threshold, Range, Listen, mode
  (wide-band / split-band)." Mehr: Detection (Absolute/Relative), Frequency.
- Smash: "Big SMASH control, gain-reduction column, and visible small controls Blend, Tone, Output; Mehr: Attack, Release."
- Ride: "Bars per word/phrase showing level before (grey) and after (cyan) and the target line; one big "Ausgleich" control (Amount)
  plus Target; Mehr: max boost/cut, response, phrase, transient, silence."
- Gestures (2.6) for every graph handle; every edit one undo step (`proc.getUndo()` records parameter gestures); host gestures
  begin / end around drags.
- No permanent explanatory text; short labels + values. Paths built in update() / resized(), paint only draws; live data ≤ 30 Hz.
- Instances 1 and 2 (`pidFor`, `getCompressor (instance)` …). Module colours Comp #2563eb, De-Esser #0ea5e9, Smash #3b82f6,
  Ride #00d1ff (`moduleColour`).
- DesignTests keep: comp knobs Threshold/Ratio/Attack/Release/Knee/Makeup/Mix/SC; compDetector + deessMode as segments,
  deessListen toggle; smashAmount knob; ride knobs Target/MaxBoost/MaxCut/Silence/Response.
- PanelFrameTests: remove the temporary dyn exemption; 3–5 main controls each.

## Review Focus

1. Silence / module off / no audio callback: the displays never read stale feed data as live (comp/smash history keeps scrolling
   only while frames arrive; ride bars keep their positions; de-ess spectrum falls away) — test "off: no frames, no reduction shown".
2. Feeds drained by the panel only: a closed panel leaves up to 10 s of frames queued; on open the graph discards the backlog
   beyond its window instead of animating it — test "backlog: a full queue is drained in one update".
3. Threshold / ratio drags to the range ends (−60 / 0 dB, 1:1 / 20:1) clamp and stay finite; the knee point stays on screen — test.
4. Instance 2: every graph edits only `_2` IDs and reads instance 2's feed — test "instance 2".
5. The fresh-instance Relative default never reaches loaded projects or duplicates (a duplicate copies its original's detection) —
   tests "old project loads Absolute", "duplicate keeps Absolute".

---

### Task 1: Unit, HandleGraph and the Compressor panel

**Files:** Create `Source/UI/DynPanels.{h,cpp}`, `Tests/DynPanelTests.cpp` (UnitTest "Headliner dynamics panels", category
"Headliner"); Modify `CMakeLists.txt` (a "Dynamics module panels" block like TimePanels), `Source/UI/DetailPanels.cpp`
(comp → `createDynPanel`, CompPanel removed), `Source/UI/ModuleViews.cpp` (comp → `createDynView`, CompView removed).

**Interfaces (Produces):**
```cpp
namespace velvox::ui {
    class HandleGraph : public ModuleView {           // base of every dyn graph
    protected:
        virtual int handleAt (juce::Point<float>) const = 0;          // -1 = none
        virtual int wheelHandleAt (juce::Point<float> p) const;       // default handleAt
        virtual juce::StringArray paramsOf (int handle) const = 0;    // [0] = the menu / typed-value parameter
        virtual void dragHandle (int handle, juce::Point<float> delta, float scale) = 0;  // writes inside the open gesture
        virtual void wheelHandle (int handle, float notches, float fine) = 0;          // one complete gesture
        virtual juce::String cardTitleFor (int) const; virtual juce::String cardValueFor (int) const;
        virtual juce::Point<float> anchorFor (int) const;
    public: int getDraggedHandle() const; int getHoveredHandle() const; bool isCardShown() const;
            const juce::String& getCardTitle() const; const juce::String& getCardValues() const;
            juce::PopupMenu buildHandleMenu (int handle);
    };
    class CompGraph final : public HandleGraph {      // handles: 0 = knee point (threshold), 1 = upper segment (ratio); wheel in the curve = knee (2)
    public: CompGraph (VelvoxAudioProcessor&, int instance);
        juce::Rectangle<float> levelArea() const; juce::Rectangle<float> curveArea() const;
        float xForInput (float db) const; float yForOutput (float db) const;  // curve, -60..0 dB
        juce::Point<float> kneePoint() const; juce::Point<float> segmentPoint() const;  // a point on the upper segment
        int getNumFrames() const; dsp::DynamicsFrame getFrame (int ago) const; float getShownGainReductionDb() const;
        static constexpr int maxFrames = 1024; static constexpr float floorDb = -60.0f;
    };
    std::unique_ptr<ModuleView> createDynView (VelvoxAudioProcessor&, dsp::ModuleId, int instance);
    std::unique_ptr<DetailPanel> createDynPanel (VelvoxAudioProcessor&, dsp::ModuleId);
}
```

- [ ] **Step 1: failing tests** — "Compressor panel: main Threshold Ratio Attack Release; Mehr Knee Makeup Auto Makeup Mix Detector
  Auto Release SC Filter", "Compressor graph: feed frames and the GR read-out follow the module", "knee point drag = threshold (one
  step, host gesture, Shift fine 1/10, clamped)", "upper segment drag down = more ratio", "wheel over the curve = knee ±1 dB / notch",
  "double-click / Alt+click = defaults", "right-click = Standardwert / Wert eingeben + panel items", "hover card THRESHOLD / RATIO",
  "backlog drained", "instance 2".
- [ ] **Step 2:** build → fails (no DynPanels.h).
- [ ] **Step 3:** implement. Level display: frames (5 ms) right to left, 1 px per frame column budget, input grey fill, output in the
  module colour, GR hanging from the top on the same dB scale (pink line), big GR read-out top right. Curve: `transferDb` over
  −60..0, knee point at (T, transferDb (T)), signal dot at the newest input level. Threshold drag: dominant axis (x right / y up =
  higher), 60 dB per curve width; ratio drag: log scale, 220 px = 1→20, down = more.
- [ ] **Step 4:** tests pass; PanelFrameTests comp exemption removed (`compAutoRelease`, `compAutoMakeup` reached).
- [ ] **Step 5:** commit.

### Task 2: De-Esser panel + fresh-instance Relative

**Files:** Modify `Source/UI/DynPanels.{h,cpp}`, `Source/UI/ChainView.{h,cpp}` (`insertModule` applies `freshInstanceValues`),
`DetailPanels.cpp`, `ModuleViews.cpp`, `Tests/DynPanelTests.cpp`, `Tests/PanelFrameTests.cpp` (exemption gone).

**Interfaces (Produces):**
```cpp
class DeEssGraph final : public HandleGraph {      // handle 0 = the S band: x = deessFreq, y = deessRange (down = more)
public: DeEssGraph (VelvoxAudioProcessor&, int instance);
    juce::Rectangle<float> plotArea() const; float xForFreq (float hz) const; float freqForX (float x) const;
    juce::Rectangle<float> bandArea() const; float getShownReductionDb() const; float bandShade() const;  // 0..1
    bool hasSpectrum() const; float shownInputDb (float hz) const;
};
// ChainView: values a freshly added instance gets on top of the parameter defaults (base id -> real value)
static std::vector<std::pair<juce::String, float>> freshInstanceValues (dsp::ModuleId);   // deess: { deessDetection, 1 }
```
- [ ] **Step 1: failing tests** — panel controls (main Threshold, Range, Mode, Listen; Mehr Detection, Frequency), band drag x/y,
  wheel = freq 1/24 oct, reset both, spectrum from the module tap, reduction read-out + shade grows with reduction, "browser-added
  de-esser starts on Relative (one undo step with the insert)", "old project without deessDetection loads Absolute", "duplicate keeps
  the original's detection", "fresh plugin's default chain stays Absolute".
- [ ] **Step 2–5:** fail, implement (a private `SpectrumSource` over the module's `spectrumTap`; after-curve = input − reduction ×
  shelf shape (split) or − reduction (wide)), pass, commit.

### Task 3: Vocal Smash panel

**Interfaces:** `class SmashStage final : public ModuleView` — `getShownGainReductionDb()`, `getPeakHoldDb()`, `getNumFrames()`;
panel places the SMASH knob (dial 120–200) and the stage's GR column in the visual. Main: Blend, Tone, Output; Mehr: Attack, Release.
- [ ] Tests: controls, big knob in the visual, GR column follows `currentGainReductionDb`, history scrolls; fail → implement → pass → commit.

### Task 4: Level Ride panel

**Interfaces:** `class RideBars final : public HandleGraph` — handle 0 = target line (`rideTarget`, y, −40..−6 dB);
`yForLevel (float db)`, `targetY()`, `getNumSegments()`, `getSegmentRect (int i, bool after)`, `secondsShown = 8`.
Panel: Ausgleich big in the visual; main row Target, Speed (rideResponse), Ignore Below (rideSilence) — the "Ride 2" mockup
(Ziel, Tempo, Atmer ignorieren); Mehr: Max Boost, Max Cut, Phrase, Transients.
- [ ] Tests: segments drained and placed by samplePosition (x of a segment = right edge − age / 8 s), before grey / after cyan heights,
  target drag / wheel / reset one step, instance 2; fail → implement → pass → commit.

### Task 5: Snapshots, help, full suite

- [ ] Snapshots (`--snapshot … pro <moduleId> "set:…"`) of each panel at 1000 px and at minimum scale into
  `C:\Users\jakob\dev\velvox-wt\shots-dynpanel\`, look at them, iterate.
- [ ] Help pages for comp / deess / smash / ride mention the graph gestures and the new controls.
- [ ] Full suite 0 failed; ledger final line; commit.
