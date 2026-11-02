#pragma once

// 0.8: a profile whose spectrum lies exactly on a mix target (+ delta (hz) dB), keeping the base's pitch,
// material and beat measurements - for tests of the curve fit
#include "AutoMix/VocalProfile.h"
#include "AutoMix/MixTarget.h"
#include <functional>

namespace velvox::test
{
    inline automix::VocalProfile onTarget (automix::VocalProfile base, const automix::MixTarget& t, const std::function<float (float)>& delta)
    {
        using namespace automix;
        auto& spec = base.spectrumDb;
        for (int b = 0; b < VocalProfiler::numBands; ++b)
            spec[(size_t) b] = VocalProfiler::bandCentre (b) < 70.0f ? -200.0f : -40.0f;
        for (int iteration = 0; iteration < 6; ++iteration)
        {
            const auto now = thirdsOf (spec);
            for (int b = 0; b < VocalProfiler::numBands; ++b)
            {
                if (spec[(size_t) b] <= -190.0f)
                    continue;
                const int k = juce::jlimit (0, numThirds - 1, (b - 12 + 2) / 4);
                spec[(size_t) b] += t.curveDb[(size_t) k] + delta (thirdCentre (k)) - now[(size_t) k];
            }
        }
        base.thirdsDb = thirdsOf (spec);
        base.resonances.clear();
        return base;
    }
}
