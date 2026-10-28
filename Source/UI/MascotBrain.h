#pragma once

#include <algorithm>
#include <array>
#include <deque>
#include <optional>

// Vocalypse mascot Memphy (the Flame Hoodie Imp, Jakob 2026-10-28): which animation plays and which mouth
// he shows. Pure logic, no images - the view (MascotView) draws what this says.
//
//   idle                Listening Gesture; now and then, at random times, "Relax arms" (once)
//   auto mix listening  idle (he listens too)
//   singing             by the Vocal Check score: < badBelow the depressed full turn, >= goodFrom hip hop or
//                       breakdance (he switches between them), in between and without a score he listens
//   silence > 2 s       idle
//   tour                playIntro(): falls in (Fall4), waves (Big Wave Hello); playBow(): Gentleman's Bow
//
// A new state waits until the current one has played minHold seconds (one-shots play to their end),
// scores near a border keep the current state (hysteresis), and every switch cross-fades from the
// last frame of the old clip - except the fall, which starts from nothing. Loops wrap straight: where the
// packer cut one, it blended the end into the start (a fade over a frozen frame showed as a hitch).
// The mouth moves only while he talks (the view's speech lines).
namespace velvox::ui::mascot
{
    enum class Clip { fall, wave, bow, idle, relax, bad, hipHop, breakdance };
    constexpr int numClips = 8;

    enum class Mouth { closed, half, open };

    struct ClipInfo
    {
        int frames = 1;
        double fps = 30.0;
        bool loops = true;
    };

    struct Inputs
    {
        float inputDb = -100.0f;     // input level of this frame
        int score = -1;              // Vocal Check 0..100, -1 = not enough material
        bool autoMixBusy = false;    // listening / capturing / analysing
        bool talking = false;        // a speech line is being said
    };

    struct FrameRef
    {
        Clip clip;
        int frame;
    };

    class Brain
    {
    public:
        static constexpr double minHold = 2.5;          // s a state plays before another may take over
        static constexpr double silenceToIdle = 2.0;    // s of silence before he stops reacting to the singing
        static constexpr double fadeTime = 0.25;        // s cross-fade between clips
        static constexpr double danceSwap = 8.0;        // s of one dance before he may switch to the other
        static constexpr double relaxMinGap = 15.0, relaxMaxGap = 40.0;   // s of listening between two relaxes
        static constexpr float signalDb = -55.0f;
        static constexpr int badBelow = 45, goodFrom = 75;
        static constexpr int hysteresis = 3;            // score points around the borders

        explicit Brain (const std::array<ClipInfo, numClips>& clipInfo, unsigned seed = 1)
            : info (clipInfo), rng (seed == 0 ? 1u : seed)
        {
            relaxIn = nextRelaxGap();
        }

        // Tour: falls in and waves (then listens); bows
        void playIntro()
        {
            scene = { Clip::fall, Clip::wave };
            switchTo (popScene(), false);   // he was not there: no cross-fade from the old figure
        }

        void playBow()
        {
            scene = { Clip::bow };
            switchTo (popScene(), true);
        }

        // A tour scene (fall, wave, bow) is playing or queued
        bool sceneRunning() const noexcept { return inScene || ! scene.empty(); }

        void update (const Inputs& in, double dt)
        {
            updateMouth (in.talking, dt);
            silent = in.inputDb > signalDb ? 0.0 : silent + dt;
            time += dt;
            if (fading)
                fadeElapsed += dt;
            if (fading && fadeElapsed >= fadeTime)
                fading = false;

            if (! current().loops && ! finished())
                return;   // one-shots play to their end
            if (! scene.empty())
            {
                switchTo (popScene(), true);
                return;
            }
            inScene = false;

            Clip want = desired (in);
            if (want == Clip::idle && now == Clip::idle)
            {
                relaxIn -= dt;
                if (relaxIn <= 0.0)
                {
                    relaxIn = nextRelaxGap();
                    want = Clip::relax;
                }
            }
            const bool mayLeave = ! current().loops || time >= minHold;
            if (want != now && mayLeave)
                switchTo (want, true);
        }

        Clip clip() const noexcept { return now; }

        int frame() const noexcept { return frameAt (now, time); }

