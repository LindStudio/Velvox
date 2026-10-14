#pragma once

#include "DspUtilities.h"

namespace velvox::dsp
{
    struct DelaySettings
    {
        float timeMs = 375.0f;
        float feedback = 0.3f;   // 0..0.9
        float lowCutHz = 300.0f;
        float highCutHz = 6000.0f;
        bool pingPong = false;
    };

    // Stereo feedback delay that outputs WET ONLY (the chain adds the dry signal
    // exactly once). Repeats are band-limited by a high-pass and low-pass in the
    // loop and pass through a soft limiter, so the loop can never run away.
    class DelayModule
    {
    public:
        static constexpr float maxDelayMs = 4000.0f;

        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        void setSettings (const DelaySettings& s) noexcept;

        // input: the signal to send into the delay; wetOut: receives the return
        void process (const float* const* input, float* const* wetOut, int numChannels, int numSamples) noexcept;

        static float timeForTempo (double bpm, int divisionIndex) noexcept;

    private:
        float readInterpolated (const std::vector<float>& buf, float delaySamples) const noexcept;
        void updateFilters (bool snap) noexcept;

        double fs = 44100.0;
        DelaySettings settings;
        std::array<std::vector<float>, maxChannels> lines;
        int writePos = 0;
        int size = 0;

        juce::SmoothedValue<float> delaySamples { 1.0f }, feedback { 0.0f };
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> lowCut { 300.0f }, highCut { 6000.0f };
        Biquad hp, lp;
    };
}
