#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "EmberParams.h"
#include "EmberEngine.h"
#include "EmberAuto.h"
#include "EmberPresets.h"
#include "EmberLicence.h"
#include <future>

namespace ember
{
    // EMBER De-Esser by Lind Audio (first single plugin of the VOCALYPSE family)
    class EmberAudioProcessor final : public juce::AudioProcessor, private juce::Timer
    {
    public:
        EmberAudioProcessor();
        ~EmberAudioProcessor() override;

        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override {}
        bool isBusesLayoutSupported (const BusesLayout&) const override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        using AudioProcessor::processBlock;

        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override { return true; }

        const juce::String getName() const override { return "EMBER De-Esser"; }
        bool acceptsMidi() const override { return false; }
        bool producesMidi() const override { return false; }
        bool isMidiEffect() const override { return false; }
        double getTailLengthSeconds() const override { return 0.0; }

        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock&) override;
        void setStateInformation (const void* data, int sizeInBytes) override;

        juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

        APVTS state;
        Engine engine;

        // ---- AUTO (message thread) -----------------------------------------------------------
        enum class AutoPhase { idle, listening, analysing, done, failed };
        void startAuto();
        void finishAuto();   // starts the analysis of what was captured (background thread)
        // Message thread: applies a finished analysis; true when it did. An analysis whose settings no longer hold
        // is not applied: a loaded project, preset or the other A/B slot discards it, a change of mode, detection,
        // Amount, attack or lookahead runs it again on the same take with the new settings.
        bool pollAuto();
        void waitForAuto();  // tests: blocks until the analysis is applied (or discarded)
        void cancelAuto();   // while listening or analysing: back to idle, nothing applied
        // Timer step (10 Hz; tests call it with their own clock): finishes on its own after autoEnoughSeconds of voice,
        // or once at least autoMinSeconds were heard and then nothing new for autoQuietSeconds (playback stopped)
        void updateAuto (double nowSeconds);
        // Listening, but nothing new heard for a while (no audio playing): the editor says "press play"
        double getAutoQuietSeconds() const noexcept { return autoPhase == AutoPhase::listening ? autoNow - autoLastGrowth : 0.0; }
        bool isAutoWaitingForAudio() const noexcept;
        // The AUTO result is still what is set (nothing changed since, the other slot holds the settings from before)
        bool isShowingAutoResult() const;
        // An AUTO result is loaded (maybe changed since): undo / a preset / A/B to another setting end it
        bool isAutoResultLoaded() const noexcept { return autoPhase == AutoPhase::done && fromAuto; }
        AutoPhase getAutoPhase() const noexcept { return autoPhase; }
        double getAutoProgress() const noexcept;   // 0..1 while listening
        int getAutoRuns() const noexcept { return autoRuns; }   // analyses started (tests: a restart counts)
        const AutoResult& getLastAuto() const noexcept { return lastAuto; }
        juce::String getAutoMessage() const;       // in the interface language
        // Applies an analysis result to the parameters (one host gesture per parameter): frequency, threshold, range,
        // release (and Amount when it was too low to work with); mode, mix, output, detection and the times stay.
        // The settings from before go to slot A, the result to slot B (active): A/B is the comparison and the undo.
        void applyAuto (const AutoResult&);
        bool didAutoRaiseAmount() const noexcept { return autoRaisedAmount; }
        AutoCapture& getCapture() noexcept { return capture; }

        double getCurrentSampleRate() const noexcept { return currentRate.load(); }
        // The host's transport is stopped (only known once it has been seen playing; see processBlock)
        bool isHostStopped() const noexcept { return hostStopped.load (std::memory_order_relaxed); }
        float getHostBpm() const noexcept { return hostBpm.load (std::memory_order_relaxed); }

        // ---- Presets and A/B (message thread) -----------------------------------------------
        void loadFactoryPreset (int index);
        bool loadUserPreset (const juce::File&);
        bool saveUserPreset (const juce::File&);
        // User preset files: rename (the shown name follows when it is the loaded one) and delete (to the recycle bin).
        // An empty result = done, otherwise the reason in the interface language.
        juce::String renameUserPreset (const juce::File&, const juce::String& newName);
        juce::String deleteUserPreset (const juce::File&);
        juce::String getPresetName() const;        // shown name; factory names follow the interface language
        bool isPresetModified() const;              // the values differ from the loaded preset
        int getFactoryIndex() const noexcept { return factoryIndex; }
        void stepPreset (int direction);           // previous / next over factory + user presets

        int getActiveSlot() const noexcept { return activeSlot; }   // 0 = A, 1 = B
        void switchSlot (int slot);                // stores the current values in the active slot, loads the other
        void copyActiveToOther();
        // Where AUTO left the settings from before: 0 = A, 1 = B, -1 = only in undo (the other slot held a different
        // setting the user had parked there; AUTO never overwrites it)
        int getAutoBeforeSlot() const noexcept { return autoBeforeSlot; }

        // ---- Voice buttons (SIMPLE): LOW / MID / HIGH ------------------------------------------------
        // The ranges that light up; the borders lie between the voices and away from every factory preset's frequency
        static constexpr float voiceLowMidHz = 5750.0f, voiceMidHighHz = 7000.0f;
        static int voiceRangeOf (float hz) noexcept { return hz < voiceLowMidHz ? 0 : (hz < voiceMidHighHz ? 1 : 2); }
        int getVoiceRange() const;                 // of the current frequency
        enum class VoiceResult { set, setKeptThreshold, keptAutoFrequency };
        // One click = one undo step. Sets the voice's frequency; the threshold comes along only while it is still at a
        // voice value (default included) - a threshold set by hand, a preset or AUTO stays. A click on the lit voice
        // keeps the frequency AUTO found in that range.
        VoiceResult setVoice (int voice);