        Mouth mouth() const noexcept { return mouthNow; }

        // The clip that is fading out (nullopt when no cross-fade runs) and how much of it is still visible (1 -> 0)
        std::optional<FrameRef> fadeFrom() const noexcept { return fading ? std::optional<FrameRef> (from) : std::nullopt; }
        float fadeAmount() const noexcept { return fading ? (float) (1.0 - fadeElapsed / fadeTime) : 0.0f; }

    private:
        const ClipInfo& current() const noexcept { return info[(size_t) now]; }

        bool finished() const noexcept { return time * current().fps >= current().frames - 1e-6; }

        int frameAt (Clip c, double t) const noexcept
        {
            const auto& ci = info[(size_t) c];
            const int f = (int) (t * ci.fps + 1e-6);
            return ci.loops ? f % ci.frames : std::min (f, ci.frames - 1);
        }

        static bool dancing (Clip c) noexcept { return c == Clip::hipHop || c == Clip::breakdance; }
        static bool reacting (Clip c) noexcept { return dancing (c) || c == Clip::bad; }

        Clip desired (const Inputs& in)
        {
            if (in.autoMixBusy)
                return Clip::idle;
            if (silent > 0.0)   // a pause between phrases keeps the reaction, a long one ends it
                return silent > silenceToIdle || ! reacting (now) ? Clip::idle : now;
            const Clip mood = quality (in.score);
            if (mood != Clip::hipHop)
                return mood;
            if (dancing (now))   // keeps his dance, now and then the other one
                return time >= danceSwap ? (now == Clip::hipHop ? Clip::breakdance : Clip::hipHop) : now;
            return random() % 2 == 0 ? Clip::hipHop : Clip::breakdance;
        }

        // bad / idle / a dance (hipHop stands for both) for a score, keeping the current state near its borders
        Clip quality (int score) const noexcept
        {
            if (score < 0)
                return Clip::idle;
            const Clip held = dancing (now) ? Clip::hipHop : now;
            if (held == Clip::bad && score < badBelow + hysteresis)
                return Clip::bad;
            if (held == Clip::hipHop && score >= goodFrom - hysteresis)
                return Clip::hipHop;
            if (held == Clip::idle && score >= badBelow - hysteresis && score < goodFrom + hysteresis)
                return Clip::idle;
            return score < badBelow ? Clip::bad : score >= goodFrom ? Clip::hipHop : Clip::idle;
        }

        Clip popScene()
        {
            const Clip c = scene.front();
            scene.pop_front();
            inScene = true;
            return c;
        }

        void switchTo (Clip c, bool fade)
        {
            if (fade)
                startFade ({ now, frame() });
            else
                fading = false;
            now = c;
            time = 0.0;
        }

        void startFade (FrameRef old)
        {
            from = old;
            fading = true;
            fadeElapsed = 0.0;
        }

        // Talking: a new mouth every 70 - 160 ms (closed between syllables now and then)
        void updateMouth (bool talking, double dt)
        {
            if (! talking)
            {
                mouthNow = Mouth::closed;
                syllable = 0.0;
                return;
            }
            syllable -= dt;
            if (syllable > 0.0)
                return;
            syllable = 0.07 + 0.09 * unit();
            const auto r = random() % 10;
            const Mouth next = r < 3 ? Mouth::closed : r < 6 ? Mouth::half : Mouth::open;
            mouthNow = next == mouthNow && next != Mouth::half ? Mouth::half : next;
        }

        double nextRelaxGap() { return relaxMinGap + (relaxMaxGap - relaxMinGap) * unit(); }

        unsigned random() noexcept
        {
            rng = rng * 1664525u + 1013904223u;
            return rng >> 8;
        }
        double unit() noexcept { return (random() & 0xffff) / 65535.0; }

        std::array<ClipInfo, numClips> info;
        unsigned rng;
        Clip now = Clip::idle;
        std::deque<Clip> scene;
        bool inScene = false;
        double time = 0.0, silent = 1.0e9, fadeElapsed = 0.0, relaxIn = 0.0, syllable = 0.0;   // silent until the first signal
        bool fading = false;
        FrameRef from { Clip::idle, 0 };
        Mouth mouthNow = Mouth::closed;
    };
}
