#pragma once

// Central list of every parameter ID. These strings are persisted in DAW
// projects and preset files - never rename an existing ID, only add new ones.

#include <juce_core/juce_core.h>

namespace velvox::pid
{
    // Global / IO
    inline constexpr const char* inputGain   = "inputGain";
    inline constexpr const char* outputGain  = "outputGain";
    inline constexpr const char* bypass      = "bypass";
    inline constexpr const char* safetyClip  = "safetyClip";

    // Equalizer
    inline constexpr const char* eqOn        = "eqOn";
    inline constexpr const char* eqMix       = "eqMix";
    inline constexpr const char* hpfOn       = "hpfOn";
    inline constexpr const char* hpfFreq     = "hpfFreq";
    inline constexpr const char* hpfSlope    = "hpfSlope";
    inline constexpr const char* lowFreq     = "lowFreq";
    inline constexpr const char* lowGain     = "lowGain";
    inline constexpr const char* midFreq     = "midFreq";
    inline constexpr const char* midGain     = "midGain";
    inline constexpr const char* midQ        = "midQ";
    inline constexpr const char* highFreq    = "highFreq";
    inline constexpr const char* highGain    = "highGain";

    // Compressor
    inline constexpr const char* compOn        = "compOn";
    inline constexpr const char* compThreshold = "compThreshold";
    inline constexpr const char* compRatio     = "compRatio";
    inline constexpr const char* compAttack    = "compAttack";
    inline constexpr const char* compRelease   = "compRelease";
    inline constexpr const char* compKnee      = "compKnee";
    inline constexpr const char* compMakeup    = "compMakeup";
    inline constexpr const char* compMix       = "compMix";

    // De-esser
    inline constexpr const char* deessOn        = "deessOn";
    inline constexpr const char* deessFreq      = "deessFreq";
    inline constexpr const char* deessThreshold = "deessThreshold";
    inline constexpr const char* deessRange     = "deessRange";
    inline constexpr const char* deessListen    = "deessListen";

    // Saturation
    inline constexpr const char* satOn     = "satOn";
    inline constexpr const char* satDrive  = "satDrive";
    inline constexpr const char* satOutput = "satOutput";
    inline constexpr const char* satMix    = "satMix";

    // Delay
    inline constexpr const char* delayOn       = "delayOn";
    inline constexpr const char* delayTime     = "delayTime";
    inline constexpr const char* delaySync     = "delaySync";
    inline constexpr const char* delayDivision = "delayDivision";
    inline constexpr const char* delayFeedback = "delayFeedback";
    inline constexpr const char* delayMix      = "delayMix";
    inline constexpr const char* delayLowCut   = "delayLowCut";
    inline constexpr const char* delayHighCut  = "delayHighCut";
    inline constexpr const char* delayPingPong = "delayPingPong";

    // Reverb
    inline constexpr const char* reverbOn       = "reverbOn";
    inline constexpr const char* reverbType     = "reverbType";
    inline constexpr const char* reverbMix      = "reverbMix";
    inline constexpr const char* reverbDecay    = "reverbDecay";
    inline constexpr const char* reverbPreDelay = "reverbPreDelay";
    inline constexpr const char* reverbSize     = "reverbSize";
    inline constexpr const char* reverbDamping  = "reverbDamping";
    inline constexpr const char* reverbLowCut   = "reverbLowCut";
    inline constexpr const char* reverbWidth    = "reverbWidth";

    // ---- added in 0.2 (all default to neutral so 0.1 projects sound identical) ----

    // Routing
    inline constexpr const char* delayToReverb  = "delayToReverb";

    // Stereo
    inline constexpr const char* stereoWidth    = "stereoWidth";
    inline constexpr const char* monoBelow      = "monoBelow";

    // Level matching
    inline constexpr const char* levelMatchMode   = "levelMatchMode";
    inline constexpr const char* levelMatchWindow = "levelMatchWindow";
    inline constexpr const char* levelMatchRange  = "levelMatchRange";

    // Doubler
    inline constexpr const char* doublerOn       = "doublerOn";
    inline constexpr const char* doublerMix      = "doublerMix";
    inline constexpr const char* doublerWidth    = "doublerWidth";
    inline constexpr const char* doublerDelay    = "doublerDelay";
    inline constexpr const char* doublerDetune   = "doublerDetune";
    inline constexpr const char* doublerMonoSafe = "doublerMonoSafe";
    inline constexpr const char* doublerTone     = "doublerTone";

