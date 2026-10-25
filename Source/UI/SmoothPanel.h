#pragma once

#include "DetailPanels.h"
#include "../DSP/SmoothModule.h"

class VelvoxAudioProcessor;

// The 0.6 Smooth panel (spec 5.4, "Smooth 1 · soothe-Stil"): the live input spectrum with what is removed
// right now in pink, the weighting curve with its six draggable points over it, the low / high limits as
// shaded regions with draggable edges. Main row: Depth, Sharpness, Speed (attack + release linked), Delta;
// Mehr: Attack, Release, Mode, Output, Low, High.
namespace velvox::ui
{
    //==========================================================================
    // Smooth operations (message thread). `instance` 0..1. Every complete operation is ONE undo step.
    namespace smoothedit
    {
        // The parameter ID of `base` for an instance ("smoothLow" -> "smoothLow_2")
        juce::String paramId (const char* base, int instance);
        juce::String weightFreqId (int instance, int point);   // point 0..5
        juce::String weightGainId (int instance, int point);

        // ---- Speed: a UI link of Attack and Release (SmoothModule::speedToTimes, no parameter of its own)
        // Shown value: the mean of where Attack and Release sit on the link (0 = slow, 1 = fast)
        float speed (VelvoxAudioProcessor& proc, int instance);
        // A drag: begin once (attack, release), write as often as needed, end once (= one step)
        void beginSpeedGesture (VelvoxAudioProcessor& proc, int instance);
        void writeSpeed (VelvoxAudioProcessor& proc, int instance, float speed01);   // both onto the link
        void endSpeedGesture (VelvoxAudioProcessor& proc, int instance);
        void setSpeed (VelvoxAudioProcessor& proc, int instance, float speed01);     // complete gesture
        void resetSpeed (VelvoxAudioProcessor& proc, int instance);                   // attack / release defaults

        // ---- Weighting points and limits
        void resetPoint (VelvoxAudioProcessor& proc, int instance, int point);   // freq + gain to their defaults
        void flattenAll (VelvoxAudioProcessor& proc, int instance);              // every point's gain to 0 dB
        void resetParameter (VelvoxAudioProcessor& proc, const juce::String& id);  // one parameter to its default

        // The parameters as the module's settings (weighting curve for drawing: SmoothModule::weightingDbAt)
        dsp::SmoothSettings settings (VelvoxAudioProcessor& proc, int instance);
    }

    //==========================================================================
    // The linked Speed control: looks and behaves like every knob (drag, fine, double-click / Alt+click =
    // default 5 / 80 ms, wheel, typed value, right-click menu, hover card) and writes Attack + Release as one
    // gesture; it shows the value derived from the current Attack / Release.
    class SpeedKnob final : public Knob
    {
    public:
        SpeedKnob (VelvoxAudioProcessor& proc, int instance, juce::Colour hue);
        ~SpeedKnob() override;

        void sync();                         // shows the current Attack / Release (never writes)
        void resetToDefault();               // Attack / Release defaults (inside an open drag: the same step)
        juce::PopupMenu buildMenu();
        static juce::String textFor (float speed01);   // "50 %"
        bool isInGesture() const noexcept { return inGesture; }

        // Mouse events of the dial reach the knob as a listener of its slider
        void mouseDown (const juce::MouseEvent&) override;          // right-click = menu, Alt+click = default
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;   // default

    private:
        void show (float speed01);

        VelvoxAudioProcessor& proc;
        const int instance;
        bool inGesture = false, syncing = false, ignoreDrag = false;
    };

    //==========================================================================
    // The graph. Gestures (spec 2.6), on a point: drag = freq (x) / weight (y), Shift / Ctrl fine 1/10 ·
    // double-click or Alt+click = the point's defaults · wheel = weight +-0.5 dB per notch · right-click =
    // Punkt n: Zurücksetzen / Alle flach + panel items · hover 600 ms = card. On a limit edge: drag = freq,
    // double-click / Alt+click = default, wheel = 1/24 octave per notch, right-click = value menu.
    class SmoothGraph final : public ModuleView
    {
    public:
        SmoothGraph (VelvoxAudioProcessor& proc, int instance);
        ~SmoothGraph() override;

