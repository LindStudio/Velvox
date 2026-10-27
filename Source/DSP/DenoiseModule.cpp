#include "DenoiseModule.h"

namespace velvox::dsp
{
    void DenoiseModule::prepare (double sampleRate, int)
    {
        fs = sampleRate;
        for (auto& c : channels)
        {
            c.in.assign ((size_t) fftSize, 0.0f);
            c.out.assign ((size_t) fftSize, 0.0f);
        }
        reset();
    }

    void DenoiseModule::reset()
    {
        for (auto& c : channels)
        {
            std::fill (c.in.begin(), c.in.end(), 0.0f);
            std::fill (c.out.begin(), c.out.end(), 0.0f);
        }
        pos = 0;
        reductionDb.store (0.0f);
    }

    void DenoiseModule::processFrame (Channel&) noexcept {}

    // Task 1 stub: the latency only (a delay of `latency` samples)
    void DenoiseModule::process (float* const* io, int numChannels, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            for (int ch = 0; ch < std::min (numChannels, 2); ++ch)
            {
                auto& c = channels[(size_t) ch];
                const float x = io[ch][i];
                io[ch][i] = c.in[(size_t) pos];
                c.in[(size_t) pos] = x;
            }
            pos = (pos + 1) % fftSize;
        }
    }
}