    // Ducking (reverb / delay returns)
    inline constexpr const char* duckSource         = "duckSource";
    inline constexpr const char* delayDuckAmount    = "delayDuckAmount";
    inline constexpr const char* delayDuckThreshold = "delayDuckThreshold";
    inline constexpr const char* delayDuckAttack    = "delayDuckAttack";
    inline constexpr const char* delayDuckRelease   = "delayDuckRelease";
    inline constexpr const char* reverbDuckAmount    = "reverbDuckAmount";
    inline constexpr const char* reverbDuckThreshold = "reverbDuckThreshold";
    inline constexpr const char* reverbDuckAttack    = "reverbDuckAttack";
    inline constexpr const char* reverbDuckRelease   = "reverbDuckRelease";

    // Pitch
    inline constexpr const char* pitchOn               = "pitchOn";
    inline constexpr const char* pitchSemitones        = "pitchSemitones";
    inline constexpr const char* pitchCents            = "pitchCents";
    inline constexpr const char* pitchPreserveFormants = "pitchPreserveFormants";
    inline constexpr const char* pitchFormant          = "pitchFormant";
    inline constexpr const char* pitchMix              = "pitchMix";

    // Vocal chop
    inline constexpr const char* chopOn      = "chopOn";
    inline constexpr const char* chopPattern = "chopPattern";
    inline constexpr const char* chopRate    = "chopRate";
    inline constexpr const char* chopDepth   = "chopDepth";
    inline constexpr const char* chopSmooth  = "chopSmooth";

    // Pro extensions of existing modules
    inline constexpr const char* lowType      = "lowType";
    inline constexpr const char* lowQ         = "lowQ";
    inline constexpr const char* highType     = "highType";
    inline constexpr const char* highQ        = "highQ";
    inline constexpr const char* lpfOn        = "lpfOn";
    inline constexpr const char* lpfFreq      = "lpfFreq";
    inline constexpr const char* lpfSlope     = "lpfSlope";
    inline constexpr const char* satCharacter = "satCharacter";
    inline constexpr const char* deessMode    = "deessMode";
    inline constexpr const char* compDetector = "compDetector";
    inline constexpr const char* compScHpf    = "compScHpf";

    // Beginner macros
    inline constexpr const char* macroPresence   = "macroPresence";
    inline constexpr const char* macroWarmth     = "macroWarmth";
    inline constexpr const char* macroAggression = "macroAggression";
    inline constexpr const char* macroWidth      = "macroWidth";
    inline constexpr const char* macroSpace      = "macroSpace";
    inline constexpr const char* macroDensity    = "macroDensity";

    // ---- added in 0.3 (all modules default to off: older projects sound identical) ----

    // PULSE - rhythmic volume shaper
    inline constexpr const char* pulseOn      = "pulseOn";
    inline constexpr const char* pulseRate    = "pulseRate";
    inline constexpr const char* pulseDepth   = "pulseDepth";
    inline constexpr const char* pulseAttack  = "pulseAttack";
    inline constexpr const char* pulseRelease = "pulseRelease";
    inline constexpr const char* pulseSmooth  = "pulseSmooth";
    inline constexpr const char* pulseInterp  = "pulseInterp";
    inline constexpr const char* pulseTension = "pulseTension";
    inline constexpr const char* pulseMix     = "pulseMix";
    inline constexpr const char* pulseBpm     = "pulseBpm";
    // Curve points (time 0..1 within the cycle, level 0..1)
    inline constexpr const char* pulseX[6] { "pulseX1", "pulseX2", "pulseX3", "pulseX4", "pulseX5", "pulseX6" };
    inline constexpr const char* pulseY[6] { "pulseY1", "pulseY2", "pulseY3", "pulseY4", "pulseY5", "pulseY6" };

    // VOCAL SMASH - parallel compression
    inline constexpr const char* smashOn      = "smashOn";
    inline constexpr const char* smashAmount  = "smashAmount";
    inline constexpr const char* smashBlend   = "smashBlend";
    inline constexpr const char* smashAttack  = "smashAttack";
    inline constexpr const char* smashRelease = "smashRelease";
    inline constexpr const char* smashTone    = "smashTone";
    inline constexpr const char* smashOutput  = "smashOutput";

