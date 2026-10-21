#pragma once

// The reverb of version 0.3 (8-line FDN, Room / Plate), kept ONLY as a baseline for
// before/after measurements (VelvoxTests --fxrender). Never used by the plugin.

#include "DSP/ReverbModule.h"


namespace velvox::legacy
{
    using namespace velvox::dsp;
    
    // Algorithmic reverb: pre-delay -> input low cut -> 4 series all-pass
    // diffusers -> 8 line feedback delay network (orthogonal Hadamard mixing,
    // per-line decay gain from RT60, one-pole damping, slow modulation).
    // Outputs WET ONLY; the chain adds the dry signal exactly once.
    class ReverbV03
    {
    public:
        static constexpr int numLines = 8;
        static constexpr int numDiffusers = 4;
        static constexpr float maxPreDelayMs = 250.0f;

        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        void setSettings (const dsp::ReverbSettings& s) noexcept;
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
        dsp::ReverbSettings settings;
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


namespace velvox::legacy
{
    using namespace velvox::dsp;
    namespace v03detail
    {
        constexpr float twoPi = 6.28318530718f;

        // Mutually prime-ish line lengths in ms (at size scale 1.0)
        constexpr float roomDelaysMs[ReverbV03::numLines]  { 29.7f, 37.1f, 41.1f, 43.7f, 53.9f, 59.3f, 67.1f, 73.3f };
        constexpr float plateDelaysMs[ReverbV03::numLines] { 9.1f, 11.3f, 13.7f, 17.9f, 21.1f, 25.3f, 28.7f, 33.1f };
        constexpr float diffuserMs[ReverbV03::numDiffusers] { 4.77f, 3.59f, 12.73f, 9.31f };
        constexpr float lfoRatesHz[ReverbV03::numLines] { 0.13f, 0.37f, 0.21f, 0.53f, 0.29f, 0.71f, 0.43f, 0.89f };

        constexpr float minScale = 0.4f, maxScale = 1.6f;
        constexpr float maxModMs = 0.6f;

        inline void hadamard8 (std::array<float, ReverbV03::numLines>& v) noexcept
        {
            for (int len = 1; len < ReverbV03::numLines; len <<= 1)
                for (int i = 0; i < ReverbV03::numLines; i += len << 1)
                    for (int j = i; j < i + len; ++j)
                    {
                        const float a = v[(size_t) j], b = v[(size_t) (j + len)];
                        v[(size_t) j] = a + b;
                        v[(size_t) (j + len)] = a - b;
                    }

            constexpr float norm = 0.35355339f; // 1 / sqrt(8)
            for (auto& x : v)
                x *= norm;
        }
    }

    using namespace v03detail;

    inline void ReverbV03::prepare (double sampleRate, int)
    {
        fs = sampleRate;
        const auto msToSamples = [sampleRate] (float ms) { return (int) std::ceil (ms * 0.001 * sampleRate); };

        preDelay.buffer.assign ((size_t) msToSamples (maxPreDelayMs) + 4, 0.0f);

        for (auto& l : lines)
            l.buffer.assign ((size_t) msToSamples (roomDelaysMs[numLines - 1] * maxScale + maxModMs * 2.0f) + 8, 0.0f);

        for (int i = 0; i < numDiffusers; ++i)
            diffusers[(size_t) i].buffer.assign ((size_t) std::max (1, msToSamples (diffuserMs[i])), 0.0f);

        for (int i = 0; i < numLines; ++i)
            lfoInc[(size_t) i] = (float) (twoPi * lfoRatesHz[i] / sampleRate);

        sizeScale.reset (sampleRate, 0.2);
        preDelaySamples.reset (sampleRate, 0.1);
        width.reset (sampleRate, 0.05);
        decay.reset (sampleRate, 0.1);
        damping.reset (sampleRate, 0.05);
        lowCut.reset (sampleRate, 0.05);

        setSettings (settings);
        for (auto* s : { &sizeScale, &preDelaySamples, &width })
            s->setCurrentAndTargetValue (s->getTargetValue());
        for (auto* s : { &decay, &damping, &lowCut })
            s->setCurrentAndTargetValue (s->getTargetValue());

        activeType = settings.type;
        updateLineParameters();
        reset();
    }

    inline void ReverbV03::reset()
    {
        std::fill (preDelay.buffer.begin(), preDelay.buffer.end(), 0.0f);
        preDelay.writePos = 0;

        for (auto& l : lines)
        {
            std::fill (l.buffer.begin(), l.buffer.end(), 0.0f);
            l.writePos = 0;
        }

        for (auto& d : diffusers)
        {
            std::fill (d.buffer.begin(), d.buffer.end(), 0.0f);
            d.pos = 0;
        }

        dampState.fill (0.0f);
        lfoPhase.fill (0.0f);
        inputLowCut.reset();
    }

