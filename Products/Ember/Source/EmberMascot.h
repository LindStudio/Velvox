#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <memory>

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
        static constexpr float snapDb = 0.6f, fireDb = 1.5f, hotDb = 9.0f, coolDb = 7.0f;
        // Fire stays while S keep coming: a phrase with an S every second or two is one fire, not fire-dance-fire
        // (Jakob 2026-11-10: "wechselt oft zwischen tanzen und feuer, er soll bei einer pose bleiben").
        // Back to dancing only after fireHoldS without any S; snapping likewise after snapHoldS.
        static constexpr double hotAfterS = 0.4, coolAfterS = 0.8, proudForS = 2.5, fireHoldS = 4.0, snapHoldS = 2.5;
        // Calm (Jakob 2026-11-10: "Ember springt sehr viel herum"): a new mood must last debounceS, and a mood
        // is shown at least minHoldS (bypass and AUTO switch at once)
        static constexpr double debounceS = 0.25, minHoldS = 2.0;

        void update (const MascotInput& in, double dtSeconds) noexcept;
        MascotState getState() const noexcept { return state; }
        float getIntensity() const noexcept { return intensity; }   // 0..1 (fire: reduction / 12 dB)
        double getTimeInState() const noexcept { return timeInState; }

    private:
        void setState (MascotState s) noexcept;
        MascotState state = MascotState::idle;
        float intensity = 0.0f;
        double timeInState = 0.0, hotFor = 0.0, coolFor = 0.0, proudLeft = 0.0, fireLeft = 0.0, snapLeft = 0.0, pendingFor = 0.0;
        MascotState pending = MascotState::idle;
        bool hot = false;
    };

    // The clips of a mood: <state>_in_### (from the neutral pose into the mood, played once),
    // <state>_### (the loop) and <state>_out_### (back to the neutral pose, played once).
    // A change of mood plays: out of the old mood -> in of the new one -> its loop, so every change is smooth
    // when all in / out clips start / end on the same neutral pose. Moods without in / out clips cross-fade.
    enum class MascotClip { loop = 0, in = 1, out = 2 };

    struct FrameCache;

    // Frames of Jakob's 2D mascot, grouped by state and clip (built in as binary data).
    // The PNGs stay compressed; a picture is decoded when it is shown and kept in a small cache that all
    // EMBER windows in the process share. Decoding all 189 frames up front cost ~281 MB per window.
    class MascotFrames
    {
    public:
        MascotFrames();   // indexes the built-in frames (decodes nothing)
        ~MascotFrames();
        bool has (MascotState s, MascotClip c = MascotClip::loop) const noexcept;
        int count (MascotState s, MascotClip c = MascotClip::loop) const noexcept;
        juce::Image frame (MascotState s, MascotClip c, int index) const;   // message thread
        float fps (MascotState s) const noexcept;
        double duration (MascotState s, MascotClip c) const noexcept { return count (s, c) / (double) fps (s); }
        // Where his mouth is in a frame, as a fraction of the (square) picture; (-1, -1) when mascot.json has none.
        // The plugin draws the fire from there, as big as the reduction (the frames show only the pose).
        juce::Point<float> mouth() const noexcept { return mouthFrac; }
        bool anyFrames() const noexcept { return ! frames.empty(); }
        void clear() { frames.clear(); }                                   // tests
        void add (MascotState s, MascotClip c, const juce::Image& img) { frames[key (s, c)].add ({ -1, img }); }   // tests

        static constexpr int cacheCapacity = 24;   // decoded pictures kept (~35 MB at 600 x 600)
        static int decodedPicturesForTest();      // pictures held decoded right now, all windows

    private:
        struct Source { int resource = -1; juce::Image image; };   // built-in (decoded on demand) or a test picture
        static int key (MascotState s, MascotClip c) noexcept { return (int) s * 3 + (int) c; }
        std::map<int, juce::Array<Source>> frames;
        std::map<int, float> rates;
        float defaultFps = 24.0f;
        juce::Point<float> mouthFrac { -1.0f, -1.0f };
        std::shared_ptr<FrameCache> cache;
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
        MascotState getShownState() const noexcept { return shownState; }
        MascotClip getShownClip() const noexcept { return shownClip; }
        // Tests: one tick without the timer, and the frames to play
        void stepForTest (double dt) { brain.update (input, dt); input.autoJustFinished = false; advanceClips (dt); }
        MascotFrames& getFramesForTest() noexcept { return frames; }
        bool usesFrames() const noexcept { return frames.anyFrames(); }

    private:
        void timerCallback() override;
        void advanceClips (double dt);
        void paintFrame (juce::Graphics&, juce::Rectangle<float> area, MascotState, MascotClip, double time, float alpha);
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
        // What is on screen (follows the brain through the in / loop / out clips)
        MascotState shownState = MascotState::idle;
        MascotClip shownClip = MascotClip::loop;
        double clipTime = 0.0;
        MascotState previousState = MascotState::idle;   // for the cross-fade between two clips
        MascotClip previousClip = MascotClip::loop;
        double previousTime = 0.0, fadeTime = 1.0, fadeLength = 0.35;
        bool fading = false;
        double t = 0.0;
        bool compact = false, hover = false;
    };
}
