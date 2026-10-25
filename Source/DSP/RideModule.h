#pragma once

#include "DspUtilities.h"

namespace velvox::dsp
{
    struct RideSettings
    {
        bool enabled = false;
        float targetDb = -18.0f;    // aimed voice level (RMS)
        float amount = 0.7f;        // 0..1 share of the distance that is corrected
        float maxBoostDb = 8.0f;
        float maxCutDb = 8.0f;
        float response = 0.5f;      // 0 slow .. 1 fast
        float phrase = 0.4f;        // 0 syllable .. 1 whole phrase
        float transient = 0.6f;     // 0..1 how strongly short peaks are ignored
        float silenceDb = -50.0f;   // below: pause - never boosted
    };

    // LEVEL RIDE - vocal loudness rider (zero latency).
    //
    // Not a compressor: it does not react to every peak with a fixed ratio, it
    // moves a slow gain so that words and phrases end up near a target level.
    //  1. Detector on the voice band (100 Hz .. 8 kHz, mid signal):
    //     fast envelope (5 ms, transients) and syllable envelope (60 ms RMS).
    //  2. Phrase level: a slower average of the syllable level that only runs
    //     while the voice is active. `Phrase` blends syllable -> phrase level.
    //  3. Desired gain = amount * (target - level), limited by Max boost / cut.
    //  4. The gain follows with separate speeds (lowering faster than lifting,
    //     both set by Response) in the dB domain, ramped per sample.
    //  5. Transients (fast envelope well above the syllable level) freeze the
    //     adaptation, so consonants are neither ducked nor pumped.
    //  6. In pauses (below Ignore below) boosts are released and never added:
    //     breaths and room noise are not pulled up.
    class RideModule
    {
    public:
        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        void setSettings (const RideSettings& s) noexcept;
        void process (float* const* channels, int numChannels, int numSamples) noexcept;

        // Constant latency (the lookahead), also while the module is off
        static constexpr float lookaheadMs = 80.0f;
        int getLatencySamples() const noexcept { return latency; }

        // Word-level control law (0.6, QUALITY_BASELINE gap #8)
        static constexpr float syllableMs = 80.0f;        // syllable level window (0.5: 60 ms)
        static constexpr float pauseHoldMs = 250.0f;      // gaps shorter than this hold the gain
        static constexpr float onsetGraceMs = 60.0f;      // no transient freeze at word onsets
        static constexpr float downSlowMs = 150.0f, downFastMs = 10.0f;   // Response 0 .. 1
        static constexpr float upSlowMs = 200.0f, upFastMs = 10.0f;
        // Share of the distance to the target that is corrected: the gain lags the level, so the
        // knob maps onto a curve that makes the realised correction follow the set Amount
        static float effectiveAmount (float amount) noexcept
        {
            const float a = juce::jlimit (0.0f, 1.0f, amount);
            return 1.0f - (1.0f - a) * (1.0f - a);
        }

        // UI / test read-outs
        std::atomic<float> currentGainDb { 0.0f };
        std::atomic<float> detectedLevelDb { -100.0f };
        std::atomic<bool> voiceActive { false };

    private:
        void control() noexcept;
        void resetControl() noexcept;
        void analyse (float* const* channels, int numChannels, int numSamples) noexcept;

        static constexpr int chunkSize = 256;
        IntegerDelay lookahead;
        int latency = 0;
        int warmupSamples = 0, fadeInPos = 0, fadeInLength = 480;
        void applyWarmup (float* const* channels, int numChannels, int numSamples) noexcept;
        std::array<float, chunkSize> gains {};

        double fs = 48000.0;
        RideSettings settings;

        Biquad bandHpf, bandLpf;
        float fastPower = 0.0f, syllablePower = 0.0f;
        float fastCoeff = 0.0f, syllableCoeff = 0.0f;
        float transientClamp = 1000.0f;   // power ratio above the syllable level that is ignored
        int freezeSamples = 0;            // hold after a transient
        float phraseDb = -100.0f;
        float phrasePower = 0.0f;       // phraseDb as power (0 until the first phrase)
        float gainDb = 0.0f;           // control-rate gain
        float rampFrom = 1.0f, rampTo = 1.0f;
        int rampPos = 0;
        bool voiced = false;
        int voicedSamples = 0, pauseSamples = 0;
        int controlCounter = 0;

        juce::SmoothedValue<float> enabled { 0.0f };
        bool snapNext = true;    // first settings after prepare(): no fade from the defaults
        bool wasActive = false;
    };
}
