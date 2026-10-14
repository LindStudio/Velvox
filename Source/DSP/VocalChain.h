#pragma once

#include "EqualizerModule.h"
#include "CompressorModule.h"
#include "DeEsserModule.h"
#include "SaturationModule.h"
#include "DelayModule.h"
#include "ReverbModule.h"
#include "Routing.h"
#include "Loudness.h"
#include "StereoStage.h"

namespace velvox::dsp
{
    // Plain value snapshot of every parameter for one processing block.
    // Filled by the processor from the APVTS atomics; no JUCE parameter types
    // leak into the DSP code, which keeps the chain testable on its own.
    struct ChainSettings
    {
        float inputGainDb = 0.0f, outputGainDb = 0.0f;
        bool bypass = false, safetyClip = true;

        bool eqOn = true;       float eqMix = 1.0f;     EqSettings eq;
        bool compOn = true;     float compMix = 1.0f;   CompSettings comp;
        bool deessOn = true;    DeEssSettings deess;
        SatSettings sat;
        bool delayOn = false;   float delayMix = 0.2f;  DelaySettings delay;
        bool delaySync = false; int delayDivision = 9;  float delayTimeMs = 375.0f;
        bool reverbOn = false;  float reverbMix = 0.18f; ReverbSettings reverb;
        float delayIntoReverb = 0.0f; // 0..1, only when both are sends

        Routing routing = Routing::defaults();

        StereoSettings stereo;

        enum class LevelMatch { off = 0, bypassMatch = 1, autoGain = 2 };
        LevelMatch levelMatch = LevelMatch::off;
        float levelMatchWindowSeconds = 3.0f;
        float levelMatchRangeDb = 12.0f;

        bool monoMonitor = false;   // listening aid, not a saved parameter

        double hostBpm = 0.0;   // <= 0 when unknown
    };

    struct ChainMeters
    {
        std::array<PeakMeter, maxChannels> input, output;
        std::atomic<bool> clipped { false };       // output exceeded 0 dBFS before the safety clipper
        std::atomic<bool> tempoSynced { false };   // delay currently follows host tempo
        std::atomic<float> effectiveDelayMs { 375.0f };

        std::atomic<float> inputLufs { LoudnessMeter::silenceLufs };
        std::atomic<float> processedLufs { LoudnessMeter::silenceLufs };
        std::atomic<float> levelMatchDb { 0.0f };  // correction the matcher currently suggests
        std::atomic<float> correlation { 1.0f };
    };

    //  Input -> serial modules in Routing order (delay/reverb only when "insert")
    //        -> + parallel sends (delay/reverb when "send", wet only) -> Output
    // Default routing reproduces the v0.1 chain exactly.
    class VocalChain
    {
    public:
        static constexpr int internalBlockSize = 256;

        void prepare (double sampleRate, int maxHostBlockSize);
        void reset();

        // Processes any block size (internally split into chunks), mono or stereo.
        void process (juce::AudioBuffer<float>& buffer, int numChannels, const ChainSettings& settings) noexcept;

        int getLatencySamples() const noexcept { return saturation.getLatencySamples(); }

        CompressorModule& getCompressor() noexcept { return compressor; }
        DeEsserModule& getDeEsser() noexcept { return deesser; }
        ChainMeters meters;

        // Exposed for tests: soft clip transfer curve
        static float safetyClip (float x) noexcept;

    private:
        void applySettings (const ChainSettings& s) noexcept;
        void processChunk (float* const* io, int numChannels, int numSamples, const ChainSettings& s) noexcept;
        void processSerialModule (ModuleId id, float* const* io, int numChannels, int n) noexcept;
        void processSends (float* const* io, int numChannels, int n, const ChainSettings& s) noexcept;
        bool wakeIfNeeded (juce::SmoothedValue<float>& ret, bool& active, ModuleId id) noexcept;
        void applyRoutingFade (float* const* io, int numChannels, int n) noexcept;

        double fs = 44100.0;
        bool prepared = false;

        EqualizerModule eq;
        CompressorModule compressor;
        DeEsserModule deesser;
        SaturationModule saturation;
        DelayModule delay;
        ReverbModule reverb;

        ModuleGate eqGate, compGate, deessGate;
        juce::SmoothedValue<float> delayReturn { 0.0f }, reverbReturn { 0.0f };
        bool delayActive = false, reverbActive = false;

        juce::SmoothedValue<float> inputGain { 1.0f }, outputGain { 1.0f }, wetAmount { 1.0f };
        juce::SmoothedValue<float> delayIntoReverb { 0.0f };
        IntegerDelay bypassDelay;

        StereoStage stereo;
        LevelMatcher matcher;
        CorrelationMeter correlation;
        juce::SmoothedValue<float> autoGain { 1.0f }, bypassGain { 1.0f };
        bool monoInput = false;

        // Routing changes are applied at a short dip to silence (no clicks)
        Routing activeRouting = Routing::defaults(), pendingRouting = Routing::defaults();
        juce::SmoothedValue<float> routeGain { 1.0f };
        bool adoptRoutingImmediately = true;

        juce::AudioBuffer<float> work, dryForBypass, delayWet, reverbWet, reverbIn;
    };
}
