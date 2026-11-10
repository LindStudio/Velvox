#pragma once

#include "DSP/DspUtilities.h"
#include <atomic>
#include <vector>

// EMBER De-Esser - the audio engine (no JUCE plugin classes, so the tests drive it directly).
//
//   input ──┬────────────────────────── delay (latency L) ──► shelf / wide gain ──► mix ──► output gain
//           └─ detector (4th-order high-pass at `freq`, stereo-linked peak, optional relative to the
//              full-band level) ─► soft-knee 4:1 ─► range cap ─► attack / release ─► delayed by L - lookahead
//
// The latency is constant (5 ms) whatever the lookahead, so hosts never see it change; the lookahead only
// decides how early the reduction starts before an S. At 0 dB reduction the split mode is an exact identity
// (the shelf is skipped), so a voice without S passes bit-exact apart from the delay.
namespace ember
{
    using namespace vocalypse::dsp;

    struct Settings
    {
        float freq = 6500.0f;        // detection / shelf corner, Hz
        float thresholdDb = -30.0f;
        float rangeDb = 8.0f;        // most the S band is ever reduced (before Amount)
        float amount = 0.5f;         // Amount 0..1; 0.5 = the reduction threshold / range ask for, 1 = twice
        float attackMs = 0.5f;
        float releaseMs = 60.0f;
        float lookaheadMs = 2.0f;    // 0 .. maxLookaheadMs
        float mix = 1.0f;
        float outputDb = 0.0f;
        int mode = 0;                // 0 = split (only the band above freq), 1 = wideband
        bool relative = true;        // detection relative to the full-band level (loud and quiet phrases alike)
        int listen = 0;              // 0 = off, 1 = S band (detector band), 2 = delta (what is removed)
        bool bypass = false;
    };

    inline constexpr float maxLookaheadMs = 5.0f;
    inline constexpr float ratio = 4.0f;
    inline constexpr float kneeDb = 6.0f;
    inline constexpr float maxReductionDb = 24.0f;
    // Relative detection: band level - full-band envelope + referenceDb, so a threshold means the same as in
    // absolute mode for a voice peaking at -18 dBFS. Below fullFloorDb (near-silence) nothing is de-essed.
    inline constexpr float referenceDb = -18.0f;
    inline constexpr float fullFloorDb = -60.0f;

    // Static curve: reduction in dB (>= 0) for a detector level, soft knee
    float staticReductionDb (float inDb, float thresholdDb) noexcept;

    // The detector alone (used by the engine and by the Auto analysis, so both measure the same thing)
    class Detector
    {
    public:
        void prepare (double sampleRate);
        void reset();
        void setFrequency (float hz) noexcept;   // recomputes the filter
        // One frame of all channels -> detector level in dB (relative or absolute). `band` receives the
        // filtered band of channel 0 / 1 (for S-band listening) when not null.
        float process (const float* frame, int numChannels, bool relative, float* band = nullptr) noexcept;
        bool isFinite() const noexcept;

    private:
        double fs = 44100.0;
        Biquad hp1, hp2;
        float fullEnv = 0.0f, fullRelease = 0.0f;
    };

    class Engine
    {
    public:
        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        void setSettings (const Settings& s) noexcept;
        const Settings& getSettings() const noexcept { return settings; }
        void process (float* const* channels, int numChannels, int numSamples) noexcept;

        int getLatencySamples() const noexcept { return latency; }
        static int latencyFor (double sampleRate) noexcept { return (int) std::lround (maxLookaheadMs * 0.001 * sampleRate); }

        // UI feed (message thread reads)
        PeakMeter gainReductionDb, inputPeak, outputPeak;
        SampleTap inputTap, outputTap;
        std::atomic<float> currentReductionDb { 0.0f };

    private:
        void updateShelf (float reductionDb) noexcept;

        double fs = 44100.0;
        int latency = 0;
        Settings settings;
        Detector detector;
        Biquad shelf, listenHp1, listenHp2;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> freq { 6500.0f };
        juce::SmoothedValue<float> mixSm { 1.0f }, outSm { 1.0f }, bypassSm { 0.0f };
        float attackCoeff = 0.0f, releaseCoeff = 0.0f;
        float envDb = 0.0f, shelfDb = 0.0f;

        // Delay lines: audio (per channel) and the reduction envelope
        std::array<std::vector<float>, maxChannels> audioDelay;
        std::vector<float> envDelay;
        int writePos = 0, delaySize = 1;
        std::vector<float> dryScratch[maxChannels], wetScratch[maxChannels];
    };
}
