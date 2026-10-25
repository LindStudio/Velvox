#include "FormantCorrector.h"

#include <cmath>
#include <cstdint>
#include <cstring>

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

        // Fast natural log / exp (rel. error ~1e-4; plenty for a spectral envelope)
        inline float fastLog (float x) noexcept
        {
            std::uint32_t bits;
            std::memcpy (&bits, &x, 4);
            const int e = (int) ((bits >> 23) & 0xff) - 127;
            bits = (bits & 0x007fffffu) | 0x3f800000u;
            float m;
            std::memcpy (&m, &bits, 4);   // [1, 2)
            const float t = m - 1.0f;
            const float p = t * (1.4425449f + t * (-0.7181452f + t * (0.4575485f + t * (-0.2779042f + t * (0.1217970f + t * -0.0258411f)))));
            return ((float) e + p) * 0.69314718f;
        }

        inline float fastExp (float x) noexcept
        {
            float y = x * 1.44269504f;
            y = juce::jlimit (-126.0f, 126.0f, y);
            const float fl = std::floor (y);
            const float f = y - fl;
            const float p = 1.0f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * (0.0096181f + f * 0.0013334f))));
            const std::uint32_t bits = (std::uint32_t) ((int) fl + 127) << 23;
            float s;
            std::memcpy (&s, &bits, 4);
            return p * s;
        }
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
        hop = std::max (1, (int) std::lround (0.010 * fs));   // 10 ms
        smoothing = (float) (1.0 - std::exp (-hop / (0.012 * fs)));

        const int histLen = nextPow2 (delay + frameSize + 16);
        history.assign ((size_t) histLen, 0.0f);
        histMask = histLen - 1;

        window.resize ((size_t) frameSize);
        // 4-term Blackman-Harris: sidelobes at -92 dB, so the envelope outside the partials is the
        // (deterministic) floor and does not wobble with the phase of a steady tone in the frame
        for (int i = 0; i < frameSize; ++i)
        {
            const double x = 2.0 * juce::MathConstants<double>::pi * i / frameSize;
            window[(size_t) i] = (float) (0.35875 - 0.48829 * std::cos (x) + 0.14128 * std::cos (2.0 * x) - 0.01168 * std::cos (3.0 * x));
        }

        const int bins = frameSize / 2 + 1;
        work.assign ((size_t) frameSize * 2, 0.0f);
        for (auto* v : { &logEnv, &logY, &logT, &powerY, &powerT })
            v->assign ((size_t) bins, 0.0f);
        autoc.assign ((size_t) LatticePair::order + 1, 0.0f);

        // autocorrelation of an even power spectrum = cosine sums (only order + 1 lags needed),
        // with the 1/N scaling and a Gaussian lag window (60 Hz bandwidth expansion: well-
        // conditioned fits, no near-unit-circle poles that would ring or wander between frames)
        acTable.assign ((size_t) (LatticePair::order + 1) * (size_t) bins, 0.0f);
        for (int m = 0; m <= LatticePair::order; ++m)
        {
            const double x = 2.0 * juce::MathConstants<double>::pi * 60.0 * m / fs;
            const double lag = std::exp (-0.5 * x * x) / frameSize;
            for (int k = 0; k < bins; ++k)
            {
                const double w = (k == 0 || k == bins - 1) ? 1.0 : 2.0;
                acTable[(size_t) m * (size_t) bins + (size_t) k]
                    = (float) (w * lag * std::cos (2.0 * juce::MathConstants<double>::pi * (double) k * m / frameSize));
            }
        }
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
        envValid = false;
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
        const int bins = frameSize / 2 + 1;
        for (int m = 0; m <= LatticePair::order; ++m)
        {
            const float* row = acTable.data() + (size_t) m * (size_t) bins;
            float acc = 0.0f;
            for (int k = 0; k < bins; ++k)
                acc += row[k] * power[(size_t) k];
            autoc[(size_t) m] = acc;
        }

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
            const float floorP = maxP * 1.0e-6f + 1.0e-30f;   // -60 dB

            // Log magnitude; for voiced frames a peak hold over +-f0/2 so the curve runs through
            // the harmonics and not through the (phase dependent) valleys between them
            for (int k = 0; k <= half; ++k)
                logY[(size_t) k] = 0.5f * fastLog (powerY[(size_t) k] + floorP);
            const int hold = period > 0.0f ? (int) std::floor (0.5 * N / period) : 0;
            if (hold >= 1)
            {
                for (int k = 0; k <= half; ++k)
                {
                    float m = -1.0e30f;
                    for (int j = std::max (0, k - hold); j <= std::min (half, k + hold); ++j)
                        m = std::max (m, logY[(size_t) j]);
                    logT[(size_t) k] = m;
                }
                std::swap (logY, logT);
            }

            // Real cepstrum of the log magnitude (even sequence: forward FFT / N)
            std::fill (work.begin(), work.end(), 0.0f);
            for (int k = 0; k <= half; ++k)
            {
                const float l = logY[(size_t) k];
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
            // ~12 ms smoothing over time: steady notes get a steady filter (no AM)
            const float a = envValid ? smoothing : 1.0f;
            for (int k = 0; k <= half; ++k)
                logEnv[(size_t) k] += a * (work[(size_t) (2 * k)] - logEnv[(size_t) k]);
            envValid = true;

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
                powerY[(size_t) k] = fastExp (2.0f * (logY[(size_t) k] - top));
                powerT[(size_t) k] = fastExp (2.0f * (logT[(size_t) k] - top));
                maxY = std::max (maxY, powerY[(size_t) k]);
                maxT = std::max (maxT, powerT[(size_t) k]);
            }
            const float white = 1.0e-5f * std::max (maxY, maxT);
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

            if (numChannels == 2)
            {
                float l = io[0][i], r = io[1][i];
                lattice.processStereo (l, r, kyCur.data(), ktCur.data());
                io[0][i] = gainCur * l;
                io[1][i] = gainCur * r;
            }
            else
                io[0][i] = gainCur * lattice.processAllPole (lattice.processAllZero (io[0][i], kyCur.data(), 0), ktCur.data(), 0);
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
