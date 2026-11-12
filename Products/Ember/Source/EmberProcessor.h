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
        AutoPhase getAutoPhase() const noexcept { return autoPhase; }
        double getAutoProgress() const noexcept;   // 0..1 while listening
        const AutoResult& getLastAuto() const noexcept { return lastAuto; }
        juce::String getAutoMessage() const;       // in the interface language
        // Applies an analysis result to the parameters (one host gesture per parameter)
        void applyAuto (const AutoResult&);
        AutoCapture& getCapture() noexcept { return capture; }

        double getCurrentSampleRate() const noexcept { return currentRate; }

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

        static constexpr const char* stateType = "EmberState";

        // ---- Licence: locked (trial over / key invalid) = the audio passes through as in bypass -------------
        vocalypse::licensing::Manager& getLicence() noexcept { return *licence; }
        void setLicenceForTest (vocalypse::licensing::Manager& m) noexcept { licence = &m; }

    private:
        void timerCallback() override;

        juce::SharedResourcePointer<EmberLicence> sharedLicence;
        vocalypse::licensing::Manager* licence = &sharedLicence.getObject();

        ParamCache params { state };
        juce::AudioProcessorParameter* bypassParam = nullptr;
        AutoCapture capture;
        AutoPhase autoPhase = AutoPhase::idle;
        AutoResult lastAuto;
        std::future<AutoResult> pending;

        int factoryIndex = 0;                      // -1 = user preset / AUTO result
        juce::String userPresetName;
        ParamValues presetValues;                  // what the loaded preset set (for "modified")
        ParamValues slots[2];
        bool slotUsed[2] { false, false };
        int activeSlot = 0;
        double currentRate = 44100.0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EmberAudioProcessor)
    };
}
