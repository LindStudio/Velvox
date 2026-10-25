#include "ReverbDepth.h"

#include <algorithm>
#include <cmath>

namespace velvox::dsp
{
    namespace
    {
        // Per room type (ReverbSettings::type order): the vocal in front (depth 0) and far back (depth 1).
        // In front: little reverb, a long pre-delay keeps the vocal's attack clear of the tail, a small
        // room. Far back: a lot of reverb, almost no gap (the vocal sits inside the space), the room at
        // its largest. Long, dark characters go further (more mix, longer gaps); small rooms stay tighter.
        struct Ends
        {
            float mix0, mix1, pre0, pre1, size0, size1;
        };

        constexpr Ends ends[8] {
            { 3.0f, 42.0f, 35.0f,  5.0f, 25.0f,  80.0f },   // Room
            { 4.0f, 45.0f, 60.0f, 10.0f, 35.0f,  85.0f },   // Plate
            { 3.0f, 48.0f, 70.0f,  8.0f, 45.0f,  95.0f },   // Hall
            { 3.0f, 50.0f, 80.0f, 10.0f, 50.0f, 100.0f },   // Large Hall
            { 4.0f, 40.0f, 30.0f,  2.0f, 20.0f,  80.0f },   // Ambience
            { 2.0f, 55.0f, 90.0f, 12.0f, 55.0f, 100.0f },   // Atmosphere
            { 2.0f, 55.0f, 90.0f, 15.0f, 55.0f, 100.0f },   // Cinematic
            { 3.0f, 60.0f, 75.0f, 10.0f, 50.0f, 100.0f },   // Ethereal
        };

        const Ends& endsFor (int type) noexcept
        {
            return ends[std::clamp (type, 0, 7)];
        }
    }

    ReverbDepthPoint reverbDepthPath (int type, float depth) noexcept
    {
        const auto& e = endsFor (type);
        const float d = std::isfinite (depth) ? std::clamp (depth, 0.0f, 1.0f) : 0.0f;

        // Mix eases in (fine steps near the front, where small changes are most audible); the gap
        // closes a little faster than linear; the room grows along a smoothstep (settles at both ends)
        const float mixCurve = std::pow (d, 1.6f);
        const float preCurve = 1.0f - std::pow (1.0f - d, 1.2f);
        const float sizeCurve = d * d * (3.0f - 2.0f * d);

        ReverbDepthPoint p;
        p.mixPercent = e.mix0 + (e.mix1 - e.mix0) * mixCurve;
        p.preDelayMs = e.pre0 + (e.pre1 - e.pre0) * preCurve;
        p.sizePercent = e.size0 + (e.size1 - e.size0) * sizeCurve;
        return p;
    }

    float reverbDepthFromSettings (int type, float mixPercent, float preDelayMs, float sizePercent) noexcept
    {
        const auto& e = endsFor (type);
        const auto norm = [] (float v, float a, float b) { return std::isfinite (v) ? (v - a) / (b - a) : 0.0f; };
        const float m = norm (mixPercent, e.mix0, e.mix1), p = norm (preDelayMs, e.pre0, e.pre1), s = norm (sizePercent, e.size0, e.size1);

        // Closest point on the path; the mix weighs most because it is what moves the vocal back audibly
        constexpr int steps = 400;
        float best = 0.0f, bestCost = 1.0e30f;
        for (int i = 0; i <= steps; ++i)
        {
            const float d = (float) i / steps;
            const auto q = reverbDepthPath (type, d);
            const float dm = norm (q.mixPercent, e.mix0, e.mix1) - m;
            const float dp = norm (q.preDelayMs, e.pre0, e.pre1) - p;
            const float ds = norm (q.sizePercent, e.size0, e.size1) - s;
            const float cost = dm * dm + 0.25f * (dp * dp + ds * ds);
            if (cost < bestCost)
            {
                bestCost = cost;
                best = d;
            }
        }
        return best;
    }
}
