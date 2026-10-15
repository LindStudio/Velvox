#pragma once

#include "DspUtilities.h"

namespace velvox::dsp
{
    struct SmoothSettings
    {
        bool enabled = false;
        float amount = 0.5f;        // 0..1
        float sensitivity = 0.5f;   // 0..1 (higher = smaller peaks count)
        float lowHz = 800.0f;
        float highHz = 12000.0f;
        float attackMs = 5.0f;
        float releaseMs = 80.0f;
        int mode = 0;               // 0 soft, 1 hard
        float outputDb = 0.0f;
        bool delta = false;         // output only what is removed
    };

    // SMOOTH - adaptive resonance control (own design, zero latency).
    //
    // A bank of 32 narrow band-pass detectors (about 1/4 octave apart) measures the
    // short-term energy of the vocal in every band. Each band is compared with
    // its own neighbourhood (mean level of the bands 2 to 5 places away):
    // a resonance is a band that sticks out of the local spectrum, not a band
    // that is merely loud. Only the excess above a sensitivity-dependent
    // threshold is reduced, by a dynamic bell filter at that band, smoothed with
    // attack / release. Broad brightness or sibilance noise raises the
    // neighbours too and is therefore left alone - the voice does not get dull.
    class SmoothModule
    {
    public:
        static constexpr int numBands = 32;
        static float bandFrequency (int band) noexcept;   // centre frequency in Hz
        static float thresholdFor (float sensitivity) noexcept { return 12.0f - 9.0f * juce::jlimit (0.0f, 1.0f, sensitivity); }

        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        void setSettings (const SmoothSettings& s) noexcept;
        void process (float* const* channels, int numChannels, int numSamples) noexcept;

        // Current reduction per band in dB (>= 0) - exactly what the filters apply
        std::array<std::atomic<float>, numBands> bandCutDb {};
        float getMaxCutDb() const noexcept;

    private:
        void updateControl() noexcept;

        double fs = 48000.0;
        SmoothSettings settings;
        int activeBands = numBands;   // bands below Nyquist

        std::array<Biquad, numBands> detectors, cutters;
        std::array<float, numBands> power {}, cut {}, appliedCut {}, rangeWeight {};
        std::array<float, numBands> powerCoeff {};
        float attackCoeff = 0.0f, releaseCoeff = 0.0f;
        int controlCounter = 0;

        juce::SmoothedValue<float> enabled { 0.0f }, outputGain { 1.0f }, deltaMix { 0.0f };
        bool snapNext = true;    // first settings after prepare(): no fade from the defaults
        bool wasActive = false;
    };
}
