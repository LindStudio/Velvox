#include "CheckModule.h"

namespace velvox::dsp
{
    namespace
    {
        constexpr float fullScale = 0.999f;
        constexpr int clipRunLength = 3;
    }

    CheckModule::CheckModule()
        : left ((size_t) tapCapacity, 0.0f), right ((size_t) tapCapacity, 0.0f)
    {
    }

    void CheckModule::process (const float* const* ch, int numChannels, int n) noexcept
    {
        if (! enabled || numChannels <= 0)
            return;

        if (resetRequested.exchange (false))
        {
            peak.store (0.0f);
            clipEvents.store (0);
            clipRun = 0;
        }

        const float* l = ch[0];
        const float* r = ch[numChannels > 1 ? 1 : 0];

        float p = peak.load (std::memory_order_relaxed);
        int events = 0;
        bool differs = false;
        for (int i = 0; i < n; ++i)
        {
            const float a = std::max (std::abs (l[i]), std::abs (r[i]));
            p = std::max (p, a);
            if (a >= fullScale)
            {
                if (++clipRun == clipRunLength)
                    ++events;
            }
            else
            {
                clipRun = 0;
            }
            differs = differs || std::abs (l[i] - r[i]) > 1.0e-6f;
        }
        peak.store (p, std::memory_order_relaxed);
        if (events > 0)
            clipEvents.fetch_add (events, std::memory_order_relaxed);
        if (differs)
            stereoContent.store (true, std::memory_order_relaxed);

        int start1, size1, start2, size2;
        fifo.prepareToWrite (n, start1, size1, start2, size2);
        std::copy_n (l, size1, left.data() + start1);
        std::copy_n (r, size1, right.data() + start1);
        std::copy_n (l + size1, size2, left.data() + start2);
        std::copy_n (r + size1, size2, right.data() + start2);
        fifo.finishedWrite (size1 + size2);
        if (size1 + size2 < n)
            droppedSamples.fetch_add (n - size1 - size2, std::memory_order_relaxed);
    }

    int CheckModule::pull (float* destL, float* destR, int maxSamples) noexcept
    {
        int start1, size1, start2, size2;
        fifo.prepareToRead (maxSamples, start1, size1, start2, size2);
        std::copy_n (left.data() + start1, size1, destL);
        std::copy_n (right.data() + start1, size1, destR);
        std::copy_n (left.data() + start2, size2, destL + size1);
        std::copy_n (right.data() + start2, size2, destR + size1);
        fifo.finishedRead (size1 + size2);
        return size1 + size2;
    }
}
