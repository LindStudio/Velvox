#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "EmberParams.h"
#include "EmberEngine.h"
#include "EmberAuto.h"
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

        static constexpr const char* stateType = "EmberState";

    private:
        void timerCallback() override;

        ParamCache params { state };
        juce::AudioProcessorParameter* bypassParam = nullptr;
        AutoCapture capture;
        AutoPhase autoPhase = AutoPhase::idle;
        AutoResult lastAuto;
        std::future<AutoResult> pending;
        double currentRate = 44100.0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EmberAudioProcessor)
    };
}
