#include "BreathModule.h"

namespace velvox::dsp
{
    namespace
    {
        constexpr float flatnessMin = 0.25f;
        constexpr float voicingMax = 0.4f;
        constexpr float belowPhraseMin = 12.0f, belowPhraseMax = 45.0f;
        constexpr double centroidLo = 500.0, centroidHi = 5000.0;
        constexpr double flatLo = 500.0, flatHi = 6000.0;
        constexpr double pitchLoHz = 66.0, pitchHiHz = 400.0;
        constexpr float holdTime = 2.0f, phraseDecayDbPerSecond = 20.0f;
        constexpr float windowCorrectionMin = 0.3f;   // further out the window correction blows noise up into "voiced"
    }

    float BreathModule::breathGainDb (float amountPercent) noexcept
    {
        const float a = juce::jlimit (0.0f, 1.0f, amountPercent / 100.0f);
        return a <= 0.5f ? -24.0f * a : -(12.0f + 36.0f * (a - 0.5f));
    }

    void BreathModule::prepare (double sampleRate, int)
    {
        fs = sampleRate;
        latency = latencyFor (fs);
        const bool high = fs > 50000.0;
        hopSize = high ? 1024 : 512;                               // ~10 ms steps
        analysisSize = high ? 4096 : 2048;                         // ~43 ms: resolves harmonics, sees periods down to 66 Hz
        fftOrder = (high ? 12 : 11) + 1;                           // zero-padded x2 for the autocorrelation
        minRunFrames = (int) std::ceil (0.06 * fs / hopSize);      // 60 ms
        fft = std::make_unique<juce::dsp::FFT> (fftOrder);
        history.assign ((size_t) analysisSize, 0.0f);
        window.resize ((size_t) analysisSize);
        for (int i = 0; i < analysisSize; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) (analysisSize - 1));
        // the window's own normalised autocorrelation (divided out of the signal's)
        windowAc.assign ((size_t) analysisSize, 0.0f);
        double w0 = 0.0;
        for (auto w : window)
            w0 += (double) w * w;
        const int maxLag = (int) (fs / pitchLoHz) + 1;
        for (int lag = 0; lag < std::min (analysisSize, maxLag + 1); ++lag)
        {
            double acc = 0.0;
            for (int i = 0; i + lag < analysisSize; ++i)
                acc += (double) window[(size_t) i] * window[(size_t) (i + lag)];
            windowAc[(size_t) lag] = (float) (acc / w0);
        }
        fftData.assign ((size_t) (2 << fftOrder), 0.0f);
        for (auto& d : delayLine)
            d.assign ((size_t) std::max (1, latency), 0.0f);
        marks.assign ((size_t) std::max (1, latency), 0.0f);
        attackCoeff = (float) std::exp (-1.0 / (0.005 * fs));
        releaseCoeff = (float) std::exp (-1.0 / (0.030 * fs));
        reset();
    }

    void BreathModule::reset()
    {
        for (auto& d : delayLine)
            std::fill (d.begin(), d.end(), 0.0f);
        std::fill (marks.begin(), marks.end(), 0.0f);
        std::fill (history.begin(), history.end(), 0.0f);
        delayPos = historyPos = hopFill = 0;
        run = misses = 0;
        phraseDb = -120.0f;
        holdSeconds = 0.0f;
        gain = 1.0f;
        reductionDb.store (0.0f);
    }

    bool BreathModule::analysisIsBreathLike() noexcept
    {
        // Level of the newest hop against the phrase (peak hold before it)
        double e = 0.0;
        for (int i = 1; i <= hopSize; ++i)
        {
            const float x = history[(size_t) ((historyPos - i + analysisSize) % analysisSize)];
            e += (double) x * x;
        }
        const float level = (float) (10.0 * std::log10 (e / hopSize + 1.0e-20));
        const float phrase = phraseDb;
        const float hopSeconds = (float) (hopSize / fs);
        if (level > phraseDb)
        {
            phraseDb = level;
            holdSeconds = holdTime;
        }
        else if ((holdSeconds -= hopSeconds) <= 0.0f)
            phraseDb -= phraseDecayDbPerSecond * hopSeconds;

        if (! (level < phrase - belowPhraseMin && level > phrase - belowPhraseMax))
            return false;

        // Spectrum of the last ~43 ms (Hann, zero-padded x2)
        const int n = 1 << fftOrder;
        std::fill (fftData.begin(), fftData.end(), 0.0f);
        for (int i = 0; i < analysisSize; ++i)
            fftData[(size_t) i] = history[(size_t) ((historyPos + i) % analysisSize)] * window[(size_t) i];
        fft->performRealOnlyForwardTransform (fftData.data(), true);
        const double binHz = fs / n;
        double logSum = 0.0, linSum = 0.0, cNum = 0.0, cDen = 0.0;
        int count = 0;
        for (int k = 0; k <= n / 2; ++k)
        {
            const double re = fftData[(size_t) (2 * k)], im = fftData[(size_t) (2 * k + 1)];
            const double p = re * re + im * im + 1.0e-24;
            const double f = k * binHz;
            if (f >= flatLo && f <= flatHi)
            {
                logSum += std::log (p);
                linSum += p;
                ++count;
            }
            if (f >= 300.0 && f <= 8000.0)
            {
                cNum += f * p;
                cDen += p;
            }
            fftData[(size_t) (2 * k)] = (float) p;   // the power spectrum, for the autocorrelation below
            fftData[(size_t) (2 * k + 1)] = 0.0f;
        }
        if (count == 0 || cDen <= 0.0)
            return false;
        const double flatness = std::exp (logSum / count) / (linSum / count);
        const double centroid = cNum / cDen;
        if (flatness < flatnessMin || centroid < centroidLo || centroid > centroidHi)
            return false;

        // Voicing: normalised autocorrelation (inverse FFT of the power spectrum) over 66 - 400 Hz,
        // corrected for the window's own autocorrelation
        fft->performRealOnlyInverseTransform (fftData.data());
        const float r0 = fftData[0];
        if (r0 <= 0.0f)
            return false;
        const int lagLo = (int) (fs / pitchHiHz), lagHi = std::min ((int) (fs / pitchLoHz), analysisSize - 1);
        float best = 0.0f;
        for (int lag = lagLo; lag <= lagHi; ++lag)
        {
            const float w = windowAc[(size_t) lag];
            if (w > windowCorrectionMin)
                best = std::max (best, fftData[(size_t) lag] / r0 / w);
        }
        return best < voicingMax;
    }

    void BreathModule::process (float* const* io, int numChannels, int numSamples) noexcept
    {
        const int chans = std::min (numChannels, 2);
        const float breathGain = juce::Decibels::decibelsToGain (breathGainDb (settings.amount));
        float minGain = 1.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            // Delayed output with its mark, then the new sample into the same slot
            const bool breath = marks[(size_t) delayPos] > 0.5f;
            const float target = settings.enabled && breath ? breathGain : 1.0f;
            gain = target + (gain - target) * (target < gain ? attackCoeff : releaseCoeff);
            minGain = std::min (minGain, gain);

            float mono = 0.0f;
            for (int ch = 0; ch < chans; ++ch)
            {
                auto& d = delayLine[(size_t) ch];
                const float x = io[ch][i];
                io[ch][i] = d[(size_t) delayPos] * gain;
                d[(size_t) delayPos] = x;
                mono += x;
            }
            marks[(size_t) delayPos] = 0.0f;
            delayPos = (delayPos + 1) % latency;

            history[(size_t) historyPos] = mono / (float) std::max (1, chans);
            historyPos = (historyPos + 1) % analysisSize;
            if (++hopFill == hopSize)
            {
                hopFill = 0;
                // A confirmed breath forgives up to 2 odd frames (noise can look tonal for a moment)
                if (analysisIsBreathLike())
                {
                    ++run;
                    misses = 0;
                }
                else if (run >= minRunFrames && misses < 2)
                {
                    ++run;
                    ++misses;
                }
                else
                    run = 0;

                if (run >= minRunFrames)
                {
                    // The analysis is centred half a window back: mark the run there - the whole run once it is
                    // confirmed, afterwards hop by hop. All of it is still inside the look-ahead.
                    const int centre = analysisSize / 2 - hopSize / 2;
                    const int count = run == minRunFrames ? run * hopSize : hopSize;
                    for (int k = centre; k < centre + count && k < latency; ++k)
                        marks[(size_t) ((delayPos - 1 - k + 2 * latency) % latency)] = 1.0f;
                }
            }
        }
        reductionDb.store (juce::Decibels::gainToDecibels (minGain, -60.0f), std::memory_order_relaxed);
    }
}
