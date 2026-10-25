#pragma once

#include "DetailPanels.h"
#include "../DSP/ReverbDepth.h"

class VelvoxAudioProcessor;

// The 0.6 Reverb panel (spec 5.12, "Reverb 3 · Raumtiefe"): a room drawn in perspective with the vocal as a
// glowing dot. Dragging the dot back adds space - Mix, Pre-Delay and Size move together along the curated
// path of dsp::reverbDepthPath; manual edits of the three move the dot (dsp::reverbDepthFromSettings).
// Main row: Room (dropdown), Size, Decay, Mix (SEND while a send); everything else in Mehr.
namespace velvox::ui
{
    //==========================================================================
    // Depth operations (message thread). `instance` 0..1. Each complete operation is ONE undo step
    // and one host gesture group (mix, pre-delay, size).
    namespace reverbedit
    {
        // The depth the current Mix / Pre-Delay / Size stand for (0 = in front, 1 = far back)
        float depth (VelvoxAudioProcessor& proc, int instance);
        // The path point of the instance's current room type
        dsp::ReverbDepthPoint pointFor (VelvoxAudioProcessor& proc, int instance, float depth);

        // A drag: begin once, write as often as needed, end once (= one step)
        void beginDepthGesture (VelvoxAudioProcessor& proc, int instance);
        void writeDepth (VelvoxAudioProcessor& proc, int instance, float depth);
        void endDepthGesture (VelvoxAudioProcessor& proc, int instance);

        // Complete gestures
        void setDepth (VelvoxAudioProcessor& proc, int instance, float depth);
        void resetDepth (VelvoxAudioProcessor& proc, int instance);   // the three parameters' defaults
    }

    //==========================================================================
    // The room: one-point perspective box whose proportions follow the room type and Size, the decay per
    // frequency (ReverbModule::predictedRt60) as a faint curve on the back wall, the vocal as a glowing dot
    // on the floor (pulsing with the input, its reflections rippling out as long as the tail rings).
    // Gestures (spec 2.6): drag (on the dot or anywhere in the room, relative, up = back; Shift / Ctrl fine
    // 1/10) · double-click or Alt+click = Mix / Pre-Delay / Size defaults · wheel = step · right-click =
    // Vorne / Mitte / Hinten / Standardwerte + panel items · hover the dot 600 ms = card with the values.
    class ReverbRoom final : public ModuleView
    {
    public:
        ReverbRoom (VelvoxAudioProcessor& proc, int instance);
        ~ReverbRoom() override;

        static constexpr float pixelsPerDepth = 260.0f;   // drag distance for the whole path
        static constexpr float wheelStep = 0.02f;         // depth per wheel notch
        static constexpr int numDecayBands = 6;           // 125 Hz .. 4 kHz, octaves

        void update (const LiveData&) override;
        void paint (juce::Graphics&) override;
        void resized() override;

        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        // ---- State (also for tests)
        struct Geometry
        {
            juce::Rectangle<float> front, back;   // the room's front opening and back wall on screen
            juce::Point<float> vanishing;
        };
        const Geometry& getGeometry() const noexcept { return geo; }
        juce::Point<float> getDotPosition() const noexcept { return dot; }
        float getDotRadius() const noexcept { return dotRadius; }
        float getShownDepth() const noexcept { return shownDepth; }
        bool isDragging() const noexcept { return dragging; }
        const std::array<float, numDecayBands>& getDecayBands() const noexcept { return decayBands; }
        static float decayBandHz (int band) noexcept { return 125.0f * (float) (1 << band); }
        const juce::String& getReadout() const noexcept { return readout; }   // "Mix 18 % · Pre 20 ms · Size 60 %"
        juce::PopupMenu buildMenu();
        juce::Component& getDotHandle() noexcept;

    private:
        class DotHandle;
        void readParameters();
        void layoutRoom();
        void renderBackground (float scale);
        juce::Point<float> floorPoint (float t, float across) const noexcept;   // t 0 front .. 1 back, across -1..1
        float perspective (float t) const noexcept;
        float depthToT (float depth) const noexcept { return 0.06f + 0.86f * depth; }
        float currentPointerY (const juce::MouseEvent&) const;
        void stepDepth (float delta);

        VelvoxAudioProcessor& proc;
        const int instance;
        std::unique_ptr<DotHandle> handle;

        // Parameter state (what the drawing follows) and its cache key
        int type = 1;
        float mix = 18.0f, preDelay = 20.0f, size = 60.0f, paramDepth = 0.0f;
        std::array<float, 4> depthKey { -1.0f, -1.0f, -1.0f, -1.0f };
        dsp::ReverbSettings heard;   // effective settings (decay curve)
        std::array<float, 8> roomKey {};
        bool on = true, open = false;

        // Drag
        bool dragging = false, ignoreDrag = false;
        float dragDepth = 0.0f, lastPointerY = 0.0f;

        // Drawing
        Geometry geo;
        float shownDepth = 0.0f, dotRadius = 6.0f, level = 0.0f, duckGain = 1.0f;
        juce::Point<float> dot;
        std::array<float, numDecayBands> decayBands {};
        std::array<float, 24> decayCurve {};
        juce::String readout, depthText;
        juce::Path floorPath;
        juce::Image background;
        float backgroundScale = 0.0f;
        bool backgroundValid = false;

        struct Ripple { float age = -1.0f, strength = 0.0f; };
        std::array<Ripple, 8> ripples {};
        double lastTime = -1.0, lastPaint = -1.0, lastSpawn = -1.0;
        float levelSlow = 0.0f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReverbRoom)
    };

    std::unique_ptr<ModuleView> createReverbRoom (VelvoxAudioProcessor& proc, int instance);
    std::unique_ptr<DetailPanel> createReverbPanel (VelvoxAudioProcessor& proc);
}
