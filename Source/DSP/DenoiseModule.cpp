#include "DenoiseModule.h"

namespace velvox::dsp
{
    namespace
    {
        constexpr float bias = 3.5f;             // the minimum of the smoothed power over 5 s lies ~5 dB under the mean of steady noise
        constexpr float ddWeight = 0.98f;        // decision-directed: weight of the last frame's clean estimate
        constexpr float powerSmoothing = 0.9f;
        constexpr float tiny = 1.0e-20f;
    }

    void DenoiseModule::prepare (double sampleRate, int)
    {
        fs = sampleRate;
        fft = std::make_unique<juce::dsp::FFT> (fftOrder);
        window.resize ((size_t) fftSize);
        for (int i = 0; i < fftSize; ++i)   // periodic Hann: Hann^2 at 75 % overlap sums to 1.5
            window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) fftSize);
        spectrum.assign ((size_t) fftSize * 2, 0.0f);
        noise.assign ((size_t) numBins, 0.0f);
        for (auto& c : channels)
        {
            c.in.assign ((size_t) fftSize, 0.0f);
            c.out.assign ((size_t) fftSize, 0.0f);
            c.power.assign ((size_t) numBins, 0.0f);
            c.gain.assign ((size_t) numBins, 1.0f);
            c.prevClean.assign ((size_t) numBins, 0.0f);
            for (auto& m : c.subMin)
                m.assign ((size_t) numBins, 1.0e30f);
        }
        // 5 s of frames in 6 sub-windows: room noise is steady, and a note held for up to ~4 s never
        // becomes "the floor"
        const double frameSeconds = hop / fs;
        subWindowFrames = std::max (1, (int) std::round (5.0 / frameSeconds / numSubWindows));
        reset();
    }

    void DenoiseModule::reset()
    {
        for (auto& c : channels)
        {
            std::fill (c.in.begin(), c.in.end(), 0.0f);
            std::fill (c.out.begin(), c.out.end(), 0.0f);
            std::fill (c.power.begin(), c.power.end(), 0.0f);
            c.primed = false;
            c.framesSeen = 0;
            std::fill (c.gain.begin(), c.gain.end(), 1.0f);
            std::fill (c.prevClean.begin(), c.prevClean.end(), 0.0f);
            for (auto& m : c.subMin)
                std::fill (m.begin(), m.end(), 1.0e30f);
        }
        pos = 0;
        hopCounter = 0;
        framesInSub = 0;
        subIndex = 0;
        onAmount = settings.enabled ? 1.0f : 0.0f;
        reductionDb.store (0.0f);
    }

    void DenoiseModule::processFrame (Channel& c) noexcept
    {
        // Oldest sample first (pos points at the oldest one of the circular input)
        for (int i = 0; i < fftSize; ++i)
            spectrum[(size_t) i] = c.in[(size_t) ((pos + i) % fftSize)] * window[(size_t) i];
        std::fill (spectrum.begin() + fftSize, spectrum.end(), 0.0f);
        fft->performRealOnlyForwardTransform (spectrum.data(), true);

        // Smoothed power and the minimum of the current sub-window. The minimum is tracked only once the input
        // buffer is full and the smoothing has settled (~20 frames): the empty buffer after a reset or a single
        // random low frame would otherwise be "the noise floor" for the next 5 s. Digital silence does count:
        // it is the floor of a clean recording.
        const int settle = fftSize / hop + 20;
        c.framesSeen = std::min (c.framesSeen + 1, settle);
        if (c.framesSeen > fftSize / hop)
        {
            auto& sub = c.subMin[(size_t) subIndex];
            for (int k = 0; k < numBins; ++k)
            {
                const float re = spectrum[(size_t) (2 * k)], im = spectrum[(size_t) (2 * k + 1)];
                const float p = re * re + im * im;
                c.power[(size_t) k] = c.primed ? powerSmoothing * c.power[(size_t) k] + (1.0f - powerSmoothing) * p : p;
                if (c.framesSeen >= settle)
                    sub[(size_t) k] = std::min (sub[(size_t) k], c.power[(size_t) k]);
            }
            c.primed = true;
        }

        // Noise floor: the minimum over the 5 s (nothing learned yet = no noise)
        for (int k = 0; k < numBins; ++k)
        {
            float m = 1.0e30f;
            for (const auto& sw : c.subMin)
                m = std::min (m, sw[(size_t) k]);
            noise[(size_t) k] = m < 1.0e29f ? bias * m : 0.0f;
        }

        // Wiener gain with the decision-directed a-priori SNR; the noise scaled by 1 + amount, a floor of -18 dB * amount
        const float a = juce::jlimit (0.0f, 1.0f, settings.amount / 100.0f);
        const float over = 1.0f + a;
        const float floorGain = juce::Decibels::decibelsToGain (-18.0f * a);
        for (int k = 0; k < numBins; ++k)
        {
            const float re = spectrum[(size_t) (2 * k)], im = spectrum[(size_t) (2 * k + 1)];
            const float p = re * re + im * im;
            const float n = std::max (tiny, over * noise[(size_t) k]);
            const float post = p / n;
            // The decision-directed estimate lags behind onsets and vibrato (a harmonic sliding into a bin):
            // a bin 9 dB above the noise (noise alone practically never gets there) takes its instantaneous SNR
            const float dd = ddWeight * c.prevClean[(size_t) k] / n + (1.0f - ddWeight) * std::max (0.0f, post - 1.0f);
            const float prior = post > 8.0f ? std::max (dd, post - 1.0f) : dd;
            const float g = std::max (floorGain, prior / (1.0f + prior));
            c.gain[(size_t) k] = g;
            c.prevClean[(size_t) k] = g * g * p;
        }
        double sumGain = 0.0;
        for (int k = 0; k < numBins; ++k)
        {
            const float lo = c.gain[(size_t) std::max (0, k - 1)], mid = c.gain[(size_t) k], hi = c.gain[(size_t) std::min (numBins - 1, k + 1)];
            const float g = (lo + mid + hi) / 3.0f;
            const float applied = 1.0f - onAmount * (1.0f - g);
            spectrum[(size_t) (2 * k)] *= applied;
            spectrum[(size_t) (2 * k + 1)] *= applied;
            sumGain += applied;
        }
        reductionDb.store (juce::Decibels::gainToDecibels ((float) (sumGain / numBins), -60.0f), std::memory_order_relaxed);

        fft->performRealOnlyInverseTransform (spectrum.data());
        // Synthesis window; Hann^2 at 75 % overlap sums to 1.5. The frame's first sample belongs to the
        // output slot `pos` (read again after fftSize samples = the latency)
        constexpr float norm = 1.0f / 1.5f;
        for (int i = 0; i < fftSize; ++i)
            c.out[(size_t) ((pos + i) % fftSize)] += spectrum[(size_t) i] * window[(size_t) i] * norm;
    }

    void DenoiseModule::process (float* const* io, int numChannels, int numSamples) noexcept
    {
        const int chans = std::min (numChannels, 2);
        for (int i = 0; i < numSamples; ++i)
        {
            for (int ch = 0; ch < chans; ++ch)
            {
                auto& c = channels[(size_t) ch];
                const float x = io[ch][i];
                io[ch][i] = c.out[(size_t) pos];
                c.out[(size_t) pos] = 0.0f;
                c.in[(size_t) pos] = x;
            }
            pos = (pos + 1) % fftSize;

            if (++hopCounter >= hop)
            {
                hopCounter = 0;
                // On / off: the reduction fades over about 50 ms (the STFT itself stays, so the timing never moves)
                const float target = settings.enabled ? 1.0f : 0.0f;
                onAmount += (target - onAmount) * 0.2f;
                if (std::abs (target - onAmount) < 1.0e-3f)
                    onAmount = target;

                for (int ch = 0; ch < chans; ++ch)
                    processFrame (channels[(size_t) ch]);

                if (++framesInSub >= subWindowFrames)
                {
                    framesInSub = 0;
                    subIndex = (subIndex + 1) % numSubWindows;
                    for (auto& c : channels)
                        std::fill (c.subMin[(size_t) subIndex].begin(), c.subMin[(size_t) subIndex].end(), 1.0e30f);
                }
            }
        }
    }
}
