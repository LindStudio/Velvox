#pragma once

#include "DspUtilities.h"
#include "MixLaw.h"

namespace velvox::dsp
{
    // Voice layout of the Doubler (0.6). Classic: the 0.2 controls (Delay, Detune, Width) set two
    // voices as before - every project from 0.2-0.5 loads in this mode. Two / Four: the per-voice
    // controls (on, delay, detune, gain, pan) like Waves Doubler.
    enum class DoublerMode { classic = 0, two = 1, four = 2 };

    struct DoublerVoiceSettings
    {
        bool on = true;
        float delayMs = 14.0f;
        float detuneCents = 0.0f;   // static pitch offset (-30..+30 ct)
        float gainDb = 0.0f;
        float pan = 0.0f;           // -1 (left) .. +1 (right), scaled by `width`
    };

    struct DoublerSettings
    {
        bool enabled = false;
        float mix = 0.5f;          // level of the doubled voices (0..1), dry stays at unity
        float width = 0.8f;        // 0 = voices in the centre, 1 = at their pan positions (Classic: hard left / right)
        float delayMs = 14.0f;     // Classic: offset of the left voice (right = 1.37x)
        float detuneCents = 8.0f;  // Classic: modulation depth (pitch wobble) of the voices
        float monoSafe = 0.5f;     // 0 = voices fully in the mono sum, 1 = side only (mono sum = dry)
        float toneHz = 9000.0f;    // high cut on the voices
        // legacy: the dry stays at unity at every Mix; true: the dry fades out above 50 %, 100 % = voices only
        MixLaw mixLaw = MixLaw::legacy;
        float lawMix = -1.0f;      // Mix that sets the dry gain on the true law (< 0: `mix`); set by macros

        // 0.6 engine
        DoublerMode mode = DoublerMode::classic;
        std::array<DoublerVoiceSettings, 4> voices { {
            { true, 14.0f, 0.0f, 0.0f, -1.0f },
            { true, 19.18f, 0.0f, 0.0f, +1.0f },
            { true, 24.0f, -5.0f, -3.0f, -0.5f },
            { true, 31.0f, +5.0f, -3.0f, +0.5f },
        } };
        float modDepthCents = 8.0f;   // Two / Four: modulation depth (Classic uses detuneCents)
        float modRateHz = 0.31f;      // modulation speed of voice 1 (the others run at their own ratio)
        float lowCutHz = 120.0f;      // high-pass on the voices
        float dryDb = 0.0f;           // level of the original (<= -60 dB = off)
    };

    // One resolved voice: what the engine plays and what the stage strip shows
    struct DoublerVoice
    {
        bool on = false;
        float delayMs = 0.0f;
        float detuneCents = 0.0f;
        float gain = 0.0f;          // linear, before the voice-count normalisation
        float pan = 0.0f;           // effective position -1..1 (width applied)
    };

    struct DoublerVoices
    {
        int count = 2;                          // voices of the mode (2 or 4), on or off
        std::array<DoublerVoice, 4> v {};
        float modDepthCents = 8.0f;
        float normalisation = 1.0f;             // sqrt (2 / count): four voices as loud as two
    };

    // Waves-Doubler-style doubler (0.6). Every voice is the band-limited mono input read from one
    // delay line by a 6-point (5th-order) Lagrange tap:
    //  - its own delay, slowly moved by two smoothed random walks (pitch and timing wobble; every
    //    voice has its own seeds and rates, so no two voices move together),
    //  - its own constant detune: the tap drifts at (1 - ratio) samples per sample and is spliced
    //    back by a correlation-aligned raised-cosine crossfade when it reaches the end of its range
    //    - most of the time one clean tap; formants move with the detune only (< 2 % at 30 ct),
    //  - its own gain and pan (linear pan law of 0.2); Mono safe removes the voices' mid from the
    //    mono sum.
    // Zero latency: the dry passes untouched apart from the dry level and the true-law dry gain.
    class DoublerModule
    {
    public:
        static constexpr int maxVoices = 4;
        static constexpr float maxDelayMs = 60.0f;
        static constexpr float maxDetuneCents = 30.0f;
        static constexpr float maxModCents = 25.0f;

        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        void setSettings (const DoublerSettings& s) noexcept;
        void process (float* const* channels, int numChannels, int numSamples) noexcept;

        // Settings -> per-voice values (engine and UI use the same mapping)
        static DoublerVoices resolveVoices (const DoublerSettings& s) noexcept;

        // Delay modulation depth (samples) producing `cents` at modulation rate `rateHz`
        static double depthForCents (double cents, double rateHz, double sampleRate) noexcept;

        // UI feed (stage strip), written once per block: effective pan (-1..1), level (dB incl. the
        // voice-count normalisation and Mix; -100 = off), current pitch offset incl. the wobble (ct)
        // and current delay (ms) of every voice; number of voices of the mode (2 or 4)
        std::array<std::atomic<float>, maxVoices> voicePan {}, voiceGainDb {}, voiceCents {}, voiceDelayMs {};
        std::atomic<int> voiceCount { 2 };

    private:
        // Smooth random walk: raised-cosine moves between random targets of alternating sign
        struct Walk
        {
            juce::Random rng;
            double from = 0.0, to = 0.0, pos = 0.0, len = 1.0;
            double value() const noexcept;
            void advance (double samples, double meanLength) noexcept;
        };

        struct Voice
        {
            Walk walk1, walk2;
            double rateFactor = 1.0;
            double mod = 0.0, modStep = 0.0;      // delay offset of the wobble (samples), per-sample step
            double shift = 0.0;                   // splice shifter: offset of the playing tap (samples)
            double fadeShift = 0.0;               // offset of the tap that fades out
            int fadeLeft = 0;                     // samples left of the crossfade (0 = one tap)
            double turn = 0.0;                    // |offset| where the sweep turns (= its landing point)
            float detune = 0.0f;
            bool started = false;
            juce::SmoothedValue<float> delayMs { 14.0f }, gain { 0.0f }, pan { 0.0f };
        };

        float tap (double delaySamples) const noexcept;
        double findJump (double delayA, double direction, double minJ, double maxJ) const noexcept;
        void updateFilters() noexcept;
        void updateModulation (Voice& v, int samples) noexcept;

        double fs = 48000.0;
        std::vector<float> buffer;
        int mask = 0;
        int writePos = 0;
        int fadeLength = 1440;                    // 30 ms
        double shiftLimit = 576.0;                // splice shifter: tap offset within +-12 ms
        double minLanding = 96.0;                 // ... landing at least 2 ms from the centre
        double minDelaySamples = 48.0;            // 1 ms
        double maxModSamples = 480.0;             // 10 ms per walk
        int corrLength = 768;

        Biquad lowCut, highCut;
        std::array<Voice, maxVoices> voices;
        int count = 2;
        float modDepth = 8.0f, modRate = 0.31f, modDepthNow = 8.0f, modRateNow = 0.31f;
        float lowCutTarget = 120.0f, toneTarget = 9000.0f, lowCutNow = 120.0f, toneNow = 9000.0f;

        juce::SmoothedValue<float> amount { 0.0f }, monoSafe { 0.5f };
        juce::SmoothedValue<float> dryGain { 1.0f };
        bool wasActive = false;
        bool snapNext = true;     // first settings after prepare(): no glide
        MixLaw law = MixLaw::legacy;
    };
}
