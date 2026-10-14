#pragma once

#include "DspUtilities.h"

namespace velvox::dsp
{
    struct EqSettings
    {
        bool hpfOn = true;
        float hpfFreq = 80.0f;
        int hpfSlope = 1;          // 0 = 12, 1 = 24, 2 = 48 dB/oct
        int lowType = 0;           // 0 = shelf, 1 = bell
        float lowFreq = 150.0f, lowGain = 0.0f, lowQ = 0.7071f;
        float midFreq = 1500.0f, midGain = 0.0f, midQ = 1.0f;
        int highType = 0;          // 0 = shelf, 1 = bell
        float highFreq = 10000.0f, highGain = 0.0f, highQ = 0.7071f;
        bool lpfOn = false;
        float lpfFreq = 18000.0f;
        int lpfSlope = 1;          // 0 = 12, 1 = 24, 2 = 48 dB/oct
    };

    // Low cut (Butterworth, up to 8th order) -> low band (shelf/bell) -> mid bell
    // -> high band (shelf/bell) -> high cut (Butterworth, up to 8th order).
    class EqualizerModule
    {
    public:
        static constexpr int maxCutSections = 4; // 48 dB/oct
        enum Section
        {
            hp1 = 0, hp2, hp3, hp4,
            lowBand, midBell, highBand,
            lp1, lp2, lp3, lp4,
            numSections
        };
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
        int currentHpSlope = 1, currentLpSlope = 1;

        using Mult = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>;
        Mult hpfFreq { 80.0f }, lowFreq { 150.0f }, midFreq { 1500.0f }, highFreq { 10000.0f }, lpfFreq { 18000.0f },
             midQ { 1.0f }, lowQ { 0.7071f }, highQ { 0.7071f };
        juce::SmoothedValue<float> lowGain, midGain, highGain, hpfAmount, lpfAmount;

        std::array<Biquad, numSections> filters;
        static constexpr int updateInterval = 16;
    };
}
