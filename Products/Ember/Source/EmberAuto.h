#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <vector>

// EMBER AUTO: Ember listens to the voice for a while and sets himself up.
//
//   AutoCapture   audio thread -> mono buffer of the non-silent blocks (lock-free, preallocated 30 s)
//   analyse()     message thread / tests: finds the S range of this voice and a threshold that
//                 takes ~5 dB off the S sounds while vowels stay untouched (searched by rendering the take)
namespace ember
{
    struct AutoResult
    {
        enum class Status { ok, tooShort, noSibilance };
        Status status = Status::tooShort;

        float freq = 6500.0f;          // detection / shelf corner
        float thresholdDb = -30.0f;
        float rangeDb = 8.0f;

        // What was measured (report, tests)
        float peakHz = 0.0f;           // strongest S frequency
        float sibilantMedianDb = 0.0f; // detector level of a typical S (relative detection)
        float sibilant90Db = 0.0f;
        float vowel95Db = 0.0f;        // detector level that vowels hardly exceed
        float expectedReductionDb = 0.0f;   // energy the S frames lose with the chosen settings (rendered)
        float vowelReductionDb = 0.0f;      // the same for the vowel frames
        int sibilantFrames = 0, vowelFrames = 0, voicedFrames = 0;
        double seconds = 0.0;
    };

    // Analysis of a mono take. Needs at least ~3 s of voice and some S sounds.
    AutoResult analyse (const float* mono, int numSamples, double sampleRate);

    inline constexpr float autoTargetReductionDb = 5.0f;   // energy the S sounds lose
    inline constexpr float autoVowelMaxDb = 0.3f;          // vowels lose at most this
    inline constexpr float autoCornerRatio = 0.7f, autoMinCornerHz = 4500.0f, autoMaxCornerHz = 9000.0f;
    inline constexpr double autoMinSeconds = 3.0;          // finish button works from here
    inline constexpr double autoEnoughSeconds = 12.0;      // stops on its own after this much voice
    inline constexpr double autoCapacitySeconds = 30.0;

    class AutoCapture
    {
    public:
        void prepare (double sampleRate);
        void start() noexcept;
        void stop() noexcept;
        bool isCapturing() const noexcept { return state.load (std::memory_order_acquire) == capturing; }

        // Audio thread. Blocks quieter than -50 dBFS are skipped.
        void push (const float* const* channels, int numChannels, int numSamples) noexcept;

        int getNumSamples() const noexcept { return count.load (std::memory_order_acquire); }
        double getSeconds() const noexcept { return fs > 0.0 ? getNumSamples() / fs : 0.0; }
        double getSampleRate() const noexcept { return fs; }
        bool isFull() const noexcept { return getNumSamples() >= (int) buffer.size(); }
        const float* data() const noexcept { return buffer.data(); }

    private:
        enum { idle = 0, capturing = 1 };
        std::vector<float> buffer;
        std::atomic<int> count { 0 }, state { idle };
        double fs = 0.0;
    };
}
