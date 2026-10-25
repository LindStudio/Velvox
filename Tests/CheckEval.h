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

    // Artificial problems added to a real vocal (calibration and the data-backed tests)
    enum class Injection { none, mud, boxy, sibilance, rumble, resonance, plosives };
    const char* injectionName (Injection);
    const char* injectionPrefix (Injection);          // finding id prefix that must appear
    float injectionHz (Injection);                    // frequency it must be found at (+-1/3 octave), 0 = any
    automix::CapturedAudio inject (const automix::CapturedAudio& in, Injection what, unsigned seed = 1);
    // The finding that detects the injected problem (right id and frequency), or nullptr
    const dsp::Finding* detection (const dsp::AnalysisResult& r, Injection what);
    // The take rendered through Headliner with the automatic mix (default parameters as the start)
    std::optional<automix::CapturedAudio> autoMixed (const automix::CapturedAudio& in);

    int runCheckTemplate (const juce::File& outFile, const juce::StringArray& wavs);
    int runCheckEval (const juce::File& outDir, const juce::StringArray& wavs);
}