    inline void ReverbV03::setSettings (const dsp::ReverbSettings& s) noexcept
    {
        settings = s;
        sizeScale.setTargetValue (minScale + (maxScale - minScale) * juce::jlimit (0.0f, 1.0f, s.size));
        preDelaySamples.setTargetValue ((float) (juce::jlimit (0.0f, maxPreDelayMs, s.preDelayMs) * 0.001 * fs));
        width.setTargetValue (juce::jlimit (0.0f, 1.0f, s.width));
        decay.setTargetValue (juce::jlimit (0.1f, 20.0f, s.decaySeconds));
        damping.setTargetValue (juce::jlimit (500.0f, 20000.0f, s.dampingHz));
        lowCut.setTargetValue (juce::jlimit (20.0f, 2000.0f, s.lowCutHz));
    }

    inline void ReverbV03::updateLineParameters() noexcept
    {
        const bool plate = activeType == 1;
        const float* base = plate ? plateDelaysMs : roomDelaysMs;
        const float scale = sizeScale.getCurrentValue();
        const float t60 = decay.getCurrentValue();

        for (int i = 0; i < numLines; ++i)
        {
            const float d = (float) (base[i] * scale * 0.001 * fs);
            lineDelay[(size_t) i] = d;
            // Gain so that the line loses 60 dB after t60 seconds
            lineGain[(size_t) i] = std::pow (10.0f, -3.0f * d / (float) (t60 * fs));
        }

        for (auto& ap : diffusers)
            ap.g = plate ? 0.72f : 0.6f;

        modDepth = (float) ((plate ? 0.25f : maxModMs) * 0.001 * fs);
        dampCoeff = std::exp (-twoPi * damping.getCurrentValue() / (float) fs);
        inputLowCut.setCoeffs (design::highPass (fs, lowCut.getCurrentValue(), 0.70710678));
    }

    inline void ReverbV03::process (const float* const* input, float* const* wetOut, int numChannels, int numSamples) noexcept
    {
        if (settings.type != activeType)
        {
            // Changing the network topology: start from silence instead of glitching
            activeType = settings.type;
            reset();
            updateLineParameters();
        }

        const float inScale = numChannels > 1 ? 0.5f : 1.0f;

        for (int start = 0; start < numSamples; start += updateInterval)
        {
            const int n = std::min (updateInterval, numSamples - start);

            if (sizeScale.isSmoothing() || decay.isSmoothing() || damping.isSmoothing() || lowCut.isSmoothing())
            {
                sizeScale.skip (n);
                decay.skip (n);
                damping.skip (n);
                lowCut.skip (n);
                updateLineParameters();
            }

            for (int i = start; i < start + n; ++i)
            {
                float mono = 0.0f;
                for (int ch = 0; ch < numChannels; ++ch)
                    mono += input[ch][i];
                mono *= inScale;

                // Pre-delay, low cut and diffusion
                preDelay.write (mono);
                float x = preDelay.read (preDelaySamples.getNextValue());
                x = inputLowCut.process (x, 0);
                for (auto& ap : diffusers)
                    x = ap.process (x);

                // Read the network
                std::array<float, numLines> out {}, fb {};
                for (int l = 0; l < numLines; ++l)
                {
                    auto& phase = lfoPhase[(size_t) l];
                    phase += lfoInc[(size_t) l];
                    if (phase > twoPi)
                        phase -= twoPi;

                    const float d = lineDelay[(size_t) l] + modDepth * (1.0f + std::sin (phase));
                    const float o = lines[(size_t) l].read (std::max (0.0f, d - 1.0f));
                    out[(size_t) l] = o;

                    auto& z = dampState[(size_t) l];
                    z = o + dampCoeff * (z - o);
                    fb[(size_t) l] = z * lineGain[(size_t) l];
                }

                hadamard8 (fb);

                for (int l = 0; l < numLines; ++l)
                {
                    const float sign = (l & 1) ? -1.0f : 1.0f;
                    lines[(size_t) l].write (fb[(size_t) l] + x * sign * 0.5f);
                }

                // Decorrelated stereo taps
                float left  = out[0] - out[2] + out[4] - out[6] + 0.5f * (out[1] - out[5]);
                float right = out[1] - out[3] + out[5] - out[7] + 0.5f * (out[2] - out[6]);
                left *= 0.32f;
                right *= 0.32f;

                const float w = width.getNextValue();
                const float mid = 0.5f * (left + right);
                const float side = 0.5f * (left - right) * w;

                if (numChannels > 1)
                {
                    wetOut[0][i] = mid + side;
                    wetOut[1][i] = mid - side;
                }
                else
                {
                    wetOut[0][i] = mid;
                }
            }
        }

        for (auto z : dampState)
            if (! std::isfinite (z))
            {
                reset();
                break;
            }
    }
}
