#include "ReverbModule.h"

namespace velvox::dsp
{
    namespace
    {
        constexpr float twoPi = 6.28318530718f;

        // Mutually prime-ish line lengths in ms (at size scale 1.0)
        constexpr float roomDelaysMs[ReverbModule::numLines]  { 29.7f, 37.1f, 41.1f, 43.7f, 53.9f, 59.3f, 67.1f, 73.3f };
        constexpr float plateDelaysMs[ReverbModule::numLines] { 9.1f, 11.3f, 13.7f, 17.9f, 21.1f, 25.3f, 28.7f, 33.1f };
        constexpr float diffuserMs[ReverbModule::numDiffusers] { 4.77f, 3.59f, 12.73f, 9.31f };
        constexpr float lfoRatesHz[ReverbModule::numLines] { 0.13f, 0.37f, 0.21f, 0.53f, 0.29f, 0.71f, 0.43f, 0.89f };

        constexpr float minScale = 0.4f, maxScale = 1.6f;
        constexpr float maxModMs = 0.6f;

        inline void hadamard8 (std::array<float, ReverbModule::numLines>& v) noexcept
        {
            for (int len = 1; len < ReverbModule::numLines; len <<= 1)
                for (int i = 0; i < ReverbModule::numLines; i += len << 1)
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

    void ReverbModule::prepare (double sampleRate, int)
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

    void ReverbModule::reset()
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

    void ReverbModule::setSettings (const ReverbSettings& s) noexcept
    {
        settings = s;
        sizeScale.setTargetValue (minScale + (maxScale - minScale) * juce::jlimit (0.0f, 1.0f, s.size));
        preDelaySamples.setTargetValue ((float) (juce::jlimit (0.0f, maxPreDelayMs, s.preDelayMs) * 0.001 * fs));
        width.setTargetValue (juce::jlimit (0.0f, 1.0f, s.width));
        decay.setTargetValue (juce::jlimit (0.1f, 20.0f, s.decaySeconds));
        damping.setTargetValue (juce::jlimit (500.0f, 20000.0f, s.dampingHz));
        lowCut.setTargetValue (juce::jlimit (20.0f, 2000.0f, s.lowCutHz));
    }

    void ReverbModule::updateLineParameters() noexcept
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

    void ReverbModule::process (const float* const* input, float* const* wetOut, int numChannels, int numSamples) noexcept
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
