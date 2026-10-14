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
}
