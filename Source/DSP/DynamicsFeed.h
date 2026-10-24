#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

namespace velvox::dsp
{
    // Lock-free single-producer / single-consumer queue of small values for the UI
    // data feeds (0.6 module panels). The audio thread pushes (never blocks, never
    // allocates; a full queue drops the new value), the UI thread pulls.
    template <typename T, int Capacity>
    class FrameFifo
    {
        static_assert (std::is_trivially_copyable_v<T>, "FrameFifo needs a trivially copyable type");

    public:
        bool push (const T& value) noexcept
        {
            int start1, size1, start2, size2;
            fifo.prepareToWrite (1, start1, size1, start2, size2);
            if (size1 + size2 < 1)
                return false;
            data[(size_t) (size1 > 0 ? start1 : start2)] = value;
            fifo.finishedWrite (1);
            return true;
        }

        // Copies up to `max` values (oldest first) into dest; returns the number copied
        int pull (T* dest, int max) noexcept
        {
            int start1, size1, start2, size2;
            fifo.prepareToRead (max, start1, size1, start2, size2);
            std::copy_n (data.data() + start1, size1, dest);
            std::copy_n (data.data() + start2, size2, dest + size1);
            fifo.finishedRead (size1 + size2);
            return size1 + size2;
        }

        int available() const noexcept { return fifo.getNumReady(); }

        // Only when neither side is running (prepare / reset of the owner)
        void clear() noexcept { fifo.reset(); }

    private:
        // AbstractFifo keeps one slot free: Capacity + 1 slots hold Capacity values
        juce::AbstractFifo fifo { Capacity + 1 };
        std::array<T, (size_t) Capacity + 1> data {};
    };

    // One column of a scrolling dynamics display (Pro-C style): peak input and output
    // level and the largest gain reduction within one frame.
    struct DynamicsFrame
    {
        float inputDb = -120.0f;
        float outputDb = -120.0f;
        float gainReductionDb = 0.0f;   // positive dB of reduction
    };

    // Audio thread: add() once per sample; every `frameMs` a frame is queued.
    // UI thread: pull() the frames that arrived since the last call.
    class DynamicsHistory
    {
    public:
        static constexpr int capacity = 2048;   // about 10 s at 5 ms frames
        static constexpr float floorDb = -120.0f;

        void prepare (double sampleRate, double frameMs = 5.0)
        {
            samplesPerFrame = std::max (1, juce::roundToInt (sampleRate * frameMs * 0.001));
            seconds = samplesPerFrame / sampleRate;
            reset();
        }

        void reset() noexcept
        {
            fifo.clear();
            count = 0;
            inPeak = outPeak = grPeak = 0.0f;
        }

        void add (float inAbs, float outAbs, float grDb) noexcept
        {
            inPeak = std::max (inPeak, inAbs);
            outPeak = std::max (outPeak, outAbs);
            grPeak = std::max (grPeak, grDb);
            if (++count < samplesPerFrame)
                return;
            fifo.push ({ juce::Decibels::gainToDecibels (inPeak, floorDb),
                         juce::Decibels::gainToDecibels (outPeak, floorDb), grPeak });
            count = 0;
            inPeak = outPeak = grPeak = 0.0f;
        }

        int pull (DynamicsFrame* dest, int max) noexcept { return fifo.pull (dest, max); }
        double frameSeconds() const noexcept { return seconds; }

    private:
        FrameFifo<DynamicsFrame, capacity> fifo;
        int samplesPerFrame = 240;
        double seconds = 0.005;
        int count = 0;
        float inPeak = 0.0f, outPeak = 0.0f, grPeak = 0.0f;
    };
}
