#pragma once

#include "DetailPanels.h"
#include "Spectrum.h"
#include "../DSP/DynamicsFeed.h"
#include "../DSP/RideModule.h"

class VelvoxAudioProcessor;

// The 0.6 dynamics panels on the common frame (spec 5.2, 5.3, 5.5, 5.7):
//   Compressor  "Comp 1 · Pro-C-Stil"       scrolling input / output / gain reduction + draggable transfer curve
//   De-Esser    "De-Esser 1 · Pro-DS-Stil"  spectrum with the draggable S band, live reduction
//   Vocal Smash "Smash 1 · Riesen-Regler"   big SMASH control, gain-reduction column, Blend / Tone / Output
//   Level Ride  "Ride 2 · Wörter-Balken"    bars per word (before grey, after cyan), draggable target line, big Ausgleich
// Every visual drains its module's lock-free UI feed (DSP/DynamicsFeed.h, RideModule::segments) on the message thread.
namespace velvox::ui
{
    namespace dynedit
    {
        // The parameter ID of `base` for an instance ("compThreshold" -> "compThreshold_2")
        juce::String paramId (const char* base, int instance);
        // Current value in real units (0 when the parameter does not exist)
        float value (VelvoxAudioProcessor& proc, const juce::String& id);
    }

    //==========================================================================
    // A module visual whose graph handles share the gestures of every control (spec 2.6):
    //   drag = change (Shift / Ctrl: fine 1/10, relative, no jump) · double-click or Alt+click = the handle's defaults ·
    //   wheel = step (one undo step per wheel event) · right-click = the value menu of the handle's first parameter
    //   (Standardwert, Wert eingeben …, copy / paste) + the panel items · hover 600 ms (at once while dragging) = card.
    // A drag is one host gesture on every parameter of the handle and one undo step.
    class HandleGraph : public ModuleView
    {
    public:
        HandleGraph (VelvoxAudioProcessor& proc, int instance, dsp::ModuleId module);
        ~HandleGraph() override;

        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        juce::StringArray getEditedParameters() const override;

        // ---- State (also for tests)
        int getDraggedHandle() const noexcept { return dragged; }
        int getHoveredHandle() const noexcept { return hovered; }
        bool isCardShown() const noexcept { return cardShown; }
        const juce::String& getCardTitle() const noexcept { return cardTitle; }
        const juce::String& getCardValues() const noexcept { return cardValues; }
        juce::PopupMenu buildHandleMenu (int handle);
        int getInstance() const noexcept { return instance; }
        // Hit test in local coordinates (-1 = no handle)
        int handleAtPoint (juce::Point<float> p) const { return handleAt (p); }

        static constexpr float hitRadius = 11.0f;

    protected:
        virtual int numHandles() const = 0;
        virtual int handleAt (juce::Point<float>) const = 0;
        virtual int wheelHandleAt (juce::Point<float> p) const { return handleAt (p); }
        // [0] = the parameter of the menu / typed value
        virtual juce::StringArray paramsOf (int handle) const = 0;
        virtual void beginHandleDrag (int handle) { juce::ignoreUnused (handle); }
        // Writes the handle's parameters for a relative movement (inside the open gesture); scale 1 or 0.1 (fine)
        virtual void dragHandle (int handle, juce::Point<float> delta, float scale) = 0;
        // A complete gesture per call (gestures::setReal)
        virtual void wheelHandle (int handle, float notches, float fine) = 0;
        virtual juce::String cardTitleFor (int handle) const = 0;
        virtual juce::String cardValueFor (int handle) const = 0;
        virtual juce::Point<float> anchorFor (int handle) const = 0;
        virtual juce::MouseCursor cursorFor (int handle) const;
        // After an edit: re-read the settings, rebuild what is drawn
        virtual void parametersEdited() {}

        // Card state from drag / hover; true when it changed
        bool updateCard();
        void paintCard (juce::Graphics&) const;
        // At most 30 repaints a second for live data (always while dragging)
        bool paintDue (double time);

