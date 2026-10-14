#pragma once

#include "DspUtilities.h"

namespace velvox::dsp
{
    struct EqSettings
    {
        bool hpfOn = true;
        float hpfFreq = 80.0f;
        int hpfSlope = 1;          // 0 = 12 dB/oct, 1 = 24 dB/oct
        float lowFreq = 150.0f, lowGain = 0.0f;
        float midFreq = 1500.0f, midGain = 0.0f, midQ = 1.0f;
        float highFreq = 10000.0f, highGain = 0.0f;
    };

    // Low cut (2 x 2nd order Butterworth) + low shelf + mid bell + high shelf.
    class EqualizerModule
    {
    public:
        enum Section { hp1 = 0, hp2, lowShelf, midBell, highShelf, numSections };
        using Sections = std::array<BiquadCoeffs, numSections>;

        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        void setSettings (const EqSettings& s);
        void process (float* const* channels, int numChannels, int numSamples) noexcept;

        // Shared with the UI so the drawn curve is exactly what is processed
        static Sections designSections (const EqSettings& s, double sampleRate);
        static double responseDb (const EqSettings& s, double freq, double sampleRate);

    private:
        void updateCoefficients (bool snap) noexcept;

        double fs = 44100.0;
        EqSettings target;
        int currentSlope = 1;

        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> hpfFreq { 80.0f }, lowFreq { 150.0f },
                                                                              midFreq { 1500.0f }, highFreq { 10000.0f }, midQ { 1.0f };
        juce::SmoothedValue<float> lowGain, midGain, highGain, hpfAmount;

        std::array<Biquad, numSections> filters;
        static constexpr int updateInterval = 16;
    };
}
