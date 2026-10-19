#pragma once

#include "DspUtilities.h"

namespace velvox::dsp
{
    // Loudness following ITU-R BS.1770: K-weighting (high shelf + RLB high-pass)
    // and channel-summed mean square, reported in LUFS. The integration window
    // is an exponential average (time constant = window length) - a close,
    // continuous approximation of momentary (0.4 s) / short-term (3 s) loudness.
    class LoudnessMeter
    {
    public:
        static constexpr float silenceLufs = -120.0f;

        void prepare (double sampleRate);
        void reset();
        void setWindowSeconds (float seconds) noexcept;

        void process (const float* const* channels, int numChannels, int numSamples) noexcept;
        float getLufs() const noexcept;

        // K-weighting magnitude at a frequency (for tests)
        static double kWeightingGainDb (double freq, double sampleRate);

        // Ungated integrated loudness of samples [skip, numSamples) - the first
        // `skip` samples only settle the filters. Offline use (allocates nothing,
        // but is not meant for the audio thread).
        static float integratedLufs (const float* const* channels, int numChannels, int numSamples,
                                     double sampleRate, int skip);

        // K-weighting filter pair for a sample rate (also used by the offline analysis)
        static void designKWeighting (double sampleRate, BiquadCoeffs& shelf, BiquadCoeffs& highPass);

    private:

        double fs = 48000.0;
        Biquad shelf, highPass;
        double meanSquare = 0.0;
        double coeff = 0.0;
        float windowSeconds = 3.0f;
    };

    // Computes the gain that makes the processed signal as loud as the input.
    //  - change rate limited (dB per second) so it never "pumps"
    //  - frozen while the input is below the gate (silence / breaths)
    //  - limited to +-range dB
    class LevelMatcher
    {
    public:
        static constexpr float gateLufs = -50.0f;
        static constexpr float slewDbPerSecond = 6.0f;

        void prepare (double sampleRate);
        void reset();
        void setWindowSeconds (float seconds) noexcept;
        void setRangeDb (float range) noexcept { rangeDb = juce::jlimit (0.0f, 24.0f, range); }

        void measureInput (const float* const* ch, int numChannels, int n) noexcept     { input.process (ch, numChannels, n); }
        void measureProcessed (const float* const* ch, int numChannels, int n) noexcept { processed.process (ch, numChannels, n); }

        // Advance the correction after measuring a chunk of n samples
        void update (int n) noexcept;

        // Gain (dB) to apply to the processed signal so it matches the input
        float getCorrectionDb() const noexcept { return correctionDb; }
        float getInputLufs() const noexcept { return input.getLufs(); }
        float getProcessedLufs() const noexcept { return processed.getLufs(); }

    private:
        double fs = 48000.0;
        LoudnessMeter input, processed;
        float rangeDb = 12.0f;
        float correctionDb = 0.0f;
        float warmupSeconds = 0.0f;
        float windowSeconds = 3.0f;
    };
}
