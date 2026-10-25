#pragma once

#include "DetailPanels.h"
#include "../DSP/DelayModule.h"
#include "../DSP/PulseModule.h"
#include "../DSP/ChopModule.h"

// The three time-module panels of 0.6 (spec 5.11, 5.13, 5.14) on the common frame:
//   Delay   - echo map: the repeats as bars (L above / R below the centre line, height = level),
//             the first (or any) echo dragged: x = time, y = feedback
//   Pulse   - "like Kickstart": one big Amount, Custom + 4 pump shape tiles, rate buttons, the gain
//             curve with a live playhead (the 6-point editor while Custom is selected)
//   Stutter - cuts on the waveform: the scrolling vocal with the muted / repeated slices shaded
// The views are public so the tests can drive them like a user.
namespace velvox::ui
{
    // Visual / panel for delay, pulse or chop; nullptr for every other module
    std::unique_ptr<ModuleView> createTimeModuleView (VelvoxAudioProcessor& p, dsp::ModuleId id, int instance);
    std::unique_ptr<DetailPanel> createTimeModulePanel (VelvoxAudioProcessor& p, dsp::ModuleId id);

    //==========================================================================
    // DELAY: echo map. Drag an echo: horizontally = time (snaps to note values while synced to a host
    // tempo; Shift leaves the grid = free ms; free: Shift / Ctrl = fine), vertically = feedback (the
    // height ratio from one repeat to the next, DelayModule::feedbackForRatio). Double-click / Alt+click
    // = default time and feedback, wheel = feedback, right-click = menu. Every drag is one undo step.
    class EchoMapView final : public ModuleView
    {
    public:
        EchoMapView (VelvoxAudioProcessor& p, int instance);
        ~EchoMapView() override;

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

        static constexpr int maxTaps = 64;
        static constexpr float floorDb = -48.0f;
        static constexpr float pixelsPerRatio = 200.0f;   // vertical drag: 200 px = ratio 1.0

        // Bar height (0..1 of the half height) for a linear echo level: dB scale floorDb .. 0 dB
        static float heightForLevel (float linear) noexcept;

        int getNumTaps() const noexcept { return numTaps; }
        dsp::DelayModule::EchoTap getTap (int i) const noexcept { return taps[(size_t) juce::jlimit (0, maxTaps - 1, i)]; }
        juce::Rectangle<float> getBarBounds (int tap, bool left) const noexcept;   // empty: no bar on that side
        juce::Rectangle<float> getPlotArea() const noexcept { return plot; }
        float xForMs (float ms) const noexcept;
        float getWindowMs() const noexcept { return windowMs; }
        float getEffectiveTimeMs() const noexcept { return effectiveMs; }
        float getRepeatRatio() const noexcept;   // level of a repeat relative to the one before it

        // Host tempo in BPM (<= 0: none). Default: what the chain last saw (tests replace it)
        std::function<double()> tempoSource;

    private:
        juce::String idFor (const char* base) const;
        juce::RangedAudioParameter* param (const char* base) const;
        void recompute();
        int tapAt (juce::Point<float> p) const noexcept;
        void endDrag();
        void resetEcho();
        juce::String readout() const;

        VelvoxAudioProcessor& proc;
        const int instance;
        dsp::DelaySettings settings;
        bool on = false, synced = false, hostSynced = false;
        int division = 9;
        float timeMs = 375.0f, effectiveMs = 375.0f, shownMs = 375.0f, duckGain = 1.0f;
        double sampleRate = 48000.0, hostBpm = 0.0, time = 0.0;
        std::array<dsp::DelayModule::EchoTap, maxTaps> taps {};
        std::array<float, maxTaps> lit {};
        int numTaps = 0;
        float windowMs = 2250.0f;
        juce::Rectangle<float> plot;

        // Drag state
        int hovered = -1, dragged = -1, dragK = 1;
        float dragStartTime = 0.0f, dragStartTapMs = 0.0f, dragStartRatio = 0.0f, dragDx = 0.0f, dragDy = 0.0f;
        juce::Point<float> lastPos;
        bool dragFree = false, freeByShift = false;
        std::vector<juce::RangedAudioParameter*> gestureParams;

        juce::String readoutText;
        std::array<juce::String, 8> axisText;
        std::array<float, 8> axisMs {};
        int numAxis = 0;
        int frameCount = 0;
        std::unique_ptr<HoverInfo> info;
    };

    //==========================================================================
    // PULSE: the gain over one cycle (what is processed: PulseModule::gainAt), the raw shape as a glowing
    // line and the live playhead (displayPhase / displayGain). While the shape is Custom its 6 points are
    // handles (drag, Shift/Ctrl fine, double-click / Alt+click = default point, right-click = menu).
    class PulseCurveView final : public ModuleView
    {
    public:
        explicit PulseCurveView (VelvoxAudioProcessor& p);
        ~PulseCurveView() override;

