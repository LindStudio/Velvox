#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "DSP/VocalChain.h"
#include "DSP/Macros.h"
#include "DSP/VocalAnalyzer.h"
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

    // Beat sidechain status for the assistant (written by the audio thread, lock-free).
    // Levels are short-term RMS in dBFS (~300 ms), measured before any processing.
    struct SidechainStatus
    {
        bool connected = false;   // the host enabled the sidechain bus
        float beatRmsDb = -100.0f;
        float vocalRmsDb = -100.0f;
        bool hasSignal() const noexcept { return connected && beatRmsDb > -60.0f; }
    };
    SidechainStatus getSidechainStatus() const noexcept;

    // VOCAL CHECK analysis (message thread). Fed from the Check module in the
    // chain by a timer that runs while the module is on - also with the editor closed.
    velvox::dsp::VocalAnalyzer& getAnalyzer() noexcept { return analyzer; }
    // Drains the Check tap into the analyzer (called by the timer; tests call it directly)
    void pumpAnalyzer();
    // Latest findings (recomputed about twice a second while the module is on)
    const velvox::dsp::AnalysisResult& getLatestAnalysis() const noexcept { return latestAnalysis; }
    void refreshAnalysis() { latestAnalysis = analyzer.analyse(); lastAnalysisMs = juce::Time::getMillisecondCounterHiRes(); }

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
public:
    // Thread-safe reads (atomics): base = Pro parameters, effective = with macros
    velvox::dsp::ChainSettings readSettings() const noexcept;
    velvox::dsp::MacroValues readMacros() const noexcept;
    velvox::dsp::ChainSettings readEffectiveSettings() const noexcept { return velvox::dsp::applyMacros (readSettings(), readMacros()); }

    // Processed value of a parameter (in parameter units) when macros move it;
    // nullopt for parameters that macros never touch. Used for the UI markers.
    std::optional<float> effectiveValueOf (const juce::String& paramId) const;
private:

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
        Raw chopOn, chopPattern, chopRate, chopDepth, chopSmooth;
        Raw lowType, lowQ, highType, highQ, lpfOn, lpfFreq, lpfSlope, satCharacter, deessMode, compDetector, compScHpf;
        Raw macroPresence, macroWarmth, macroAggression, macroWidth, macroSpace, macroDensity;
        Raw pulseOn, pulseRate, pulseDepth, pulseAttack, pulseRelease, pulseSmooth, pulseInterp, pulseTension, pulseMix, pulseBpm;
        std::array<Raw, 6> pulseX, pulseY;
        Raw smashOn, smashAmount, smashBlend, smashAttack, smashRelease, smashTone, smashOutput;
        Raw choirOn, choirVoices, choirDetune, choirTiming, choirHumanize, choirWidth, choirDensity, choirMix;
        Raw smoothOn, smoothAmount, smoothSensitivity, smoothLow, smoothHigh, smoothAttack, smoothRelease, smoothMode, smoothOutput, smoothDelta;
        Raw checkOn, checkMode;
        Raw rideOn, rideTarget, rideAmount, rideMaxBoost, rideMaxCut, rideResponse, ridePhrase, rideTransient, rideSilence;
    } raw;

    std::atomic<bool> monoMonitor { false };

    // Sidechain / vocal level (audio thread state + published values)
    float scMeanSquare = 0.0f, vocalMeanSquare = 0.0f;
    std::atomic<bool> scConnected { false };
    std::atomic<float> scRmsDb { -100.0f }, vocalRmsDb { -100.0f };
    void measureLevels (const juce::AudioBuffer<float>& vocal, int vocalChannels, const juce::AudioBuffer<float>* sidechain) noexcept;

    velvox::dsp::VocalAnalyzer analyzer { chain.getCheck() };
    velvox::dsp::AnalysisResult latestAnalysis;
    double lastAnalysisMs = 0.0;
    struct AnalyzerTimer final : juce::Timer
    {
        explicit AnalyzerTimer (VelvoxAudioProcessor& p) : owner (p) {}
        void timerCallback() override { owner.pumpAnalyzer(); }
        VelvoxAudioProcessor& owner;
    } analyzerTimer { *this };

    std::atomic<std::uint64_t> routingPacked { velvox::dsp::Routing::defaults().pack() };

    // A/B slots hold full parameter snapshots
    std::array<velvox::ParameterSnapshot, 2> slots;
    std::array<bool, 2> slotValid { false, false };
    int activeSlot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VelvoxAudioProcessor)
};
