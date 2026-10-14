#pragma once

#include "DspUtilities.h"

namespace velvox::dsp
{
    struct StereoSettings
    {
        float width = 1.0f;          // 0 = mono, 1 = unchanged, 2 = double side level
        float monoBelowHz = 20.0f;   // side signal is removed below this frequency; <= 20 Hz = off
    };

    // Mid/side width control. Only the side signal is scaled - the mid (centre,
    // i.e. the lead vocal) is never touched, so the mono sum L+R is identical
    // for every width setting. Optionally keeps the low end mono.
    class StereoStage
    {
    public:
        static constexpr float maxWidth = 2.0f;
        static constexpr float monoBelowOffHz = 20.5f;

        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        void setSettings (const StereoSettings& s) noexcept;
        void process (float* const* channels, int numChannels, int numSamples) noexcept;

    private:
        void updateFilter() noexcept;

        double fs = 48000.0;
        juce::SmoothedValue<float> width { 1.0f };
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> monoBelow { 20.0f };
        juce::SmoothedValue<float> bassMonoAmount { 0.0f };
        Biquad sideHighPass1, sideHighPass2; // 4th order Linkwitz-Riley-like (2 x Butterworth)
    };

    // Phase correlation of a stereo signal (-1 .. +1), exponentially averaged.
    class CorrelationMeter
    {
    public:
        void prepare (double sampleRate, float timeConstantSeconds = 0.3f);
        void reset();
        void process (const float* left, const float* right, int numSamples) noexcept;
        float getCorrelation() const noexcept;

    private:
        double lr = 0.0, ll = 0.0, rr = 0.0;
        double coeff = 0.999;
    };
}
