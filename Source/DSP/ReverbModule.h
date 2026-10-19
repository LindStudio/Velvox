#pragma once

#include "DspUtilities.h"

namespace velvox::dsp
{
    struct ReverbSettings
    {
        int type = 1;               // 0 = Room, 1 = Plate
        float decaySeconds = 1.8f;  // RT60
        float preDelayMs = 20.0f;
        float size = 0.6f;          // 0..1
        float dampingHz = 7000.0f;
        float lowCutHz = 200.0f;
        float width = 1.0f;         // 0..1
    };

    // Algorithmic reverb: pre-delay -> input low cut -> 4 series all-pass
    // diffusers -> 8 line feedback delay network (orthogonal Hadamard mixing,
    // per-line decay gain from RT60, one-pole damping, slow modulation).
    // Outputs WET ONLY; the chain adds the dry signal exactly once.
    class ReverbModule
    {
    public:
        static constexpr int numLines = 8;
        static constexpr int numDiffusers = 4;
        static constexpr float maxPreDelayMs = 250.0f;

        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        void setSettings (const ReverbSettings& s) noexcept;
        void process (const float* const* input, float* const* wetOut, int numChannels, int numSamples) noexcept;

    private:
        struct Line
        {
            std::vector<float> buffer;
            int writePos = 0;

            void write (float v) noexcept
            {
                buffer[(size_t) writePos] = v;
                if (++writePos >= (int) buffer.size())
                    writePos = 0;
            }

            // Linear interpolated read, `delay` samples behind the last write
            // (integer-safe wrap, see fractionalIndex)
            float read (float delay) const noexcept
            {
                const int newest = writePos == 0 ? (int) buffer.size() - 1 : writePos - 1;
                return readFractional (buffer, newest, delay);
            }
        };

        struct Allpass
        {
            std::vector<float> buffer;
            int pos = 0;
            float g = 0.6f;

            float process (float x) noexcept
            {
                const float delayed = buffer[(size_t) pos];
                const float v = x + g * delayed;
                buffer[(size_t) pos] = v;
                if (++pos >= (int) buffer.size())
                    pos = 0;
                return delayed - g * v;
            }
        };

        void updateLineParameters() noexcept;

        double fs = 44100.0;
        ReverbSettings settings;
        int activeType = 1;

        Line preDelay;
        std::array<Line, numLines> lines;
        std::array<Allpass, numDiffusers> diffusers;
        std::array<float, numLines> lineDelay {}, lineGain {}, dampState {};
        std::array<float, numLines> lfoPhase {}, lfoInc {};
        float dampCoeff = 0.5f;
        float modDepth = 0.0f;
        Biquad inputLowCut;

        juce::SmoothedValue<float> sizeScale { 1.0f }, preDelaySamples { 0.0f }, width { 1.0f };
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> decay { 1.8f }, damping { 7000.0f }, lowCut { 200.0f };
        static constexpr int updateInterval = 32;
    };
}
