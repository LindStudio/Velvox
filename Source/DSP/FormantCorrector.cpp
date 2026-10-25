#include "FormantCorrector.h"

#if defined (_MSC_VER)
 #pragma warning (push, 0) // third-party header: keep our warning level clean
#endif
#include "signalsmith-linear/fft.h"
#if defined (_MSC_VER)
 #pragma warning (pop)
#endif

#include <cmath>
#include <cstdint>
#include <cstring>

#if defined (_M_X64) || defined (__x86_64__) || defined (__SSE2__)
 #include <emmintrin.h>
 #define VELVOX_FORMANT_SSE 1
#endif

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

        constexpr float ln10over20 = 0.11512925f;   // dB -> natural log of magnitude

        // Fast natural log (rel. error ~1e-4; plenty for a spectral envelope)
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

        inline float dot (const float* a, const float* b, int n) noexcept
        {
           #if VELVOX_FORMANT_SSE
            __m128 s0 = _mm_setzero_ps(), s1 = _mm_setzero_ps();
            int i = 0;
            for (; i + 8 <= n; i += 8)
            {
                s0 = _mm_add_ps (s0, _mm_mul_ps (_mm_loadu_ps (a + i), _mm_loadu_ps (b + i)));
                s1 = _mm_add_ps (s1, _mm_mul_ps (_mm_loadu_ps (a + i + 4), _mm_loadu_ps (b + i + 4)));
            }
            s0 = _mm_add_ps (s0, s1);
            alignas (16) float t[4];
            _mm_store_ps (t, s0);
            float s = (t[0] + t[1]) + (t[2] + t[3]);
            for (; i < n; ++i) s += a[i] * b[i];
            return s;
           #else
            float s0 = 0, s1 = 0, s2 = 0, s3 = 0;
            int i = 0;
            for (; i + 4 <= n; i += 4) { s0 += a[i] * b[i]; s1 += a[i + 1] * b[i + 1]; s2 += a[i + 2] * b[i + 2]; s3 += a[i + 3] * b[i + 3]; }
            for (; i < n; ++i) s0 += a[i] * b[i];
            return (s0 + s1) + (s2 + s3);
           #endif
        }
    }

    struct FormantCorrector::Fft
    {
        signalsmith::linear::RealFFT<float> fft;
    };

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
        rfft = std::make_unique<Fft>();
        rfft->fft.resize ((size_t) frameSize);
        hop = std::max (1, (int) std::lround (0.010 * fs));                     // 10 ms
        smoothing = (float) (1.0 - std::exp (-hop / (0.012 * fs)));            // ~12 ms
        taps = std::min (frameSize / 2, nextPow2 ((int) std::lround (0.0053 * fs)));   // 256 at 48 kHz
        fadeLen = std::max (1, (int) std::lround (0.0025 * fs));

        const int histLen = nextPow2 (delay + frameSize + 16);
        history.assign ((size_t) histLen, 0.0f);
        histMask = histLen - 1;

        // 4-term Blackman-Harris: sidelobes at -92 dB, so the envelope outside the partials is the
        // (deterministic) floor and does not wobble with the phase of a steady tone in the frame
        window.resize ((size_t) frameSize);
        for (int i = 0; i < frameSize; ++i)
        {
            const double x = 2.0 * juce::MathConstants<double>::pi * i / frameSize;
            window[(size_t) i] = (float) (0.35875 - 0.48829 * std::cos (x) + 0.14128 * std::cos (2.0 * x) - 0.01168 * std::cos (3.0 * x));
        }

        const int bins = frameSize / 2 + 1;
        work.assign ((size_t) frameSize * 2, 0.0f);
        for (auto* v : { &logEnv, &logY, &logT, &logG })
            v->assign ((size_t) bins, 0.0f);
        spec.assign ((size_t) frameSize / 2, {});

        // scaling conventions of the transform, measured once
        std::fill (work.begin(), work.end(), 0.0f);
        work[0] = 1.0f;
        rfft->fft.fft (work.data(), spec.data());
        fwdScale = 1.0f / spec[1].real();                 // delta -> flat spectrum of 1 for a plain DFT
        rfft->fft.ifft (spec.data(), work.data());
        roundTrip = work[0];

        for (auto& l : line)
            l.assign ((size_t) taps * 2, 0.0f);
        curTaps.assign ((size_t) taps, 0.0f);
        oldTaps.assign ((size_t) taps, 0.0f);
        nextTaps.assign ((size_t) taps, 0.0f);
        reset();
    }

    void FormantCorrector::reset() noexcept
    {
        std::fill (history.begin(), history.end(), 0.0f);
        for (auto& l : line)
            std::fill (l.begin(), l.end(), 0.0f);
        histPos = hopCounter = linePos = fadeLeft = 0;
        std::fill (curTaps.begin(), curTaps.end(), 0.0f);
        curTaps[(size_t) taps - 1] = 1.0f;   // reversed: the newest sample is last
        oldTaps = curTaps;
        curIdentity = true;
        bypassed = true;
        envValid = false;
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

    void FormantCorrector::analyse (float period) noexcept
    {
        const int N = frameSize, half = N / 2;

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

        if (identity)
        {
            startFilter (true);
            return;
        }
        if (energy <= 1.0e-10)
            return;   // silence: keep the current filter (nothing to hear, no jump at the next onset)

        rfft->fft.fft (work.data(), spec.data());
        float maxP = 0.0f;
        for (int k = 0; k <= half; ++k)
        {
            const auto v = k == 0 ? std::complex<float> (spec[0].real(), 0.0f)
                         : k == half ? std::complex<float> (spec[0].imag(), 0.0f) : spec[(size_t) k];
            logY[(size_t) k] = std::norm (v);
            maxP = std::max (maxP, logY[(size_t) k]);
        }
        const float floorP = maxP * 1.0e-6f + 1.0e-30f;   // -60 dB

        // Log magnitude; for voiced frames a peak hold over +-f0/2 so the curve runs through
        // the harmonics and not through the (phase dependent) valleys between them
        for (int k = 0; k <= half; ++k)
            logY[(size_t) k] = 0.5f * fastLog (logY[(size_t) k] + floorP);
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

        // Real cepstrum of the log magnitude, liftered below the pitch period (removes the
        // harmonic ripple, cosine taper), back to a smooth log envelope
        inverseEven (logY, work);
        const float qMax = (float) (0.003 * fs), qMin = (float) (0.0005 * fs);
        const float qc = std::min ((float) half - 1.0f, period > 0.0f ? juce::jlimit (qMin, qMax, 0.8f * period) : (float) (0.0015 * fs));
        const float flat = 0.6f * qc;
        for (int n = 0; n <= half; ++n)
        {
            float w = 0.0f;
            if (n <= flat) w = 1.0f;
            else if (n < qc) w = 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * ((float) n - flat) / (qc - flat));
            work[(size_t) n] *= w;
            if (n > 0 && n < half)
                work[(size_t) (N - n)] *= w;
        }
        rfft->fft.fft (work.data(), spec.data());
        for (int k = 0; k <= half; ++k)
            logT[(size_t) k] = (k == 0 ? spec[0].real() : k == half ? spec[0].imag() : spec[(size_t) k].real()) * fwdScale;

        // ~12 ms smoothing over time: steady notes get a steady filter (no AM)
        const float a = envValid ? smoothing : 1.0f;
        for (int k = 0; k <= half; ++k)
            logEnv[(size_t) k] += a * (logT[(size_t) k] - logEnv[(size_t) k]);
        envValid = true;

        // Correction = envelope it should have / envelope the shifted signal has
        envelopeFrom (logEnv, rho, logY);
        envelopeFrom (logEnv, target, logT);
        const float binHz = (float) (fs / N);
        const float contentTop = 0.95f * (float) (0.5 * fs) * std::min (1.0f, rho);   // y is empty above this
        for (int k = 0; k <= half; ++k)
        {
            const float f = (float) k * binHz;
            // boost limit: 36 dB in the voice band, 18 dB from 10 kHz, none where y has no content
            float boostDb = f <= 4000.0f ? 36.0f : (f >= 10000.0f ? 18.0f : 36.0f - 18.0f * (f - 4000.0f) / 6000.0f);
            if (f > contentTop) boostDb = 0.0f;
            const float g = logT[(size_t) k] - logY[(size_t) k];
            logG[(size_t) k] = juce::jlimit (-36.0f * ln10over20, boostDb * ln10over20, g);
        }
        designFilter();
        startFilter (false);
    }

    void FormantCorrector::designFilter() noexcept
    {
        // Minimum-phase filter with |H| = exp (logG): fold the (real, even) cepstrum of logG
        const int N = frameSize, half = N / 2;
        inverseEven (logG, work);
        for (int n = 1; n < half; ++n)
            work[(size_t) n] *= 2.0f;
        for (int n = half + 1; n < N; ++n)
            work[(size_t) n] = 0.0f;
        rfft->fft.fft (work.data(), spec.data());
        // H = exp (C), packed like the transform (bin 0 = DC, Nyquist)
        constexpr float pi = juce::MathConstants<float>::pi, twoPi = juce::MathConstants<float>::twoPi;
        for (int k = 1; k < half; ++k)
        {
            const auto c = spec[(size_t) k] * fwdScale;
            const float m = std::exp (juce::jlimit (-20.0f, 20.0f, c.real()));
            float ph = c.imag();
            ph -= twoPi * std::floor ((ph + pi) / twoPi);   // wrap to [-pi, pi)
            spec[(size_t) k] = { m * juce::dsp::FastMathApproximations::cos (ph), m * juce::dsp::FastMathApproximations::sin (ph) };
        }
        spec[0] = { std::exp (juce::jlimit (-20.0f, 20.0f, spec[0].real() * fwdScale)),
                    std::exp (juce::jlimit (-20.0f, 20.0f, spec[0].imag() * fwdScale)) };
        rfft->fft.ifft (spec.data(), work.data());
        const float inv = 1.0f / (fwdScale * roundTrip);

        const int taper = taps / 8;
        for (int j = 0; j < taps; ++j)
        {
            float h = work[(size_t) j] * inv;
            if (j >= taps - taper)
                h *= 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * (float) (j - (taps - taper) + 1) / (float) (taper + 1));
            nextTaps[(size_t) (taps - 1 - j)] = std::isfinite (h) ? h : (j == 0 ? 1.0f : 0.0f);
        }
    }

    void FormantCorrector::inverseEven (const std::vector<float>& v, std::vector<float>& time) noexcept
    {
        const int half = frameSize / 2;
        spec[0] = { v[0], v[(size_t) half] };
        for (int k = 1; k < half; ++k)
            spec[(size_t) k] = { v[(size_t) k], 0.0f };
        rfft->fft.ifft (spec.data(), time.data());
        const float inv = 1.0f / (fwdScale * roundTrip);
        for (int n = 0; n < frameSize; ++n)
            time[(size_t) n] *= inv;
    }

    void FormantCorrector::startFilter (bool identityFilter) noexcept
    {
        if (identityFilter)
        {
            std::fill (nextTaps.begin(), nextTaps.end(), 0.0f);
            nextTaps[(size_t) taps - 1] = 1.0f;
        }
        oldTaps.swap (curTaps);
        curTaps.swap (nextTaps);
        curIdentity = identityFilter;
        fadeLeft = fadeLen;
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

            // the filter's input history is kept even while bypassed (seamless start)
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float v = io[ch][i];
                if (! std::isfinite (v)) v = 0.0f;
                auto& l = line[(size_t) ch];
                l[(size_t) linePos] = v;
                l[(size_t) (linePos + taps)] = v;
            }
            linePos = linePos + 1 == taps ? 0 : linePos + 1;

            if (++hopCounter >= hop)
            {
                hopCounter = 0;
                if (! bypassed)
                    analyse (period[i]);
            }

            if (bypassed)
                continue;

            if (fadeLeft == 0 && identity && curIdentity)
            {
                bypassed = true;   // back at the identity: bit exact again
                continue;
            }

            const float a = fadeLeft > 0 ? 1.0f - (float) fadeLeft / (float) fadeLen : 1.0f;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float* w = line[(size_t) ch].data() + linePos;   // oldest .. newest
                float y = dot (curTaps.data(), w, taps);
                if (fadeLeft > 0)
                    y = a * y + (1.0f - a) * dot (oldTaps.data(), w, taps);
                io[ch][i] = y;
            }
            if (fadeLeft > 0)
                --fadeLeft;
        }
    }
}
