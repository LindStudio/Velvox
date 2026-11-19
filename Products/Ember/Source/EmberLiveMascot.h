#pragma once

#include "EmberMascot.h"
#include "EmberRig.h"

// Ember lives in the plugin (2026-11-17, step 3 of FIGUR_ANIMIEREN.md section 6): the behaviour of the approved
// browser prototype (v17), ported to C++, drawing with RigRenderer. Same interface as the old MascotView (input,
// bubble, click = AUTO), so the editor only swaps the class.
namespace ember
{
    // A damped spring: everything that moves eases through one of these.
    struct Spring
    {
        float v = 0, vel = 0, target = 0, k = 120, d = 14;
        Spring (float value = 0, float stiffness = 120, float damping = 14) : v (value), target (value), k (stiffness), d (damping) {}
        void step (float dt) noexcept { vel += (k * (target - v) - d * vel) * dt; v += vel * dt; }
        void kick (float x) noexcept { vel += x; }
    };

    // What Ember does and feels, frame by frame (prototype: brain). No drawing: testable on its own.
    class LiveBrain
    {
    public:
        enum class Mode { idle, sleep, listen, proud, hot, groove };

        struct Eat { float age = 0, dur = 0.6f, y0 = 300, size = 60, spin = 0, strength = 0.5f, db = 5; int side = 1; juce::Point<float> at; };
        struct Fx { enum Kind { crumb, note, zed, puff, ring } kind; juce::Point<float> pos, vel; float age = 0, life = 1, size = 10; };

        void update (const MascotInput& in, float dt, juce::Point<float> mouthInPicture);
        void lookAt (juce::Point<float> pictureTarget, float forSeconds);   // cursor or a turned knob, in the 1024 space
        void setHover (bool h) noexcept { hover = h; }
        void say (const juce::String& text);                                 // types into the bubble, mouth talks
        void wake() noexcept;

        RigPose pose() const;
        Mode getMode() const noexcept { return mode; }
        bool isTalking() const noexcept { return talkText.isNotEmpty(); }
        juce::String shownText() const { return talkText.substring (0, (int) talkShown); }
        const juce::String& fullText() const noexcept { return talkText; }
        const std::vector<Eat>& eats() const noexcept { return eating; }
        const std::vector<Fx>& effects() const noexcept { return fx; }
        int eatenCount() const noexcept { return eaten; }

        // springs (public for tests)
        Spring head { 0, 90, 12 }, headX { 0, 90, 13 }, headY { 0, 110, 14 }, earL { 0, 160, 7 }, earR { 0, 160, 7 },
               tail { 0, 40, 4 }, armL { 0, 70, 9 }, armR { 0, 70, 9 }, lean { 0, 60, 10 }, bob { 0, 260, 22 },
               squash { 0, 220, 24 }, lookX { 0, 200, 22 }, lookY { 0, 200, 22 }, lid { 0, 400, 30 }, lowLid { 0, 250, 25 },
               mouth { 0, 900, 42 }, mouthW { 1, 260, 22 }, pupil { 1, 200, 18 }, browY { 0, 160, 18 }, browTilt { 0, 160, 18 };

    private:
        void chomp (const Eat&);
        void talkStep (float dt);
        void pickAction();

        juce::Random rnd;
        Mode mode = Mode::idle;
        float t = 0, beat = 0, silentFor = 0, sleepy = 0, hot = 0, proudFor = 0, lookFor = 0, chompT = -99, lastS = -99;
        float nextBlink = 1.5f, blinkT = -1, nextAction = 2.5f, hotSaid = -99;
        bool doubleBlink = false, asleep = false, hover = false, wasListening = false, armed = true;
        juce::Point<float> look;
        // idle action
        int action = -1; float actionT = 0, actionLen = 1; bool actionDone = false;
        // talking
        juce::String talkText; float talkShown = 0, talkT = 0, talkEnd = -1, syl = 0, sylOpen = 0, sylWide = 1; int gesture = 0; float gestureAt = 0;
        std::vector<Eat> eating;
        std::vector<Fx> fx;
        int eaten = 0;
    };

    class LiveMascotView final : public juce::Component, private juce::Timer
    {
    public:
        LiveMascotView();
        ~LiveMascotView() override;

        void setInput (const MascotInput& in) { input = in; }
        void setBubble (const juce::String& text);      // a new text: Ember says it
        void setCompact (bool c) { compact = c; }
        std::function<void()> onClick;

        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseEnter (const juce::MouseEvent&) override { brain.setHover (true); }
        void mouseExit (const juce::MouseEvent&) override { brain.setHover (false); }

        LiveBrain& getBrain() noexcept { return brain; }
        void stepForTest (float dt) { tick (dt); }
        static inline std::atomic<bool> alwaysInFront { false };   // EmberTests --bench: as if the cursor were on him

    private:
        void timerCallback() override;
        void tick (float dt);
        juce::Rectangle<float> stageArea() const;
        void paintEffects (juce::Graphics&);
        void paintBubble (juce::Graphics&);

        // Drawing the figure costs 3-15 ms a frame; on the message thread (the host's whole UI) that made the DAW
        // stutter with two or three windows open (bench 2026-11-19). The figure is drawn on a thread of its own into
        // one of two images; paint() only puts the newest finished one on the screen.
        void renderFrame (const RigPose&, juce::Rectangle<float> area, float scale);   // any thread
        void updateRate();
        struct Drawer;

        LiveBrain brain;
        RigRenderer renderer;                 // only under renderLock
        juce::CriticalSection renderLock, frameLock;
        juce::Image frames[2];                // frameLock guards which one is shown
        int front = -1;
        juce::Rectangle<float> frontArea;
        float frontScale = 0;
        struct Job { RigPose pose; juce::Rectangle<float> area; float scale = 1; bool pending = false; } job;   // frameLock
        std::atomic<double> renderMs { 0 };
        std::unique_ptr<Drawer> drawer;

        MascotInput input;
        juce::String bubble;
        juce::Point<int> lastMouse;
        double lastTick = 0, lastActive = -1.0e9;
        float screenScale = 1;                // the screen's pixel density, from the last paint
        int currentHz = 60;
        bool compact = false, counted = false;
    };
}
