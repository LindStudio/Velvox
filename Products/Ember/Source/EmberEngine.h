#pragma once

#include "DSP/DspUtilities.h"
#include <atomic>
#include <vector>

// EMBER De-Esser - the audio engine (no JUCE plugin classes, so the tests drive it directly).
//
//   input ──┬────────────────────────── delay (latency L) ──► S bell / wide gain ──► mix ──► output gain
//           └─ detector (4th-order high-pass at `freq`, stereo-linked peak, optional relative to the
//              full-band level) ─► soft-knee 4:1 ─► range cap ─► attack / release ─► delayed by L - lookahead
//
// The latency is constant (5 ms) whatever the lookahead, so hosts never see it change; the lookahead only
// decides how early the reduction starts before an S. At 0 dB reduction the split mode is an exact identity
// (the bell is skipped), so a voice without S passes bit-exact apart from the delay.
//
// Split (2026-11-23, from the sound audit EMBER_KLANG_TEST.md): a wide dynamic bell centred on the S
// (splitCenterRatio x freq, Q splitQ) instead of the old open high shelf at freq. The shelf took 1.4x more off the
// air above the S than off the S itself and pulled the colour of the S down by ~600 Hz (towards "sh" / a lisp);
// the bell takes more off the S core (15 takes, AUTO: -4.5 instead of -3.3 dB), leaves the air on it and moves the
// colour by ~70 Hz (Pro-DS wideband: 145 Hz).
namespace ember
{
    using namespace vocalypse::dsp;

    struct Settings
    {
        float freq = 6200.0f;        // detection corner, Hz (the split bell sits at splitCenterRatio x freq)
        float thresholdDb = -32.0f;
        float rangeDb = 5.0f;        // most the S band is ever reduced (before Amount)
        float amount = 0.5f;         // Amount 0..1; 0.5 = the reduction threshold / range ask for, 1 = 1.5x (amountScale)
        float attackMs = 0.5f;
        float releaseMs = 60.0f;
        float lookaheadMs = 2.0f;    // 0 .. maxLookaheadMs
        float mix = 1.0f;
        float outputDb = 0.0f;
        int mode = 0;                // 0 = split (only the band above freq), 1 = wideband
        bool relative = true;        // detection relative to the full-band level (loud and quiet phrases alike)
        bool smart = true;           // only where the S band outweighs the body of the voice (relative implied)
        int listen = 0;              // 0 = off, 1 = S band (detector band), 2 = delta (what is removed)
        bool bypass = false;
    };

    inline constexpr float maxLookaheadMs = 5.0f;
    // Split bell: centre = the typical S of a voice whose detection corner is `freq` (AUTO puts the corner at
    // 0.8 x the S centre), Q 0.4 is wide enough (about 3 octaves) to catch the S even when it sits half an
    // octave off. Measured Q 0.4 / 0.5 / 0.7 and shelf variants on 15 takes: 0.4 keeps the colour best.
    inline constexpr float splitCenterRatio = 1.25f, splitQ = 0.4f;
    // The detector band loses ~5 dB per octave of S energy as the corner rises (15 takes: 5 kHz +1.5 dB,
    // 8 kHz -1.9 dB against 6.5 kHz). The detector adds it back, so a threshold means the same at every
    // frequency and the voice buttons aim instead of turning EMBER up or down (HIGH used to be the weakest
    // setting even for high voices).
    inline constexpr float detectorTiltDbPerOct = 5.0f, detectorTiltRefHz = 6500.0f;
    // Amount: 0 % = off, 50 % = as set, 100 % = 1.5x (was 2x: "Kräftig" + 100 % lisped half of the S)
    inline float amountScale (float amount) noexcept
    {
        return amount <= 0.5f ? 2.0f * amount : 1.0f + (amount - 0.5f);
    }
    inline constexpr float ratio = 4.0f;
    inline constexpr float kneeDb = 6.0f;
    inline constexpr float maxReductionDb = 24.0f;
    // Relative detection: band level - full-band envelope + referenceDb, so a threshold means the same as in
    // absolute mode for a voice peaking at -18 dBFS. Below fullFloorDb (near-silence) nothing is de-essed.
    inline constexpr float referenceDb = -18.0f;
    inline constexpr float fullFloorDb = -60.0f;
    // Smart detection (2026-11-13, measured against FabFilter Pro-DS on 9 real takes): the energy above
    // smartBandHz against the body of the voice (150 Hz - 3 kHz), smartEnvMs RMS envelopes. Real S sit at
    // +1 .. +20 dB, vowels under -13 dB, the loud S-into-vowel transitions at -10 .. -1 dB. Weight 0 at
    // smartNoneDb, 1 at smartFullDb: a transition keeps its vowel, only the S itself is taken down.
    inline constexpr float smartNoneDb = -1.0f, smartFullDb = 5.0f;
    inline constexpr float smartBandHz = 4000.0f, smartEnvMs = 5.0f;
    // The weight follows fast (it is the gate that keeps vowels out of the reduction)
    inline constexpr float smartRiseMs = 0.5f, smartFallMs = 1.0f;

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

        // Smart detection: how much the energy above smartBandHz outweighs the body of the voice
        // (150 Hz - 3 kHz), in dB, from 5 ms RMS envelopes, and the resulting weight 0..1 for the reduction.
        // An S is noise high up with little body; a vowel, or a vowel the S runs into, has its energy in the
        // body -> weight 0, left alone.
        float getSibilanceIndexDb() const noexcept { return indexDb; }
        float getSibilanceWeight() const noexcept { return weight; }
        static float weightForIndex (float indexDb) noexcept;

    private:
        double fs = 44100.0;
        Biquad hp1, hp2, bodyHp, bodyLp, idxHp1, idxHp2;
        float fullEnv = 0.0f, fullRelease = 0.0f;
        float sEnv = 0.0f, bodyEnv = 0.0f, envCoeff = 0.0f;   // energies (mean square)
        float indexDb = -60.0f, weight = 0.0f, tiltDb = 0.0f;
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
        // Loudest detector level of the last block (dB, relative or absolute as set) and the smart weight there
        std::atomic<float> detectorDb { -100.0f }, detectorWeight { 0.0f };

    private:
        void updateBell (float reductionDb) noexcept;

        double fs = 44100.0;
        int latency = 0;
        Settings settings;
        Detector detector;
        Biquad bell, listenHp1, listenHp2;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> freq { 6500.0f };
        juce::SmoothedValue<float> mixSm { 1.0f }, outSm { 1.0f }, bypassSm { 0.0f };
        float attackCoeff = 0.0f, releaseCoeff = 0.0f, riseCoeff = 0.0f, fallCoeff = 0.0f;
        float envDb = 0.0f, bellDb = 0.0f, weightSm = 0.0f;

        // Delay lines: audio (per channel) and the reduction envelope
        std::array<std::vector<float>, maxChannels> audioDelay;
        std::vector<float> envDelay;
        int writePos = 0, delaySize = 1, gridPhase = 0;
        bool bellStale = false;
        std::vector<float> dryScratch[maxChannels], wetScratch[maxChannels];
    };
}
