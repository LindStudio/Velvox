#pragma once

#include <juce_dsp/juce_dsp.h>
#include <deque>
#include "CheckModule.h"
#include "Routing.h"

namespace velvox::dsp
{
    // One diagnosis. Only produced when the measurement supports it:
    //  - certainty `sure`     = likely problem (a strict threshold was passed)
    //  - certainty `possible` = uncertain observation (softer threshold; may be intended)
    // Every finding names its evidence (the measurement), why it matters musically, a
    // suggestion, and a confidence. The confidence is a documented heuristic, not a
    // probability: how far the measurement lies beyond its threshold, scaled down when
    // little material was analysed. Sample-accurate level facts (clipping) are ~0.99.
    struct Finding
    {
        enum class Certainty { sure, possible };
        Certainty certainty = Certainty::possible;
        juce::String title;          // the issue
        juce::String detail;         // evidence: what was measured
        juce::String advice;         // suggested correction, naming the Headliner module
        std::optional<ModuleId> module;
        float severity = 0.0f;       // for sorting (larger = more important)
        juce::String why;            // why it matters musically
        float confidence = 0.5f;     // 0..1 (see above)

        // Filled in by the processor from the chain around VOCAL CHECK
        enum class Handling { notHandled, handledLater, appliedBefore, autoMix };
        Handling handling = Handling::notHandled;
        juce::String handlingText;   // e.g. "De-esser is on after this point"
    };

    struct AnalysisResult
    {
        bool enoughMaterial = false;
        float voicedSeconds = 0.0f;
        float analysedSeconds = 0.0f;
        std::vector<Finding> findings;

        // Measurements (shown in the UI as facts behind the findings)
        float peakDb = -100.0f;
        int clipEvents = 0;
        float loudnessRangeDb = 0.0f;     // 10th..95th percentile of 400 ms levels (voice only)
        float crestDb = 0.0f;             // median peak-to-RMS per analysis frame
        float correlation = 1.0f;
        bool stereo = false;
        float sibilanceVsVowelDb = -100.0f;
        float noiseFloorDb = -200.0f;     // median level of the pauses (only with enough pause frames)
        float snrDb = 0.0f;               // loud voice level above the noise floor
        std::array<float, 6> bandDb {};   // rumble, body, mid, presence, sibilance, air (relative to mid)
        std::vector<float> spectrumDb;    // 1/12 octave average spectrum (voice frames), for the UI
        float spectrumLowHz = 40.0f;      // first band centre
    };

    // VOCAL CHECK analysis. Runs on the message thread (never the audio thread):
    // pump() drains the CheckModule tap, cuts the signal into 85 ms frames and
    // stores compact features per frame; analyse() turns the stored frames into
    // findings. Continuous mode keeps the last ~30 s, Capture mode everything
    // between start and stop (up to 5 minutes).
    class VocalAnalyzer
    {
    public:
        static constexpr int fftOrder = 12;
        static constexpr int fftSize = 1 << fftOrder;
        static constexpr int hop = fftSize / 2;
        static constexpr int numBands = 104;        // 1/12 octave from 40 Hz
        static constexpr float minVoicedSeconds = 4.0f;

        enum class Mode { continuous, capture };

        explicit VocalAnalyzer (CheckModule& module);

        void setSampleRate (double sampleRate);
        void setMode (Mode m);
        Mode getMode() const noexcept { return mode; }

        void startCapture();
        void stopCapture();
        bool isCapturing() const noexcept { return capturing; }
        void clear();

        // Message thread: drain the module and analyse new frames
        void pump();
        // Direct feeding (tests, offline use)
        void feed (const float* left, const float* right, int numSamples);

        AnalysisResult analyse() const;

        static float bandCentre (int band) noexcept { return 40.0f * std::pow (2.0f, (float) band / 12.0f); }

    private:
        struct Frame
        {
            float rmsDb = -100.0f, peakDb = -100.0f;
            float lr = 0.0f, ll = 0.0f, rr = 0.0f;
            std::array<float, numBands> bands {};    // power per 1/12 octave band
        };

        void analyseFrame();
        bool storing() const noexcept { return mode == Mode::continuous || capturing; }
        size_t maxFrames() const noexcept;

        CheckModule& check;
        double fs = 48000.0;
        Mode mode = Mode::continuous;
        bool capturing = false;

        juce::dsp::FFT fft { fftOrder };
        juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann, false };
        std::vector<float> inL, inR, pullL, pullR, work;
        int fill = 0;
        std::vector<int> binToBand;
        std::array<int, numBands> binsPerBand {};
        std::deque<Frame> frames;
    };
}
