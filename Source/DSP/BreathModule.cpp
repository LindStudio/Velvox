#include "BreathModule.h"

namespace velvox::dsp
{
    float BreathModule::breathGainDb (float amountPercent) noexcept
    {
        const float a = juce::jlimit (0.0f, 1.0f, amountPercent / 100.0f);
        return a <= 0.5f ? -24.0f * a : -(12.0f + 36.0f * (a - 0.5f));
    }

    void BreathModule::prepare (double sampleRate, int)
    {
        fs = sampleRate;
        for (auto& d : delayLine)
            d.assign ((size_t) std::max (1, latencyFor (fs)), 0.0f);
        reset();
    }

    void BreathModule::reset()
    {
        for (auto& d : delayLine)
            std::fill (d.begin(), d.end(), 0.0f);
        delayPos = 0;
        reductionDb.store (0.0f);
    }

    // Task 1 stub: the look-ahead latency only
    void BreathModule::process (float* const* io, int numChannels, int numSamples) noexcept
    {
        const int len = (int) delayLine[0].size();
        int pos = delayPos;
        for (int i = 0; i < numSamples; ++i)
        {
            for (int ch = 0; ch < std::min (numChannels, 2); ++ch)
            {
                auto& d = delayLine[(size_t) ch];
                const float x = io[ch][i];
                io[ch][i] = d[(size_t) pos];
                d[(size_t) pos] = x;
            }
            pos = (pos + 1) % len;
        }
        delayPos = pos;
    }
}
