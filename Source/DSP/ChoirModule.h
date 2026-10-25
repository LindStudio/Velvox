#pragma once

#include "DspUtilities.h"
#include "VoiceSplice.h"

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
    //  - transposed by its own small, constant pitch offset (0.6: splice pitch
    //    shifter - one clean 6-point Lagrange tap, correlation-aligned splices;
    //    a real detune, not a chorus wobble, and no crossfade combing),
    //  - delayed by its own entry time,
    //  - drifting slowly and independently in pitch and timing (humanize: smoothed
    //    random walks with their own seeds and rates per voice),
    //  - panned to its own, alternating place (outer voices at the edges, scaled by
    //    Width) with its own slightly different level.
    // Offsets alternate up / down and are spread unevenly so no two voices move
    // together. Zero latency: the original passes untouched and is blended with
    // the voices by Mix.
    class ChoirModule
    {
    public:
        static constexpr int maxVoices = 8;
        static constexpr float maxTimingMs = 60.0f;
        static constexpr float maxDetuneCents = 40.0f;

        // Fixed character of each voice (shared with the UI so the picture matches):
        // detune (x Detune), timing (x Timing), pan (x Width), level offset in dB
        struct VoiceShape { float detune, timing, pan, gainDb; };
        static const std::array<VoiceShape, maxVoices>& shapes();
        static int voiceCountForIndex (int choiceIndex) noexcept { return 2 + 2 * juce::jlimit (0, 3, choiceIndex); }

        // Where a voice stands on the stage (UI, half circle around the lead): pan -1..1 (Width applied),
        // depth 0..1 (later entry = further back), level = its linear gain in the choir (0 = not playing)
        struct VoicePosition { float pan = 0.0f, depth = 0.0f, level = 0.0f; };
        static VoicePosition voicePosition (int voice, const ChoirSettings& s) noexcept;
        // Effective pan (-1..1) of a voice shape at a Width (0..1)
        static float stagePan (float shapePan, float width) noexcept;

        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        void setSettings (const ChoirSettings& s) noexcept;
        void process (float* const* channels, int numChannels, int numSamples) noexcept;

        // Correlation of the module output (L vs R), for the mono warning
        std::atomic<float> outputCorrelation { 1.0f };
        // UI feed: current pitch offset (ct, incl. drift), pan (-1..1) and level (linear) of each voice
        std::array<std::atomic<float>, maxVoices> voiceCents {}, voicePan {}, voiceLevel {};

    private:
        double fs = 48000.0;
        ChoirSettings settings;
        LagrangeDelayLine line;
        VoiceSpliceConfig splice;
        Biquad sourceHpf, sourceLpf;

        struct Voice
        {
            SmoothRandomWalk pitchWalk, timeWalk;
            double pitchRate = 0.2, timeRate = 0.15;   // Hz
            VoiceSpliceShifter shifter;
            float driftCents = 0.0f, driftMs = 0.0f;
            double drift = 0.0;                         // splice shifter drift (1 - ratio)
            float gain = 1.0f;                          // level offset (normalised)
            juce::SmoothedValue<float> level { 0.0f };
            juce::SmoothedValue<float> delayMs { 10.0f };
        };
        std::array<Voice, maxVoices> voices;

        juce::SmoothedValue<float> enabled { 0.0f }, mix { 0.35f }, width { 0.8f }, voiceGain { 0.5f };
        int updateCounter = 0;
        double sumLR = 0.0, sumLL = 0.0, sumRR = 0.0;
        bool snapNext = true;    // first settings after prepare(): no fade from the defaults
        bool wasActive = false;
    };
}
