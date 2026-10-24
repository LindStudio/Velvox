#pragma once

#include "DspUtilities.h"
#include "MixLaw.h"

namespace velvox::dsp
{
    struct DoublerSettings
    {
        bool enabled = false;
        float mix = 0.5f;          // level of the doubled voices (0..1), dry stays at unity
        float width = 0.8f;        // 0 = voices in the centre, 1 = hard left / right
        float delayMs = 14.0f;     // base offset of the voices (left voice; right = 1.37x)
        float detuneCents = 8.0f;  // peak pitch deviation of the voices
        float monoSafe = 0.5f;     // 0 = voices fully in the mono sum, 1 = side only (mono sum = dry)
        float toneHz = 9000.0f;    // high cut on the voices
        // legacy: the dry stays at unity at every Mix; true: the dry fades out above 50 %, 100 % = voices only
        MixLaw mixLaw = MixLaw::legacy;
        float lawMix = -1.0f;      // Mix that sets the dry gain on the true law (< 0: `mix`); set by macros
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
