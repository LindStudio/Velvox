#pragma once

#include "DspUtilities.h"

namespace velvox::dsp
{
    struct CompSettings
    {
        float thresholdDb = -18.0f;
        float ratio = 3.0f;
        float attackMs = 10.0f;
        float releaseMs = 120.0f;
        float kneeDb = 6.0f;
        float makeupDb = 0.0f;
    };

    // Feed-forward, stereo-linked peak compressor with soft knee. Gain
    // reduction is smoothed in the log domain with separate attack/release.
    class CompressorModule
    {
    public:
        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        void setSettings (const CompSettings& s) noexcept;
        void process (float* const* channels, int numChannels, int numSamples) noexcept;

        // Static curve: output level for a given input level (both dB)
        static float computeGainDb (float inputDb, float thresholdDb, float ratio, float kneeDb) noexcept;

        PeakMeter gainReductionDb; // positive dB of reduction

    private:
        double fs = 44100.0;
        CompSettings settings;
        float attackCoeff = 0.0f, releaseCoeff = 0.0f;
        float envDb = 0.0f; // current gain reduction in dB (>= 0)
        juce::SmoothedValue<float> makeup { 1.0f };
    };
}
