#pragma once

#include "DspUtilities.h"

namespace velvox::dsp
{
    // VOCAL CHECK, the part that sits in the chain. It never changes the audio:
    // it measures sample-accurate peak / clipping statistics and hands a copy of
    // the signal to the VocalAnalyzer through a lock-free FIFO (no allocation,
    // no blocking; samples are dropped if the analyzer does not keep up).
    class CheckModule
    {
    public:
        static constexpr int tapCapacity = 1 << 16;

        CheckModule();

        void setEnabled (bool shouldBeEnabled) noexcept { enabled = shouldBeEnabled; }
        bool isEnabled() const noexcept { return enabled; }

        // Read-only on the audio
        void process (const float* const* channels, int numChannels, int numSamples) noexcept;

        // Analyzer side (message thread)
        int pull (float* left, float* right, int maxSamples) noexcept;
        void resetStats() noexcept { resetRequested.store (true); }

        std::atomic<float> peak { 0.0f };          // largest |sample| since the last reset
        std::atomic<int> clipEvents { 0 };         // runs of >= 3 samples at full scale
        std::atomic<bool> stereoContent { false }; // left and right differ
        std::atomic<int> droppedSamples { 0 };

    private:
        bool enabled = false;
        juce::AbstractFifo fifo { tapCapacity };
        std::vector<float> left, right;
        int clipRun = 0;
        std::atomic<bool> resetRequested { false };
    };
}
