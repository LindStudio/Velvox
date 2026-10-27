#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace velvox::dsp
{
    struct BreathSettings
    {
        bool enabled = false;
        float amount = 50.0f;   // %, 0..100: 50 = breaths -12 dB, 100 = -30 dB (practically gone)
    };

    // ATEM-ABSENKER (0.7, docs/superpowers/plans/2026-10-27-denoise-breath-modules.md): lowers breaths
    // between the lines. Every ~10 ms the detector looks at the last ~43 ms of the mono sum (2048 samples,
    // 4096 above 50 kHz - long enough to resolve the harmonics of a voice and see its period): a step is
    // breath-like when it is quiet against the phrase (12 - 45 dB below a 2 s peak hold), noisy (spectral
    // flatness 500 Hz - 6 kHz >= 0.25), centred low enough (energy centroid 500 Hz - 5 kHz; S sounds sit
    // higher and stay) and unvoiced (normalised autocorrelation for 66 - 400 Hz < 0.4). At least 60 ms in a
    // row are a breath (a confirmed one forgives 2 odd steps). The look-ahead of 90 ms covers that minimum
    // plus half the analysis window, so a breath is lowered from its start; the gain moves in over 5 ms and
    // out over 30 ms. Constant latency of 90 ms while in the chain (also when off).
    class BreathModule
    {
    public:
        static int latencyFor (double sampleRate) noexcept { return juce::roundToInt (sampleRate * 0.09); }

        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        void setSettings (const BreathSettings& s) noexcept { settings = s; }
        void process (float* const* io, int numChannels, int numSamples) noexcept;

        int getLatencySamples() const noexcept { return latencyFor (fs); }
        // Current reduction in dB (<= 0), for the meter (lock-free)
        float getReductionDb() const noexcept { return reductionDb.load (std::memory_order_relaxed); }

        // Gain in dB on a breath for an amount in % (50 -> -12, 100 -> -30)
        static float breathGainDb (float amountPercent) noexcept;

    private:
        bool analysisIsBreathLike() noexcept;   // the newest hop and the last analysisSize samples of `history`

        BreathSettings settings;
        double fs = 48000.0;
        int latency = 4320, hopSize = 512, analysisSize = 2048, fftOrder = 12, minRunFrames = 6;
        std::array<std::vector<float>, 2> delayLine;
        std::vector<float> marks;               // per delayed sample: 1 = breath (same ring as the delay)
        int delayPos = 0;
        std::vector<float> history, window, windowAc, fftData;   // history: mono sum, circular
        std::unique_ptr<juce::dsp::FFT> fft;
        int historyPos = 0, hopFill = 0, run = 0, misses = 0;
        float phraseDb = -120.0f, holdSeconds = 0.0f;
        float gain = 1.0f, attackCoeff = 0.0f, releaseCoeff = 0.0f;
        std::atomic<float> reductionDb { 0.0f };
    };
}
