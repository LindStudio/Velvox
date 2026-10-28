#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>

// Vocalypse mascot Memphy as a comic sticker (Jakob 2026-10-28: no 3D animation in the plugin, one comic
// picture per mood; the animated figure lives on the website). Pure logic, no images - the view
// (MascotView) draws the sticker this says, with a small pop, breathing, a talking bounce and a hop.
//
//   idle                 listening (hand behind his ear)
//   auto mix analysing   thinking; no signal for 2 s while it waits: annoyed
//   mix just finished    proud (thumbs up) for a few seconds
//   singing              by the Vocal Check score: < badBelow sad, >= goodFrom wink, >= greatFrom celebrate,
//                        in between and without a score he listens
//   talking              talking (the view's speech lines)
//   tour                 playIntro(): shocked (pops in), then wink; playBow(): proud
//
// A new mood waits until the current one has shown minHold seconds and scores near a border keep the
// current mood (hysteresis), so he does not flicker; a scene (tour) plays its moods to their end.
namespace velvox::ui::mascot
{
    enum class Mood { listening, talking, wink, celebrate, sad, shocked, thinking, proud, annoyed };
    constexpr int numMoods = 9;

    struct Inputs
    {
        float inputDb = -100.0f;     // input level of this frame
        int score = -1;              // Vocal Check 0..100, -1 = not enough material
        bool autoMixBusy = false;    // listening / capturing / analysing
        bool mixFinished = false;    // a mix has just been applied (true for one update)
        bool talking = false;        // a speech line is being said
    };

    class Brain
    {
    public:
        static constexpr double minHold = 1.5;          // s a mood shows before another may take over
        static constexpr double silenceToIdle = 2.0;    // s of silence before he stops reacting to the singing
        static constexpr double proudFor = 4.0;         // s of thumbs up after a mix
        static constexpr double popTime = 0.6;          // s of the pop when the mood changes (a damped spring)
        static constexpr double fadeTime = 0.2;         // s cross-fade between two stickers
        static constexpr double blinkTime = 0.16;       // s an eye blink takes (closing and opening)
        static constexpr float signalDb = -55.0f;
        static constexpr int badBelow = 45, goodFrom = 75, greatFrom = 90;
        static constexpr int hysteresis = 3;            // score points around the borders

        explicit Brain (unsigned seed = 1) : rng (seed == 0 ? 1u : seed) { blinkIn = 1.5 + 3.0 * unit(); }

        // Tour: pops in shocked, then winks; at the end the thumbs up
        void playIntro() { scene = { { Mood::shocked, 1.4 }, { Mood::wink, 2.2 } }; nextScene(); }
        void playBow() { scene = { { Mood::proud, 3.5 } }; nextScene(); }
        bool sceneRunning() const noexcept { return sceneLeft > 0.0 || ! scene.empty(); }
        void stopScene() noexcept { scene.clear(); sceneLeft = 0.0; }   // the tour moved on: back to the usual moods
        static constexpr double noBlinkAfterSwitch = 0.5;   // s: a blink in the cross-fade looked like a flicker

        void update (const Inputs& in, double dt)
        {
            silent = in.inputDb > signalDb ? 0.0 : silent + dt;
            time += dt;
            clock += dt;
            proudLeft = in.mixFinished ? proudFor : std::max (0.0, proudLeft - dt);
            updateTalk (in.talking, dt);
            updateBlink (dt);

            if (sceneLeft > 0.0)
            {
                sceneLeft -= dt;
                if (sceneLeft > 0.0)
                    return;
                if (! scene.empty())
                {
                    nextScene();
                    return;
                }
            }
            const Mood want = desired (in);
            if (want != now && time >= minHold)
                switchTo (want);
        }

        Mood mood() const noexcept { return now; }
        // The sticker that is fading out (when a cross-fade runs) and how much of it still shows (1 -> 0)
        Mood fadeFrom() const noexcept { return from; }
        float fadeAmount() const noexcept { return time < fadeTime ? (float) (1.0 - time / fadeTime) : 0.0f; }

