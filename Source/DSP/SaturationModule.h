#pragma once

#include "DspUtilities.h"
#include <juce_dsp/juce_dsp.h>

namespace velvox::dsp
{
    struct SatSettings
    {
        bool enabled = false;
        float driveDb = 6.0f;
        float outputDb = 0.0f;
        float mix = 1.0f; // 0..1
        int character = 0; // 0 warm, 1 tape, 2 tube, 3 hard
    };

    // Asymmetric tanh saturation, 2x oversampled (polyphase IIR half-band
    // filters with integer latency). The module ALWAYS delays the signal by its
    // latency - also when switched off - so the plugin's reported latency stays
    // constant and the internal dry path is phase aligned for the Mix control.
    class SaturationModule
    {
    public:
        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        void setSettings (const SatSettings& s) noexcept;
        void process (float* const* channels, int numChannels, int numSamples) noexcept;

        int getLatencySamples() const noexcept { return latency; }

        // Transfer curves incl. level compensation (also used by the UI)
        static constexpr int numCharacters = 4;
        static float shape (float x, float driveLin, int character = 0) noexcept;
        static float compensationFor (float driveLin, int character = 0) noexcept;

    private:
        std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
        IntegerDelay dryDelay;
        juce::AudioBuffer<float> dry;
        juce::SmoothedValue<float> driveDb { 6.0f }, outGain { 1.0f }, amount { 0.0f };
        float driveLin = 2.0f, compensation = 1.0f;
        int character = 0, compensationCharacter = -1;
        float amountTarget = 0.0f;
        bool needsReset = true;
        int latency = 0;

        // DC blocker (the asymmetric curve produces DC on loud input)
        std::array<float, maxChannels> dcX {}, dcY {};
        float dcR = 0.9995f;
    };
}
