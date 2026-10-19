#pragma once

#include "DspUtilities.h"

namespace velvox::dsp
{
    struct PulseSettings
    {
        static constexpr int numPoints = 6;

        bool enabled = false;
        int rate = 2;               // index into PulseModule::rateNames()
        float depth = 0.6f;         // 0..1
        float attackMs = 1.0f;      // gain going down
        float releaseMs = 10.0f;    // gain coming back up
        float smoothMs = 2.0f;      // final rounding
        int interpolation = 1;      // 0 linear, 1 smooth (S-curve), 2 step
        float tension = 0.0f;       // -1..1, bends each segment
        float mix = 1.0f;           // 0..1
        float manualBpm = 150.0f;   // used when the host reports no tempo
        std::array<float, numPoints> x { 0.0f, 0.06f, 0.28f, 0.5f, 0.75f, 0.95f };
        std::array<float, numPoints> y { 0.0f, 0.1f, 0.7f, 0.95f, 1.0f, 1.0f };
    };

    // PULSE - rhythmic volume shaper. A user-drawn level curve is repeated once
    // per cycle (a note value at the host tempo) and applied as gain:
    //   gain = 1 - depth * (1 - curve(phase))
    // The raw gain is slew-limited (attack for drops, release for rises) and
    // rounded by a one-pole smoother, so even a hard step curve never clicks.
    // Locks to the host song position while playing (loops and jumps are
    // followed on the next block); free-runs otherwise. Zero latency.
    class PulseModule
    {
    public:
        static const juce::StringArray& rateNames();
        static double beatsPerCycle (int rateIndex) noexcept;

        // Curve value 0..1 at phase 0..1 (shared with the UI so the drawing
        // is exactly what is processed). Points need not be sorted.
        static float evaluate (const PulseSettings& s, float phase) noexcept;

        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        void setSettings (const PulseSettings& s) noexcept;
        void setTransport (double bpm, double ppqAtBlockStart, bool playing) noexcept;

        void process (float* const* channels, int numChannels, int numSamples) noexcept;

        // UI read-outs (relaxed atomics)
        std::atomic<float> displayPhase { -1.0f }; // < 0 while off
        std::atomic<float> displayGain { 1.0f };
        std::atomic<float> displayBpm { 150.0f };
        std::atomic<bool> displayHostTempo { false };

    private:
        void rebuildTable() noexcept;

        static constexpr int tableSize = 1024;
        std::array<float, tableSize + 1> table {};

        double fs = 48000.0;
        PulseSettings settings;

        double bpm = 150.0;
        double hostPpq = -1.0;
        bool hostPlaying = false, hostTempoKnown = false;
        double freePpq = 0.0;

        float slewed = 1.0f, smoothed = 1.0f;
        float attackCoeff = 0.0f, releaseCoeff = 0.0f, smoothCoeff = 0.0f;
        juce::SmoothedValue<float> amount { 0.0f };   // on/off x mix, click free
        bool snapNext = true;    // first settings after prepare(): no fade from the defaults
        bool wasActive = false;
    };
}
