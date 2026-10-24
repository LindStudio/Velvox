#include "FormantCorrector.h"

#include <cmath>

namespace velvox::dsp
{
    namespace
    {
        int nextPow2 (int n)
        {
            int p = 1;
            while (p < n) p <<= 1;
            return p;
        }

        constexpr float maxCorrectionLog = 2.7631f;   // ln (10^(24/20)): +-24 dB
    }

    FormantCorrector::FormantCorrector() = default;
    FormantCorrector::~FormantCorrector() = default;

    void FormantCorrector::prepare (double sampleRate, int analysisDelay, int maxBlockSize)
    {
        juce::ignoreUnused (maxBlockSize);
        fs = sampleRate;
        delay = std::max (1, analysisDelay);

        // ~21 ms analysis frame, but never reaching past the newest input sample
        frameSize = nextPow2 ((int) std::lround (0.0213 * fs));
        while (frameSize / 2 > delay && frameSize > 256)
            frameSize /= 2;
        order10 = 0;
        while ((1 << order10) < frameSize) ++order10;
        fft = std::make_unique<juce::dsp::FFT> (order10);
        hop = std::max (1, (int) std::lround (0.005 * fs));

        const int histLen = nextPow2 (delay + frameSize + 16);
        history.assign ((size_t) histLen, 0.0f);
        histMask = histLen - 1;

        window.resize ((size_t) frameSize);
        for (int i = 0; i < frameSize; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) frameSize);

