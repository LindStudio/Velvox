#include "PitchTracker.h"

#include <array>
#include <cmath>

namespace velvox::dsp
{
    namespace
    {
        int nextPow2 (int n)
        {
            int p = 1;
            while (p < n) p <<= 1;
            return p;
        }
    }

    void PitchTracker::prepare (double sampleRate)
    {
        fs = sampleRate;
        decimation = std::max (1, (int) std::lround (fs / 8000.0));
        const double fsDec = fs / decimation;

        // 4th-order Butterworth at 0.4 * the decimated rate
        const double fc = 0.4 * fsDec;
        aa1.setCoeffs (design::lowPass (fs, fc, 0.5411961));
        aa2.setCoeffs (design::lowPass (fs, fc, 1.3065630));

        window = (int) std::lround (0.0213 * fsDec);           // ~21 ms
        tauMin = std::max (2, (int) std::floor (fsDec / 1100.0)); // f0 <= 1100 Hz
        tauMax = (int) std::ceil (fsDec / 65.0);                // f0 >= 65 Hz
        hop = std::max (1, (int) std::lround (0.010 * fsDec));  // 10 ms

        const int decLen = nextPow2 (window + tauMax + 4);
        dec.assign ((size_t) decLen, 0.0f);
        decMask = decLen - 1;
        const int fullLen = nextPow2 ((window + tauMax + 4) * decimation + 8);
        full.assign ((size_t) fullLen, 0.0f);
        fullMask = fullLen - 1;

        frame.assign ((size_t) (window + tauMax + 2), 0.0f);
        diff.assign ((size_t) (tauMax + 2), 0.0f);
        reset();
    }

    void PitchTracker::reset() noexcept
    {
        aa1.reset();
        aa2.reset();
        std::fill (dec.begin(), dec.end(), 0.0f);
        std::fill (full.begin(), full.end(), 0.0f);
        decPos = fullPos = phase = hopCounter = 0;
        period = 0.0f;
        aperiodicity = 1.0f;
    }

    void PitchTracker::push (float x) noexcept
    {
        if (! std::isfinite (x) || std::abs (x) < 1.0e-20f)
            x = 0.0f;

        fullPos = (fullPos + 1) & fullMask;
        full[(size_t) fullPos] = x;

        float y = aa2.process (aa1.process (x, 0), 0);
        if (! aa2.isStateFinite() || ! aa1.isStateFinite())
        {
            aa1.reset();
            aa2.reset();
            y = 0.0f;
        }
        if (++phase < decimation)
            return;
        phase = 0;

        decPos = (decPos + 1) & decMask;
        dec[(size_t) decPos] = y;

        if (++hopCounter >= hop)
        {
            hopCounter = 0;
            analyse();
        }
    }

    void PitchTracker::analyse() noexcept
    {
        const int len = window + tauMax;
        double energy = 0.0;
        for (int i = 0; i < len; ++i)
        {
            const float v = dec[(size_t) ((decPos - len + 1 + i) & decMask)];
            frame[(size_t) i] = v;
            energy += (double) v * v;
        }
        if (energy < 1.0e-9 * len)
        {
            period = 0.0f;
            aperiodicity = 1.0f;
            return;
        }

        // Difference function and its cumulative mean normalisation (YIN steps 2-3)
        diff[0] = 1.0f;
        double running = 0.0;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double d = 0.0;
            const float* a = frame.data();
            const float* b = frame.data() + tau;
            for (int j = 0; j < window; ++j)
            {
                const float e = a[j] - b[j];
                d += (double) e * e;
            }
            running += d;
            diff[(size_t) tau] = running > 0.0 ? (float) (d * tau / running) : 1.0f;
        }

        // Absolute threshold, then the local minimum after it (YIN step 4)
        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
            if (diff[(size_t) tau] < 0.15f)
            {
                while (tau + 1 < tauMax && diff[(size_t) tau + 1] < diff[(size_t) tau])
                    ++tau;
                best = tau;
                break;
            }
        if (best < 0)
        {
            best = tauMin;
            for (int tau = tauMin + 1; tau < tauMax; ++tau)
                if (diff[(size_t) tau] < diff[(size_t) best])
                    best = tau;
        }

        aperiodicity = diff[(size_t) best];
        if (aperiodicity > 0.3f)
        {
            period = 0.0f;
            return;
        }

        // Parabolic interpolation (step 5), then refinement at the full rate
        const float y0 = diff[(size_t) best - 1], y1 = diff[(size_t) best], y2 = diff[(size_t) best + 1];
        const float den = y0 - 2.0f * y1 + y2;
        const float off = den > 0.0f ? juce::jlimit (-0.5f, 0.5f, 0.5f * (y0 - y2) / den) : 0.0f;
        period = refine (((float) best + off) * (float) decimation);
    }

    float PitchTracker::refine (float coarse) const noexcept
    {
        if (decimation == 1)
            return coarse;

        const int w = std::min (window * decimation, std::max ((int) (1.5f * coarse), (int) (0.008 * fs)));
        const int centre = (int) std::lround (coarse);
        const int lo = std::max (2, centre - decimation - 1), hi = centre + decimation + 1;
        const int maxTau = (int) full.size() - w - 2;
        if (hi + 1 > maxTau)
            return coarse;

        // newest sample at fullPos; the frame plus the largest lag ends exactly there
        const int start = fullPos - (w + hi);
        const auto at = [this, start] (int i) { return full[(size_t) ((start + i) & fullMask)]; };
        const auto d = [&] (int tau)
        {
            double s = 0.0;
            for (int j = 0; j < w; ++j)
            {
                const float e = at (j) - at (j + tau);
                s += (double) e * e;
            }
            return s;
        };

        std::array<double, 64> vals {};
        const int count = std::min (hi - lo + 1, (int) vals.size());
        for (int k = 0; k < count; ++k)
            vals[(size_t) k] = d (lo + k);
        int best = 1;
        for (int k = 2; k + 1 < count; ++k)
            if (vals[(size_t) k] < vals[(size_t) best])
                best = k;
        const double a = vals[(size_t) best - 1], b = vals[(size_t) best], c = vals[(size_t) best + 1];
        const double den = a - 2.0 * b + c;
        const double off = den > 0.0 ? juce::jlimit (-0.5, 0.5, 0.5 * (a - c) / den) : 0.0;
        return (float) ((lo + best) + off);
    }
}
