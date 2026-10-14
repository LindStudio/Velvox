#pragma once

// Central list of every parameter ID. These strings are persisted in DAW
// projects and preset files - never rename an existing ID, only add new ones.

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
}
