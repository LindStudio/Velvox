#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <vector>

namespace velvox::dsp
{
    struct BreathSettings
    {
        bool enabled = false;
        float amount = 50.0f;   // %, 0..100: 50 = breaths -12 dB, 100 = -30 dB (practically gone)
    };

    // ATEM-ABSENKER (0.7, docs/superpowers/plans/2026-10-27-denoise-breath-modules.md): lowers breaths
    // between the lines. A breath is a stretch of >= 80 ms of 10 ms frames that are noisy (spectral flatness
    // 300 Hz - 8 kHz >= 0.35), unvoiced (normalised autocorrelation < 0.4), quiet against the phrase
    // (12 - 45 dB below a 2 s peak hold) and centred low enough (energy centroid 500 Hz - 5 kHz; S sounds
    // sit higher and are left alone). The gain ramps in over the 5 ms look-ahead and out over 30 ms.
    // Constant latency of 5 ms while in the chain (also when off).
    class BreathModule
    {
    public:
        static int latencyFor (double sampleRate) noexcept { return juce::roundToInt (sampleRate * 0.005); }

        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        void setSettings (const BreathSettings& s) noexcept { settings = s; }
        void process (float* const* io, int numChannels, int numSamples) noexcept;

        int getLatencySamples() const noexcept { return latencyFor (fs); }
        float getReductionDb() const noexcept { return reductionDb.load (std::memory_order_relaxed); }

        // Gain in dB on a breath for an amount in % (50 -> -12, 100 -> -30)
        static float breathGainDb (float amountPercent) noexcept;

    private:
        BreathSettings settings;
        double fs = 48000.0;
        std::array<std::vector<float>, 2> delayLine;
        int delayPos = 0;
        std::atomic<float> reductionDb { 0.0f };
    };
}
