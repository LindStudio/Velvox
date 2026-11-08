#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <thread>
#include <vector>

namespace vocalypse::automix
{
    // Audio handed from the capture buffer to the analysis (a plain copy)
    struct CapturedAudio
    {
        double sampleRate = 48000.0;
        int channels = 0;                            // vocal channels (1 or 2)
        std::vector<std::vector<float>> vocal;       // [channel][sample]
        std::vector<std::vector<float>> beat;        // sidechain [channel][sample], empty = none
        int clipSamples = 0;                         // vocal samples at or above full scale
        bool stoppedEarly = false;                   // the user analysed before the target was reached
        double hostBpm = 0.0;                        // 0.8: host tempo during the capture (0 = unknown)
        // Song position (quarter notes) at the start of every 512-sample block of the stored audio, -1 = unknown.
        // Empty when the host gave no position. The segmentation of the analysis is aligned to it.
        std::vector<double> blockPpq;
        static constexpr int ppqBlock = 512;

        int numSamples() const noexcept { return vocal.empty() ? 0 : (int) vocal[0].size(); }
        double seconds() const noexcept { return sampleRate > 0.0 ? numSamples() / sampleRate : 0.0; }
        bool hasBeat() const noexcept { return ! beat.empty() && ! beat[0].empty(); }
        bool hasPpq() const noexcept { return ! blockPpq.empty(); }
    };

    struct CaptureConfig
    {
        float targetVoicedSeconds = 60.0f;   // stop once this much voice has been heard
        float maxSeconds = 120.0f;           // hard limit of the buffer (signal incl. gaps)
        float minVoicedSeconds = 5.0f;       // less is "not enough material"
        // Level independent: the capture starts with the first block that is not digital silence and stores everything
        // from there (a +-2 dB copy of the same audio captures the same window). Only the running count of "voice"
        // (progress, the 60 s stop) looks at the level: a block is voice when it is within `voiceRangeDb` of the
        // loudest block so far and above `voiceFloorDb`.
        float silenceDb = -100.0f;           // blocks below this peak are digital silence: neither start nor stored
        float voiceRangeDb = 35.0f;
        float voiceFloorDb = -65.0f;
    };

    // Bounded recording of the plugin input during playback.
    //
    //  - Memory is reserved on the message thread in arm(); the audio thread only
    //    copies samples into it (no allocation, no locks, no system calls).
    //  - The capacity is fixed: at most `maxSeconds`, and at most 5.76 M samples per channel
    //    (120 s at 48 kHz: 2 vocal channels + the mono beat = 69 MB, shorter above 48 kHz).
    //  - Capture starts with the first block that is not digital silence, digital
    //    silence (e.g. a stopped transport) is skipped, and it stops by itself
    //    once enough voiced material has been seen, the buffer is full or the host stopped.
    //  - The song position of every 512-sample block is stored when the host gives one.
    //  - The audio thread never waits. Re-arming / cancelling from the message
    //    thread uses a short hand-shake (state + "writing" flag) so the buffer
    //    is never resized while a block is being copied.
    class CaptureBuffer
    {
    public:
        enum class State : int { idle = 0, waiting, capturing, complete };

        // ---- message thread
        void arm (double sampleRate, int vocalChannels, const CaptureConfig& config);
        void cancel();
        // Ends a capture that already holds some material (the user chose to analyse it)
        bool finishNow();
        // Copy of the captured audio; only valid while the state is `complete`
        CapturedAudio copyOut() const;

        // ---- audio thread
        void push (const float* const* vocal, int vocalChannels, const float* const* beat, int beatChannels,
                   int numSamples, bool hostPlaying, double bpm = 0.0, double ppq = -1.0) noexcept;

        // ---- any thread (relaxed read-outs for the UI)
        struct Progress
        {
            State state = State::idle;
            float capturedSeconds = 0.0f;   // stored audio (signal and short gaps)
            float voicedSeconds = 0.0f;     // part of it that carried voice
            float targetSeconds = 0.0f;
            float recentRmsDb = -100.0f;    // level of the latest blocks (also while waiting)
            float peakDb = -100.0f;         // highest stored peak
            int clipSamples = 0;
            bool transportStopped = false;  // the host is stopped now, after the capture had started (cleared when it plays again)
            bool beatSeen = false;          // the sidechain carried signal
            float minVoicedSeconds = 0.0f;

            float fraction() const noexcept { return targetSeconds > 0.0f ? juce::jlimit (0.0f, 1.0f, voicedSeconds / targetSeconds) : 0.0f; }
            bool enough() const noexcept { return voicedSeconds >= minVoicedSeconds; }
        };
        Progress getProgress() const noexcept;
        State getState() const noexcept { return (State) state.load(); }

        int capacitySamples() const noexcept { return capacity; }
        // Memory of the armed buffers (vocal, beat, song positions)
        size_t memoryBytes() const noexcept;
        static int capacityFor (double sampleRate, float maxSeconds) noexcept;

    private:
        void waitForWriter() const noexcept;

        std::array<std::vector<float>, 2> vocalBuf;
        std::vector<float> beatBuf;                  // mono
        std::vector<double> ppqBuf;                  // per 512-sample block of the stored audio
        int capacity = 0;
        double fs = 48000.0;
        int channels = 2;
        CaptureConfig cfg;

        std::atomic<int> state { (int) State::idle };
        std::atomic<bool> writing { false };
        std::atomic<int> written { 0 }, voicedSamples { 0 }, clips { 0 };
        std::atomic<float> recentRms { -100.0f }, peak { 0.0f };
        std::atomic<bool> stopped { false }, beatPresent { false }, beatStored { false }, early { false }, ppqStored { false };
        std::atomic<double> lastBpm { 0.0 };
        bool sawPlaying = false;   // audio thread only
        float runningMaxDb = -200.0f;   // audio thread only: loudest block RMS since arm
        bool ppqSeen = false;           // audio thread only
    };
}
