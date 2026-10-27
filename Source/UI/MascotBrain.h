#pragma once

#include <algorithm>
#include <array>
#include <optional>

// Vocalypse mascot (the 3D devil, docs/design/VOCALYPSE_NAME_UND_MASKOTTCHEN.md): which animation
// plays and which mouth he shows. Pure logic, no images - the view (MascotView) draws what this says.
//
//   plugin opens        wake-up (once), then idle
//   silence > 2 s       idle (breathes, looks around)
//   auto mix listening  idle (stands and listens; running on the spot looked odd - Jakob, 2026-10-27)
//   singing             a dance picked by the Vocal Check score:
//                         < 40 bad (on the phone, ignores you) · < 60 problem (scolds) · < 75 okay
//                         < 90 good · else super · no score yet = okay
//   score >= 95         one spin jump, then super; at most once a minute
//
// A new state waits until the current one has played minHold seconds (one-shots play to their end),
// scores near a border keep the current dance (hysteresis), and every switch cross-fades from the
// last frame of the old clip (also at the wrap of a loop, where the packer may have cut it). The mouth follows the input level (closed / half / open) so he sings along.
namespace velvox::ui::mascot
{
    enum class Clip { wakeUp, idle, bad, problem, okay, good, super, spin, working, walk, call };
    constexpr int numClips = 11;

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
        static constexpr double silenceToIdle = 2.0;    // s of silence before he stops dancing
        static constexpr double fadeTime = 0.25;        // s cross-fade between clips
        static constexpr double spinCooldown = 60.0;    // s between two spin jumps
        static constexpr float signalDb = -55.0f;
        static constexpr int hysteresis = 3;            // score points around the dance borders

        explicit Brain (const std::array<ClipInfo, numClips>& clipInfo) : info (clipInfo) {}

        void update (const Inputs& in, double dt)
        {
            updateMouth (in.inputDb, dt);
            silent = in.inputDb > signalDb ? 0.0 : silent + dt;
            cooldown = std::max (0.0, cooldown - dt);
            const int before = frame();
            time += dt;
            if (fading)
                fadeElapsed += dt;
            if (fading && fadeElapsed >= fadeTime)
                fading = false;
            // The packer cuts long loops to their most seamless window: hide what's left of the seam
            if (current().loops && frame() < before)
                startFade ({ now, before });

            const bool oneShotRunning = ! current().loops && ! finished();
            if (oneShotRunning)
                return;

            const Clip want = desired (in);
            const bool mayLeave = ! current().loops || time >= minHold;
            if (want != now && mayLeave)
                switchTo (want);
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

        Clip desired (const Inputs& in)
        {
            if (in.autoMixBusy)
                return Clip::idle;
            if (silent > 0.0)   // a pause between phrases keeps the dance, a long one ends it
                return silent > silenceToIdle || ! isDance (now) ? Clip::idle : now;
            if (in.score >= 95 && cooldown <= 0.0)
                return Clip::spin;
            return quality (in.score);
        }

        static bool isDance (Clip c) noexcept
        {
            return c == Clip::bad || c == Clip::problem || c == Clip::okay || c == Clip::good || c == Clip::super || c == Clip::spin;
        }

        // The dance for a score, keeping the current dance while the score is near its borders
        Clip quality (int score) const noexcept
        {
            if (score < 0)
                return Clip::okay;
            struct Band { Clip clip; int lo, hi; };
            static constexpr Band bands[] { { Clip::bad, 0, 40 }, { Clip::problem, 40, 60 }, { Clip::okay, 60, 75 },
                                            { Clip::good, 75, 90 }, { Clip::super, 90, 101 } };
            const Clip held = now == Clip::spin ? Clip::super : now;
            for (const auto& b : bands)
                if (b.clip == held && score >= b.lo - hysteresis && score < b.hi + hysteresis)
                    return held;
            for (const auto& b : bands)
                if (score >= b.lo && score < b.hi)
                    return b.clip;
            return Clip::super;
        }

        void switchTo (Clip c)
        {
            startFade ({ now, frame() });
            now = c;
            time = 0.0;
            if (c == Clip::spin)
                cooldown = spinCooldown;
        }

        void startFade (FrameRef old)
        {
            from = old;
            fading = true;
            fadeElapsed = 0.0;
        }

        void updateMouth (float db, double dt)
        {
            // Fast attack, short release: the mouth snaps open on a syllable and closes between them
            const double tau = db > level ? 0.02 : 0.08;
            level += (float) ((db - level) * std::min (1.0, dt / tau));
            mouthNow = level < -40.0f ? Mouth::closed : level < -24.0f ? Mouth::half : Mouth::open;
        }

        std::array<ClipInfo, numClips> info;
        Clip now = Clip::wakeUp;
        double time = 0.0, silent = 1.0e9, cooldown = 0.0, fadeElapsed = 0.0;   // silent until the first signal
        bool fading = false;
        FrameRef from { Clip::wakeUp, 0 };
        float level = -100.0f;
        Mouth mouthNow = Mouth::closed;
    };
}
