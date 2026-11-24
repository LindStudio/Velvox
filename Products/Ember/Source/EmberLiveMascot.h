#pragma once

#include "EmberMascot.h"
#include "EmberRig.h"
#include "EmberSeal.h"

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
        void say (const juce::String& text);                                 // types onto the text plate, mouth talks
        void wake() noexcept;
        void stopTalking() noexcept { talkText.clear(); }
        void setFewerParticles (bool f) noexcept { fewer = f; }              // animation "reduced": fewer crumbs

        RigPose pose() const;
        Mode getMode() const noexcept { return mode; }
        bool isTalking() const noexcept { return talkText.isNotEmpty(); }
        juce::String shownText() const { return talkText.substring (0, (int) talkShown); }
        const juce::String& fullText() const noexcept { return talkText; }
        const std::vector<Eat>& eats() const noexcept { return eating; }
        const std::vector<Fx>& effects() const noexcept { return fx; }
        int eatenCount() const noexcept { return eaten; }
        void seedForTest (juce::int64 seed) { rnd.setSeed (seed); }   // EmberTests --frames: the same frames every run

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
        bool doubleBlink = false, asleep = false, hover = false, wasListening = false, armed = true, fewer = false;
        juce::Point<float> look;
        // idle action
        int action = -1; float actionT = 0, actionLen = 1; bool actionDone = false;
        // talking
        juce::String talkText; float talkRate = 15.0f, talkShown = 0, talkT = 0, talkEnd = -1, syl = 0, sylOpen = 0, sylWide = 1; int gesture = 0; float gestureAt = 0;
        std::vector<Eat> eating;
        std::vector<Fx> fx;
        int eaten = 0;
    };

    // How much moves (≡ menu, a user setting for all windows; 2026-11-23): full = as designed (60 / 30 fps);
    // reduced = 30 fps, the seal's letters, the circle and the border stand still, fewer crumbs; off = Ember stands in
    // his rest pose on a still seal, drawn again only when something he shows changes (bypass, text plate, size).
    enum class AnimationLevel { full = 0, reduced = 1, off = 2 };
    AnimationLevel animationLevelFromString (const juce::String&);
    juce::String toString (AnimationLevel);

    class LiveMascotView final : public juce::Component, private juce::Timer
    {
    public:
        LiveMascotView();
        ~LiveMascotView() override;

        static void setAnimationLevel (AnimationLevel l) noexcept { animationLevel = (int) l; }
        static AnimationLevel getAnimationLevel() noexcept { return (AnimationLevel) animationLevel.load(); }
        // Something covers him completely (the licence card): he pauses and the last picture stays
        void setCovered (bool c) noexcept { covered = c; }

        void setInput (const MascotInput& in) { input = in; }
        // A new text: Ember says it, typed onto the text plate above him (animation off: it just shows). Empty: the
        // plate fades out. The plate hangs fixed between the chains (2026-11-24: no speech bubble on him any more).
        void setBubble (const juce::String& text);
        juce::Rectangle<float> plateArea() const;      // where the text plate hangs (this view's coordinates)
        juce::String plateTextForTest() const { return plateText; }
        float plateAlphaForTest() const noexcept { return plateAlpha; }
        void setCompact (bool c) { compact = c; }
        // The pink seal stage (2026-11-19): Ember stands on the summoning circle, its letters float round him.
        // Off = the plain stage (tests, old look).
        void setSealStage (bool on);
        float secondsSinceSwallow() const noexcept { return sinceSwallow; }   // the window's wave of light follows this
        // The wall behind the stage (the window's background cut to this view, in screen pixels at that scale). With
        // it the view paints every pixel itself (opaque), so the window does not redraw underneath 60 times a second.
        void setBackdrop (const juce::Image& wall, float scale);
        std::function<void()> onClick;

        void paint (juce::Graphics&) override;
        // Only a click on Ember himself starts AUTO (not the empty stage around him); a double-click counts once
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        bool hitsFigure (juce::Point<float>) const;   // on his drawn pixels (a few px around them count)

        LiveBrain& getBrain() noexcept { return brain; }
        void stepForTest (float dt) { tick (dt); }
        static inline std::atomic<bool> alwaysInFront { false };   // EmberTests --bench: as if the cursor were on him
        static inline std::atomic<int> fixedHzForTest { 0 };        // EmberTests --bench hz=: this frame rate, whatever happens
        // EmberTests --bench: where a frame's time goes (ms, summed over all frames): figure, seal floor, runes + braziers, the rest
        static inline std::atomic<double> benchFigureMs { 0 }, benchFloorMs { 0 }, benchRunesMs { 0 }, benchComposeMs { 0 };
        static inline std::atomic<int> benchFrames { 0 };

    private:
        void timerCallback() override;
        void tick (float dt);
        juce::Rectangle<float> stageArea() const;
        juce::AffineTransform toStage() const;   // picture -> view, with this stage's fit
        seal::Scene scene() const;
        void paintEffects (juce::Graphics&);
        void paintPlate (juce::Graphics&);

        // Drawing the figure costs 3-15 ms a frame; on the message thread (the host's whole UI) that made the DAW
        // stutter with two or three windows open (bench 2026-11-19). The figure is drawn on a thread of its own into
        // one of two images; paint() only puts the newest finished one on the screen.
        void renderFrame (const RigPose&, juce::Rectangle<float> bounds, float scale, const seal::Scene&);   // any thread
        void updateRate (AnimationLevel);
        bool hostWindowHidden() const;   // the host's window minimised or hidden (the editor itself still "shows")
        void postFrame (const RigPose&, const seal::Scene&);
        struct Drawer;
        static inline std::atomic<int> animationLevel { 0 };
        static constexpr float maxRenderScale = 2.0f;   // beyond 2x screens: drawn at 2x and scaled (same look, far less work)

        std::shared_ptr<const MascotRig> rig = MascotRig::acquire();   // shared by all windows, freed with the last
        LiveBrain brain;
        RigRenderer renderer;                 // only under renderLock
        mutable juce::CriticalSection renderLock, frameLock;
        juce::Image frames[2];                // frameLock guards which one is shown
        int front = -1, frameSerial = 0;      // frameSerial counts finished frames (frameLock)
        juce::Image screen;                   // message thread: the newest frame as the screen's own image (uploaded
        int shownSerial = -1;                 // in place, not made anew each frame); the frame it holds
        juce::Image pendingBackdrop;          // frameLock: a new wall for the drawing thread (setBackdrop never waits)
        bool backdropPending = false;
        juce::Image paintBackdrop;            // message thread: shown until the first frame is ready
        juce::Rectangle<float> frontArea;     // the view's bounds the frame was drawn for
        float frontScale = 0;
        struct Job { RigPose pose; juce::Rectangle<float> area; float scale = 1; seal::Scene scene; bool pending = false; } job;   // frameLock
        juce::Image figure;                   // the figure alone (drawing thread / renderLock), composed into a frame
        juce::Image floorLayer;               // the seal on the floor, renewed every fourth frame (it turns slowly);
        juce::Point<int> floorAt;             // only the part where the floor is drawn, at this pixel of the frame
        RigPose figurePose;                   // what the figure image shows (drawn again only when it changes)
        juce::Rectangle<float> figureArea;
        float figureScale = 0;
        juce::Image backdrop;                 // renderLock
        float backdropScale = 0.0f;
        seal::RuneAtlas runeAtlas { seal::pinkLight };
        int frameCount = 0;
        std::atomic<double> renderMs { 0 };
        std::unique_ptr<Drawer> drawer;

        MascotInput input;
        juce::String bubble;
        juce::Point<int> lastMouse;
        double lastTick = 0, lastActive = -1.0e9;
        float screenScale = 0;                // the screen's pixel density, from the last paint (0 = not painted yet)
        float renderScale = 0, wantedScale = 0;   // frames are drawn at renderScale; a new density waits until the
        double wantedSince = 0;                   // window has stopped resizing (paint stretches the last frame)
        juce::Path sGlyph;                    // the flying S at 100 px (made once, scaled per frame)
        // the text plate: its text laid out once per text (typing shows the first glyphs of it, so nothing moves),
        // the text it shows (kept while it fades out) and how much it shows (0..1)
        struct PlateLayout { juce::String text; juce::Rectangle<float> area; juce::GlyphArrangement glyphs; } plateLayout;
        juce::String plateText;
        std::map<juce::String, double> recentlySaid;   // what he said and when (clock): the same text again is not retyped
        float plateAlpha = 0.0f;
        bool covered = false;
        AnimationLevel shownLevel = AnimationLevel::full;
        juce::String stillKey;                // animation off: what the still picture shows
        bool sealStage = false;
        int lastEaten = 0;
        float sinceSwallow = 99.0f, heat = 0.0f;
        double clock = 0;
        int currentHz = 60;
        bool compact = false, counted = false;
    };
}
