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
#include "DoublerModule.h"
#include "Ducker.h"
#include "PitchModule.h"
#include "ChopModule.h"
#include "PulseModule.h"
#include "SmashModule.h"
#include "ChoirModule.h"
#include "SmoothModule.h"
#include "CheckModule.h"
#include "RideModule.h"
#include "Instances.h"
#include "MixLaw.h"

namespace velvox::dsp
{
    // Settings of one additional instance (instance k >= 1) of every module that has
    // repeated instances. Instance 0 lives directly in ChainSettings (the 0.1-0.3 fields).
    struct InstanceSettings
    {
        bool eqOn = true;       float eqMix = 1.0f;     EqSettings eq;
        bool compOn = true;     float compMix = 1.0f;   CompSettings comp;
        bool deessOn = true;    DeEssSettings deess;
        SatSettings sat;
        DoublerSettings doubler;
        bool delayOn = false;   float delayMix = 0.2f;  DelaySettings delay;
        bool delaySync = false; int delayDivision = 9;  float delayTimeMs = 375.0f;
        bool reverbOn = false;  float reverbMix = 0.18f; ReverbSettings reverb;
        float delayLawMix = -1.0f, reverbLawMix = -1.0f;   // Mix for the dry gain on the true law (< 0: the Mix); set by macros
        DuckSettings delayDuck, reverbDuck;
        SmashSettings smash;
        ChoirSettings choir;
        SmoothSettings smooth;
        RideSettings ride;
    };

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
        DoublerSettings doubler;
        PitchSettings pitch;
        ChopSettings chop;
        bool delayOn = false;   float delayMix = 0.2f;  DelaySettings delay;
        bool delaySync = false; int delayDivision = 9;  float delayTimeMs = 375.0f;
        bool reverbOn = false;  float reverbMix = 0.18f; ReverbSettings reverb;
        float delayIntoReverb = 0.0f; // 0..1, only when both are sends
        // Mix that sets the insert's dry gain on the true law (< 0: the Mix). The macros add wet
        // but never take the original away (DSP/MixLaw.h)
        float delayLawMix = -1.0f, reverbLawMix = -1.0f;

        // 0.3 modules (all off by default)
        PulseSettings pulse;
        SmashSettings smash;
        ChoirSettings choir;
        SmoothSettings smooth;
        bool checkOn = false;
        RideSettings ride;

        DuckSettings delayDuck, reverbDuck;
        bool duckExternal = false;    // use the sidechain input when available

        Routing routing = Routing::legacyDefaults();   // the processor always sets it

        // Mix law of every module instance (0.6). The DSP default is the 0.5 behaviour;
        // the processor fills it from the instance state (fresh instances: true dry/wet).
        MixLaws mixLaws = MixLaws::allLegacy();

        StereoSettings stereo;

        enum class LevelMatch { off = 0, bypassMatch = 1, autoGain = 2 };
        LevelMatch levelMatch = LevelMatch::off;
        float levelMatchWindowSeconds = 3.0f;
        float levelMatchRangeDb = 12.0f;

        bool monoMonitor = false;   // listening aid, not a saved parameter

        // Instances 2..n (extra[k - 1] = instance k); see DSP/Instances.h
        std::array<InstanceSettings, maxExtraInstances> extra {};

        // The instance-0 fields above in the per-instance layout
        InstanceSettings instance0() const noexcept;
        // Writes `b` into the instance-0 fields (views that show instance k reuse the instance-0 code)
        void setInstance0 (const InstanceSettings& b) noexcept;
        // Settings of instance k (0 = the fields above)
        InstanceSettings instance (int k) const noexcept
        {
            return k <= 0 ? instance0() : extra[(size_t) juce::jlimit (0, maxExtraInstances - 1, k - 1)];
        }

