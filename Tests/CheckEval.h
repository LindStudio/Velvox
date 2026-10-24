#pragma once

#include "AutoMix/CaptureBuffer.h"
#include "DSP/VocalAnalyzer.h"
#include "Check/CheckTargets.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <optional>

// VOCAL CHECK calibration helpers (CheckEval.cpp)
namespace velvox::test
{
    std::optional<automix::CapturedAudio> loadVocal (const juce::File& file, double maxSeconds = 300.0);
    // Feeds the audio through a CheckModule into a VocalAnalyzer (capture mode) and analyses it
    dsp::AnalysisResult analyseCaptured (const automix::CapturedAudio& v, check::Genre genre = check::Genre::party);

    int runCheckTemplate (const juce::File& outFile, const juce::StringArray& wavs);
    int runCheckEval (const juce::File& outDir, const juce::StringArray& wavs);
}