        const int bins = frameSize / 2 + 1;
        work.assign ((size_t) frameSize * 2, 0.0f);
        for (auto* v : { &logEnv, &logY, &logT, &powerY, &powerT })
            v->assign ((size_t) bins, 0.0f);
        autoc.assign ((size_t) LatticePair::order + 1, 0.0f);
        reset();
    }

    void FormantCorrector::reset() noexcept
    {
        std::fill (history.begin(), history.end(), 0.0f);
        histPos = hopCounter = stepsLeft = 0;
        kyCur.fill (0.0f); ktCur.fill (0.0f);
        kyStep.fill (0.0f); ktStep.fill (0.0f);
        gainCur = 1.0f;
        gainStep = 0.0f;
        bypassed = true;
        lattice.reset();
    }

    void FormantCorrector::setWarp (float newRho, float newTarget) noexcept
    {
        rho = juce::jlimit (0.125f, 8.0f, newRho);
        target = juce::jlimit (0.125f, 8.0f, newTarget);
        identity = std::abs (rho - target) < 1.0e-6f * rho;
        if (! identity)
            bypassed = false;
    }

    void FormantCorrector::envelopeFrom (const std::vector<float>& env, float warp, std::vector<float>& out) const noexcept
    {
        const int last = (int) env.size() - 1;
        for (int k = 0; k <= last; ++k)
        {
            const float pos = juce::jlimit (0.0f, (float) last, (float) k / warp);
            const int i0 = std::min ((int) pos, last - 1);
            const float fr = pos - (float) i0;
            out[(size_t) k] = env[(size_t) i0] + fr * (env[(size_t) i0 + 1] - env[(size_t) i0]);
        }
    }

    float FormantCorrector::levinson (const std::vector<float>& power, float* k) noexcept
    {
        // autocorrelation = inverse FFT of the (real, even) power spectrum
        const int N = frameSize;
        std::fill (work.begin(), work.end(), 0.0f);
        for (int i = 0; i <= N / 2; ++i)
        {
            work[(size_t) i] = power[(size_t) i];
            if (i > 0 && i < N / 2)
                work[(size_t) (N - i)] = power[(size_t) i];
        }
        fft->performRealOnlyForwardTransform (work.data());
        for (int m = 0; m <= LatticePair::order; ++m)
            autoc[(size_t) m] = work[(size_t) (2 * m)] / (float) N;

        auto& a = lev;
        auto& t = levTmp;
        a.fill (0.0);
        a[0] = 1.0;
        double e = (double) autoc[0] * (1.0 + 1.0e-9) + 1.0e-30;
        for (int i = 1; i <= LatticePair::order; ++i)
        {
            double acc = autoc[(size_t) i];
            for (int j = 1; j < i; ++j)
                acc += a[(size_t) j] * autoc[(size_t) (i - j)];
            double ki = juce::jlimit (-0.999, 0.999, -acc / e);
            t = a;
            for (int j = 1; j < i; ++j)
                a[(size_t) j] = t[(size_t) j] + ki * t[(size_t) (i - j)];
            a[(size_t) i] = ki;
            e *= (1.0 - ki * ki);
            k[i - 1] = (float) ki;
        }
        return (float) e;
    }

    void FormantCorrector::analyse (float period) noexcept
    {
        const int N = frameSize, half = N / 2;
        std::array<float, LatticePair::order> ky {}, kt {};
        float gain = 1.0f;

        // Frame centred `delay` samples behind the newest input
        const int start = histPos - delay - half + 1;
        double energy = 0.0;
        std::fill (work.begin(), work.end(), 0.0f);
        for (int i = 0; i < N; ++i)
        {
            const float v = history[(size_t) ((start + i) & histMask)] * window[(size_t) i];
            work[(size_t) i] = v;
            energy += (double) v * v;
        }

        if (! identity && energy > 1.0e-10)
        {
            fft->performRealOnlyForwardTransform (work.data());
            float maxP = 0.0f;
            for (int k = 0; k <= half; ++k)
            {
                const float re = work[(size_t) (2 * k)], im = work[(size_t) (2 * k + 1)];
                powerY[(size_t) k] = re * re + im * im;
                maxP = std::max (maxP, powerY[(size_t) k]);
            }
            const float floorP = maxP * 1.0e-7f + 1.0e-30f;

            // Real cepstrum of the log magnitude (even sequence: forward FFT / N)
            std::fill (work.begin(), work.end(), 0.0f);
            for (int k = 0; k <= half; ++k)
            {
                const float l = 0.5f * std::log (powerY[(size_t) k] + floorP);
                work[(size_t) k] = l;
                if (k > 0 && k < half)
                    work[(size_t) (N - k)] = l;
            }
            fft->performRealOnlyForwardTransform (work.data());

            // Lifter below the pitch period (removes the harmonic ripple), cosine taper
            const float qMax = (float) (0.00625 * fs), qMin = (float) (0.0005 * fs);
            const float qc = std::min ((float) half - 1.0f, period > 0.0f ? juce::jlimit (qMin, qMax, 0.8f * period) : (float) (0.0015 * fs));
            const float flat = 0.6f * qc;
            std::vector<float>& cep = logT;   // scratch (half + 1 values)
            for (int n = 0; n <= half; ++n)
            {
                float w = 0.0f;
                if (n <= flat) w = 1.0f;
                else if (n < qc) w = 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * ((float) n - flat) / (qc - flat));
                cep[(size_t) n] = work[(size_t) (2 * n)] / (float) N * w;
            }
            std::fill (work.begin(), work.end(), 0.0f);
            for (int n = 0; n <= half; ++n)
            {
                work[(size_t) n] = cep[(size_t) n];
                if (n > 0 && n < half)
                    work[(size_t) (N - n)] = cep[(size_t) n];
            }
            fft->performRealOnlyForwardTransform (work.data());
            for (int k = 0; k <= half; ++k)
                logEnv[(size_t) k] = work[(size_t) (2 * k)];

            // Envelope the shifted signal has, and the one it should have
            envelopeFrom (logEnv, rho, logY);
            envelopeFrom (logEnv, target, logT);
            float top = -1.0e30f;
            for (int k = 0; k <= half; ++k)
            {
                logT[(size_t) k] = juce::jlimit (logY[(size_t) k] - maxCorrectionLog, logY[(size_t) k] + maxCorrectionLog, logT[(size_t) k]);
                top = std::max (top, std::max (logY[(size_t) k], logT[(size_t) k]));
            }
            float maxY = 0.0f, maxT = 0.0f;
            for (int k = 0; k <= half; ++k)
            {
                powerY[(size_t) k] = std::exp (2.0f * (logY[(size_t) k] - top));
                powerT[(size_t) k] = std::exp (2.0f * (logT[(size_t) k] - top));
                maxY = std::max (maxY, powerY[(size_t) k]);
                maxT = std::max (maxT, powerT[(size_t) k]);
            }
            const float white = 1.0e-6f * std::max (maxY, maxT);
            for (int k = 0; k <= half; ++k)
            {
                powerY[(size_t) k] += white;
                powerT[(size_t) k] += white;
            }
            const float ey = levinson (powerY, ky.data());
            const float et = levinson (powerT, kt.data());
            gain = juce::jlimit (0.06f, 16.0f, std::sqrt (et / std::max (ey, 1.0e-30f)));
        }
        else if (! identity)
        {
            // silence: keep the last correction (nothing to hear), avoid jumping on the next onset
            ky = kyCur; kt = ktCur;
            for (int i = 0; i < LatticePair::order; ++i) { ky[(size_t) i] += kyStep[(size_t) i] * (float) stepsLeft; kt[(size_t) i] += ktStep[(size_t) i] * (float) stepsLeft; }
            gain = gainCur + gainStep * (float) stepsLeft;
        }

        const float inv = 1.0f / (float) hop;
        for (int i = 0; i < LatticePair::order; ++i)
        {
            kyStep[(size_t) i] = (ky[(size_t) i] - kyCur[(size_t) i]) * inv;
            ktStep[(size_t) i] = (kt[(size_t) i] - ktCur[(size_t) i]) * inv;
        }
        gainStep = (gain - gainCur) * inv;
        stepsLeft = hop;
    }

    void FormantCorrector::process (const float* monoIn, float* const* io, int numChannels, int n, const float* period) noexcept
    {
        numChannels = std::min (numChannels, 2);
        for (int i = 0; i < n; ++i)
        {
            float x = monoIn[i];
            if (! std::isfinite (x)) x = 0.0f;
            histPos = (histPos + 1) & histMask;
            history[(size_t) histPos] = x;

            if (++hopCounter >= hop)
            {
                hopCounter = 0;
                if (! bypassed)
                    analyse (period[i]);
            }

            if (bypassed)
                continue;

            if (stepsLeft > 0)
            {
                for (int j = 0; j < LatticePair::order; ++j)
                {
                    kyCur[(size_t) j] += kyStep[(size_t) j];
                    ktCur[(size_t) j] += ktStep[(size_t) j];
                }
                gainCur += gainStep;
                if (--stepsLeft == 0 && identity)
                {
                    // back at the identity: stop processing (bit exact again)
                    kyCur.fill (0.0f); ktCur.fill (0.0f);
                    gainCur = 1.0f;
                    bypassed = true;
                    lattice.reset();
                }
            }

            if (bypassed)
                continue;

            for (int ch = 0; ch < numChannels; ++ch)
                io[ch][i] = gainCur * lattice.processAllPole (lattice.processAllZero (io[ch][i], kyCur.data(), ch), ktCur.data(), ch);
        }

        if (! lattice.isFinite())
        {
            lattice.reset();
            kyCur.fill (0.0f); ktCur.fill (0.0f);
            stepsLeft = 0;
            gainCur = 1.0f;
        }
    }
}