        juce::RangedAudioParameter* param (const juce::String& id) const;
        float paramValue (const juce::String& id) const;
        juce::String pidFor (const char* base) const { return dynedit::paramId (base, instance); }
        void writeParam (const juce::String& id, float realValue);   // inside the open gesture
        void stepParam (const juce::String& id, float realValue);    // one complete gesture, clamped to the range

        VelvoxAudioProcessor& proc;
        const int instance;
        const juce::Colour hue;

    private:
        void beginDrag (int handle);
        void endDrag();
        void resetHandle (int handle);
        void showValueEditor (int handle);

        int dragged = -1, hovered = -1;
        bool ignoreDrag = false;
        juce::Point<float> dragLast;
        juce::uint32 hoverSince = 0;
        bool cardShown = false;
        juce::String cardTitle, cardValues;
        juce::Point<float> cardAnchor;
        float cardWidth = 0.0f;
        double lastPaint = -1.0;
        std::unique_ptr<juce::TextEditor> valueEditor;
    };

    //==========================================================================
    // COMPRESSOR: scrolling level display (input grey, output, gain reduction from the top on the same dB scale) left,
    // the transfer curve right. Handles: 0 = knee point (threshold; drag along the dominant axis, right / up = higher),
    // 1 = the upper segment (ratio; down = more, log, 220 px = 1:1 .. 20:1); wheel over the curve = knee (+-1 dB / notch).
    class CompGraph final : public HandleGraph
    {
    public:
        CompGraph (VelvoxAudioProcessor& proc, int instance);

        static constexpr int maxFrames = 1024;          // 5 ms frames: about 5 s
        static constexpr int framesShown = 800;         // 4 s across the level display
        static constexpr float floorDb = -60.0f;        // level / curve axes -60 .. 0 dB
        static constexpr float wheelKneeDb = 1.0f;
        enum Handle { kneeHandle = 0, ratioHandle = 1, kneeWheel = 2 };

        void update (const LiveData&) override;
        void paint (juce::Graphics&) override;
        void resized() override;

        // ---- Geometry (also for tests)
        juce::Rectangle<float> levelArea() const noexcept;
        juce::Rectangle<float> curveArea() const noexcept;
        float xForInput (float db) const noexcept;
        float yForOutput (float db) const noexcept;
        juce::Point<float> kneePoint() const noexcept;
        juce::Point<float> segmentPoint() const noexcept;   // on the upper segment, 3/4 of the way from the knee to 0 dB

        // ---- State (also for tests)
        int getNumFrames() const noexcept { return numFrames; }
        dsp::DynamicsFrame getFrame (int ago) const noexcept;    // 0 = newest
        float getShownGainReductionDb() const noexcept { return grNow; }
        const dsp::CompSettings& getSettings() const noexcept { return s; }

    protected:
        int numHandles() const override { return 3; }
        int handleAt (juce::Point<float>) const override;
        int wheelHandleAt (juce::Point<float>) const override;
        juce::StringArray paramsOf (int handle) const override;
        void beginHandleDrag (int handle) override;
        void dragHandle (int handle, juce::Point<float> delta, float scale) override;
        void wheelHandle (int handle, float notches, float fine) override;
        juce::String cardTitleFor (int handle) const override;
        juce::String cardValueFor (int handle) const override;
        juce::Point<float> anchorFor (int handle) const override;
        juce::MouseCursor cursorFor (int handle) const override;
        void parametersEdited() override;

    private:
        void readSettings (const dsp::InstanceSettings&);
        void rebuildLevels();
        void rebuildCurve();

        dsp::CompSettings s;
        bool on = true;
        std::array<dsp::DynamicsFrame, maxFrames> frames {};
        std::array<dsp::DynamicsFrame, 256> scratch {};
        int head = 0, numFrames = 0;
        bool primed = false;
        float grNow = 0.0f;
        float dragValue = 0.0f;
        std::array<float, 6> settingsKey {};
        juce::Path inputFill, outputFill, outputLine, grFill, grLine, curve;
        juce::String grText;
    };

