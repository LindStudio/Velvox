#pragma once

#include "../DSP/VocalChain.h"
#include "../DSP/Macros.h"
#include "ParameterLayout.h"

#include <functional>
#include <map>
#include <memory>

namespace velvox
{
    // Turns raw parameter values (atomics) into the plain ChainSettings the DSP
    // consumes. The processor binds it to its APVTS; the offline renderer of
    // the automatic mixer binds it to its own value store. Both therefore use
    // exactly the same parameter -> DSP mapping.
    class SettingsReader
    {
    public:
        using Lookup = std::function<std::atomic<float>* (const char* paramId)>;

        void bind (const Lookup& lookup);

        // Thread-safe (relaxed atomic reads, no allocation). Routing and the mono
        // monitor are not parameters and come from the caller. Every instance bank
        // (instances 2..n) is read with the same mapping as instance 1.
        dsp::ChainSettings read (const dsp::Routing& routing, bool monoMonitor) const noexcept;
        dsp::MacroValues readMacros() const noexcept;

        bool pitchOn() const noexcept   { return raw.pitchOn.on(); }
        bool checkOn() const noexcept   { return raw.checkOn.on(); }
        int checkMode() const noexcept  { return raw.checkMode.index(); }

    private:
    struct Raw
    {
        std::atomic<float>* p = nullptr;
        float get() const noexcept { return p->load (std::memory_order_relaxed); }
        bool on() const noexcept { return get() > 0.5f; }
        int index() const noexcept { return (int) std::lround (get()); }
    };

    struct Params
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
        Raw doublerVoices, doublerModDepth, doublerModRate, doublerLowCut, doublerDry;
        std::array<Raw, 4> doublerVOn, doublerVDelay, doublerVDetune, doublerVGain, doublerVPan;
        Raw duckSource, delayDuckAmount, delayDuckThreshold, delayDuckAttack, delayDuckRelease;
        Raw reverbDuckAmount, reverbDuckThreshold, reverbDuckAttack, reverbDuckRelease;
        Raw pitchOn, pitchSemitones, pitchCents, pitchPreserveFormants, pitchFormant, pitchMix;
        Raw chopOn, chopPattern, chopRate, chopDepth, chopSmooth;
        Raw lowType, lowQ, highType, highQ, lpfOn, lpfFreq, lpfSlope, satCharacter, deessMode, compDetector, compScHpf;
        Raw macroPresence, macroWarmth, macroAggression, macroWidth, macroSpace, macroDensity;
        Raw pulseOn, pulseRate, pulseDepth, pulseAttack, pulseRelease, pulseSmooth, pulseInterp, pulseTension, pulseMix, pulseBpm;
        std::array<Raw, 6> pulseX, pulseY;
        Raw pulseShape, pulseAmount;   // 0.6
        Raw smashOn, smashAmount, smashBlend, smashAttack, smashRelease, smashTone, smashOutput;
        Raw choirOn, choirVoices, choirDetune, choirTiming, choirHumanize, choirWidth, choirDensity, choirMix;
        Raw smoothOn, smoothAmount, smoothSensitivity, smoothLow, smoothHigh, smoothAttack, smoothRelease, smoothMode, smoothOutput, smoothDelta;
        std::array<Raw, 6> smoothWFreq, smoothWGain;
        Raw checkOn, checkMode;
        Raw rideOn, rideTarget, rideAmount, rideMaxBoost, rideMaxCut, rideResponse, ridePhrase, rideTransient, rideSilence;
        Raw reverbEarly, reverbDiffusion, reverbMod, reverbBass, reverbHighCut;
        Raw delaySpread, delayDrive, delayDiffusion, delayMod;
        Raw satMultiband, satXoverLow, satXoverHigh;            // 0.6 Saturation multiband
        std::array<Raw, 3> satBandOn, satBandDrive, satBandChar;
        struct EqBandRaw { Raw on, type, freq, gain, q, slope, role; };
        std::array<EqBandRaw, dsp::maxEqBands> eqBands;
        Raw eqLegacyResponse;
    };
    Params raw;                                                  // instance 1 (and all global parameters)
    std::array<Params, dsp::maxExtraInstances> extraRaw;         // instances 2..n: module fields bound to "<id>_<k>"

    static void bindParams (Params& raw, const Lookup& lookup, int instance);
    static dsp::ChainSettings readParams (const Params& raw) noexcept;
    };

    // A complete set of parameter values outside any APVTS (defaults for
    // everything not set). Used for offline rendering on a worker thread.
    class OfflineParameters
    {
    public:
        OfflineParameters();
        void set (const juce::String& id, float value);   // clamped to the parameter range
        void setAll (const std::map<juce::String, float>& values);
        float get (const juce::String& id) const;

        dsp::ChainSettings read (const dsp::Routing& routing) const noexcept { return dsp::applyMacros (reader.read (routing, false), reader.readMacros()); }

    private:
        std::map<juce::String, std::unique_ptr<std::atomic<float>>> store;
        SettingsReader reader;
    };
}