        static constexpr float rangeDb = 12.0f;            // weighting axis +-12 dB (the parameter range)
        static constexpr float spectrumTopDb = 0.0f, spectrumBottomDb = -90.0f;
        static constexpr float tiltDbPerOct = 3.0f;        // display tilt of the spectrum around 1 kHz
        static constexpr float wheelGainDb = 0.5f;         // per wheel notch on a point
        static constexpr float wheelEdgeOct = 1.0f / 24.0f;
        static constexpr int numColumns = 240;             // spectrum resolution on screen

        void update (const LiveData&) override;
        void paint (juce::Graphics&) override;
        void resized() override;

        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        juce::StringArray getEditedParameters() const override;

        // ---- Geometry (also for tests)
        juce::Rectangle<float> plotArea() const noexcept;
        float xForFreq (float hz) const noexcept;
        float freqForX (float x) const noexcept;
        float yForWeight (float db) const noexcept;
        float weightForY (float y) const noexcept;
        float yForLevel (float db, float hz) const noexcept;   // spectrum (with the display tilt)
        juce::Point<float> pointPosition (int point) const noexcept;
        int pointAt (juce::Point<float> p) const noexcept;    // -1: none
        enum class Edge { none, low, high };
        Edge edgeAt (juce::Point<float> p) const noexcept;
        float edgeX (Edge e) const noexcept;

        // ---- State (also for tests)
        float shownRemovedDb (float hz) const noexcept;       // the pink there (0 = nothing removed)
        float shownInputDb (float hz) const noexcept;
        bool hasSpectrum() const noexcept { return haveFrame; }
        int getDraggedPoint() const noexcept { return dragPoint; }
        Edge getDraggedEdge() const noexcept { return dragEdge; }
        int getHoveredPoint() const noexcept { return hoverPoint; }
        bool isCardShown() const noexcept { return cardShown; }
        const juce::String& getCardTitle() const noexcept { return cardTitle; }
        const juce::String& getCardValues() const noexcept { return cardValues; }
        const juce::String& getPeakText() const noexcept { return peakText; }
        const dsp::SmoothSettings& getSettings() const noexcept { return s; }
        juce::PopupMenu buildPointMenu (int point);
        juce::PopupMenu buildEdgeMenu (Edge e);

    private:
        void readParameters();
        void rebuildCurve();
        void rebuildSpectrum();
        void updateCard();
        void beginPointDrag (int point);
        void beginEdgeDrag (Edge e);
        void endDrag();
        int columnFor (float hz) const noexcept;
        juce::String edgeParam (Edge e) const;
        void addPanelItems (juce::PopupMenu& m);
        void showEdgeValueEditor (Edge e);

        VelvoxAudioProcessor& proc;
        const int instance;

        dsp::SmoothSettings s;
        bool on = false;
        std::array<float, 2 * dsp::SmoothSettings::numWeights + 3> paramKey {};

        // Live spectrum per screen column (smoothed for the eye)
        dsp::SmoothSpectrum frame;
        bool haveFrame = false;
        std::array<float, numColumns> colHz {}, inputDb {}, removedDb {};
        double lastRead = -1.0, lastPaint = -1.0;
        juce::String peakText;
        float peakDb = 0.0f;

        // Paths, built in update() / resized() (paint only draws them)
        juce::Path inputLine, outputFill, outputLine, removedFill, weightLine, weightFill;

        // Mouse
        int dragPoint = -1, hoverPoint = -1;
        Edge dragEdge = Edge::none, hoverEdge = Edge::none;
        juce::Point<float> dragLast;
        float dragFreq = 1000.0f, dragGain = 0.0f;
        bool ignoreDrag = false;
        juce::uint32 hoverSince = 0;
        bool cardShown = false;
        juce::String cardTitle, cardValues;
        juce::Point<float> cardAnchor;
        float cardWidth = 0.0f;
        std::unique_ptr<juce::TextEditor> valueEditor;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SmoothGraph)
    };

    std::unique_ptr<ModuleView> createSmoothGraph (VelvoxAudioProcessor& proc, int instance);
    std::unique_ptr<DetailPanel> createSmoothPanel (VelvoxAudioProcessor& proc);
}
