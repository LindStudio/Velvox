#pragma once

#include "CheckTargets.h"
#include "CheckTypes.h"
#include <functional>

namespace velvox::dsp
{
    struct AnalysisResult;
    struct Finding;
}

// VOCAL CHECK rules: measurements (dsp::AnalysisResult) against the targets -> findings.
//
// Every rule compares one measurement with its target and states a confidence: 0.5 at the
// rule's threshold, rising to 0.95 when the measurement lies `span` beyond it, scaled down
// when little voice was analysed. Only findings with confidence >= showThreshold are kept
// (the others are counted in hiddenFindings). Pure functions, any thread.
namespace velvox::check
{
    CheckTargets targetsFor (const dsp::AnalysisResult& r, Genre genre);

    // Replaces r.findings, sets r.hiddenFindings and r.score
    void evaluate (dsp::AnalysisResult& r, Genre genre);

    // 0..100 over the findings `include` accepts (e.g. not ignored); -1 without enough material
    int scoreOf (const dsp::AnalysisResult& r, const std::function<bool (const dsp::Finding&)>& include = {});

    // ISO 1/3-octave centre nearest to `hz` (ids of frequency-specific findings: "resonance.3150")
    int thirdOctaveCentre (float hz);

    // German number formats used by the texts: "3,5 kHz", "300 Hz", "−3 dB", "+4 dB"
    juce::String hzText (float hz);
    juce::String dbText (float db, bool withPlus = true);
    // A level in dBFS without the unit, signed on both sides of 0: "+3", "−0,5", "0"
    juce::String dbfsText (float dbfs);
}
