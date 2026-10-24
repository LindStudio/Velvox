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

    // Two-voice doubler. Each voice is the (mono) input through a slowly
    // modulated delay line: the moving delay produces a real, small pitch
    // variation and a time offset, like a second take - not just a level
    // difference between the channels. Zero latency (the dry signal is passed
    // through untouched; only voices are added).
    class DoublerModule
    {
    public:
        static constexpr float maxDelayMs = 40.0f;
        static constexpr float maxDetuneCents = 25.0f;

        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        void setSettings (const DoublerSettings& s) noexcept;
        void process (float* const* channels, int numChannels, int numSamples) noexcept;

        // Settings -> per-voice values (engine and UI use the same mapping)
        static DoublerVoices resolveVoices (const DoublerSettings& s) noexcept;

        // Delay modulation depth (samples) producing `cents` at modulation rate `rateHz`
        static double depthForCents (double cents, double rateHz, double sampleRate) noexcept;

    private:
        float read (float delaySamples) const noexcept;
        void updateFilters() noexcept;

        double fs = 48000.0;
        std::vector<float> buffer;
        int writePos = 0;

        struct Voice
        {
            double phase1 = 0.0, phase2 = 0.0;   // two incommensurate LFOs per voice
            double inc1 = 0.0, inc2 = 0.0;
            float baseFactor = 1.0f;
            Biquad lowCut, highCut;
        };
        std::array<Voice, 2> voices;

        juce::SmoothedValue<float> amount { 0.0f }, width { 0.8f }, monoSafe { 0.5f }, delayMs { 14.0f }, detune { 8.0f };
        juce::SmoothedValue<float> dryGain { 1.0f };   // true law only (legacy: always 1)
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> tone { 9000.0f };
        bool wasActive = false;
        MixLaw law = MixLaw::legacy;
    };
}