    //==========================================================================
    // DE-ESSER: the module's input spectrum (grey) and what leaves it (filled), the S band from deessFreq to the top of the
    // band shaded by the current reduction, the reduction as a bar from the top of the band and the Range as its floor.
    // Handle 0 = the band: x = Frequency (log), y = Range (down = more); wheel = Frequency (1/24 octave per notch).
    class DeEssGraph final : public HandleGraph
    {
    public:
        DeEssGraph (VelvoxAudioProcessor& proc, int instance);

        static constexpr float topDb = 0.0f, bottomDb = -90.0f;   // spectrum axis
        static constexpr float reductionScaleDb = 24.0f;          // the reduction bar: 0 .. 24 dB over reductionDepth of the plot
        static constexpr float reductionDepth = 0.45f;
        static constexpr float wheelOct = 1.0f / 24.0f;

        void update (const LiveData&) override;
        void paint (juce::Graphics&) override;
        void resized() override;

        // ---- Geometry (also for tests)
        juce::Rectangle<float> plotArea() const noexcept;
        float xForFreq (float hz) const noexcept;
        float freqForX (float x) const noexcept;
        float yForLevel (float db) const noexcept;
        float yForReduction (float db) const noexcept;
        juce::Rectangle<float> bandArea() const noexcept;

        // ---- State (also for tests)
        bool hasSpectrum() const noexcept { return haveSpectrum; }
        float shownInputDb (float hz) const noexcept;
        float shownOutputDb (float hz) const noexcept;
        float getShownReductionDb() const noexcept { return reduction.current; }
        float bandShade() const noexcept;   // 0..1, grows with the reduction
        const dsp::DeEssSettings& getSettings() const noexcept { return s; }

    protected:
        int numHandles() const override { return 1; }
        int handleAt (juce::Point<float>) const override;
        juce::StringArray paramsOf (int handle) const override;
        void beginHandleDrag (int handle) override;
        void dragHandle (int handle, juce::Point<float> delta, float scale) override;
        void wheelHandle (int handle, float notches, float fine) override;
        juce::String cardTitleFor (int handle) const override;
        juce::String cardValueFor (int handle) const override;
        juce::Point<float> anchorFor (int handle) const override;
        juce::MouseCursor cursorFor (int) const override { return juce::MouseCursor::DraggingHandCursor; }
        void parametersEdited() override;

    private:
        float shelfShare (float hz) const noexcept;   // share of the reduction at hz (split band: a 2nd-order shelf)
        void rebuildPaths();

        dsp::SampleTap unusedTap;                 // SpectrumSource reads two taps; only the input is drawn
        std::unique_ptr<SpectrumSource> spectrum;
        dsp::DeEssSettings s;
        bool on = true;
        float bandHighHz = 20000.0f;
        bool haveSpectrum = false;
        Smoothed reduction;
        double lastRead = -1.0;
        float dragFreq = 6500.0f, dragRange = 6.0f;
        std::array<float, 6> settingsKey {};
        juce::Path inputLine, outputFill, outputLine;
        juce::String reductionText;
        juce::String freqText;
    };

    //==========================================================================
    // VOCAL SMASH: the gain reduction of the squashed path scrolling from the top (left) and the GR column (right) with a
    // peak hold; the panel puts the big SMASH control between them (setLayout).
    class SmashStage final : public ModuleView
    {
    public:
        SmashStage (VelvoxAudioProcessor& proc, int instance);

        static constexpr int maxFrames = 1024, framesShown = 800;
        static constexpr float scaleDb = 24.0f;
        static constexpr double peakHoldSeconds = 1.5;

        void update (const LiveData&) override;
        void paint (juce::Graphics&) override;
        void resized() override { rebuild(); }
        // Areas inside the stage (local coordinates)
        void setLayout (juce::Rectangle<float> trace, juce::Rectangle<float> column);
        juce::Rectangle<float> getTraceArea() const noexcept { return traceArea; }
        juce::Rectangle<float> getColumnArea() const noexcept { return columnArea; }

