#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "AutoMixEngine.h"
#include "../Presets/ParameterSnapshot.h"

namespace velvox::automix
{
    // What the UI shows about the last automatic mix (persisted with the project)
    struct MixReport
    {
        struct Line { juce::String moduleKey, title, reason; bool measured = false; };
        struct Finding { juce::String title, detail; Confidence confidence = Confidence::possible; };

        bool valid = false;
        juce::String timestamp;
        float voicedSeconds = 0.0f;
        float capturedSeconds = 0.0f;
        bool conservative = false;
        bool beatAnalysed = false;
        float inputLufs = -100.0f, outputLufs = -100.0f, outputPeakDb = -100.0f;
        int iterations = 0;
        std::vector<Line> steps;           // processing that was actually set up
        std::vector<Finding> findings;     // observations about the recording
        juce::StringArray notes;           // things left alone and why
        juce::StringArray adjustments;     // corrections made by the verification
        juce::StringArray changedModules;  // module keys switched on / set by the mix

        juce::ValueTree toValueTree() const;
        static MixReport fromValueTree (const juce::ValueTree&);
    };

    // The automatic mix workflow of one plugin instance:
    //
    //   start() -> capture (audio thread fills a bounded buffer during playback)
    //           -> analysis + planning + verification on a worker thread
    //           -> applied on the message thread through the normal parameters
    //
    // Message thread only, except push() (audio thread, lock-free). The audio
    // keeps running whatever happens here, also with the editor closed.
    class AutoMixController final : public juce::ChangeBroadcaster, private juce::Timer
    {
    public:
        enum class Phase { idle, listening, capturing, analysing, done, failed };

        // First-open prompt of a new instance
        enum class Prompt { pending, done, dismissed };

        AutoMixController (juce::AudioProcessor& processor, juce::AudioProcessorValueTreeState& state);
        ~AutoMixController() override;

        // ---- workflow (message thread)
        void start (const CaptureConfig& config = {});
        void cancel();
        bool analyseNow();          // analyse what has been captured so far (needs the minimum)
        void dismissPrompt();       // "Später"

        // ---- audio thread
        void push (const float* const* vocal, int vocalChannels, const float* const* beat, int beatChannels,
                   int numSamples, bool hostPlaying) noexcept
        {
            capture.push (vocal, vocalChannels, beat, beatChannels, numSamples, hostPlaying);
        }

        struct Status
        {
            Phase phase = Phase::idle;
            CaptureBuffer::Progress capture;
            float analysisFraction = 0.0f;
            juce::String stage;            // analysis stage text
            juce::String error;            // failed: why
            double secondsListening = 0.0; // since start() (for "no signal" hints)
        };
        Status getStatus() const;
        Phase getPhase() const noexcept { return phase; }

        Prompt getPrompt() const;
        bool shouldShowPrompt() const { return getPrompt() == Prompt::pending; }

        // ---- result
        const MixReport& getReport() const noexcept { return report; }
        bool hasMix() const noexcept { return report.valid && mixSnapshot.values.size() > 0; }
        bool hasPrevious() const noexcept { return hasPreviousSnapshot; }

        // Compare the automatic mix with the state before it (real parameter switch)
        void showPrevious (bool previous);
        bool isShowingPrevious() const noexcept { return isComparePrevious ? (hasPreviousSnapshot && isComparePrevious()) : showingPrevious; }

        // The plugin's A/B slots do the comparison (set by the processor): after a mix
        // A = the state before, B = the mix; "Vorher / Mix" switches the slots
        std::function<void (const ParameterSnapshot& before, const ParameterSnapshot& mix)> onMixApplied;
        std::function<void (bool showPrevious)> selectCompare;
        std::function<bool()> isComparePrevious;
        // Back to the state before the last automatic mix
        void restorePrevious();

        // ---- persistence (called from get/setStateInformation)
        void writeState (juce::ValueTree& pluginState) const;
        void readState (const juce::ValueTree& pluginState, bool isOlderProject);

        // For tests: run the whole analysis synchronously on captured audio
        AutoMixResult runSynchronously (const CapturedAudio& audio);
        void applyResult (const AutoMixResult& result);
        CaptureBuffer& getCapture() noexcept { return capture; }

        static inline const juce::Identifier treeType { "AutoMix" };

    private:
        void timerCallback() override;
        void startWorker (CapturedAudio audio);
        void setPhase (Phase p);
        std::map<juce::String, float> currentValues() const;

        juce::AudioProcessor& processor;
        juce::AudioProcessorValueTreeState& apvts;
        CaptureBuffer capture;
        CaptureConfig config;

        Phase phase = Phase::idle;
        juce::String error;
        double startedMs = 0.0;

        class Worker;
        std::unique_ptr<Worker> worker;

        MixReport report;
        ParameterSnapshot previousSnapshot, mixSnapshot;
        bool hasPreviousSnapshot = false, showingPrevious = false;
        juce::String promptState { "pending" };
    };
}
