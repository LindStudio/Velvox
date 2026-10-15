#pragma once

#include "DspUtilities.h"

namespace velvox::dsp
{
    struct SmashSettings
    {
        bool enabled = false;
        float amount = 0.5f;      // 0..1 -> threshold and ratio of the squashed path
        float blend = 0.35f;      // 0..1 -> level of the squashed path under the original
        float attackMs = 2.0f;
        float releaseMs = 80.0f;
        float tone = 0.2f;        // -1 (warm) .. +1 (bright), squashed path only
        float outputDb = 0.0f;
    };

    // VOCAL SMASH - parallel compression on its own signal path.
    //
    //   dry ───────────────────────────────┐
    //    └─ squash (heavy comp) ─ tone ─ x blend ─ + ─ x norm ─ x output
    //
    // The squashed path is a separate, zero-latency copy (same polarity, sample
    // aligned), so the original is present exactly once and keeps its
    // transients. Threshold and ratio follow Amount; automatic makeup puts a
    // loud word (about -12 dBFS peak) at unity on the squashed path, so Blend
    // mostly lifts the quiet parts. A downward expander on the squashed path
    // keeps breaths and room noise from being pulled up. `norm` trims the sum
    // so the output stays within about +2 dB of the input at any blend.
    class SmashModule
    {
    public:
        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        void setSettings (const SmashSettings& s) noexcept;
        void process (float* const* channels, int numChannels, int numSamples) noexcept;

        // Static curve of the squashed path in dB (input level -> gain change), for tests and the UI
        static float gainComputerDb (float levelDb, float amount) noexcept;
        static float thresholdFor (float amount) noexcept { return -12.0f - 30.0f * juce::jlimit (0.0f, 1.0f, amount); }
        static float ratioFor (float amount) noexcept     { return 2.0f + 18.0f * juce::jlimit (0.0f, 1.0f, amount); }
        static float makeupFor (float amount) noexcept    { return -gainComputerDb (-12.0f, amount); }

        PeakMeter gainReductionDb;              // squashed path, dB (positive)
        std::atomic<float> blendLevelDb { -100.0f }; // level of the added path after blend

    private:
        void updateTone() noexcept;

        double fs = 48000.0;
        SmashSettings settings;
        float appliedTone = 1000.0f;

        Biquad detectorHpf, lowShelf, highShelf;
        float envDb = -120.0f;                 // detector level
        float grDb = 0.0f;                     // smoothed gain change (<= 0)
        float attackCoeff = 0.0f, releaseCoeff = 0.0f, levelCoeff = 0.0f;

        juce::SmoothedValue<float> enabled { 0.0f }, blend { 0.35f }, outputGain { 1.0f }, amountSmoothed { 0.5f };
        bool snapNext = true;
        bool wasActive = false;
    };
}