        float getShownGainReductionDb() const noexcept { return gr.current; }
        float getPeakHoldDb() const noexcept { return peakDb; }
        int getNumFrames() const noexcept { return numFrames; }

    private:
        void rebuild();

        VelvoxAudioProcessor& proc;
        const int instance;
        bool on = true;
        std::array<float, maxFrames> grFrames {};
        std::array<dsp::DynamicsFrame, 256> scratch {};
        int head = 0, numFrames = 0;
        bool primed = false;
        Smoothed gr;
        float peakDb = 0.0f;
        double peakTime = 0.0, lastPaint = -1.0;
        juce::Rectangle<float> traceArea, columnArea;
        bool customLayout = false;
        juce::Path grFill, grLine;
        juce::String grText;
    };

    //==========================================================================
    // LEVEL RIDE: one bar per word / phrase (RideModule::segments) scrolling with RideModule::samplePosition: level before
    // the ride grey, after it in the Ride colour; the target line across (handle 0: drag up / down, wheel +-0.5 dB).
    class RideBars final : public HandleGraph
    {
    public:
        RideBars (VelvoxAudioProcessor& proc, int instance);

        static constexpr int maxSegments = 128;
        static constexpr double secondsShown = 8.0;
        static constexpr float topDb = -6.0f, bottomDb = -54.0f;
        static constexpr float wheelDb = 0.5f;

        void update (const LiveData&) override;
        void paint (juce::Graphics&) override;
        void resized() override { rebuild(); }
        // Area of the bars inside the graph (local; the panel keeps room for the big Ausgleich control)
        void setPlotInset (float left) { inset = left; rebuild(); }

        // ---- Geometry (also for tests)
        juce::Rectangle<float> plotArea() const noexcept;
        float yForLevel (float db) const noexcept;
        float targetY() const noexcept { return yForLevel (target); }
        float xForSample (std::int64_t sample) const noexcept;

        // ---- State (also for tests)
        int getNumSegments() const noexcept { return numSegments; }
        dsp::RideSegment getSegment (int ago) const noexcept;    // 0 = newest
        juce::Rectangle<float> getSegmentRect (int ago, bool after) const noexcept;
        std::int64_t getNow() const noexcept { return now; }
        float getTarget() const noexcept { return target; }
        float getShownGainDb() const noexcept { return gain.current; }

    protected:
        int numHandles() const override { return 1; }
        int handleAt (juce::Point<float>) const override;
        juce::StringArray paramsOf (int handle) const override;
        void beginHandleDrag (int handle) override;
        void dragHandle (int handle, juce::Point<float> delta, float scale) override;
        void wheelHandle (int handle, float notches, float fine) override;
        juce::String cardTitleFor (int handle) const override;
        juce::String cardValueFor (int handle) const override;
        juce::Point<float> anchorFor (int handle) const override;
        juce::MouseCursor cursorFor (int) const override { return juce::MouseCursor::UpDownResizeCursor; }
        void parametersEdited() override;

    private:
        void rebuild();

        std::array<dsp::RideSegment, maxSegments> segments {};
        std::array<dsp::RideSegment, 32> scratch {};
        int head = 0, numSegments = 0;
        std::int64_t now = 0;
        double fs = 48000.0;
        bool on = true;
        float target = -18.0f, dragTarget = -18.0f;
        float liveLevel = -120.0f;
        bool voice = false;
        Smoothed gain;
        float inset = 0.0f;
        juce::String gainText;
        juce::String targetText;
    };

    //==========================================================================
    std::unique_ptr<ModuleView> createDynView (VelvoxAudioProcessor& proc, dsp::ModuleId module, int instance);
    std::unique_ptr<DetailPanel> createDynPanel (VelvoxAudioProcessor& proc, dsp::ModuleId module);
}
