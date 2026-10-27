#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace velvox::dsp
{
    struct DenoiseSettings
    {
        bool enabled = false;
        float amount = 50.0f;   // %, 0..100: 100 = up to 18 dB less noise floor
    };

    // RAUMRAUSCHEN (0.7, docs/superpowers/plans/2026-10-27-denoise-breath-modules.md): lowers steady room
    // noise (fan, room hum, preamp hiss) without touching the voice. STFT (FFT 1024, hop 256, Hann analysis
    // and synthesis), per channel. Noise floor per bin: the minimum of its smoothed power over the last
    // 1.5 s (minimum statistics, so it is learned while the voice sings), at most the median of its 17
    // neighbouring bins (room noise is smooth across frequency; a sustained note is a narrow peak and never
    // counts as noise), times a bias of 1.5. Gain per bin max (1 - a * N / P, floor), a = 1 + 2 * amount,
    // floor = -18 dB * amount; gains fall within one frame, rise over 60 ms, and are averaged over 3 bins
    // against "musical noise". Constant latency of 1024 samples while in the chain; switching on / off
    // fades the reduction in / out (the STFT itself is transparent).
    class DenoiseModule
    {
    public:
        static constexpr int fftOrder = 10;
        static constexpr int fftSize = 1 << fftOrder;   // 1024
        static constexpr int hop = fftSize / 4;          // 256
        static constexpr int numBins = fftSize / 2 + 1;
        static constexpr int latency = fftSize;          // 1024 samples

        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        void setSettings (const DenoiseSettings& s) noexcept { settings = s; }
        void process (float* const* io, int numChannels, int numSamples) noexcept;

        int getLatencySamples() const noexcept { return latency; }
        // Mean reduction of the last frame in dB (<= 0), for the meter (lock-free)
        float getReductionDb() const noexcept { return reductionDb.load (std::memory_order_relaxed); }

    private:
        static constexpr int numSubWindows = 6;

        struct Channel
        {
            std::vector<float> in, out;                    // circular input / overlap-add output, fftSize each
            std::vector<float> power, gain;                // per bin
            std::array<std::vector<float>, numSubWindows> subMin;
        };

        void processFrame (Channel& c) noexcept;

        DenoiseSettings settings;
        double fs = 48000.0;
        std::array<Channel, 2> channels;
        std::vector<float> window, frame, spectrum, noise, rawGain, scratch;
        std::unique_ptr<juce::dsp::FFT> fft;
        int pos = 0, hopCounter = 0;
        int subWindowFrames = 47, framesInSub = 0, subIndex = 0;
        float onAmount = 0.0f;                             // smoothed 0..1 per frame
        float riseCoeff = 0.8f;
        std::atomic<float> reductionDb { 0.0f };
    };
}
