#pragma once

#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace velvox::dsp
{
    // Mono delay line (power-of-two ring) with 6-point (5th-order) Lagrange reads and a
    // correlation search for splice points. The newest sample is the one last written.
    class LagrangeDelayLine
    {
    public:
        void setSize (int minSamples)
        {
            int size = 1;
            while (size < minSamples)
                size <<= 1;
            buffer.assign ((size_t) size, 0.0f);
            mask = size - 1;
            writePos = 0;
        }

        void clear() noexcept
        {
            std::fill (buffer.begin(), buffer.end(), 0.0f);
            writePos = 0;
        }

        void write (float x) noexcept    { buffer[(size_t) writePos] = x; }
        void advance() noexcept          { writePos = (writePos + 1) & mask; }
        int size() const noexcept        { return (int) buffer.size(); }

        // `delay` samples behind the newest sample (clamped to the line; >= 3 for the Lagrange span)
        float read (double delay) const noexcept
        {
            const double d = juce::jlimit (3.0, (double) buffer.size() - 8.0, delay);
            const double pos = (double) writePos - d + (double) buffer.size();
            const int i = (int) pos;
            const double t = pos - (double) i;
            const double a = t + 2.0, b = t + 1.0, c = t, e = t - 1.0, f = t - 2.0, g = t - 3.0;
            const double ab = a * b, fg = f * g;
            const float* x = buffer.data();
            const double y = - (b * c * e * fg) / 120.0 * x[(i - 2) & mask]
                             + (a * c * e * fg) / 24.0 * x[(i - 1) & mask]
                             - (ab * e * fg) / 12.0 * x[i & mask]
                             + (ab * c * fg) / 12.0 * x[(i + 1) & mask]
                             - (ab * c * e * g) / 24.0 * x[(i + 2) & mask]
                             + (ab * c * e * f) / 120.0 * x[(i + 3) & mask];
            return (float) y;
        }

        // Jump J in [minJ, maxJ] (samples) whose tap at delayA + direction * J has the most similar
        // recent past (normalised cross-correlation over `length` samples) to the tap at delayA:
        // on a voiced note a whole number of pitch periods, so a crossfade joins two copies in phase.
        // Coarse search every `step` samples, then refined to one sample.
        double findJump (double delayA, double direction, double minJ, double maxJ, int length, int step) const noexcept
        {
            const int ia = writePos - (int) std::lround (delayA);
            const auto at = [this] (int i) { return (double) buffer[(size_t) (i & mask)]; };
            const auto score = [&] (int jump, int kStep, double& energyA)
            {
                const int ib = ia - (int) direction * jump;
                double c = 0.0, eb = 0.0, ea = 0.0;
                for (int k = 0; k < length; k += kStep)
                {
                    const double xa = at (ia - k), xb = at (ib - k);
                    c += xa * xb;
                    eb += xb * xb;
                    ea += xa * xa;
                }
                energyA = ea;
                return c / std::sqrt (ea * eb + 1.0e-24);
            };

            const int lo = juce::jmax (1, (int) std::ceil (minJ)), hi = juce::jmax (lo, (int) std::floor (maxJ));
            step = juce::jmax (1, step);
            double energy = 0.0, best = -2.0;
            int bestJ = (lo + hi) / 2;
            for (int j = lo; j <= hi; j += step)
            {
                const double sc = score (j, step, energy);
                if (sc > best) { best = sc; bestJ = j; }
            }
            if (energy < 1.0e-12)
                return (double) ((lo + hi) / 2);   // silence: any jump is inaudible

            int fine = bestJ;
            best = -2.0;
            for (int j = juce::jmax (lo, bestJ - step + 1); j <= juce::jmin (hi, bestJ + step - 1); ++j)
            {
                const double sc = score (j, 1, energy);
                if (sc > best) { best = sc; fine = j; }
            }
            return (double) fine;
        }

    private:
        std::vector<float> buffer;
        int mask = 0;
        int writePos = 0;
    };

    // Low-artefact detune for small intervals (a few to ~40 cents): one clean tap that drifts at
    // (1 - ratio) samples per sample, spliced back by a correlation-aligned raised-cosine crossfade
    // when its offset reaches the turning point. The offset sweeps symmetrically (+L -> -L, then a
    // new landing L' in [minLanding, shiftLimit]), so the tap's delay averages `base` over every
    // cycle. Formants move with the detune only (< 2.5 % at 40 ct).
    struct VoiceSpliceConfig
    {
        int fadeLength = 1440;       // 30 ms
        double shiftLimit = 576.0;   // 12 ms
        double minLanding = 96.0;    // 2 ms
        double minDelay = 48.0;      // 1 ms
        int corrLength = 768;        // 16 ms
        int coarseStep = 4;

        static VoiceSpliceConfig forRate (double fs) noexcept
        {
            VoiceSpliceConfig c;
            c.fadeLength = juce::jmax (16, (int) std::lround (0.030 * fs));
            c.shiftLimit = 0.012 * fs;
            c.minLanding = 0.002 * fs;
            c.minDelay = 0.001 * fs;
            c.corrLength = juce::jmax (64, (int) std::lround (0.016 * fs));
            c.coarseStep = juce::jmax (1, (int) std::lround (4.0 * fs / 48000.0));
            return c;
        }

        // Extra line length a shifter needs beyond the largest base delay
        double headroom() const noexcept { return 3.0 * shiftLimit + corrLength + 16.0; }
    };

    struct VoiceSpliceShifter
    {
        double shift = 0.0;       // offset of the playing tap (samples)
        double fadeShift = 0.0;   // offset of the tap that fades out
        double turn = 0.0;        // |offset| where the sweep turns (= its last landing point)
        int fadeLeft = 0;         // samples left of the crossfade (0 = one tap)

        void reset() noexcept { shift = fadeShift = turn = 0.0; fadeLeft = 0; }

        // Offset of the voice's delay now (for display)
        double offset() const noexcept { return shift; }

        // One sample at delay `base` (samples, >= cfg.minDelay); drift = 1 - pitch ratio
        float process (const LagrangeDelayLine& line, double base, double drift, const VoiceSpliceConfig& cfg) noexcept
        {
            if (drift != 0.0 || fadeLeft > 0)
            {
                shift += drift;
                if (fadeLeft > 0)
                    fadeShift += drift;
                else
                {
                    const double floorShift = cfg.minDelay - base + cfg.fadeLength * std::abs (drift) + 4.0;
                    if (drift < 0.0 && shift <= std::max (-turn, floorShift))
                    {
                        const double jLo = std::max (1.0, cfg.minLanding - shift), jHi = std::max (jLo, cfg.shiftLimit - shift);
                        fadeShift = shift;
                        shift += line.findJump (base + shift, 1.0, jLo, jHi, cfg.corrLength, cfg.coarseStep);
                        turn = shift;
                        fadeLeft = cfg.fadeLength;
                    }
                    else if (drift > 0.0 && shift >= turn)
                    {
                        // the landing (shift - J) must stay above the shortest delay
                        const double room = shift - floorShift;
                        const double jHi = std::max (1.0, std::min (shift + cfg.shiftLimit, room));
                        const double jLo = std::min (jHi, std::max (1.0, shift + cfg.minLanding));
                        fadeShift = shift;
                        shift -= line.findJump (base + shift, -1.0, jLo, jHi, cfg.corrLength, cfg.coarseStep);
                        turn = -shift;
                        fadeLeft = cfg.fadeLength;
                    }
                }
            }

            float y = line.read (base + shift);
            if (fadeLeft > 0)
            {
                const double t = 1.0 - (double) fadeLeft / (double) cfg.fadeLength;
                const float gNew = (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * t));
                y = gNew * y + (1.0f - gNew) * line.read (base + fadeShift);
                --fadeLeft;
            }
            return y;
        }
    };

    // Smooth random walk: raised-cosine moves between random targets of alternating sign; a short
    // move is also a small one, so no move is steeper than the sine of the same mean rate.
    struct SmoothRandomWalk
    {
        juce::Random rng;
        double from = 0.0, to = 0.0, pos = 0.0, len = 1.0;

        void reset (juce::int64 seed, bool startUp, double meanLength) noexcept
        {
            rng.setSeed (seed);
            from = 0.0;
            to = (startUp ? 1.0 : -1.0) * (0.5 + 0.5 * rng.nextDouble());
            pos = 0.0;
            len = std::max (1.0, meanLength);
        }

        // -1 .. +1
        double value() const noexcept
        {
            return from + (to - from) * (0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * pos / len));
        }

        void advance (double samples, double meanLength) noexcept
        {
            pos += samples;
            while (pos >= len)
            {
                pos -= len;
                from = to;
                const double stretch = 0.5 + rng.nextDouble();
                const double magnitude = std::max (0.1, std::min (0.5 + 0.5 * rng.nextDouble(), 2.0 * stretch - std::abs (from)));
                to = from > 0.0 ? -magnitude : magnitude;
                len = std::max (1.0, meanLength * stretch);
            }
        }
    };
}
