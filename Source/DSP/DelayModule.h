#pragma once

#include "DspUtilities.h"

namespace velvox::dsp
{
    struct DelaySettings
    {
        float timeMs = 375.0f;
        float feedback = 0.3f;   // 0..0.9
        float lowCutHz = 300.0f;
        float highCutHz = 6000.0f;
        bool pingPong = false;

        // 0.4 (all neutral by default)
        float spreadMs = 0.0f;   // the right side repeats this much later (independent L/R timing)
        float drive = 0.0f;      // 0..1 tape-like saturation of the repeats
        float diffusion = 0.0f;  // 0..1 smears every repeat (ambient, "washed" echoes)
        float modulation = 0.0f; // 0..1 slow wow / flutter on the repeats
    };

    // Stereo feedback delay that outputs WET ONLY (the chain adds the dry signal
    // exactly once). Repeats are band-limited by a high-pass and low-pass in the
    // loop and pass through a soft limiter, so the loop can never run away.
    //
    // Delay time changes (0.6): small and continuous moves (automation, echo-map drags,
    // tempo drifts) glide tape-style - the read position follows the target smoothly
    // (50 ms, slew limited to about +-1.4 semitones), so a sweep has no flutter and no
    // dips. A jump (new note value, more than 15 ms away) crossfades between the old and
    // the new read position over 60 ms instead: no pitch dive and no click.
    // Optional in the loop: diffusion all-passes (latency compensated - a diffusion of 0
    // keeps the timing exact), saturation and slow modulation.
    class DelayModule
    {
    public:
        static constexpr float maxDelayMs = 4000.0f;
        static constexpr float maxSpreadMs = 50.0f;

        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        void setSettings (const DelaySettings& s) noexcept;

        // input: the signal to send into the delay; wetOut: receives the return
        void process (const float* const* input, float* const* wetOut, int numChannels, int numSamples) noexcept;

        static float timeForTempo (double bpm, int divisionIndex) noexcept;

        // UI read-out: the current (gliding) delay time of the left tap in ms
        std::atomic<float> displayTimeMs { 375.0f };

    private:
        float readInterpolated (const std::vector<float>& buf, float delaySamples) const noexcept;
        void updateFilters (bool snap) noexcept;

        struct Allpass
        {
            std::vector<float> buffer;
            int pos = 0;
            float process (float x, float g) noexcept
            {
                const float delayed = buffer[(size_t) pos];
                const float v = x + g * delayed;
                buffer[(size_t) pos] = v;
                if (++pos >= (int) buffer.size())
                    pos = 0;
                return delayed - g * v;
            }
        };
        static constexpr int numDiffusers = 3;

        double fs = 44100.0;
        DelaySettings settings;
        std::array<std::vector<float>, maxChannels> lines;
        std::array<std::array<Allpass, numDiffusers>, maxChannels> diffusers;
        std::array<int, maxChannels> diffuserLatency {};   // total all-pass length per side (subtracted from the read position)
        int writePos = 0;
        int size = 0;

        // Time changes: the gliding read position, the crossfade target for jumps, the fade
        double currentDelay = 1.0;   // double: the glide's last tiny steps must not vanish in float rounding
        float nextDelay = 1.0f, targetDelay = 1.0f;
        float fade = 1.0f, fadeStep = 0.0f;
        float glideCoeff = 0.0f, maxGlideStep = 0.08f, jumpSamples = 720.0f;
        bool snapTime = true;

        double modPhase1 = 0.0, modPhase2 = 0.0;
        juce::SmoothedValue<float> feedback { 0.0f }, spread { 0.0f }, drive { 0.0f }, diffusion { 0.0f }, modulation { 0.0f };
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> lowCut { 300.0f }, highCut { 6000.0f };
        Biquad hp, lp;
    };
}