        // Motion of the sticker, for the view: scale around his feet and a lift in sticker heights.
        // Everything is a smooth curve (Jakob 2026-10-28: "smoother"): the pop is a damped spring, the talking
        // bounce a smoothed envelope, the hop a sine squared.
        float scale() const noexcept
        {
            float s = 1.0f + 0.012f * (float) std::sin (clock * 2.0 * pi / 3.2);   // breathing
            if (time < popTime)                                                    // 0.88, a soft overshoot, settles
                s *= (float) (1.0 - 0.12 * std::exp (-time * 9.0) * std::cos (time * 16.0));
            return s * (1.0f + 0.022f * talkBounce);
        }
        float lift() const noexcept
        {
            if (now == Mood::celebrate)   // hops
            {
                const double h = std::sin (clock * pi * 1.5);
                return 0.055f * (float) (h * h);
            }
            return 0.01f * talkBounce;
        }

        // Eye blink: 0 = open, 1 = closed (the view draws the lids)
        float blink() const noexcept { return blinkT < blinkTime ? (float) std::sin (pi * blinkT / blinkTime) : 0.0f; }

    private:
        Mood desired (const Inputs& in)
        {
            if (in.talking)
                return Mood::talking;
            if (in.autoMixBusy)
                return silent > silenceToIdle ? Mood::annoyed : Mood::thinking;
            if (proudLeft > 0.0)
                return Mood::proud;
            if (silent > 0.0)   // a pause between phrases keeps the reaction, a long one ends it
                return silent > silenceToIdle || ! reacting (now) ? Mood::listening : now;
            return quality (in.score);
        }

        static bool reacting (Mood m) noexcept { return m == Mood::sad || m == Mood::wink || m == Mood::celebrate; }

        // The mood for a score, keeping the current one near its borders
        Mood quality (int score) const noexcept
        {
            if (score < 0)
                return Mood::listening;
            struct Band { Mood mood; int lo, hi; };
            static constexpr Band bands[] { { Mood::sad, 0, badBelow }, { Mood::listening, badBelow, goodFrom },
                                            { Mood::wink, goodFrom, greatFrom }, { Mood::celebrate, greatFrom, 101 } };
            for (const auto& b : bands)
                if (b.mood == now && score >= b.lo - hysteresis && score < b.hi + hysteresis)
                    return now;
            for (const auto& b : bands)
                if (score >= b.lo && score < b.hi)
                    return b.mood;
            return Mood::celebrate;
        }

        void nextScene()
        {
            const auto [m, seconds] = scene.front();
            scene.pop_front();
            switchTo (m);
            sceneLeft = seconds;
        }

        void switchTo (Mood m)
        {
            from = now;
            now = m;
            time = 0.0;
            // A blink that is under way ends, the next one waits until the new sticker has settled
            blinkT = 10.0;
            blinkIn = std::max (blinkIn, noBlinkAfterSwitch);
        }

        // Talking: a nod every 0.35 - 0.6 s, like the stress of a sentence (every syllable looked like fast
        // jumping, Jakob 2026-10-28); the bounce follows it smoothly
        void updateTalk (bool talking, double dt)
        {
            if (talking)
            {
                syllable -= dt;
                if (syllable <= 0.0)
                {
                    syllable = 0.35 + 0.25 * unit();
                    talkTarget = 1.0f;
                }
            }
            talkTarget = std::max (0.0f, talkTarget - (float) dt * 3.0f);
            talkBounce += (talkTarget - talkBounce) * (float) std::min (1.0, dt / 0.09);
        }

        // A blink every 2 - 6 s, now and then a double blink
        void updateBlink (double dt)
        {
            blinkT += dt;
            blinkIn -= dt;
            if (blinkIn > 0.0)
                return;
            if (time + dt < noBlinkAfterSwitch)   // the mood changed just now (time is updated after this)
            {
                blinkIn = noBlinkAfterSwitch - time;
                return;
            }
            blinkT = 0.0;
            blinkIn = unit() < 0.2 ? blinkTime + 0.12 : 2.0 + 4.0 * unit();
        }

        static constexpr double pi = 3.14159265358979;

        double unit() noexcept
        {
            rng = rng * 1664525u + 1013904223u;
            return ((rng >> 8) & 0xffff) / 65535.0;
        }

        unsigned rng;
        Mood now = Mood::listening, from = Mood::listening;
        std::deque<std::pair<Mood, double>> scene;
        double sceneLeft = 0.0;
        double time = 10.0, clock = 0.0, silent = 1.0e9, proudLeft = 0.0, syllable = 0.0;   // silent until the first signal
        double blinkT = 10.0, blinkIn = 3.0;
        float talkBounce = 0.0f, talkTarget = 0.0f;
    };
}
