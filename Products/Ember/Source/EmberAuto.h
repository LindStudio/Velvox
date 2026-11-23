#pragma once

#include <juce_core/juce_core.h>
#include "EmberEngine.h"
#include <atomic>
#include <vector>

// EMBER AUTO: Ember listens to the voice for a while and sets himself up.
//
//   AutoCapture   audio thread -> stereo buffer of the take (lock-free, made on first use, never on the audio
//                 thread). Long pauses are shortened to a fixed gap on a fixed sample grid, so the take (and
//                 the result) is the same whatever block size the host uses.
//   analyse()     background thread / tests: finds the S range of this voice and a threshold that takes
//                 autoMinTargetDb-autoMaxTargetDb off the S sounds above autoSBandHz (depending on how harsh they
//                 are) while vowels (full band, presence and air) and the vowel side of every S stay untouched
//                 (searched by rendering the take through the engine, in stereo as the plugin detects it)
namespace ember
{
    struct AutoResult
    {
        enum class Status { ok, tooShort, noSibilance, cancelled };
        Status status = Status::tooShort;

        float freq = 6200.0f;          // detection corner (the split bell sits at splitCenterRatio x freq)
        float thresholdDb = -32.0f;
        float rangeDb = 8.0f;

        // What was measured (report, tests)
        float peakHz = 0.0f;           // centre of the S energy (log-frequency centroid 3-12 kHz)
        float sibilantMedianDb = 0.0f; // detector level of a typical S (relative detection)
        float sibilant90Db = 0.0f;
        float vowel95Db = 0.0f;        // detector level that vowels hardly exceed
        float expectedReductionDb = 0.0f;   // energy the S frames lose above autoSBandHz with the chosen settings (rendered)
        float vowelReductionDb = 0.0f;      // energy the vowel frames lose (full band)
        float vowelPresenceDb = 0.0f;       // ... in the presence (2-5 kHz)
        float vowelAirDb = 0.0f;            // ... in the air (above 10 kHz)
        float mixedReductionDb = 0.0f;      // the vowel part (below autoVowelPartHz) of the mixed frames (S next to a vowel)
        float mixedPresenceDb = 0.0f;       // ... and their presence (2-5 kHz)
        float sibilanceVsVowelDb = 0.0f;    // mean level of the S frames relative to the vowel frames
        float targetDb = 0.0f;              // how much the S should lose (from sibilanceVsVowelDb)
        int sibilantFrames = 0, vowelFrames = 0, voicedFrames = 0, rejectedFrames = 0;   // rejected: breath, hi-hat, clicks
        int renders = 0;                    // engine renders the search needed
        int numChannels = 1;                // 2 = analysed in stereo (the channels differ)
        double seconds = 0.0;
    };

    // Analysis of a take (1 or 2 channels). Needs at least autoMinSeconds of voice and some S sounds.
    // Measured with the user's own settings (mode, Amount, detection, attack, lookahead): AUTO only searches
    // frequency, threshold and range (release is part of its recipe, autoReleaseMs) and leaves the rest as the user
    // set it. Mix, output, listen and bypass of `base` are ignored (the de-esser itself is measured).
    // `cancel` (optional): set from another thread, the analysis stops within a few ms (status cancelled).
    AutoResult analyse (const float* const* channels, int numChannels, int numSamples, double sampleRate, const Settings& base,
                        const std::atomic<bool>* cancel = nullptr);
    // Mono shortcuts. smart = the engine's smart detection (default, as the plugin runs it); false = plain relative
    AutoResult analyse (const float* mono, int numSamples, double sampleRate, bool smart = true,
                        const std::atomic<bool>* cancel = nullptr);
    AutoResult analyse (const float* mono, int numSamples, double sampleRate, const Settings& base,
                        const std::atomic<bool>* cancel = nullptr);

