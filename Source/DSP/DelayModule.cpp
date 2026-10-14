#include "DelayModule.h"
#include "../Parameters/ParameterLayout.h"

namespace velvox::dsp
{
    namespace
    {
        constexpr float maxFeedback = 0.9f;
        constexpr int updateInterval = 32;

        // Transparent below ~0.5, smoothly bounded to +-1.5 above
        inline float loopLimiter (float x) noexcept
        {
            return 1.5f * std::tanh (x * (1.0f / 1.5f));
        }
    }

    float DelayModule::timeForTempo (double bpm, int divisionIndex) noexcept
    {
        if (! (bpm > 1.0 && bpm < 1000.0))
            return -1.0f;
        const double ms = 60000.0 / bpm * delaydiv::beatsForIndex (divisionIndex);
        return (float) std::min ((double) maxDelayMs, ms);
    }

    void DelayModule::prepare (double sampleRate, int)
    {
        fs = sampleRate;
        size = (int) std::ceil (maxDelayMs * 0.001 * sampleRate) + 8;

        for (auto& l : lines)
            l.assign ((size_t) size, 0.0f);

        delaySamples.reset (sampleRate, 0.12);
        feedback.reset (sampleRate, 0.05);
        lowCut.reset (sampleRate, 0.05);
        highCut.reset (sampleRate, 0.05);

        setSettings (settings);
        delaySamples.setCurrentAndTargetValue (delaySamples.getTargetValue());
        feedback.setCurrentAndTargetValue (feedback.getTargetValue());
        updateFilters (true);
        reset();
    }

    void DelayModule::reset()
    {
        for (auto& l : lines)
            std::fill (l.begin(), l.end(), 0.0f);
        writePos = 0;
        hp.reset();
        lp.reset();
    }

    void DelayModule::setSettings (const DelaySettings& s) noexcept
    {
        settings = s;
        const float ms = juce::jlimit (1.0f, maxDelayMs, s.timeMs);
        delaySamples.setTargetValue ((float) (ms * 0.001 * fs));
        feedback.setTargetValue (juce::jlimit (0.0f, maxFeedback, s.feedback));
        lowCut.setTargetValue (juce::jlimit (20.0f, 20000.0f, s.lowCutHz));
        highCut.setTargetValue (juce::jlimit (200.0f, 20000.0f, s.highCutHz));
    }

    void DelayModule::updateFilters (bool snap) noexcept
    {
        if (snap)
        {
            lowCut.setCurrentAndTargetValue (lowCut.getTargetValue());
            highCut.setCurrentAndTargetValue (highCut.getTargetValue());
        }

        hp.setCoeffs (design::highPass (fs, lowCut.getCurrentValue(), 0.70710678));
        lp.setCoeffs (design::lowPass (fs, highCut.getCurrentValue(), 0.70710678));
    }

    // 4-point, 3rd order Hermite interpolation
    float DelayModule::readInterpolated (const std::vector<float>& buf, float delay) const noexcept
    {
        const float readPos = (float) writePos - delay;
        float base = std::floor (readPos);
        const float frac = readPos - base;
        int i1 = (int) base;
        while (i1 < 0)
            i1 += size;

        const int i0 = i1 == 0 ? size - 1 : i1 - 1;
        const int i2 = i1 + 1 >= size ? i1 + 1 - size : i1 + 1;
        const int i3 = i2 + 1 >= size ? i2 + 1 - size : i2 + 1;

        const float y0 = buf[(size_t) i0], y1 = buf[(size_t) i1], y2 = buf[(size_t) i2], y3 = buf[(size_t) i3];
        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }

    void DelayModule::process (const float* const* input, float* const* wetOut, int numChannels, int numSamples) noexcept
    {
        const bool pingPong = settings.pingPong && numChannels > 1;

        for (int start = 0; start < numSamples; start += updateInterval)
        {
            const int n = std::min (updateInterval, numSamples - start);

            if (lowCut.isSmoothing() || highCut.isSmoothing())
            {
                lowCut.skip (n);
                highCut.skip (n);
                updateFilters (false);
            }

            for (int i = start; i < start + n; ++i)
            {
                const float d = juce::jlimit (1.0f, (float) (size - 4), delaySamples.getNextValue());
                const float fb = feedback.getNextValue();

                float taps[maxChannels] {};
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    float t = readInterpolated (lines[(size_t) ch], d);
                    t = lp.process (hp.process (t, ch), ch);
                    taps[ch] = t;
                    wetOut[ch][i] = t;
                }

                if (pingPong)
                {
                    // Mono input enters on the left; repeats bounce L -> R -> L
                    const float in = 0.5f * (input[0][i] + input[1][i]);
                    lines[0][(size_t) writePos] = loopLimiter (in + taps[1] * fb);
                    lines[1][(size_t) writePos] = loopLimiter (taps[0] * fb);
                }
                else
                {
                    for (int ch = 0; ch < numChannels; ++ch)
                        lines[(size_t) ch][(size_t) writePos] = loopLimiter (input[ch][i] + taps[ch] * fb);
                }

                if (++writePos >= size)
                    writePos = 0;
            }
        }

        if (! hp.isStateFinite() || ! lp.isStateFinite())
            reset();
    }
}
