#include "SmoothModule.h"

namespace velvox::dsp
{
    namespace
    {
        constexpr float bandLowestHz = 120.0f, bandHighestHz = 18000.0f;   // display bands (0.3 panel)
        constexpr float detectReleaseMs = 10.0f;     // hold of the detection spectrum
        constexpr float envelopeOctaves = 1.5f;      // width of the spectral envelope
        constexpr float peakHoldHz = 100.0f;         // harmonic-peak contour: running max over +-this (or +-1/12 oct)
        constexpr float envelopeClipDb = 3.0f;       // peaks above envelope + this are ignored by the envelope
        constexpr float gainOctaves = 1.0f / 12.0f;  // smoothing of the reduction across frequency
        constexpr float kneeDb = 3.0f;
        constexpr float progressiveDb = 6.0f;        // the reduction slope doubles at this excess
        constexpr float primeFadeMs = 8.0f;          // fade-in after a reset, once the delay line is filled
        constexpr float floorBelowPeakDb = 60.0f;    // bins this far below the loudest are never treated
        constexpr float absoluteFloorDb = -110.0f;

        // Radius (bins) of a running-sum box; two passes give a triangle whose width at half
        // height is 2r + 1 bins, matched to a Hann smoothing over +-octaves/2 (at least `minBins`)
        int radiusFor (float hz, float binHz, float octaves, float minBins) noexcept
        {
            const float width = std::max (minBins, std::max (hz, binHz) * (std::pow (2.0f, octaves * 0.5f) - 1.0f) / binHz);
            return std::max (0, (int) std::lround ((width - 1.0f) * 0.5f));
        }

        void boxSmooth (const float* in, float* out, const int* radius, int n, double* prefix) noexcept
        {
            prefix[0] = 0.0;
            for (int k = 0; k < n; ++k)
                prefix[k + 1] = prefix[k] + (double) in[k];
            for (int k = 0; k < n; ++k)
            {
                const int a = std::max (0, k - radius[k]), b = std::min (n, k + radius[k] + 1);
                out[k] = (float) ((prefix[b] - prefix[a]) / (double) (b - a));
            }
        }
    }

    float SmoothModule::bandFrequency (int band) noexcept
    {
        return bandLowestHz * std::pow (bandHighestHz / bandLowestHz, (float) band / (float) (numBands - 1));
    }

    int SmoothModule::fftSizeFor (double sampleRate) noexcept
    {
        if (sampleRate < 32000.0)  return 512;
        if (sampleRate < 66000.0)  return 1024;
        if (sampleRate < 132000.0) return 2048;
        return 4096;
    }

    std::pair<float, float> SmoothModule::speedToTimes (float speed01) noexcept
    {
        const float s = juce::jlimit (0.0f, 1.0f, speed01);
        // 0.5 = the defaults (5 ms / 80 ms); slow end 25 / 400 ms, fast end 1 / 16 ms
        return { 25.0f * std::pow (1.0f / 25.0f, s), 400.0f * std::pow (16.0f / 400.0f, s) };
    }

    float SmoothModule::weightingDbAt (const SmoothSettings& s, float hz) noexcept
    {
        constexpr int n = SmoothSettings::numWeights;
        std::array<std::pair<float, float>, n> p {};
        for (int i = 0; i < n; ++i)
            p[(size_t) i] = { std::log2 (juce::jlimit (10.0f, 30000.0f, s.weightFreq[(size_t) i])),
                              juce::jlimit (-24.0f, 24.0f, s.weightGain[(size_t) i]) };
        std::sort (p.begin(), p.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });

        const float x = std::log2 (std::max (1.0f, hz));
        if (x <= p[0].first)
            return p[0].second;
        if (x >= p[n - 1].first)
            return p[n - 1].second;

        // Fritsch-Carlson monotone cubic: no overshoot between the points
        std::array<float, n - 1> d {};
        std::array<float, n> m {};
        for (int i = 0; i < n - 1; ++i)
        {
            const float dx = p[(size_t) i + 1].first - p[(size_t) i].first;
            d[(size_t) i] = dx > 1.0e-6f ? (p[(size_t) i + 1].second - p[(size_t) i].second) / dx : 0.0f;
        }
        m[0] = d[0];
        m[n - 1] = d[n - 2];
        for (int i = 1; i < n - 1; ++i)
            m[(size_t) i] = d[(size_t) i - 1] * d[(size_t) i] <= 0.0f ? 0.0f : 0.5f * (d[(size_t) i - 1] + d[(size_t) i]);
        for (int i = 0; i < n - 1; ++i)
        {
            if (d[(size_t) i] == 0.0f)
            {
                m[(size_t) i] = 0.0f;
                m[(size_t) i + 1] = 0.0f;
                continue;
            }
            const float a = m[(size_t) i] / d[(size_t) i], b = m[(size_t) i + 1] / d[(size_t) i];
            const float h = a * a + b * b;
            if (h > 9.0f)
            {
                const float t = 3.0f / std::sqrt (h);
                m[(size_t) i] = t * a * d[(size_t) i];
                m[(size_t) i + 1] = t * b * d[(size_t) i];
            }
        }