        // ---- Undo / redo of the settings (message thread). Every edit in the editor (a host gesture) is one step;
        // AUTO and loading a preset are one step each. Listen and bypass are not part of it.
        bool undo();
        bool redo();
        bool canUndo() const;
        bool canRedo() const noexcept { return ! redoSteps.empty(); }

        static constexpr const char* stateType = "EmberState";

        // ---- Licence: locked (trial over / key invalid) = the audio passes through as in bypass -------------
        vocalypse::licensing::Manager& getLicence() noexcept { return *licence; }
        void setLicenceForTest (vocalypse::licensing::Manager& m) noexcept { licence = &m; }

    private:
        void timerCallback() override;
        void setListenOff();
        void launchAuto();                         // (re)starts the analysis of autoTake with the current settings
        void discardPendingAuto();                 // a preset / A/B slot / project was loaded: the running analysis is dropped
        void autoProjectLoaded();                  // ... a project: also the last result (message thread)
        bool autoBaseChanged() const;              // the settings AUTO measures with differ from the running analysis
        void beginUndoStep (int parameterIndex);   // remembers the values before an edit
        struct UndoListener;
        struct UndoGroup;                          // edits inside one step (AUTO, presets, A/B, undo itself)

        // A slot remembers which preset its values came from (and whether they are an AUTO result)
        struct Slot
        {
            ParamValues values, presetValues;
            int factoryIndex = 0;
            juce::String userPresetName;
            bool fromAuto = false;
            bool operator== (const Slot&) const = default;
        };
        Slot currentSlot() const;
        void restoreSlot (const Slot&);
        void restoreMeta (const Slot&);            // preset name / marker / AUTO flag (not the values)
        void setPresetMeta (int factory, const juce::String& user, bool isAuto);   // presetValues = what is set now
        void assignAutoSlots (const Slot& before);   // AUTO's result into A/B (see getAutoBeforeSlot)

        // An undo step: the values and the preset name / marker; AUTO steps also bring the A/B slots back
        struct Snapshot
        {
            Slot current;
            Slot slots[2];
            bool slotUsed[2] { false, false };
            int activeSlot = 0;
            bool restoresSlots = false;
        };
        Snapshot snapshot() const;
        void restoreSnapshot (const Snapshot&);

        // A/B and the preset name in the project (getStateInformation / setStateInformation). A state restored off the
        // message thread is taken over by the message thread (timer); metaLock guards what getStateInformation reads.
        struct Meta
        {
            Slot current;                          // values unused
            Slot slots[2];
            bool slotUsed[2] { false, false };
            int activeSlot = 0;
        };
        Meta currentMeta() const;
        void applyMeta (const Meta&);              // message thread
        void applyPendingMeta();                   // message thread
        static void writeMeta (juce::XmlElement&, const Meta&);
        static Meta readMeta (const juce::XmlElement& root, const juce::XmlElement* ab, const ParamValues& restored);
        juce::CriticalSection metaLock;
        std::unique_ptr<Meta> pendingMeta;

        juce::SharedResourcePointer<EmberLicence> sharedLicence;
        vocalypse::licensing::Manager* licence = &sharedLicence.getObject();

        ParamCache params { state };
        juce::AudioProcessorParameter* bypassParam = nullptr;
        AutoCapture capture;
        AutoPhase autoPhase = AutoPhase::idle;
        AutoResult lastAuto;
        std::future<AutoResult> pending;
        std::shared_ptr<std::atomic<bool>> autoCancel = std::make_shared<std::atomic<bool>> (false);

        int factoryIndex = 0;                      // -1 = user preset / AUTO result
        juce::String userPresetName;
        ParamValues presetValues;                  // what the loaded preset set (for "modified")
        bool fromAuto = false;                     // the loaded "preset" is an AUTO result
        Slot slots[2];
        bool slotUsed[2] { false, false };
        int activeSlot = 0;
        int autoBeforeSlot = 0;
        std::atomic<double> currentRate { 44100.0 };
        std::atomic<bool> hostStopped { false };
        std::atomic<float> hostBpm { 0.0f };        // for Ember's dancing (0 = unknown)
        bool hostEverPlayed = false;               // audio thread only
        double autoNow = 0.0, autoLastGrowth = 0.0;
        int autoLastSamples = 0;
        bool autoRaisedAmount = false;
        Settings autoBase;                         // the settings AUTO measured with
        float autoUserAmount = 0.5f;               // the Amount the user had (autoBase may hold the raised one)
        std::shared_ptr<const AutoCapture::Take> autoTake;   // kept while analysing (a restart needs it)
        int autoRuns = 0;
        // Projects loaded (setStateInformation, any thread): an analysis started before one is not applied
        std::atomic<int> projectLoads { 0 };
        int autoLoadsAtStart = 0, seenProjectLoads = 0;

        std::vector<Snapshot> undoSteps, redoSteps;
        int undoGroupDepth = 0, lastUndoParam = -1;
        double lastUndoTime = 0.0;
        std::unique_ptr<UndoListener> undoListener;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EmberAudioProcessor)
    };
}
