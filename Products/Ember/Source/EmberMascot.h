#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

// Ember, the imp of EMBER De-Esser.
//
// MascotBrain turns what the plugin does into a mood (state) and an intensity. MascotView draws it:
// with Jakob's 2D frames when they are built in (Products/Ember/Mascot/frames, see the README there),
// otherwise with a vector placeholder, so the plugin is complete without art.
namespace ember
{
    enum class MascotState
    {
        sleeping,     // bypass
        idle,         // no signal: smoke rings, tail wags
        singing,      // signal, no S: bobs along
        snapping,     // small reduction: ears twitch, snaps the S out of the air
        fire,         // de-essing: spits fire, flame size = intensity (gain reduction)
        overheated,   // too much (> 9 dB for a while): coughs, sweats, "Klingt gelispelt!"
        listening,    // AUTO is listening: hand at the ear
        proud         // AUTO finished: thumbs up
    };

    inline constexpr int numMascotStates = 8;
    const char* mascotStateName (MascotState) noexcept;   // file prefix of the frames ("fire", "idle" ...)

    struct MascotInput
    {
        bool bypass = false;
        bool hasSignal = false;
        bool autoListening = false;
        bool autoJustFinished = false;   // one tick: AUTO has just set the plugin up
        float reductionDb = 0.0f;
    };

    class MascotBrain
    {
    public:
        // Thresholds (dB of gain reduction)
        static constexpr float snapDb = 0.3f, fireDb = 1.5f, hotDb = 9.0f, coolDb = 7.0f;
        static constexpr double hotAfterS = 0.3, coolAfterS = 0.6, proudForS = 2.5, fireHoldS = 0.25;

        void update (const MascotInput& in, double dtSeconds) noexcept;
        MascotState getState() const noexcept { return state; }
        float getIntensity() const noexcept { return intensity; }   // 0..1 (fire: reduction / 12 dB)
        double getTimeInState() const noexcept { return timeInState; }

    private:
        void setState (MascotState s) noexcept;
        MascotState state = MascotState::idle;
        float intensity = 0.0f;
        double timeInState = 0.0, hotFor = 0.0, coolFor = 0.0, proudLeft = 0.0, fireLeft = 0.0;
        bool hot = false;
    };

    // Frames of Jakob's 2D mascot, grouped by state (built in as binary data)
    class MascotFrames
    {
    public:
        MascotFrames();   // loads every built-in frame
        bool has (MascotState s) const noexcept;
        int count (MascotState s) const noexcept;
        const juce::Image& frame (MascotState s, int index) const;
        float fps (MascotState s) const noexcept;
        bool anyFrames() const noexcept { return ! frames.empty(); }

    private:
        std::map<int, juce::Array<juce::Image>> frames;
        std::map<int, float> rates;
        float defaultFps = 24.0f;
    };

    class MascotView final : public juce::Component, private juce::Timer
    {
    public:
        MascotView();
        ~MascotView() override;

        void setInput (const MascotInput& in) { input = in; }
        void setBubble (const juce::String& text) { bubble = text; }   // empty = none
        void setCompact (bool c) { compact = c; }
        std::function<void()> onClick;

        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseEnter (const juce::MouseEvent&) override { hover = true; }
        void mouseExit (const juce::MouseEvent&) override { hover = false; }

        const MascotBrain& getBrain() const noexcept { return brain; }
        bool usesFrames() const noexcept { return frames.anyFrames(); }

    private:
        void timerCallback() override;
        void paintPlaceholder (juce::Graphics&, juce::Rectangle<float> area);
        void paintFire (juce::Graphics&);
        void paintBubble (juce::Graphics&, juce::Rectangle<float> area);

        struct Particle { juce::Point<float> pos, vel; float life, size; bool smoke; };
        std::vector<Particle> particles;
        juce::Random random;
        MascotBrain brain;
        MascotInput input;
        MascotFrames frames;
        juce::String bubble;
        juce::Point<float> mouth;
        double t = 0.0;
        bool compact = false, hover = false;
    };
}
