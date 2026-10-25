#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace velvox::dsp
{
    // Lock-free hand-over of a small, trivially copyable value (e.g. the Routing) from
    // one writer (message thread) to any number of readers, including the audio thread.
    //
    // Sequence lock: the writer makes the counter odd, writes the words, makes it even.
    // A reader copies the words and accepts them only if the counter was even and did
    // not change. The audio thread uses tryLoad() with a bounded number of attempts and
    // keeps its previous value when a write is in progress - it never waits for the
    // writer (no priority inversion), it only sees the new value one block later.
    template <typename T>
    class SeqLockValue
    {
        static_assert (std::is_trivially_copyable_v<T>, "SeqLockValue needs a trivially copyable type");
        static constexpr size_t numWords = (sizeof (T) + sizeof (std::uint32_t) - 1) / sizeof (std::uint32_t);

    public:
        explicit SeqLockValue (const T& initial = T {}) noexcept { store (initial); }

        // Single writer: never call store() from two threads at once (no lock here - the counter
        // would get out of step and readers could accept a torn value). The processor writes only
        // from the message thread (VelvoxAudioProcessor::isStateWriterThread, asserted there).
        void store (const T& value) noexcept
        {
            std::array<std::uint32_t, numWords> buffer {};
            std::memcpy (buffer.data(), &value, sizeof (T));
            const auto s = sequence.load (std::memory_order_relaxed);
            sequence.store (s + 1, std::memory_order_relaxed);
            std::atomic_thread_fence (std::memory_order_release);
            for (size_t i = 0; i < numWords; ++i)
                words[i].store (buffer[i], std::memory_order_relaxed);
            sequence.store (s + 2, std::memory_order_release);
        }

        // Wait-free attempt; false when a write was in progress every time
        bool tryLoad (T& out, int attempts = 4) const noexcept
        {
            for (int a = 0; a < attempts; ++a)
            {
                const auto s1 = sequence.load (std::memory_order_acquire);
                if ((s1 & 1u) != 0)
                    continue;
                std::array<std::uint32_t, numWords> buffer {};
                for (size_t i = 0; i < numWords; ++i)
                    buffer[i] = words[i].load (std::memory_order_relaxed);
                std::atomic_thread_fence (std::memory_order_acquire);
                if (sequence.load (std::memory_order_relaxed) == s1)
                {
                    std::memcpy (&out, buffer.data(), sizeof (T));
                    return true;
                }
            }
            return false;
        }

        // For non-real-time threads: retries until it gets a consistent copy
        T load() const noexcept
        {
            T v {};
            while (! tryLoad (v, 64)) {}
            return v;
        }

        std::uint32_t version() const noexcept { return sequence.load (std::memory_order_acquire); }

    private:
        std::array<std::atomic<std::uint32_t>, numWords> words {};
        std::atomic<std::uint32_t> sequence { 0 };
    };
}