    // CHOIR - multi-voice doubling
    inline constexpr const char* choirOn       = "choirOn";
    inline constexpr const char* choirVoices   = "choirVoices";
    inline constexpr const char* choirDetune   = "choirDetune";
    inline constexpr const char* choirTiming   = "choirTiming";
    inline constexpr const char* choirHumanize = "choirHumanize";
    inline constexpr const char* choirWidth    = "choirWidth";
    inline constexpr const char* choirDensity  = "choirDensity";
    inline constexpr const char* choirMix      = "choirMix";

    // SMOOTH - adaptive resonance control
    inline constexpr const char* smoothOn          = "smoothOn";
    inline constexpr const char* smoothAmount      = "smoothAmount";
    inline constexpr const char* smoothSensitivity = "smoothSensitivity";
    inline constexpr const char* smoothLow         = "smoothLow";
    inline constexpr const char* smoothHigh        = "smoothHigh";
    inline constexpr const char* smoothAttack      = "smoothAttack";
    inline constexpr const char* smoothRelease     = "smoothRelease";
    inline constexpr const char* smoothMode        = "smoothMode";
    inline constexpr const char* smoothOutput      = "smoothOutput";
    inline constexpr const char* smoothDelta       = "smoothDelta";

    // VOCAL CHECK - analysis only (never changes the audio)
    inline constexpr const char* checkOn   = "checkOn";
    inline constexpr const char* checkMode = "checkMode";

    // LEVEL RIDE - vocal loudness rider
    inline constexpr const char* rideOn        = "rideOn";
    inline constexpr const char* rideTarget    = "rideTarget";
    inline constexpr const char* rideAmount    = "rideAmount";
    inline constexpr const char* rideMaxBoost  = "rideMaxBoost";
    inline constexpr const char* rideMaxCut    = "rideMaxCut";
    inline constexpr const char* rideResponse  = "rideResponse";
    inline constexpr const char* ridePhrase    = "ridePhrase";
    inline constexpr const char* rideTransient = "rideTransient";
    inline constexpr const char* rideSilence   = "rideSilence";

    // ---- added in 0.4 (neutral defaults; the reverb engine itself is new) ----

    // Reverb engine 0.4
    inline constexpr const char* reverbEarly     = "reverbEarly";
    inline constexpr const char* reverbDiffusion = "reverbDiffusion";
    inline constexpr const char* reverbMod       = "reverbMod";
    inline constexpr const char* reverbBass      = "reverbBass";
    inline constexpr const char* reverbHighCut   = "reverbHighCut";

    // Delay 0.4
    inline constexpr const char* delaySpread    = "delaySpread";
    inline constexpr const char* delayDrive     = "delayDrive";
    inline constexpr const char* delayDiffusion = "delayDiffusion";
    inline constexpr const char* delayMod       = "delayMod";

    // ---- added in 0.6: EQ bands (all off by default; 0.5 states are migrated, see Parameters/EqBands.h) ----
    // Per band n = 1..16: "eqBand<n><Field>", e.g. "eqBand3Gain"; instance k >= 1 appends "_<k+1>" like every
    // EQ parameter ("eqBand3Gain_2"). The fixed bands above (hpf*, low*, mid*, high*, lpf*) stay registered
    // but are hidden and not automatable.
    namespace eqband
    {
        inline constexpr const char* on    = "On";      // bool
        inline constexpr const char* type  = "Type";    // choice, dsp::EqBandType
        inline constexpr const char* freq  = "Freq";    // Hz
        inline constexpr const char* gain  = "Gain";    // dB
        inline constexpr const char* q     = "Q";
        inline constexpr const char* slope = "Slope";   // choice, 6/12/18/24/36/48 dB/oct
        inline constexpr const char* role  = "Role";    // choice, dsp::EqRole (hidden, not automatable)
    }
    inline juce::String eqBandId (int band1to16, const char* field) { return "eqBand" + juce::String (band1to16) + field; }

    // Hidden per EQ instance: 1 = bands use the 0.5 bilinear designs (set only by the migration)
    inline constexpr const char* eqLegacyResponse = "eqLegacyResponse";

    // ---- added in 0.6: Pulse "like Kickstart" (defaults = the 0.5 sound) ----
    inline constexpr const char* pulseShape  = "pulseShape";    // choice: Custom (the pulseX/Y curve), Classic, Punch, Swell, Gate
    inline constexpr const char* pulseAmount = "pulseAmount";   // %, 50 = pulseDepth exactly; 0 none, 100 full ducking
}
