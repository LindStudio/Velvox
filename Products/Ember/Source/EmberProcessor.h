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
        bool pollAuto();     // message thread: applies a finished analysis; true when it did
        void waitForAuto();  // tests: blocks until the analysis is applied
        void cancelAuto();
        // Timer step (10 Hz; tests call it with their own clock): finishes on its own after autoEnoughSeconds of voice,
        // or once at least autoMinSeconds were heard and then nothing new for autoQuietSeconds (playback stopped)
        void updateAuto (double nowSeconds);
        // Listening, but nothing new heard for a while (no audio playing): the editor says "press play"
        double getAutoQuietSeconds() const noexcept { return autoPhase == AutoPhase::listening ? autoNow - autoLastGrowth : 0.0; }
        bool isAutoWaitingForAudio() const noexcept;
        // The AUTO result is still what is set (nothing changed since, the other slot holds the settings from before)
        bool isShowingAutoResult() const;
        AutoPhase getAutoPhase() const noexcept { return autoPhase; }
        double getAutoProgress() const noexcept;   // 0..1 while listening
        const AutoResult& getLastAuto() const noexcept { return lastAuto; }
        juce::String getAutoMessage() const;       // in the interface language
        // Applies an analysis result to the parameters (one host gesture per parameter): frequency, threshold, range,
        // release (and Amount when it was too low to work with); mode, mix, output, detection and the times stay.
        // The settings from before go to slot A, the result to slot B (active): A/B is the comparison and the undo.
        void applyAuto (const AutoResult&);
        bool didAutoRaiseAmount() const noexcept { return autoRaisedAmount; }
        AutoCapture& getCapture() noexcept { return capture; }

        double getCurrentSampleRate() const noexcept { return currentRate; }
        // The host's transport is stopped (only known once it has been seen playing; see processBlock)
        bool isHostStopped() const noexcept { return hostStopped.load (std::memory_order_relaxed); }
        float getHostBpm() const noexcept { return hostBpm.load (std::memory_order_relaxed); }

        // ---- Presets and A/B (message thread) -----------------------------------------------
        void loadFactoryPreset (int index);
        bool loadUserPreset (const juce::File&);
        bool saveUserPreset (const juce::File&);
        juce::String getPresetName() const;        // shown name; factory names follow the interface language
        bool isPresetModified() const;              // the values differ from the loaded preset
        int getFactoryIndex() const noexcept { return factoryIndex; }
        void stepPreset (int direction);           // previous / next over factory + user presets

        int getActiveSlot() const noexcept { return activeSlot; }   // 0 = A, 1 = B
        void switchSlot (int slot);                // stores the current values in the active slot, loads the other
        void copyActiveToOther();

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
        void beginUndoStep (int parameterIndex);   // remembers the values before an edit
        struct UndoListener;
        struct UndoGroup;                          // edits inside one step (AUTO, presets, A/B, undo itself)

        // A slot remembers which preset its values came from
        struct Slot
        {
            ParamValues values, presetValues;
            int factoryIndex = 0;
            juce::String userPresetName;
        };
        Slot currentSlot() const;
        void restoreSlot (const Slot&);

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
        Slot slots[2];
        bool slotUsed[2] { false, false };
        int activeSlot = 0;
        double currentRate = 44100.0;
        std::atomic<bool> hostStopped { false };
        std::atomic<float> hostBpm { 0.0f };        // for Ember's dancing (0 = unknown)
        bool hostEverPlayed = false;               // audio thread only
        double autoNow = 0.0, autoLastGrowth = 0.0;
        int autoLastSamples = 0;
        bool autoRaisedAmount = false;
        Settings autoBase;                         // the settings AUTO measured with

        std::vector<ParamValues> undoSteps, redoSteps;
        int undoGroupDepth = 0, lastUndoParam = -1;
        double lastUndoTime = 0.0;
        std::unique_ptr<UndoListener> undoListener;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EmberAudioProcessor)
    };
}
