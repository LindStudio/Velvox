#pragma once

#include <vector>

// VOCAL CHECK target ranges: what a finished lead vocal should measure at the point of the
// chain where VOCAL CHECK listens. The "party" ranges are the windows of the auto-mix
// objective (AutoMix/AutoMixObjective.cpp: calibrated on real vocals and the blind listening
// tests); the other genres widen them where those styles keep more dynamics or less top.
namespace velvox::check
{
    enum class Genre { party = 0, pop, acoustic };   // persisted as the int value

    struct Window
    {
        float lo, hi;
        bool contains (float v) const noexcept { return v >= lo && v <= hi; }
    };

    struct CheckTargets
    {
        // Band levels relative to the 300 Hz - 2 kHz core of the voice (dB)
        Window body { -9.0f, -1.0f };       // 100 - 300 Hz
        Window lowMid { -7.0f, 0.0f };      // 250 - 500 Hz
        Window top { -16.0f, -7.0f };       // above 2 kHz
        Window air { -32.0f, -16.0f };      // 10 - 16 kHz
        float presenceMax = -4.0f;          // 2 - 5 kHz
        float rumbleMax = -24.0f;           // 40 - 90 Hz
        float sibilanceMax = -6.0f;         // S frames against vowel frames (objective; shown as a fact)
        float sibilancePeakMax = -9.0f;     // loudest 5-10 kHz moments against the vowels (the rule)

        // Dynamics
        Window phrase { 3.0f, 6.0f };       // spread of 400 ms voice levels (10th - 95th percentile)
        float crestMin = 5.5f;              // below: over-compressed
        float crestMax = 18.0f;             // above: very peaky (needs a compressor)
        float lraMax = 10.0f;               // section-level loudness range (LU)

        // Level and technical
        float loudnessMin = -32.0f;         // LUFS of the vocal at this point
        float peakMax = -1.0f;              // dBFS
        float correlationMin = 0.3f;
        float snrMin = 30.0f;
        float tailMax = 0.9f;               // s for a 30 dB decay after words

        // Frequency-specific deviations from the voice template (1/3-octave): at least devMin
        // dB and devZ times the spread of the template voices in that band
        float devMud = 2.5f;                // 200 - 500 Hz
        float devBoxy = 2.5f;               // 500 - 1000 Hz
        float devZ = 1.8f;
        float resonanceMin = 12.0f;         // narrow peak over its +-1/2 octave surroundings

        static CheckTargets make (Genre genre, bool highVoice, bool spoken);
    };

    // Long-term spectrum of a natural dry voice on the analyzer's 1/12-octave grid from 40 Hz
    // (VocalAnalyzer::numBands values, dB, 1/3-octave smoothed, median-normalised over
    // 150 Hz - 8 kHz). Two classes: low voices and high voices (lowest notes above 250 Hz).
    // Generated from VocalSet train singers by `VelvoxTests --check-template` (VoiceTemplate.h).
    const std::vector<float>& voiceTemplate (bool highVoice);
    // Spread (standard deviation, dB) of the template voices per band
    const std::vector<float>& voiceTemplateSpread (bool highVoice);
}
