#pragma once

#include "DspUtilities.h"
#include <array>
#include <vector>

namespace velvox::dsp
{
    // Oversampler of the Saturation module (0.6): 4x below 60 kHz, 2x above.
    //
    //  up:   polyphase IIR half-band stages (two all-pass branches, coefficients designed with
    //        Laurent de Soras' hiir method): base -> 2x steep (passband to 0.4167 of the base
    //        Nyquist, > 110 dB stopband), 2x -> 4x relaxed (> 89 dB above 3/4 of the 4x Nyquist).
    //  down: the relaxed half-band 4x -> 2x, then an order-11 elliptic low-pass at the 2x rate
    //        (passband 0.4167, stopband from 0.5 of the 2x Nyquist = the base Nyquist, 95 dB, 0.05 dB
    //        ripple), then plain decimation. A half-band can't do this last step: its transition is
    //        centred on the base Nyquist, so harmonics just above it (5 x 4987 Hz = 24.9 kHz) would fold
    //        back almost unattenuated.
    // All coefficients are normalised, so the same filters serve every sample rate.
    // Latency: the group delay of the linear chain at 1 kHz (about 5 base samples at 48 kHz).
    class SatOversampler
    {
    public:
        void prepare (double baseRate, int maxBlockSize);
        void reset() noexcept;

        int getFactor() const noexcept { return factor; }

        // Upsamples n base samples per channel; returns the high-rate buffers (n * factor samples)
        float* const* upsample (const float* const* in, int numChannels, int n) noexcept;
        // Downsamples the high-rate buffers (as returned by upsample, processed in place) into out
        void downsample (float* const* out, int numChannels, int n) noexcept;

        // Phase delay of the linear chain at 1 kHz plus `extraHighRateSamples` at the high rate,
        // in base samples (measured once in prepare)
        double getDelayBaseSamples (double extraHighRateSamples = 0.0) const noexcept;

    private:
        // One branch-pair of a polyphase half-band, one channel
        template <int N>
        struct Halfband
        {
            std::array<float, N> c {};
            std::array<float, N> x1 {}, y1 {};   // per first-order section: last input / output (at the low rate)
            float oddOut = 0.0f;                  // down: previous output of the odd branch

            void reset() noexcept { x1.fill (0.0f); y1.fill (0.0f); oddOut = 0.0f; }

            // first-order all-pass sections k = start, start + 2, ...
            float branch (float x, int start) noexcept
            {
                for (int k = start; k < N; k += 2)
                {
                    const float y = c[(size_t) k] * (x - y1[(size_t) k]) + x1[(size_t) k];
                    x1[(size_t) k] = x;
                    y1[(size_t) k] = y;
                    x = y;
                }
                return x;
            }

            void up (float x, float& out0, float& out1) noexcept
            {
                out0 = branch (x, 0);
                out1 = branch (x, 1);
            }

            float down (float even, float odd) noexcept
            {
                const float y = 0.5f * (branch (even, 0) + oddOut);
                oddOut = branch (odd, 1);
                return y;
            }
        };

        struct Elliptic
        {
            static constexpr int sections = 6;
            std::array<std::array<double, 2>, sections> z {};
            void reset() noexcept { for (auto& s : z) s = { 0.0, 0.0 }; }
            double process (double x) noexcept;
        };

        using Steep = Halfband<7>;
        using Relaxed = Halfband<3>;

        int factor = 4;
        int maxBlock = 0;
        std::array<Steep, maxChannels> upSteep;
        std::array<Relaxed, maxChannels> upRelaxed, downRelaxed;
        std::array<Elliptic, maxChannels> ellip;
        std::array<std::vector<float>, maxChannels> high, mid;
        std::array<float*, maxChannels> highPtr {};
        double delayAt1k = 0.0;   // base samples
        double baseRate = 48000.0;

        void initCoefficients();
    };
}
