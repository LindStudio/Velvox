#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "DSP/VocalChain.h"
#include "Parameters/ParameterLayout.h"
#include "Presets/PresetManager.h"

class VelvoxAudioProcessor final : public juce::AudioProcessor,
                                   private juce::ValueTree::Listener,
                                   private juce::AudioProcessorValueTreeState::Listener,
                                   private juce::AsyncUpdater
{
public:
    VelvoxAudioProcessor();
    ~VelvoxAudioProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorParameter* getBypassParameter() const override;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    juce::AudioProcessorValueTreeState& getState() noexcept { return apvts; }
    velvox::PresetManager& getPresetManager() noexcept { return presetManager; }
    velvox::dsp::VocalChain& getChain() noexcept { return chain; }

    // A/B comparison (message thread only)
    int getActiveSlot() const noexcept { return activeSlot; }
    void selectSlot (int slot);
    void copyActiveSlotToOther();

    // Module order / send-insert modes. setRouting is message-thread only;
    // the audio thread reads a packed atomic copy.
    velvox::dsp::Routing getRouting() const noexcept;
    void setRouting (const velvox::dsp::Routing& routing);

    // Listening aid: sums the output to mono. Deliberately not a parameter and
    // not saved, so a project can never be left in mono by accident.
    bool isMonoMonitor() const noexcept { return monoMonitor.load(); }
    void setMonoMonitor (bool shouldBeMono) noexcept { monoMonitor.store (shouldBeMono); }

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeRedirected (juce::ValueTree&) override;
    void syncRoutingFromState();

    // Latency follows the pitch module (on/off, quality); reported on the message thread
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;
public:
    // Recomputes the latency for the current parameters and reports it to the host
    void updateHostLatency();
private:
    void cacheParameterPointers();
    velvox::dsp::ChainSettings readSettings() const noexcept;

    juce::AudioProcessorValueTreeState apvts;
    velvox::PresetManager presetManager;
    velvox::dsp::VocalChain chain;

    struct Raw
    {
        std::atomic<float>* p = nullptr;
        float get() const noexcept { return p->load (std::memory_order_relaxed); }
        bool on() const noexcept { return get() > 0.5f; }
        int index() const noexcept { return (int) std::lround (get()); }
    };

    struct
    {
        Raw inputGain, outputGain, bypass, safetyClip;
        Raw eqOn, eqMix, hpfOn, hpfFreq, hpfSlope, lowFreq, lowGain, midFreq, midGain, midQ, highFreq, highGain;
        Raw compOn, compThreshold, compRatio, compAttack, compRelease, compKnee, compMakeup, compMix;
        Raw deessOn, deessFreq, deessThreshold, deessRange, deessListen;
        Raw satOn, satDrive, satOutput, satMix;
        Raw delayOn, delayTime, delaySync, delayDivision, delayFeedback, delayMix, delayLowCut, delayHighCut, delayPingPong;
        Raw reverbOn, reverbType, reverbMix, reverbDecay, reverbPreDelay, reverbSize, reverbDamping, reverbLowCut, reverbWidth;
        Raw delayToReverb;
        Raw stereoWidth, monoBelow, levelMatchMode, levelMatchWindow, levelMatchRange;
        Raw doublerOn, doublerMix, doublerWidth, doublerDelay, doublerDetune, doublerMonoSafe, doublerTone;
        Raw duckSource, delayDuckAmount, delayDuckThreshold, delayDuckAttack, delayDuckRelease;
        Raw reverbDuckAmount, reverbDuckThreshold, reverbDuckAttack, reverbDuckRelease;
        Raw pitchOn, pitchSemitones, pitchCents, pitchPreserveFormants, pitchFormant, pitchMix;
    } raw;

    std::atomic<bool> monoMonitor { false };

    std::atomic<std::uint64_t> routingPacked { velvox::dsp::Routing::defaults().pack() };

    // A/B slots hold full parameter snapshots
    std::array<velvox::ParameterSnapshot, 2> slots;
    std::array<bool, 2> slotValid { false, false };
    int activeSlot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VelvoxAudioProcessor)
};