        int i = 0;
        while (i < n - 2 && x > p[(size_t) i + 1].first)
            ++i;
        const float x0 = p[(size_t) i].first, x1 = p[(size_t) i + 1].first, dx = x1 - x0;
        if (dx <= 1.0e-6f)
            return p[(size_t) i + 1].second;
        const float t = (x - x0) / dx, t2 = t * t, t3 = t2 * t;
        return (2.0f * t3 - 3.0f * t2 + 1.0f) * p[(size_t) i].second + (t3 - 2.0f * t2 + t) * dx * m[(size_t) i]
             + (-2.0f * t3 + 3.0f * t2) * p[(size_t) i + 1].second + (t3 - t2) * dx * m[(size_t) i + 1];
    }

    void SmoothModule::prepare (double sampleRate, int)
    {
        fs = sampleRate;
        fftSize = fftSizeFor (fs);
        fftOrder = 0;
        while ((1 << fftOrder) < fftSize)
            ++fftOrder;
        hop = fftSize / 4;
        numBins = fftSize / 2 + 1;
        fft = std::make_unique<juce::dsp::FFT> (fftOrder);

        window.assign ((size_t) fftSize, 0.0f);
        for (int j = 0; j < fftSize; ++j)
            window[(size_t) j] = (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * j / fftSize));   // periodic Hann

        for (int c = 0; c < maxChannels; ++c)
        {
            inRing[(size_t) c].assign ((size_t) fftSize, 0.0f);
            outFifo[(size_t) c].assign ((size_t) hop, 0.0f);
            outAccum[(size_t) c].assign ((size_t) fftSize, 0.0f);
            spec[(size_t) c].assign ((size_t) fftSize * 2, 0.0f);
        }
        for (auto* v : { &detPower, &levelDb, &envelope, &work, &work2, &reduction, &gainDb, &rangeAndWeight, &weightDb, &cutScale })
            v->assign ((size_t) numBins, 0.0f);
        for (auto* v : { &detRadius, &envRadius, &gainRadius, &maxRadius, &binBand })
            v->assign ((size_t) numBins, 0);
        prefix.assign ((size_t) numBins + 1, 0.0);

        const float binHz = (float) (fs / fftSize);
        for (int k = 0; k < numBins; ++k)
        {
            const float hz = (float) k * binHz;
            envRadius[(size_t) k] = radiusFor (hz, binHz, envelopeOctaves, 3.0f);
            gainRadius[(size_t) k] = radiusFor (hz, binHz, gainOctaves, 1.0f);
            maxRadius[(size_t) k] = (int) std::lround (std::max (peakHoldHz, hz * (std::pow (2.0f, 1.0f / 12.0f) - 1.0f)) / binHz);
            int band = -1;
            if (hz > bandLowestHz * 0.917f && hz < bandHighestHz * 1.09f)
                band = juce::jlimit (0, numBands - 1, (int) std::lround ((numBands - 1) * std::log (hz / bandLowestHz) / std::log (bandHighestHz / bandLowestHz)));
            binBand[(size_t) k] = band;
        }
        frameOut.numBins = numBins;
        frameOut.binHz = binHz;

        const double frameRate = fs / hop;
        detReleaseCoeff = timeConstantCoeff (detectReleaseMs, frameRate);

        for (auto* s : { &enabled, &outputGain, &deltaMix })
        {
            s->reset (sampleRate, 0.03);
            s->setCurrentAndTargetValue (s->getTargetValue());
        }
        appliedMode = -1;   // radii and weights are recomputed for the new rate
        appliedLow = appliedHigh = -1.0f;
        primeStep = (float) (1.0 / (0.001 * primeFadeMs * fs));
        setSettings (settings);
        clearState();
        primeRemaining = 0;   // a fresh start: the zeros in the delay line are real silence
        primeGain = 1.0f;
        snapNext = true;
    }

    void SmoothModule::reset()
    {
        // Mid-stream (e.g. the instance joins the chain): the delay line holds no valid audio
        // yet - stay silent until it is filled, then fade in (no click after the gap)
        clearState();
        primeRemaining = fftSize;
        primeGain = 0.0f;
    }

    void SmoothModule::clearState() noexcept
    {
        for (int c = 0; c < maxChannels; ++c)
        {
            std::fill (inRing[(size_t) c].begin(), inRing[(size_t) c].end(), 0.0f);
            std::fill (outFifo[(size_t) c].begin(), outFifo[(size_t) c].end(), 0.0f);
            std::fill (outAccum[(size_t) c].begin(), outAccum[(size_t) c].end(), 0.0f);
        }
        std::fill (detPower.begin(), detPower.end(), 0.0f);
        std::fill (gainDb.begin(), gainDb.end(), 0.0f);
        for (auto& c : bandCutDb)
            c.store (0.0f, std::memory_order_relaxed);
        ringPos = 0;
        hopCounter = 0;
    }

    void SmoothModule::updateWeights() noexcept
    {
        const bool weightsChanged = appliedWeightFreq != settings.weightFreq || appliedWeightGain != settings.weightGain;
        const bool rangeChanged = appliedLow != settings.lowHz || appliedHigh != settings.highHz;
        const bool modeChanged = appliedMode != settings.mode;
        if (! (weightsChanged || rangeChanged || modeChanged) || numBins <= 0 || detRadius.size() != (size_t) numBins)
            return;
        appliedWeightFreq = settings.weightFreq;
        appliedWeightGain = settings.weightGain;
        appliedLow = settings.lowHz;
        appliedHigh = settings.highHz;
        appliedMode = settings.mode;

        const float binHz = (float) (fs / fftSize);
        // Soft edges of the frequency range (1/4 octave fade on each side, as in 0.3-0.5)
        const float lo = juce::jlimit (20.0f, 20000.0f, std::min (settings.lowHz, settings.highHz));
        const float hi = juce::jlimit (20.0f, 20000.0f, std::max (settings.lowHz, settings.highHz));
        const bool flat = std::all_of (settings.weightGain.begin(), settings.weightGain.end(), [] (float g) { return g == 0.0f; });
        for (int k = 0; k < numBins; ++k)
        {
            const float hz = std::max (1.0f, (float) k * binHz);
            const float below = std::log2 (hz / lo) * 4.0f + 1.0f;
            const float above = std::log2 (hi / hz) * 4.0f + 1.0f;
            rangeAndWeight[(size_t) k] = juce::jlimit (0.0f, 1.0f, std::min (below, above));
            weightDb[(size_t) k] = flat ? 0.0f : weightingDbAt (settings, hz);
            cutScale[(size_t) k] = std::exp2 (weightDb[(size_t) k] / 12.0f);   // +12 dB: twice the maximum cut
            detRadius[(size_t) k] = radiusFor (hz, binHz, settings.mode == 1 ? 1.0f / 12.0f : 1.0f / 6.0f, 1.0f);
        }
    }

    void SmoothModule::setSettings (const SmoothSettings& s) noexcept
    {
        settings = s;
        enabled.setTargetValue (s.enabled ? 1.0f : 0.0f);
        outputGain.setTargetValue (dbToGain (juce::jlimit (-24.0f, 24.0f, s.outputDb)));
        deltaMix.setTargetValue (s.delta ? 1.0f : 0.0f);
        if (snapNext)
        {
            snapNext = false;
            for (auto* v : { &enabled, &outputGain, &deltaMix })
                v->setCurrentAndTargetValue (v->getTargetValue());
        }

        hard = s.mode == 1;
        const float a = juce::jlimit (0.0f, 1.0f, s.amount);
        depth = 0.145f * std::sqrt (a) * (hard ? 1.3f : 1.0f);
        maxCut = hard ? 6.0f + 18.0f * a : 3.0f + 9.0f * a;
        threshold = 3.0f - 2.5f * juce::jlimit (0.0f, 1.0f, s.sensitivity);

        // Per-bin gain ballistics run at the frame rate
        const double frameRate = fs / hop;
        const float timeFactor = hard ? 0.6f : 1.0f;
        attackCoeff = timeConstantCoeff (juce::jlimit (0.5f, 100.0f, s.attackMs) * timeFactor, frameRate);
        releaseCoeff = timeConstantCoeff (juce::jlimit (5.0f, 1000.0f, s.releaseMs) * timeFactor, frameRate);

        updateWeights();
    }

    float SmoothModule::getMaxCutDb() const noexcept
    {
        float m = 0.0f;
        for (auto& c : bandCutDb)
            m = std::max (m, c.load (std::memory_order_relaxed));
        return m;
    }

    void SmoothModule::processFrame (int numChannels) noexcept
    {
        const int N = fftSize, nb = numBins;

        // 1) Analysis (frame = the last N input samples, oldest first)
        for (int c = 0; c < numChannels; ++c)
        {
            auto* d = spec[(size_t) c].data();
            const auto* ring = inRing[(size_t) c].data();
            for (int j = 0; j < N; ++j)
                d[j] = window[(size_t) j] * ring[(ringPos + j) & (N - 1)];
            std::fill (d + N, d + 2 * N, 0.0f);
            fft->performRealOnlyForwardTransform (d, true);
        }

        // 2) Stereo-linked power, normalised so a full-scale sine reads 0 dB
        const float norm = 16.0f / ((float) N * (float) N) / (float) numChannels;
        for (int k = 0; k < nb; ++k)
        {
            float p = 0.0f;
            for (int c = 0; c < numChannels; ++c)
            {
                const float re = spec[(size_t) c][(size_t) (2 * k)], im = spec[(size_t) c][(size_t) (2 * k + 1)];
                p += re * re + im * im;
            }
            work[(size_t) k] = p * norm;
        }

        // 3) Detection spectrum: the harmonic-peak contour (running max over about one
        //    harmonic spacing, so the ripple between the partials of a voice is not taken
        //    for resonances), a little frequency smoothing, instant attack, short hold
        for (int k = 0; k < nb; ++k)
        {
            const int r = maxRadius[(size_t) k];
            float m = 0.0f;
            for (int j = std::max (0, k - r), e = std::min (nb - 1, k + r); j <= e; ++j)
                m = std::max (m, work[(size_t) j]);
            envelope[(size_t) k] = m;   // (envelope is free scratch here)
        }
        boxSmooth (envelope.data(), work2.data(), detRadius.data(), nb, prefix.data());
        boxSmooth (work2.data(), work.data(), detRadius.data(), nb, prefix.data());
        float loudest = -300.0f;
        for (int k = 0; k < nb; ++k)
        {
            const float p = work[(size_t) k];
            auto& d = detPower[(size_t) k];
            d = p >= d ? p : p + detReleaseCoeff * (d - p);
            d = std::max (d, 1.0e-20f);
            levelDb[(size_t) k] = 10.0f * std::log10 (d);
            loudest = std::max (loudest, levelDb[(size_t) k]);
        }

        // 4) Robust spectral envelope (dB): smooth, clip the peaks, smooth again
        boxSmooth (levelDb.data(), work2.data(), envRadius.data(), nb, prefix.data());
        boxSmooth (work2.data(), envelope.data(), envRadius.data(), nb, prefix.data());
        for (int k = 0; k < nb; ++k)
            work[(size_t) k] = std::min (levelDb[(size_t) k], envelope[(size_t) k] + envelopeClipDb);
        boxSmooth (work.data(), work2.data(), envRadius.data(), nb, prefix.data());
        boxSmooth (work2.data(), envelope.data(), envRadius.data(), nb, prefix.data());

        // 5) Reduction target per bin from the excess over the envelope
        const float floorDb = std::max (absoluteFloorDb, loudest - floorBelowPeakDb);
        for (int k = 0; k < nb; ++k)
        {
            float r = 0.0f;
            const float L = levelDb[(size_t) k];
            if (depth > 0.0f && L > floorDb && rangeAndWeight[(size_t) k] > 0.0f)
            {
                const float ex = L - envelope[(size_t) k] - threshold + weightDb[(size_t) k];
                const float soft = ex <= 0.0f ? 0.0f : ex < kneeDb ? ex * ex / (2.0f * kneeDb) : ex - 0.5f * kneeDb;
                // progressive: strong resonances are cut proportionally deeper
                r = std::min (soft * depth * (1.0f + soft / progressiveDb), maxCut * cutScale[(size_t) k]) * rangeAndWeight[(size_t) k];
            }
            work[(size_t) k] = r;
        }
        boxSmooth (work.data(), work2.data(), gainRadius.data(), nb, prefix.data());
        boxSmooth (work2.data(), reduction.data(), gainRadius.data(), nb, prefix.data());

        // 6) Per-bin attack / release (dB)
        bool any = false;
        std::array<float, numBands> bandMax {};
        for (int k = 0; k < nb; ++k)
        {
            const float r = reduction[(size_t) k];
            auto& g = gainDb[(size_t) k];
            g = r + (r > g ? attackCoeff : releaseCoeff) * (g - r);
            if (g < 1.0e-5f)
                g = 0.0f;
            any = any || g > 0.0f;
            frameOut.inputDb[(size_t) k] = levelDb[(size_t) k];
            frameOut.reductionDb[(size_t) k] = g;
            if (const int b = binBand[(size_t) k]; b >= 0)
                bandMax[(size_t) b] = std::max (bandMax[(size_t) b], g);
        }
        for (int b = 0; b < numBands; ++b)
            bandCutDb[(size_t) b].store (bandMax[(size_t) b], std::memory_order_relaxed);
        spectrum.store (frameOut);

        // 7) Synthesis of the removed part X * (1 - g), weighted overlap-add
        //    (Hann analysis x Hann synthesis at 75 % overlap sums to 1.5)
        const float olaScale = 1.0f / 1.5f;
        for (int c = 0; c < numChannels; ++c)
        {
            auto& acc = outAccum[(size_t) c];
            if (any)
            {
                auto* d = spec[(size_t) c].data();
                for (int k = 0; k < nb; ++k)
                {
                    const float m = 1.0f - std::exp (-0.11512925f * gainDb[(size_t) k]);   // 1 - 10^(-g/20)
                    d[2 * k] *= m;
                    d[2 * k + 1] *= m;
                }
                fft->performRealOnlyInverseTransform (d);
                for (int j = 0; j < N; ++j)
                    acc[(size_t) j] += d[j] * window[(size_t) j] * olaScale;
            }
            auto& fifo = outFifo[(size_t) c];
            std::copy (acc.begin(), acc.begin() + hop, fifo.begin());
            std::copy (acc.begin() + hop, acc.end(), acc.begin());
            std::fill (acc.end() - hop, acc.end(), 0.0f);
        }
    }

    void SmoothModule::applyPrime (float* const* ch, int numChannels, int i) noexcept
    {
        if (primeRemaining > 0)
        {
            --primeRemaining;
            for (int c = 0; c < numChannels; ++c)
                ch[c][i] = 0.0f;
            return;
        }
        primeGain = std::min (1.0f, primeGain + primeStep);
        for (int c = 0; c < numChannels; ++c)
            ch[c][i] *= primeGain;
    }

    void SmoothModule::process (float* const* ch, int numChannels, int n) noexcept
    {
        numChannels = juce::jlimit (0, maxChannels, numChannels);
        if (numChannels == 0 || fft == nullptr)
            return;
        const int N = fftSize;
        const bool active = enabled.getTargetValue() > 0.0f || enabled.isSmoothing();

        if (! active)
        {
            // Off: a pure delay by the latency (the reported latency never changes)
            if (wasActive)
            {
                for (int c = 0; c < maxChannels; ++c)
                {
                    std::fill (outFifo[(size_t) c].begin(), outFifo[(size_t) c].end(), 0.0f);
                    std::fill (outAccum[(size_t) c].begin(), outAccum[(size_t) c].end(), 0.0f);
                }
                std::fill (gainDb.begin(), gainDb.end(), 0.0f);
                std::fill (detPower.begin(), detPower.end(), 0.0f);
                for (auto& b : bandCutDb)
                    b.store (0.0f, std::memory_order_relaxed);
            }
            wasActive = false;
            for (int i = 0; i < n; ++i)
            {
                for (int c = 0; c < numChannels; ++c)
                {
                    auto& slot = inRing[(size_t) c][(size_t) ringPos];
                    const float x = ch[c][i];
                    ch[c][i] = slot;
                    slot = x;
                }
                if (primeGain < 1.0f)
                    applyPrime (ch, numChannels, i);
                ringPos = (ringPos + 1) & (N - 1);
                hopCounter = (hopCounter + 1) % hop;
            }
            outputGain.skip (n);
            deltaMix.skip (n);
            return;
        }
        wasActive = true;

        for (int i = 0; i < n; ++i)
        {
            const float on = enabled.getNextValue();
            const float out = outputGain.getNextValue();
            const float delta = deltaMix.getNextValue();
            for (int c = 0; c < numChannels; ++c)
            {
                auto& slot = inRing[(size_t) c][(size_t) ringPos];
                const float dry = slot;                                   // input, delayed by the latency
                const float removed = outFifo[(size_t) c][(size_t) hopCounter];
                slot = ch[c][i];
                const float wet = dry - removed;
                // Delta: listen to exactly what is removed
                const float processed = (wet + delta * (dry - 2.0f * wet)) * out;
                ch[c][i] = dry + on * (processed - dry);
            }
            if (primeGain < 1.0f)
                applyPrime (ch, numChannels, i);
            ringPos = (ringPos + 1) & (N - 1);
            if (++hopCounter >= hop)
            {
                hopCounter = 0;
                processFrame (numChannels);
            }
        }
    }
}
