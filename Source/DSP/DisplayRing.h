#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>

namespace velvox::dsp
{
    // Lock-free history of small display frames (one 64-bit word each) from one writer
    // (the audio thread) to any number of readers (the UI). The writer never waits and
    // never allocates; a reader copies the newest frames and drops the ones the writer
    // may have overwritten while it was copying. Each frame is a single atomic word, so
    // a frame is never torn.
    template <int Capacity>
    class DisplayRing
    {
        static_assert (Capacity > 1 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

    public:
        // Writer only
        void push (std::uint64_t frame) noexcept
        {
            const auto c = count.load (std::memory_order_relaxed);
            frames[(size_t) (c & (Capacity - 1))].store (frame, std::memory_order_relaxed);
            count.store (c + 1, std::memory_order_release);
        }

        void clear() noexcept { count.store (0, std::memory_order_release); }

        // Copies up to maxFrames of the newest frames, oldest first; returns the count
        int readLatest (std::uint64_t* dest, int maxFrames) const noexcept
        {
            const auto c1 = count.load (std::memory_order_acquire);
            const auto n = (std::uint64_t) std::min<std::uint64_t> ({ (std::uint64_t) std::max (0, maxFrames), c1, (std::uint64_t) Capacity });
            const auto start = c1 - n;
            for (std::uint64_t i = 0; i < n; ++i)
                dest[i] = frames[(size_t) ((start + i) & (Capacity - 1))].load (std::memory_order_relaxed);
            std::atomic_thread_fence (std::memory_order_acquire);

            // Frames older than (newest count - Capacity + 1) may have been overwritten meanwhile
            const auto c2 = count.load (std::memory_order_relaxed);
            const auto firstValid = c2 + 1 > (std::uint64_t) Capacity ? c2 + 1 - (std::uint64_t) Capacity : 0;
            if (c2 < c1 || firstValid >= start + n)
                return 0;   // cleared, or everything overwritten
            const auto drop = firstValid > start ? firstValid - start : 0;
            if (drop > 0)
                std::copy (dest + drop, dest + n, dest);
            return (int) (n - drop);
        }

        // Frames pushed since the last clear (the reader can tell how many are new)
        std::uint64_t written() const noexcept { return count.load (std::memory_order_acquire); }

    private:
        std::array<std::atomic<std::uint64_t>, Capacity> frames {};
        std::atomic<std::uint64_t> count { 0 };
    };
}
