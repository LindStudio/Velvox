#pragma once

#include "DspUtilities.h"

namespace velvox::dsp
{
    struct ReverbSettings
    {
        // Character (persisted choice index - append only):
        // 0 Room, 1 Plate, 2 Hall, 3 Large Hall, 4 Ambience, 5 Atmosphere, 6 Cinematic, 7 Ethereal
        int type = 1;
        float decaySeconds = 1.8f;  // T30 of the whole tail (broadband); the mids ring a little longer when damped
        float preDelayMs = 20.0f;
        float size = 0.6f;          // 0..1
        float dampingHz = 7000.0f;  // above this the tail decays at least twice as fast
        float lowCutHz = 200.0f;    // high-pass on the reverb input
        float width = 1.0f;         // 0..1

        // 0.4 (defaults = the character's own voicing)
        float early = 0.5f;          // 0..1 early reflections level
        float diffusion = 0.7f;      // 0..1 how fast the echoes smear into a dense tail
        float modulation = 0.3f;     // 0..1 slow movement inside the tail (lushness, no metallic ringing)
        float bassMultiplier = 1.0f; // 0.5..2 decay of the lows relative to the mids
        float highCutHz = 12000.0f;  // low-pass on the reverb return
    };

    // Algorithmic reverb (own design, 0.4; quality pass 0.6):
    //
    //   input -> pre-delay -> low cut -> early reflections (stereo tap pattern of the character)
    //                                 -> input diffusion (6 all-passes per channel, incommensurate lengths)
    //                                 -> 16-line feedback delay network
    //   late network: every line = modulated delay (cubic interpolation, one slow sine per line at
    //                 incommensurate rates) -> short density all-pass -> bass shelf (low decay x
    //                 bassMultiplier) -> absorption shelf (the longest loop's Jot low-pass law, RT60
    //                 halved at dampingHz, matched per line at DC / damping / Nyquist so every loop has
    //                 the same RT60(f) and no mode outlives its neighbours); the lines are mixed by an
    //                 orthogonal 16x16 Hadamard matrix (lossless), so the decay is set by the
    //                 absorption filters alone and can never run away.
    //   decay: the set decay is the broadband T30 of the tail; decayCompensationFor() lengthens the
    //          loop T60 (x 1..1.6) for the damping and the build-up of long lines (0.6).
    //   output: early + late -> high cut -> width (mid/side).
    //
    // Outputs WET ONLY; the chain adds the dry signal exactly once. All memory is allocated
    // in prepare(); changing the character fades the return out and back in (no click, no burst).
    class ReverbModule
    {
    public:
        static constexpr int numLines = 16;
        static constexpr int numDiffusers = 6;
        static constexpr int numTypes = 8;
        static constexpr int maxEarlyTaps = 28;
        static constexpr float maxPreDelayMs = 250.0f;
        static constexpr float maxDecaySeconds = 20.0f;

        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        void setSettings (const ReverbSettings& s) noexcept;
        void process (const float* const* input, float* const* wetOut, int numChannels, int numSamples) noexcept;

        // Character voicing (exposed for tests and the UI)
        struct Voicing
        {
            const char* name;
            float lineMinMs, lineMaxMs;   // shortest / longest late line at size 1.0
            float earlyMs;                // span of the early reflections at size 1.0
            float earlyLevel;             // early reflections at early = 0.5
            float diffusion;              // input diffusion all-pass gain at diffusion = 1
            float densityMs;              // in-loop all-pass length (longest line)
            float modRateHz, modDepthMs;  // at modulation = 1
            float hfBias;                 // multiplies the damping frequency (bright > 1, dark < 1)
            float outputGain;             // keeps the characters at a similar loudness
        };
        static const Voicing& voicing (int type) noexcept;

        // Loop gain of one line at DC (< 1 = stable); for tests
        float loopGainAtDc (int line) const noexcept { return gainMid[(size_t) line] * (1.0f + bassK[(size_t) line]); }

        // Decay display feed (UI, message thread or audio thread, no allocation): the RT60 in seconds the
        // tail has at `hz` for these settings - analytic, from the same loop filters the engine uses.
        // Draw a decay-per-frequency curve by calling it at a few frequencies.
        static float predictedRt60 (const ReverbSettings& s, float hz, double sampleRate = 48000.0) noexcept;

