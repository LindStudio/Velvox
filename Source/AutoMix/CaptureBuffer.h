#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <thread>
#include <vector>

namespace velvox::automix
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

        int numSamples() const noexcept { return vocal.empty() ? 0 : (int) vocal[0].size(); }
        double seconds() const noexcept { return sampleRate > 0.0 ? numSamples() / sampleRate : 0.0; }
        bool hasBeat() const noexcept { return ! beat.empty() && ! beat[0].empty(); }
    };

    struct CaptureConfig
    {
        float targetVoicedSeconds = 14.0f;   // stop once this much voice has been heard
        float maxSeconds = 24.0f;            // hard limit of the buffer (signal incl. short gaps)
        float minVoicedSeconds = 5.0f;       // less is "not enough material"
        float startThresholdDb = -50.0f;     // RMS that counts as signal (dBFS)
        float silenceDb = -70.0f;            // blocks below this peak are not stored (stopped transport)
    };

    // Bounded recording of the plugin input during playback.
    //
    //  - Memory is reserved on the message thread in arm(); the audio thread only
    //    copies samples into it (no allocation, no locks, no system calls).
    //  - The capacity is fixed: at most `maxSeconds` (shorter above 96 kHz).
    //  - Capture starts with the first block that carries signal, digital
    //    silence (e.g. a stopped transport) is skipped, and it stops by itself
    //    once enough voiced material has been seen or the buffer is full.
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
                   int numSamples, bool hostPlaying) noexcept;

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
            bool transportStopped = false;  // the host stopped after the capture had started
            bool beatSeen = false;          // the sidechain carried signal
            float minVoicedSeconds = 0.0f;

            float fraction() const noexcept { return targetSeconds > 0.0f ? juce::jlimit (0.0f, 1.0f, voicedSeconds / targetSeconds) : 0.0f; }
            bool enough() const noexcept { return voicedSeconds >= minVoicedSeconds; }
        };
        Progress getProgress() const noexcept;
        State getState() const noexcept { return (State) state.load(); }

        int capacitySamples() const noexcept { return capacity; }
        static int capacityFor (double sampleRate, float maxSeconds) noexcept;

    private:
        void waitForWriter() const noexcept;

        std::array<std::vector<float>, 2> vocalBuf, beatBuf;
        int capacity = 0;
        double fs = 48000.0;
        int channels = 2;
        CaptureConfig cfg;

        std::atomic<int> state { (int) State::idle };
        std::atomic<bool> writing { false };
        std::atomic<int> written { 0 }, voicedSamples { 0 }, clips { 0 };
        std::atomic<float> recentRms { -100.0f }, peak { 0.0f };
        std::atomic<bool> stopped { false }, beatPresent { false }, beatStored { false }, early { false };
        bool sawPlaying = false;   // audio thread only
    };
}
