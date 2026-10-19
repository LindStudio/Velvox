#pragma once

#include "CaptureBuffer.h"
#include <optional>

namespace velvox::automix
{
    // How far a statement can be trusted.
    //  measured - a direct measurement with enough material (level, peaks, clipping)
    //  likely   - a heuristic that passed a strict threshold on enough material
    //  possible - a weak indication; the mixer reacts only gently, if at all
    enum class Confidence { possible = 0, likely = 1, measured = 2 };

    // One observation about the recording (German UI texts)
    struct Issue
    {
        juce::String key;          // stable id ("sibilance", "rumble", ...)
        juce::String title;        // short statement
        juce::String detail;       // what was measured
        Confidence confidence = Confidence::possible;
        float severity = 0.0f;     // sorting only
    };

    struct Resonance
    {
        float hz = 0.0f;
        float excessDb = 0.0f;     // above the local spectrum (1/2 octave either side)
        bool strong = false;
    };

    // Everything the analysis derived from the captured section. Measurements
    // carry a validity flag where they can fail; heuristics are only stated
    // as Issues with a confidence. Nothing here claims to know the singer's
    // voice type - only properties of this recording.
    struct VocalProfile
    {
        // ---- Material
        double sampleRate = 48000.0;
        int channels = 0;
        float capturedSeconds = 0.0f;
        float voicedSeconds = 0.0f;
        bool enoughMaterial = false;      // >= minimum voiced seconds
        bool stoppedEarly = false;

        // ---- Level (measured)
        float integratedLufs = -100.0f;   // ITU-R BS.1770 gated, all channels
        float peakDb = -100.0f;           // sample peak
        int clipRuns = 0;                 // runs of >= 3 samples at full scale
        float crestDb = 0.0f;             // median peak-to-RMS of voiced frames
        float loudnessRangeDb = 0.0f;     // EBU Tech 3342 style (3 s windows, 10th..95th percentile)
        bool loudnessRangeValid = false;
        // Word / phrase dynamics: spread of 400 ms voice levels (10th..95th percentile).
        // EBU LRA hides this on short sections; this is what a rider and compressor react to.
        float dynamicsDb = 0.0f;
        bool dynamicsValid = false;
        float voiceRmsDb = -100.0f;       // median RMS of voiced 50 ms blocks (mid signal, dBFS)
        float voiceRmsLowDb = -100.0f;    // 10th percentile (quiet words)
        float voiceRmsHighDb = -100.0f;   // 90th percentile (loud words)
        float noiseFloorDb = -100.0f;     // level of the quiet captured parts (5th percentile of 50 ms blocks)
        float snrDb = 0.0f;
        bool snrValid = false;            // enough pause material to measure a floor

        // ---- Pitch (autocorrelation of voiced frames)
        float f0MedianHz = 0.0f, f0LowHz = 0.0f, f0HighHz = 0.0f;
        float pitchedShare = 0.0f;        // share of voiced frames with a clear pitch
        float pitchSpreadSemitones = 0.0f;
        bool pitchValid = false;
        std::vector<float> sungNotesHz;   // pitches that take at least 4 % of the pitched frames (semitone grid)

        // ---- Spectrum of the voice (1/12 octave from 40 Hz, voiced frames)
        std::vector<float> spectrumDb;
        // Band energies relative to the core of the voice (300 Hz .. 2 kHz)
        float rumbleDb = -60.0f;          // 40 .. 90 Hz
        float bodyDb = -60.0f;            // 100 .. 300 Hz
        float lowMidDb = -60.0f;          // 250 .. 500 Hz
        float presenceDb = -60.0f;        // 2 .. 5 kHz
        float sibilanceBandDb = -60.0f;   // 5 .. 10 kHz
        float airDb = -60.0f;             // 10 .. 16 kHz
        float topDb = -60.0f;             // everything above 2 kHz
        float tiltDbPerOctave = 0.0f;     // regression 250 Hz .. 8 kHz
        float lowMidPeakHz = 300.0f;      // strongest point of 180 .. 500 Hz

        // ---- Sibilance
        bool sibilanceValid = false;
        float sibilanceVsVowelDb = -100.0f;   // S frames against vowel frames
        float sibilanceHz = 6500.0f;          // centre of the S energy
        float sibilantShare = 0.0f;           // share of voiced frames that are S sounds
        float deessDetectorHz = 6000.0f;      // high-pass used for the next two
        float sibBandVowelP90Db = -100.0f;    // detector peak level on vowels (90th percentile)
        float sibBandSibilantP50Db = -100.0f; // detector peak level on S sounds (median)

        // ---- Resonances (narrow peaks of the long-term spectrum)
        std::vector<Resonance> resonances;

        // ---- Room / effects already on the recording
        float tailSeconds = 0.0f;         // median decay after words, scaled to 30 dB (from the -6 .. -26 dB slope)
        int tailCount = 0;
        bool reverberant = false;

        // ---- Stereo (only meaningful for two different channels)
        bool stereoContent = false;
        float correlation = 1.0f;
        float balanceDb = 0.0f;           // left minus right
        float sideToMidDb = -100.0f;

        bool heavilyCompressed = false;   // small loudness range and low crest factor
        bool distortionLikely = false;

        // ---- Beat on the sidechain (analysis only, never audible)
        bool beatPresent = false;
        float beatLufs = -100.0f;
        float beatPresenceVsVocalDb = 0.0f;   // beat 2-5 kHz level minus vocal 2-5 kHz level (voiced frames)
        float beatLowMidVsVocalDb = 0.0f;     // same for 250-500 Hz
        bool maskingHint = false;

        std::vector<Issue> issues;

        // Frame classification (analysis frames: 4096 samples, hop 1024) - lets a
        // processed version be measured on exactly the same S and vowel moments
        std::vector<int> sibilantFrames, vowelFrames;

        bool hasIssue (const juce::String& key) const
        {
            for (auto& i : issues)
                if (i.key == key)
                    return true;
            return false;
        }
        const Issue* issue (const juce::String& key) const
        {
            for (auto& i : issues)
                if (i.key == key)
                    return &i;
            return nullptr;
        }
    };

    class VocalProfiler
    {
    public:
        static constexpr int fftOrder = 12;
        static constexpr int fftSize = 1 << fftOrder;
        static constexpr int numBands = 104;       // 1/12 octave from 40 Hz
        static float bandCentre (int band) noexcept { return 40.0f * std::pow (2.0f, (float) band / 12.0f); }

        // Pure function of the audio; safe on any thread. `minVoicedSeconds`
        // decides `enoughMaterial`. Optional cancellation: return true to stop.
        static VocalProfile analyse (const CapturedAudio& audio, float minVoicedSeconds = 5.0f,
                                     const std::function<bool()>& shouldStop = {});

        // Gated integrated loudness (ITU-R BS.1770-4) of a multichannel signal
        static float integratedLufs (const std::vector<const float*>& channels, int numSamples, double sampleRate);
        // Loudness range (EBU Tech 3342), nullopt when there is too little material
        static std::optional<float> loudnessRange (const std::vector<const float*>& channels, int numSamples, double sampleRate);

        // S frames against vowel frames (dB) of `audio`, using the frame classification of
        // `reference` (the unprocessed analysis). nullopt when the reference has too few frames.
        static std::optional<float> sibilanceAt (const CapturedAudio& audio, const VocalProfile& reference);
    };
}
