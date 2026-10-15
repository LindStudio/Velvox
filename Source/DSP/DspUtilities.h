#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "Filters.h"

#include <atomic>
#include <vector>

namespace velvox::dsp
{
    // Combines a module's on/off switch and dry/wet amount into one smoothed
    // crossfade, so toggling a module or moving its Mix never clicks.
    // Only valid for zero-latency modules (dry and wet must be time aligned).
    class ModuleGate
    {
    public:
        void prepare (double sampleRate, int maxBlockSize)
        {
            dry.setSize (maxChannels, maxBlockSize, false, true, false);
            amount.reset (sampleRate, 0.03);
            amount.setCurrentAndTargetValue (target);
        }

        void setTargets (bool enabled, float mix01) noexcept
        {
            target = enabled ? juce::jlimit (0.0f, 1.0f, mix01) : 0.0f;
            amount.setTargetValue (target);
        }

        // True when the module is fully off and needs no processing at all
        bool isFullyBypassed() const noexcept
        {
            return target <= 0.0f && ! amount.isSmoothing();
        }

        // True when the module just left the fully-bypassed state
        bool consumeWakeUp() noexcept
        {
            const bool active = ! isFullyBypassed();
            const bool woke = active && ! wasActive;
            wasActive = active;
            return woke;
        }

        bool isFullyWet() const noexcept
        {
            return target >= 1.0f && ! amount.isSmoothing();
        }

        void captureDry (const float* const* channels, int numChannels, int numSamples) noexcept
        {
            for (int ch = 0; ch < numChannels; ++ch)
                dry.copyFrom (ch, 0, channels[ch], numSamples);
        }

        // out = dry + (wet - dry) * amount
        void mix (float* const* channels, int numChannels, int numSamples) noexcept
        {
            if (isFullyWet())
                return;

            if (! amount.isSmoothing())
            {
                const float a = amount.getCurrentValue();
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const auto* d = dry.getReadPointer (ch);
                    auto* w = channels[ch];
                    for (int i = 0; i < numSamples; ++i)
                        w[i] = d[i] + (w[i] - d[i]) * a;
                }
                return;
            }

            for (int i = 0; i < numSamples; ++i)
            {
                const float a = amount.getNextValue();
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const float d = dry.getSample (ch, i);
                    channels[ch][i] = d + (channels[ch][i] - d) * a;
                }
            }
        }

    private:
        juce::AudioBuffer<float> dry;
        juce::SmoothedValue<float> amount { 0.0f };
        float target = 0.0f;
        bool wasActive = false;
    };

    // Fixed integer delay used to time-align dry paths with latent processing.
    class IntegerDelay
    {
    public:
        void prepare (int maxDelaySamples)
        {
            for (auto& b : buffers)
                b.assign ((size_t) maxDelaySamples + 1, 0.0f);
            reset();
        }

        void setDelay (int samples) noexcept
        {
            delay = juce::jlimit (0, (int) buffers[0].size() - 1, samples);
        }

        int getDelay() const noexcept { return delay; }

        void reset() noexcept
        {
            for (auto& b : buffers)
                std::fill (b.begin(), b.end(), 0.0f);
            writePos = 0;
        }

        void process (float* const* channels, int numChannels, int numSamples) noexcept
        {
            if (delay == 0)
                return;

            const int size = (int) buffers[0].size();
            int pos = writePos;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& buf = buffers[(size_t) ch];
                pos = writePos;
                for (int i = 0; i < numSamples; ++i)
                {
                    buf[(size_t) pos] = channels[ch][i];
                    int readPos = pos - delay;
                    if (readPos < 0)
                        readPos += size;
                    channels[ch][i] = buf[(size_t) readPos];
                    if (++pos == size)
                        pos = 0;
                }
            }

            writePos = pos;
        }

    private:
        std::array<std::vector<float>, maxChannels> buffers;
        int writePos = 0;
        int delay = 0;
    };

    // Lock-free peak meter: audio thread pushes, UI thread pulls and resets.
    struct PeakMeter
    {
        std::atomic<float> peak { 0.0f };

        void push (float value) noexcept
        {
            float prev = peak.load (std::memory_order_relaxed);
            while (value > prev && ! peak.compare_exchange_weak (prev, value, std::memory_order_relaxed))
            {
            }
        }

        float pull() noexcept { return peak.exchange (0.0f, std::memory_order_relaxed); }
    };

    // Lock-free mono sample tap for the UI spectrum analyser. The audio thread
    // writes the channel average (never allocates, never blocks; drops samples
    // when the UI is not reading), the UI thread drains it. Display only - the
    // audio passing through is never touched.
    class SampleTap
    {
    public:
        static constexpr int capacity = 16384;

        void push (const float* const* io, int numChannels, int n) noexcept
        {
            int start1, size1, start2, size2;
            fifo.prepareToWrite (n, start1, size1, start2, size2);
            const float scale = numChannels > 1 ? 0.5f : 1.0f;
            const auto write = [&] (int dest, int count, int offset)
            {
                for (int i = 0; i < count; ++i)
                {
                    float s = io[0][offset + i];
                    if (numChannels > 1)
                        s += io[1][offset + i];
                    data[(size_t) (dest + i)] = s * scale;
                }
            };
            write (start1, size1, 0);
            write (start2, size2, size1);
            fifo.finishedWrite (size1 + size2);
        }

        // Copies up to maxSamples into dest; returns the number copied
        int pull (float* dest, int maxSamples) noexcept
        {
            int start1, size1, start2, size2;
            fifo.prepareToRead (maxSamples, start1, size1, start2, size2);
            std::copy_n (data.data() + start1, size1, dest);
            std::copy_n (data.data() + start2, size2, dest + size1);
            fifo.finishedRead (size1 + size2);
            return size1 + size2;
        }

    private:
        juce::AbstractFifo fifo { capacity };
        std::array<float, capacity> data {};
    };
}