        void update (const LiveData&) override;
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

        bool showsPoints() const noexcept { return settings.shape == 0; }
        juce::Point<float> pointPosition (int i) const noexcept;
        juce::Rectangle<float> getPlotArea() const noexcept { return plot; }
        float yForGain (float gain) const noexcept;
        const dsp::PulseSettings& getSettings() const noexcept { return settings; }
        // The gain the drawing shows at a phase (cached PulseModule::gainAt, interpolated)
        float drawnGainAt (float phase) const noexcept;
        static constexpr int tableSize = 512;

    private:
        void rebuildPaths();
        int pointAt (juce::Point<float> p) const noexcept;
        void resetPoint (int i);

        VelvoxAudioProcessor& proc;
        dsp::PulseSettings settings;
        float phase = -1.0f, bpm = 150.0f;
        bool hostTempo = false;
        Smoothed gain { 1.0f, 1.0f };
        juce::Rectangle<float> plot, meter;
        juce::Path curvePath, appliedPath, appliedFill;
        std::array<float, tableSize + 1> gainTable {};
        bool pathsValid = false;
        int hovered = -1, dragged = -1, frameCount = 0, shownRate = -1;
        juce::Point<float> dragValue, lastPos;
        std::vector<juce::RangedAudioParameter*> gestureParams;
        juce::String hoverText;
        juce::String readoutText;
        std::unique_ptr<HoverInfo> info;
    };

    // The shape tiles: Custom + Classic, Punch, Swell, Gate, each with its curve. Bound to pulseShape
    // (component property "paramId"): click = choose (one undo step), double-click / Alt+click = default
    // (Custom), wheel = previous / next, right-click = menu, hover = info card.
    class PulseShapeTiles final : public juce::Component
    {
    public:
        explicit PulseShapeTiles (VelvoxAudioProcessor& p);
        ~PulseShapeTiles() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        // Once per frame: the selection and the Custom tile's curve follow the parameters
        void refresh (const dsp::PulseSettings& s);

        int getNumTiles() const noexcept { return numTiles; }
        juce::Rectangle<int> getTileBounds (int i) const noexcept { return tiles[(size_t) juce::jlimit (0, numTiles - 1, i)]; }
        int getSelectedTile() const noexcept { return selected; }
        void chooseTile (int i);
        void setAccent (juce::Colour c) { accent = c; repaint(); }

        static constexpr int numTiles = 5;

    private:
        int tileAt (juce::Point<int> p) const noexcept;
        void rebuildPath (int i);

        VelvoxAudioProcessor& proc;
        juce::RangedAudioParameter* parameter = nullptr;
        dsp::PulseSettings custom;
        std::array<juce::Rectangle<int>, numTiles> tiles;
        std::array<juce::Path, numTiles> paths;
        int selected = 0, hovered = -1;
        juce::Colour accent = colours::accent;
        std::unique_ptr<HoverInfo> info;
    };

    //==========================================================================
    // STUTTER: the last few seconds of the vocal scrolling from right to left (ChopModule::readDisplay):
    // the input in grey, what passes in the module colour, muted slices darkened, repeated slices outlined.
    class StutterWaveView final : public ModuleView
    {
    public:
        explicit StutterWaveView (VelvoxAudioProcessor& p);

        void update (const LiveData&) override;
        void paint (juce::Graphics&) override;
        void resized() override;

        static constexpr double windowSeconds = 4.0;
        static constexpr int windowFrames = (int) (windowSeconds / dsp::ChopModule::displayFrameSeconds);

        int getNumFrames() const noexcept { return numFrames; }
        dsp::ChopModule::DisplayFrame getFrame (int i) const noexcept { return frames[(size_t) juce::jlimit (0, dsp::ChopModule::displayCapacity - 1, i)]; }
        // Slices of the visible window drawn shaded: '.' = muted (gain < 0.5), 'r' = repeated
        int countShadedSlices (char type) const noexcept;

    private:
        bool isMuted (const dsp::ChopModule::DisplayFrame& f) const noexcept { return f.type == '.' && f.gain < 0.5f; }

        VelvoxAudioProcessor& proc;
        std::array<dsp::ChopModule::DisplayFrame, dsp::ChopModule::displayCapacity> frames {};
        int numFrames = 0, currentStep = -1, frameCount = 0, shownPattern = -1, shownRate = -1;
        bool on = false;
        const char* steps = "";
        juce::String readoutText;
        juce::Rectangle<float> wave, strip;
    };
}
