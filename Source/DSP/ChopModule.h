#pragma once

#include "DspUtilities.h"
#include "DisplayRing.h"

namespace velvox::dsp
{
    struct ChopSettings
    {
        bool enabled = false;
        int pattern = 0;          // index into ChopModule::patterns()
        int rate = 1;             // index into ChopModule::rates()
        float depth = 1.0f;       // 0 = no effect, 1 = muted steps fully silent
        float smoothMs = 3.0f;    // edge smoothing against clicks
    };

    // Host-synced 16-step vocal chopper. Each step either plays the live
    // vocal ('x'), mutes it ('.') or repeats the previous slice ('r', stutter).
    // Locks to the host song position while playing; free-runs at the host
    // tempo (or 120 BPM) otherwise. Zero latency.
    class ChopModule
    {
    public:
        static constexpr int numSteps = 16;
        static constexpr double fallbackBpm = 120.0;
        // 0.6: 4 s (was 2 s) - the longest run (4 repeats of 1/8) fits down to 30 BPM;
        // beyond that a repeat replays the oldest whole slice that still fits
        static constexpr double maxRepeatSeconds = 4.0;

        struct Pattern { const char* name; const char* steps; };
        static const std::vector<Pattern>& patterns();
        static const juce::StringArray& patternNames();
        static const juce::StringArray& rateNames();
        static double beatsPerStep (int rateIndex) noexcept;

        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        void setSettings (const ChopSettings& s) noexcept;

        // ppq < 0 when the host position is unknown / not playing
        void setTransport (double bpm, double ppqAtBlockStart, bool playing) noexcept;

        void process (float* const* channels, int numChannels, int numSamples) noexcept;

        // Current step (0..15); audio thread only - the UI uses getDisplayStep()
        int getCurrentStep() const noexcept { return currentStep; }
        int getDisplayStep() const noexcept { return displayStep.load (std::memory_order_relaxed); }

        // "Cuts on the waveform" feed (0.6): one frame per 2.5 ms of processed audio -
        // the input peak (mono sum, before the chop), the lowest gate gain applied in the
        // frame (0 = muted), the step and its type ('x' play, '.' mute, 'r' repeat).
        // Lock-free (DisplayRing): readDisplay() from any thread, newest ~5 s.
        static constexpr double displayFrameSeconds = 0.0025;
        static constexpr int displayCapacity = 2048;
        struct DisplayFrame { float peak; float gain; int step; char type; };
        static std::uint64_t pack (const DisplayFrame& f) noexcept;
        static DisplayFrame unpack (std::uint64_t word) noexcept;
        int readDisplay (DisplayFrame* dest, int maxFrames) const noexcept;   // oldest first; returns the count
        std::uint64_t displayFramesWritten() const noexcept { return display.written(); }

    private:
        char stepType (int step) const noexcept;

        DisplayRing<displayCapacity> display;
        int frameLength = 120, frameFill = 0;
        float framePeak = 0.0f, frameGain = 1.0f;

        double fs = 48000.0;
        ChopSettings settings;
        std::array<std::vector<float>, maxChannels> history;
        int writePos = 0;

        double bpm = fallbackBpm;
        double hostPpq = -1.0;
        bool hostPlaying = false;
        double freePpq = 0.0;     // internal position when the host is stopped
        bool wasPlaying = false;

        int currentStep = 0;
        std::atomic<int> displayStep { -1 };
        int repeatCount = 0;      // consecutive 'r' steps so far
        float gate = 1.0f;        // smoothed gain
        float smoothCoeff = 0.0f;

        // crossfade between live and repeated audio at step edges
        int fadeRemaining = 0;
        int fadeLength = 64;
        int prevDelay = 0, curDelay = 0;
        juce::SmoothedValue<float> amount { 0.0f };
        bool wasActive = false;
    };
}
