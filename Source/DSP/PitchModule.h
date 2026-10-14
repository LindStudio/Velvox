#pragma once

#include "DspUtilities.h"
#include <memory>

namespace velvox::dsp
{
    struct PitchSettings
    {
        bool enabled = false;
        float semitones = 0.0f;      // -24 .. +24
        float cents = 0.0f;          // -100 .. +100
        bool preserveFormants = true;
        float formantShift = 0.0f;   // semitones, creative formant move (-12 .. +12)
        float mix = 1.0f;            // dry/wet (dry is latency aligned)
    };

    // Creative pitch shifter based on Signalsmith Stretch (MIT). This is NOT a
    // pitch-correction tool: it transposes by a fixed amount.
    //
    // Latency: the shifter needs a 120 ms analysis window (shorter windows were
    // measured to detune by up to 70 cents, see Tests/PitchDiagnostics.cpp).
    // While the module is enabled its latency is reported to the host (see
    // VocalChain); when disabled it is bypassed with zero latency. Everything is
    // configured in prepare(), so nothing allocates on the audio thread.
    class PitchModule
    {
    public:
        PitchModule();
        ~PitchModule();

        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        void setSettings (const PitchSettings& s) noexcept;

        // Latency while enabled (samples)
        int getLatencySamples() const noexcept;

        // Called by the chain when the module becomes active
        void activate() noexcept;

        // Processes in place; only call while active (enabled)
        void process (float* const* channels, int numChannels, int numSamples) noexcept;

    private:
        struct Engine;
        std::unique_ptr<Engine> engine;

        double fs = 48000.0;
        PitchSettings settings;
        juce::SmoothedValue<float> mix { 1.0f };
        IntegerDelay dryDelay;
        juce::AudioBuffer<float> dry, wet;
        float lastTranspose = 1.0e9f, lastFormant = 1.0e9f;
        bool lastPreserve = true;
    };
}
