#pragma once

#include <array>
#include <cmath>
#include <algorithm>

namespace velvox::dsp
{
    inline constexpr int maxChannels = 2;

    inline float dbToGain (float db) noexcept       { return std::pow (10.0f, db * 0.05f); }
    inline float gainToDb (float g) noexcept        { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }

    // Replaces NaN/Inf with silence. Returns true if anything had to be fixed.
    bool sanitize (float* const* channels, int numChannels, int numSamples) noexcept;

    // Normalised second-order section (a0 == 1)
    struct BiquadCoeffs
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    };

    // Coefficient designs (Robert Bristow-Johnson "Audio EQ Cookbook")
    namespace design
    {
        BiquadCoeffs lowPass   (double sampleRate, double freq, double q);
        BiquadCoeffs highPass  (double sampleRate, double freq, double q);
        BiquadCoeffs peak      (double sampleRate, double freq, double q, double gainDb);
        BiquadCoeffs lowShelf  (double sampleRate, double freq, double q, double gainDb);
        BiquadCoeffs highShelf (double sampleRate, double freq, double q, double gainDb);

        // |H(f)| of a section, in linear gain
        double magnitude (const BiquadCoeffs& c, double freq, double sampleRate);
    }

    // Transposed direct form II biquad with independent state per channel.
    class Biquad
    {
    public:
        void setCoeffs (const BiquadCoeffs& newCoeffs) noexcept { c = newCoeffs; }
        const BiquadCoeffs& getCoeffs() const noexcept         { return c; }

        void reset() noexcept
        {
            s1.fill (0.0);
            s2.fill (0.0);
        }

        inline float process (float x, int channel) noexcept
        {
            const double in = x;
            const double y = c.b0 * in + s1[(size_t) channel];
            s1[(size_t) channel] = c.b1 * in - c.a1 * y + s2[(size_t) channel];
            s2[(size_t) channel] = c.b2 * in - c.a2 * y;
            return (float) y;
        }

        // Guards against a corrupted state (e.g. after invalid input)
        bool isStateFinite() const noexcept
        {
            for (int ch = 0; ch < maxChannels; ++ch)
                if (! std::isfinite (s1[(size_t) ch]) || ! std::isfinite (s2[(size_t) ch]))
                    return false;
            return true;
        }

    private:
        BiquadCoeffs c;
        std::array<double, maxChannels> s1 {}, s2 {};
    };

    // Simple one-pole smoothing coefficient for time constant in ms
    inline float timeConstantCoeff (float ms, double sampleRate) noexcept
    {
        if (ms <= 0.0f)
            return 0.0f;
        return (float) std::exp (-1.0 / (0.001 * (double) ms * sampleRate));
    }
}