        // The decay control means the T30 of the whole tail (what is heard, all frequencies together),
        // not only the mid band: the damping makes the highs decay faster, so the loop T60 in the mids is
        // lengthened by this factor (1..maxCompensation) until the broadband T30 equals the set decay.
        static float decayCompensationFor (const ReverbSettings& s, double sampleRate) noexcept;
        float decayCompensation() const noexcept { return compensation.getTargetValue(); }
        static constexpr float maxCompensation = 1.6f;
        static constexpr double dampingExponent = 0.5, buildUpWeight = 1.3;

    private:
        struct DelayLine
        {
            std::vector<float> storage;
            float* buffer = nullptr;   // inside `storage`, at a staggered cache position
            int writePos = 0;
            int mask = 0;   // size is a power of two

            // `stagger` gives every line a different cache-set offset: all lines are written
            // at the same index each sample, and power-of-two buffers that happen to start at
            // addresses congruent modulo 4 kB would all compete for the same L1 set
            void allocate (int minSize, int stagger);
            void clear() noexcept;
            void write (float v) noexcept
            {
                buffer[writePos] = v;
                writePos = (writePos + 1) & mask;
            }
            // `delay` samples behind the newest sample (clamped to the buffer)
            float readCubic (float delay) const noexcept;
            float readLinear (float delay) const noexcept;
        };

        struct Allpass
        {
            std::vector<float> buffer;
            int pos = 0, length = 1;
            float g = 0.6f;

            void allocate (int maxLength);
            void clear() noexcept;
            void setLength (int samples) noexcept
            {
                length = juce::jlimit (1, (int) buffer.size(), samples);
                if (pos >= length)
                    pos = 0;
            }
            float process (float x) noexcept
            {
                const float delayed = buffer[(size_t) pos];
                const float v = x + g * delayed;
                buffer[(size_t) pos] = v;
                if (++pos >= length)
                    pos = 0;
                return delayed - g * v;
            }
        };

        void configureTopology() noexcept;      // line, all-pass and tap lengths for the type and size
        void updateDecayFilters() noexcept;     // per-line absorption from decay, damping and bass

        double fs = 44100.0;
        ReverbSettings settings;
        int activeType = 1;

        std::array<DelayLine, maxChannels> preDelay;
        std::array<DelayLine, numLines> lines;
        std::array<Allpass, numLines> density;
        std::array<std::array<Allpass, numDiffusers>, maxChannels> diffusers;
        std::array<std::array<Allpass, 2>, maxChannels> earlySmear;   // softens the discrete reflections

        // Per line
        std::array<float, numLines> baseDelay {}, gainMid {}, dampA {}, dampB {}, dampC {}, dampX {}, dampZ {}, bassK {}, bassZ {};
        // Delay modulation: one slow sine per line, rates incommensurate (phase in cycles, 0..1)
        std::array<double, numLines> lfoPhase {}, lfoInc {};
        std::array<float, numLines> modNow {}, modStep {};
        float bassCoeff = 0.0f;   // one-pole low-pass of the bass shelf crossover

        // Early reflections (tap delays relative to the span, gains per side)
        struct Tap { float position = 0.0f, gainL = 0.0f, gainR = 0.0f; };
        std::array<Tap, maxEarlyTaps> taps {};
        int numTaps = 0;

        Biquad inputLowCut, outputHighCut;

        juce::SmoothedValue<float> sizeScale { 1.0f }, preDelaySamples { 0.0f }, width { 1.0f }, early { 0.5f },
                                   topologyGain { 1.0f }, diffusion { 0.7f }, modulation { 0.3f };
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> decay { 1.8f }, damping { 7000.0f }, lowCut { 200.0f },
                                                                               highCut { 12000.0f }, bass { 1.0f }, compensation { 1.0f };
        // inputs of the last decay-compensation computation (recomputed only when one changes)
        std::array<float, 7> compKey { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
        void updateCompensationTarget() noexcept;
        float appliedScale = -1.0f;
        static constexpr int updateInterval = 32;
    };
}
