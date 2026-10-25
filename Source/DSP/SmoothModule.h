#pragma once

#include "DspUtilities.h"
#include "SeqLockValue.h"
#include <juce_dsp/juce_dsp.h>

namespace velvox::dsp
{
    struct SmoothSettings
    {
        static constexpr int numWeights = 6;

        bool enabled = false;
        float amount = 0.5f;        // 0..1 (0 = transparent)
        float sensitivity = 0.5f;   // 0..1 (higher = smaller peaks count)
        float lowHz = 800.0f;
        float highHz = 12000.0f;
        float attackMs = 5.0f;
        float releaseMs = 80.0f;
        int mode = 0;               // 0 soft, 1 hard
        float outputDb = 0.0f;
        bool delta = false;         // output only what is removed

        // Weighting curve (0.6): points in Hz / dB; > 0 dB = more reduction there. All 0 = flat (as before).
        std::array<float, numWeights> weightFreq { 150.0f, 400.0f, 1000.0f, 2500.0f, 6000.0f, 14000.0f };
        std::array<float, numWeights> weightGain {};
    };

    // What the panel draws: the input spectrum and the reduction per FFT bin (latest frame)
    struct SmoothSpectrum
    {
        static constexpr int maxBins = 2049;   // FFT 4096 (176.4 / 192 kHz)
        int numBins = 0;
        float binHz = 0.0f;
        std::array<float, maxBins> inputDb {};      // detection level per bin, dBFS (full-scale sine ~ 0 dB)
        std::array<float, maxBins> reductionDb {};  // >= 0, what is removed per bin
    };

    // SMOOTH - dynamic resonance suppression (0.6 engine, soothe class).
    //
    // Short-time Fourier transform (periodic Hann analysis and synthesis, FFT 1024 at
    // 44.1 / 48 kHz and twice that per doubling of the rate, 75 % overlap). Every frame
    // the stereo-linked power spectrum is smoothed a little across frequency (1/6 oct,
    // 1/12 oct in hard mode) and held with a 10 ms release: the detection spectrum.
    // Its spectral envelope is a 1-octave smoothing in dB that ignores the peaks
    // themselves (a first pass clips everything 3 dB above it). A bin is a resonance
    // by the amount it sticks out of the envelope above the Sensitivity threshold
    // (shifted by the weighting curve); the reduction grows with that excess (soft
    // knee) times the depth (Amount), is limited, faded at the low / high limits,
    // smoothed across frequency (1/12 oct) and in time (attack / release per bin).
    //
    // Only the removed part X * (1 - g) is resynthesised and subtracted from the
    // latency-aligned input: with nothing to remove the output is the input delayed
    // bit for bit, and Delta plays exactly the removed signal. The module delays by
    // its latency (= FFT size) whenever it is in the chain, also while switched off,
    // so the latency reported to the host never changes while playing.
    class SmoothModule
    {
    public:
        // Display bands of the 0.3-0.5 panel (the reduction is aggregated into them)
        static constexpr int numBands = 32;
        static float bandFrequency (int band) noexcept;   // centre frequency in Hz

        static int fftSizeFor (double sampleRate) noexcept;
        int getLatencySamples() const noexcept { return fftSize; }

        // Weighting curve in dB at a frequency: monotone cubic through the points in log
        // frequency, flat outside the outermost points
        static float weightingDbAt (const SmoothSettings& s, float hz) noexcept;
        // Linked Speed control of the panel (0 = slow, 1 = fast) -> attack, release in ms
        static std::pair<float, float> speedToTimes (float speed01) noexcept;

        void prepare (double sampleRate, int maxBlockSize);
        // Clears the state while playing: silent until the delay line is refilled, then fades in
        void reset (bool fadeIn = true);
        void setSettings (const SmoothSettings& s) noexcept;
        void process (float* const* channels, int numChannels, int numSamples) noexcept;

        // Current reduction per display band in dB (>= 0, the band's deepest bin)
        std::array<std::atomic<float>, numBands> bandCutDb {};
        float getMaxCutDb() const noexcept;
        // Latest spectrum frame for the panel (wait-free; false while a frame is being written)
        bool getSpectrum (SmoothSpectrum& out) const noexcept { return spectrum.tryLoad (out); }

    private:
        static constexpr int maxChannels = 2;
        void processFrame (int numChannels) noexcept;
        void updateWeights() noexcept;
        void clearState() noexcept;
        void applyPrime (float* const* ch, int numChannels, int i) noexcept;
        int primeRemaining = 0;
        float primeGain = 1.0f, primeStep = 0.001f;

        double fs = 48000.0;
        SmoothSettings settings;
        int fftSize = 1024, hop = 256, numBins = 513, fftOrder = 10;
        std::unique_ptr<juce::dsp::FFT> fft;

        std::vector<float> window;
        std::array<std::vector<float>, maxChannels> inRing, outFifo, outAccum;
        std::array<std::vector<float>, maxChannels> spec;   // FFT work buffer (2 * fftSize)
        int ringPos = 0, hopCounter = 0;

        // per bin
        std::vector<float> detPower, levelDb, envelope, work, work2, reduction, gainDb, rangeAndWeight, weightDb, cutScale;
        std::vector<int> detRadius, envRadius, gainRadius, maxRadius, binBand;
        std::vector<double> prefix;

        float attackCoeff = 0.0f, releaseCoeff = 0.0f, detReleaseCoeff = 0.0f;
        float threshold = 7.0f, depth = 0.0f, maxCut = 0.0f;
        bool hard = false;
        std::array<float, SmoothSettings::numWeights> appliedWeightFreq {}, appliedWeightGain {};
        float appliedLow = -1.0f, appliedHigh = -1.0f;
        int appliedMode = -1;

        SeqLockValue<SmoothSpectrum> spectrum;
        SmoothSpectrum frameOut;

        juce::SmoothedValue<float> enabled { 0.0f }, outputGain { 1.0f }, deltaMix { 0.0f };
        bool snapNext = true;    // first settings after prepare(): no fade from the defaults
        bool wasActive = false;
    };
}