    // 2026-11-10 subtle (1.5-3.5 dB); 2026-11-13 Jakob: "mindestens genauso gut wie Pro-DS im S-Laute absenken".
    // Pro-DS at its default takes 1.3-4.1 dB (mean 2.7) off the S of our 9 bench takes; AUTO now 2.5 dB
    // (soft S, 14 dB under the vowels) to 4.5 dB (harsh S, 2 dB under the vowels). The vowels stay untouched
    // because the smart gate keeps them out of the reduction (EmberEngine.h), not because AUTO holds back.
    inline constexpr float autoMinTargetDb = 2.5f, autoMaxTargetDb = 4.5f;
    inline constexpr float autoSoftSDb = -14.0f, autoHarshSDb = -2.0f;
    // What the voice may lose (2026-11-23, fix/ember-auto): vowels over the full band, in their presence (2-5 kHz)
    // and their air (above 10 kHz) - a full-band check alone was blind to a dull vowel; the mixed frames (an S
    // running into a vowel) are judged on their vowel part (below autoVowelPartHz) and presence: their S part is
    // meant to lose (judged over the full band, the S inside them made every take look "harmed" and AUTO fell
    // 3.6-4 dB short of its target on the test take).
    inline constexpr float autoVowelMaxDb = 0.1f;
    inline constexpr float autoVowelPresenceMaxDb = 0.2f, autoVowelAirMaxDb = 0.5f;
    inline constexpr float autoMixedMaxDb = 0.5f, autoMixedPresenceMaxDb = 1.5f;
    inline constexpr double autoVowelPartHz = 3000.0;
    inline constexpr float autoMinRangeDb = 4.0f, autoMaxRangeDb = 8.0f;
    inline constexpr float autoReleaseMs = 35.0f;          // lets go before the next vowel
    // Detection corner = 0.8 x the S centre (the split bell sits on the centre: splitCenterRatio = 1 / 0.8)
    inline constexpr float autoCornerRatio = 1.0f / splitCenterRatio, autoMinCornerHz = 4500.0f, autoMaxCornerHz = 9500.0f;
    inline constexpr float autoMinThresholdDb = -45.0f;   // further down EMBER would no longer work dynamically
    inline constexpr double autoSBandHz = 4000.0;          // the S frames are judged above this
    // S frames: at most this far under the vowels in the S band (breath and bleed sit further below), and not
    // mostly above 12 kHz (hi-hat, cymbals, air alone)
    inline constexpr float autoSMaxBelowVowelDb = 26.0f;
    inline constexpr double autoMinSeconds = 3.0;          // the least voice AUTO can work with
    inline constexpr double autoEnoughSeconds = 12.0;      // stops on its own after this much voice
    inline constexpr double autoQuietSeconds = 4.0;        // no new voice for this long: done (enough heard) / "press play"
    // Below this Amount AUTO cannot reach its target (Amount scales every reduction): it sets 50 % (and says so)
    inline constexpr float autoMinAmount = 0.25f;
    // Capture: 16 s per channel, stereo (the same memory as the 30 s mono buffer before; AUTO stops at 12 s of voice).
    // Pauses longer than autoGapSeconds are cut to that gap (quiet to quiet, the detector settles in it)
    inline constexpr double autoCapacitySeconds = 16.0;
    inline constexpr double autoGapSeconds = 0.4;
    inline constexpr float autoSilenceGain = 0.00316f;    // -50 dBFS: quieter grid chunks count as pause

    class AutoCapture
    {
    public:
        AutoCapture() = default;
        ~AutoCapture() { stop(); }

        // Any thread but the audio thread (prepareToPlay). A capture that is running restarts at the new rate
        // (what it held belongs to the old one); the same rate leaves it alone.
        void prepare (double sampleRate);
        void start();          // message thread: makes the buffer on first use (never the audio thread)
        void stop() noexcept;  // message thread; when it returns the audio thread no longer writes
        bool isCapturing() const noexcept { return state.load() == capturing; }

        // Audio thread. Lock-free, never allocates. Takes the first two channels (mono is kept as mono).
        void push (const float* const* channels, int numChannels, int numSamples) noexcept;

        int getNumSamples() const noexcept { return count.load (std::memory_order_acquire); }
        double getSeconds() const noexcept;           // captured, gaps included
        double getVoicedSeconds() const noexcept;     // without the gaps
        double getSampleRate() const noexcept { return fs.load(); }
        bool isFull() const noexcept { return capacity > 0 && getNumSamples() >= capacity; }
        int getRestarts() const noexcept { return restarts.load(); }   // a rate change restarted the capture

        // Message thread, after stop(): the take (one channel when both were the same)
        struct Take
        {
            std::vector<float> channel[2];
            int numChannels = 0;
            double sampleRate = 0.0;
            int numSamples() const noexcept { return channel[0].empty() ? 0 : (int) channel[0].size(); }
        };
        Take copyTake() const;
        // Tests: direct access (after stop())
        const float* data (int channel = 0) const noexcept { return buffer[channel & 1].data(); }

    private:
        enum { idle = 0, capturing = 1 };
        void quiesce() const noexcept;        // waits until the audio thread is out of push()
        void resetLocked() noexcept;          // only while the audio thread is out (after quiesce)
        void commitChunk() noexcept;

        std::vector<float> buffer[2];
        int capacity = 0;
        std::atomic<int> count { 0 }, voiced { 0 }, state { idle }, restarts { 0 };
        mutable std::atomic<bool> inPush { false };
        std::atomic<bool> stereo { false };
        std::atomic<double> fs { 0.0 };
        // audio thread (handed over by quiesce + state)
        int chunkLen = 256, gapChunks = 1, fill = 0, silentRun = 0, committed = 0, committedVoiced = 0;
        float chunkPeak = 0.0f;
        bool chunkBad = false, chunkStereo = false;
    };
}
