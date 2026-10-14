#include "Filters.h"

#include <complex>

namespace velvox::dsp
{
    bool sanitize (float* const* channels, int numChannels, int numSamples) noexcept
    {
        bool fixed = false;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* d = channels[ch];
            for (int i = 0; i < numSamples; ++i)
            {
                if (! std::isfinite (d[i]))
                {
                    d[i] = 0.0f;
                    fixed = true;
                }
            }
        }

        return fixed;
    }

    namespace design
    {
        namespace
        {
            constexpr double pi = 3.14159265358979323846;

            struct Prep
            {
                double cosw, sinw, alpha;
            };

            Prep prepare (double sampleRate, double freq, double q)
            {
                const double f = std::clamp (freq, 5.0, 0.49 * sampleRate);
                const double w0 = 2.0 * pi * f / sampleRate;
                const double qq = std::max (q, 0.05);
                return { std::cos (w0), std::sin (w0), std::sin (w0) / (2.0 * qq) };
            }

            BiquadCoeffs normalise (double b0, double b1, double b2, double a0, double a1, double a2)
            {
                const double inv = 1.0 / a0;
                return { b0 * inv, b1 * inv, b2 * inv, a1 * inv, a2 * inv };
            }
        }

        BiquadCoeffs lowPass (double sampleRate, double freq, double q)
        {
            const auto p = prepare (sampleRate, freq, q);
            const double b1 = 1.0 - p.cosw;
            return normalise (b1 * 0.5, b1, b1 * 0.5, 1.0 + p.alpha, -2.0 * p.cosw, 1.0 - p.alpha);
        }

        BiquadCoeffs highPass (double sampleRate, double freq, double q)
        {
            const auto p = prepare (sampleRate, freq, q);
            const double b0 = (1.0 + p.cosw) * 0.5;
            return normalise (b0, -(1.0 + p.cosw), b0, 1.0 + p.alpha, -2.0 * p.cosw, 1.0 - p.alpha);
        }

        BiquadCoeffs peak (double sampleRate, double freq, double q, double gainDb)
        {
            const auto p = prepare (sampleRate, freq, q);
            const double A = std::pow (10.0, gainDb / 40.0);
            return normalise (1.0 + p.alpha * A, -2.0 * p.cosw, 1.0 - p.alpha * A,
                              1.0 + p.alpha / A, -2.0 * p.cosw, 1.0 - p.alpha / A);
        }

        BiquadCoeffs lowShelf (double sampleRate, double freq, double q, double gainDb)
        {
            const auto p = prepare (sampleRate, freq, q);
            const double A = std::pow (10.0, gainDb / 40.0);
            const double sq = 2.0 * std::sqrt (A) * p.alpha;

            return normalise (A * ((A + 1.0) - (A - 1.0) * p.cosw + sq),
                              2.0 * A * ((A - 1.0) - (A + 1.0) * p.cosw),
                              A * ((A + 1.0) - (A - 1.0) * p.cosw - sq),
                              (A + 1.0) + (A - 1.0) * p.cosw + sq,
                              -2.0 * ((A - 1.0) + (A + 1.0) * p.cosw),
                              (A + 1.0) + (A - 1.0) * p.cosw - sq);
        }

        BiquadCoeffs highShelf (double sampleRate, double freq, double q, double gainDb)
        {
            const auto p = prepare (sampleRate, freq, q);
            const double A = std::pow (10.0, gainDb / 40.0);
            const double sq = 2.0 * std::sqrt (A) * p.alpha;

            return normalise (A * ((A + 1.0) + (A - 1.0) * p.cosw + sq),
                              -2.0 * A * ((A - 1.0) + (A + 1.0) * p.cosw),
                              A * ((A + 1.0) + (A - 1.0) * p.cosw - sq),
                              (A + 1.0) - (A - 1.0) * p.cosw + sq,
                              2.0 * ((A - 1.0) - (A + 1.0) * p.cosw),
                              (A + 1.0) - (A - 1.0) * p.cosw - sq);
        }

        double magnitude (const BiquadCoeffs& c, double freq, double sampleRate)
        {
            const double w = 2.0 * pi * freq / sampleRate;
            const std::complex<double> z1 = std::polar (1.0, -w);
            const std::complex<double> z2 = z1 * z1;
            const auto num = c.b0 + c.b1 * z1 + c.b2 * z2;
            const auto den = 1.0 + c.a1 * z1 + c.a2 * z2;
            return std::abs (num / den);
        }
    }
}
