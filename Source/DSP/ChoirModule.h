#pragma once

#include "DspUtilities.h"

namespace velvox::dsp
{
    struct ChoirSettings
    {
        bool enabled = false;
        int voices = 4;             // 2, 4, 6 or 8
        float detuneCents = 10.0f;  // spread of the per-voice pitch offsets
        float timingMs = 18.0f;     // spread of the per-voice entry delays
        float humanize = 0.5f;      // 0..1 slow independent drift of pitch and timing
        float width = 0.8f;         // 0..1 stereo spread
        float density = 0.5f;       // 0..1 level of the voices relative to each other
        float mix = 0.35f;          // 0..1 original <-> choir
    };

    // CHOIR - multi-voice doubling. Every voice is a copy of the (band-limited)
    // vocal that is
    //  - transposed by its own small, constant pitch offset (dual-tap delay-line
    //    pitch shifter with complementary Hann crossfades - a real detune, not a
    //    chorus wobble),
    //  - delayed by its own entry time,
    //  - drifting slowly and independently in pitch and timing (humanize),
    //  - panned to its own place.
    // Offsets alternate up / down and are spread unevenly so no two voices move
    // together. Zero latency: the original passes untouched and is blended with
    // the voices by Mix.
    class ChoirModule
    {
    public:
        static constexpr int maxVoices = 8;
        static constexpr float maxTimingMs = 60.0f;
        static constexpr float maxDetuneCents = 40.0f;

        // Fixed character of each voice (shared with the UI so the picture matches)
        struct VoiceShape { float detune, timing, pan; };
        static const std::array<VoiceShape, maxVoices>& shapes();
        static int voiceCountForIndex (int choiceIndex) noexcept { return 2 + 2 * juce::jlimit (0, 3, choiceIndex); }

        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        void setSettings (const ChoirSettings& s) noexcept;
        void process (float* const* channels, int numChannels, int numSamples) noexcept;

        // Correlation of the module output (L vs R), for the mono warning
        std::atomic<float> outputCorrelation { 1.0f };
        // Current pitch offset of each voice in cents (incl. drift), for the UI
        std::array<std::atomic<float>, maxVoices> voiceCents {};

    private:
        float readHermite (float delaySamples) const noexcept;

        double fs = 48000.0;
        ChoirSettings settings;
        std::vector<float> buffer;
        int writePos = 0;
        int mask = 0;
        float windowSamples = 1920.0f;
        Biquad sourceHpf, sourceLpf;

        struct Voice
        {
            float shiftPhase = 0.0f;               // 0..1 position inside the pitch-shift window
            double driftPhaseA = 0.0, driftPhaseB = 0.0, driftIncA = 0.0, driftIncB = 0.0;
            float driftCents = 0.0f, driftMs = 0.0f;
            juce::SmoothedValue<float> level { 0.0f };
            juce::SmoothedValue<float> delayMs { 10.0f };
        };
        std::array<Voice, maxVoices> voices;

        juce::SmoothedValue<float> enabled { 0.0f }, mix { 0.35f }, width { 0.8f }, voiceGain { 0.5f };
        std::array<float, maxVoices> shiftInc {};   // window phase step per sample (pitch ratio - 1)
        int updateCounter = 0;
        double sumLR = 0.0, sumLL = 0.0, sumRR = 0.0;
        bool snapNext = true;    // first settings after prepare(): no fade from the defaults
        bool wasActive = false;
    };
}
