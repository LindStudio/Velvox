#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <map>
#include <vector>
#include <memory>

// Ember, the imp of EMBER De-Esser.
//
// MascotBrain turns what the plugin does into a mood (state) and an intensity. MascotView draws it:
// with Jakob's 2D frames when they are built in (Products/Ember/Mascot/frames, see the README there),
// otherwise with a vector placeholder, so the plugin is complete without art.
namespace ember
{
    // Four moods only (Jakob 2026-11-15: "wir haben viel zu viele states"). The S EMBER takes away is no mood of
    // its own: while he dances, every S is an accent - a snap and a spark at his hand, as strong as the reduction.
    enum class MascotState
    {
        sleeping,     // bypass
        idle,         // no signal
        dancing,      // signal: dances; S = accent (snap + spark)
        listening     // AUTO is listening
    };

    inline constexpr int numMascotStates = 4;
    const char* mascotStateName (MascotState) noexcept;   // file prefix of the frames ("dancing", "idle" ...)

    struct MascotInput
    {
        bool bypass = false;
        bool hasSignal = false;
        bool autoListening = false;
        float reductionDb = 0.0f;
    };

    class MascotBrain
    {
    public:
        // Accent: an S starts when the reduction rises above accentDb (after it was below releaseDb), at most
        // one per minAccentGapS. Too much (> hotDb for hotAfterS, until < coolDb for coolAfterS): "Klingt gelispelt!"
        static constexpr float accentDb = 1.0f, releaseDb = 0.5f, hotDb = 9.0f, coolDb = 7.0f;
        static constexpr double minAccentGapS = 0.15, accentDecayS = 0.35, hotAfterS = 0.4, coolAfterS = 0.8;
        // Calm (Jakob 2026-11-10: "Ember springt sehr viel herum"): a new mood must last debounceS, and a mood
        // is shown at least minHoldS (bypass and AUTO switch at once). Short pauses in the vocal keep him dancing.
        static constexpr double debounceS = 0.25, minHoldS = 2.0;

        void update (const MascotInput& in, double dtSeconds) noexcept;
        MascotState getState() const noexcept { return state; }
        float getIntensity() const noexcept { return intensity; }      // 0..1, reduction / 12 dB (smoothed): the glow
        float getAccent() const noexcept { return accent; }            // 1 at an S, falls to 0 in accentDecayS
        float getAccentStrength() const noexcept { return strength; }  // 0.35..1 by the reduction of that S
        int getAccentCount() const noexcept { return accents; }        // +1 per S (the view spawns sparks)
        bool isHot() const noexcept { return hot; }
        double getTimeInState() const noexcept { return timeInState; }

    private:
        void setState (MascotState s) noexcept;
        MascotState state = MascotState::idle;
        float intensity = 0.0f, accent = 0.0f, strength = 0.0f;
        int accents = 0;
        bool armed = true, hot = false;
        double timeInState = 0.0, hotFor = 0.0, coolFor = 0.0, sinceAccent = 1.0, pendingFor = 0.0;
        MascotState pending = MascotState::idle;
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
        // Where his hands are in dancing frame `index` (side 0 = left, 1 = right), as a fraction of the (square)
        // picture; (-1, -1) when mascot.json has none. The S sparks fly from there.
        juce::Point<float> hand (int index, int side) const noexcept;
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
        std::vector<std::array<juce::Point<float>, 2>> hands;
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
        void stepForTest (double dt) { brain.update (input, dt); advanceClips (dt); }
        int getSparkCountForTest() const noexcept { return (int) particles.size(); }
        void sparkForTest() { spawnSparks(); }
        MascotFrames& getFramesForTest() noexcept { return frames; }
        bool usesFrames() const noexcept { return frames.anyFrames(); }

    private:
        void timerCallback() override;
        void advanceClips (double dt);
        void paintFrame (juce::Graphics&, juce::Rectangle<float> area, MascotState, MascotClip, double time, float alpha);
        void paintPlaceholder (juce::Graphics&, juce::Rectangle<float> area);
        void paintSparks (juce::Graphics&);
        void spawnSparks();
        juce::Rectangle<float> frameBox() const;
        float accentTilt() const noexcept;
        void paintBubble (juce::Graphics&, juce::Rectangle<float> area);

        enum class Kind { spark, letter, smoke };
        struct Particle { juce::Point<float> pos, vel; float life, size; Kind kind; };
        std::vector<Particle> particles;
        juce::Random random;
        MascotBrain brain;
        MascotInput input;
        MascotFrames frames;
        juce::String bubble;
        juce::Point<float> mouth;   // placeholder only
        int lastAccents = 0, accentSide = 1;
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