        double hostBpm = 0.0;   // <= 0 when unknown
        double hostPpq = -1.0;  // song position in quarter notes, < 0 when unknown
        bool hostPlaying = false;
    };

    struct ChainMeters
    {
        std::array<PeakMeter, maxChannels> input, output;
        std::atomic<bool> clipped { false };       // output exceeded 0 dBFS before the safety clipper
        std::atomic<bool> tempoSynced { false };   // delay currently follows host tempo
        std::atomic<float> effectiveDelayMs { 375.0f };

        std::atomic<float> inputLufs { LoudnessMeter::silenceLufs };
        std::atomic<float> processedLufs { LoudnessMeter::silenceLufs };
        std::atomic<float> outputLufs { LoudnessMeter::silenceLufs };   // what leaves the plugin
        std::atomic<float> levelMatchDb { 0.0f };  // correction the matcher currently suggests
        std::atomic<float> correlation { 1.0f };
        std::atomic<bool> sidechainInUse { false }; // ducking currently follows the external sidechain

        // Audio diagnostics (bounded, lock-free; read by the UI). Sample counters
        // give the time of the last event in samples since prepare().
        std::atomic<int> nonFiniteResets { 0 };       // chain reset because something produced NaN/Inf
        std::atomic<int> structureSwitches { 0 };     // routing / pitch changes applied under a fade
        std::atomic<int> prepareCount { 0 };
        std::atomic<long long> lastResetAtSample { -1 }, samplesProcessed { 0 };

        // Spectrum analyser feeds: after input gain, and what leaves the plugin
        SampleTap inputTap, outputTap;
        StereoTap scopeTap;   // goniometer
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
        // `sidechain` (optional) feeds the reverb/delay duckers when external ducking is selected.
        void process (juce::AudioBuffer<float>& buffer, int numChannels, const ChainSettings& settings,
                      const juce::AudioBuffer<float>* sidechain = nullptr) noexcept;

        // Latency of the currently active configuration
        int getLatencySamples() const noexcept { return getLatencyFor (activeRouting, activePitchOn); }
        // Latency of a routing with the pitch module on / off (for host reporting):
        // every saturation and smooth instance in the chain plus the pitch shifter while it is on
        int getLatencyFor (const Routing& routing, bool pitchOn) const noexcept;
        // Latency of the active chain in front of a module (tempo-locked modules follow the song position late by it)
        int latencyBefore (ModuleId id) const noexcept;

        // Module instances (instance 0 = the module of 0.1-0.3)
        CompressorModule& getCompressor (int instance = 0) noexcept { return comp[(size_t) clampInstance (ModuleId::comp, instance)]; }
        DeEsserModule& getDeEsser (int instance = 0) noexcept { return deess[(size_t) clampInstance (ModuleId::deess, instance)]; }
        Ducker& getDelayDucker (int instance = 0) noexcept { return delayDucker[(size_t) clampInstance (ModuleId::delay, instance)]; }
        Ducker& getReverbDucker (int instance = 0) noexcept { return reverbDucker[(size_t) clampInstance (ModuleId::reverb, instance)]; }
        int getChopStep() const noexcept { return chop.getDisplayStep(); }
        const ChopModule& getChop() const noexcept { return chop; }   // UI: readDisplay() (lock-free)
        const DelayModule& getDelay (int instance = 0) const noexcept { return delay[(size_t) clampInstance (ModuleId::delay, instance)]; }   // UI: displayTimeMs
        PulseModule& getPulse() noexcept { return pulse; }
        const PitchModule& getPitch() const noexcept { return pitch; }   // UI: getDetectedHz (lock-free)
        const SaturationModule& getSaturation (int instance = 0) const noexcept { return saturation[(size_t) clampInstance (ModuleId::sat, instance)]; }   // UI: getMeters()
        SmashModule& getSmash (int instance = 0) noexcept { return smash[(size_t) clampInstance (ModuleId::smash, instance)]; }
        ChoirModule& getChoir (int instance = 0) noexcept { return choir[(size_t) clampInstance (ModuleId::choir, instance)]; }
        DoublerModule& getDoubler (int instance = 0) noexcept { return doubler[(size_t) clampInstance (ModuleId::doubler, instance)]; }
        SmoothModule& getSmooth (int instance = 0) noexcept { return smooth[(size_t) clampInstance (ModuleId::smooth, instance)]; }
        CheckModule& getCheck() noexcept { return check; }
        RideModule& getRide (int instance = 0) noexcept { return ride[(size_t) clampInstance (ModuleId::ride, instance)]; }
        const Routing& getActiveRouting() const noexcept { return activeRouting; }   // audio thread / tests
        ChainMeters meters;

        // Exposed for tests: soft clip transfer curve
        static float safetyClip (float x) noexcept;

    private:
        static int clampInstance (ModuleId id, int instance) noexcept { return juce::jlimit (0, maxInstances (id) - 1, instance); }
        void applySettings (const ChainSettings& s) noexcept;
        void applyInstance (int k, const InstanceSettings& b, const ChainSettings& s) noexcept;
        void processChunk (float* const* io, int numChannels, int numSamples, const ChainSettings& s) noexcept;
        void processSerialModule (const ChainSlot& slot, float* const* io, int numChannels, int n) noexcept;
        void processSends (float* const* io, int numChannels, int n, const ChainSettings& s) noexcept;
        bool wakeIfNeeded (int k, bool isDelay) noexcept;
        void applyRoutingFade (float* const* io, int numChannels, int n) noexcept;
        void resetInstance (const ChainSlot& slot) noexcept;
        // Fills `gains` for one effect return; detector = sidechain or `internal`
        const float* duckGains (Ducker& ducker, std::vector<float>& gains, const float* const* internal,
                                int numChannels, int n) noexcept;

        double fs = 44100.0;
        bool prepared = false;

        std::array<EqualizerModule, maxInstances (ModuleId::eq)> eq;
        std::array<CompressorModule, maxInstances (ModuleId::comp)> comp;
        std::array<DeEsserModule, maxInstances (ModuleId::deess)> deess;
        std::array<SaturationModule, maxInstances (ModuleId::sat)> saturation;
        std::array<DelayModule, maxInstances (ModuleId::delay)> delay;
        std::array<ReverbModule, maxInstances (ModuleId::reverb)> reverb;
        std::array<DoublerModule, maxInstances (ModuleId::doubler)> doubler;
        PitchModule pitch;
        ChopModule chop;
        PulseModule pulse;
        std::array<SmashModule, maxInstances (ModuleId::smash)> smash;
        std::array<ChoirModule, maxInstances (ModuleId::choir)> choir;
        std::array<SmoothModule, maxInstances (ModuleId::smooth)> smooth;
        CheckModule check;
        std::array<RideModule, maxInstances (ModuleId::ride)> ride;

        std::array<ModuleGate, maxInstances (ModuleId::eq)> eqGate;
        std::array<ModuleGate, maxInstances (ModuleId::comp)> compGate;
        std::array<ModuleGate, maxInstances (ModuleId::deess)> deessGate;

        static constexpr int numDelays = maxInstances (ModuleId::delay), numReverbs = maxInstances (ModuleId::reverb);
        std::array<juce::SmoothedValue<float>, numDelays> delayReturn;
        std::array<juce::SmoothedValue<float>, numReverbs> reverbReturn;
        std::array<bool, numDelays> delayActive {};
        std::array<bool, numReverbs> reverbActive {};
        // Law of each insert (a send always adds its return to the dry) and its smoothed dry gain
        std::array<MixLaw, numDelays> delayLaw {};
        std::array<MixLaw, numReverbs> reverbLaw {};
        std::array<juce::SmoothedValue<float>, numDelays> delayDry;
        std::array<juce::SmoothedValue<float>, numReverbs> reverbDry;

        juce::SmoothedValue<float> inputGain { 1.0f }, outputGain { 1.0f }, wetAmount { 1.0f };
        juce::SmoothedValue<float> delayIntoReverb { 0.0f };
        IntegerDelay bypassDelay;

        StereoStage stereo;
        LevelMatcher matcher;
        CorrelationMeter correlation;
        LoudnessMeter outputLoudness;
        juce::SmoothedValue<float> autoGain { 1.0f }, bypassGain { 1.0f };
        bool monoInput = false;

        // Routing changes are applied at a short dip to silence (no clicks)
        Routing activeRouting = Routing::defaults(), pendingRouting = Routing::defaults();
        juce::SmoothedValue<float> routeGain { 1.0f };
        bool adoptRoutingImmediately = true;

        // Pitch on/off changes the latency: switched under the same fade
        bool activePitchOn = false, pendingPitchOn = false;
        bool structureChangePending() const noexcept;
        void applyPendingStructure() noexcept;

        juce::AudioBuffer<float> work, dryForBypass, reverbIn;
        std::array<juce::AudioBuffer<float>, numDelays> delayWet;
        std::array<juce::AudioBuffer<float>, numReverbs> reverbWet;

        std::array<Ducker, numDelays> delayDucker;
        std::array<Ducker, numReverbs> reverbDucker;
        std::array<std::vector<float>, numDelays> delayDuckGains;
        std::array<std::vector<float>, numReverbs> reverbDuckGains;
        // Sidechain pointers for the chunk being processed (nullptr = internal)
        std::array<const float*, maxChannels> scChunk {};
        int scChannels = 0;
    };
}
