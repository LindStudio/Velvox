#pragma once

#include "DetailPanels.h"
#include "../DSP/DelayModule.h"

// The time-module panel of 0.6 (spec 5.11) on the common frame (Pulse and Stutter were removed in 0.7):
//   Delay   - echo map: the repeats as bars (L above / R below the centre line, height = level),
//             the first (or any) echo dragged: x = time, y = feedback
// The views are public so the tests can drive them like a user.
namespace velvox::ui
{
    // Visual / panel for the delay; nullptr for every other module
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
}
