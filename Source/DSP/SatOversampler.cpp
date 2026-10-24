#include "SatOversampler.h"
#include <cmath>
#include <complex>

namespace velvox::dsp
{
    namespace
    {
        // hiir half-band coefficients (de Soras), transition width relative to the high rate
        constexpr float steepCoefs[7] { 0.03340626274216485f, 0.12632330463071034f, 0.2606369890499899f, 0.4159551207930945f,
                                        0.5774200950249658f, 0.7397614786304547f, 0.9084253630173498f };   // tbw 0.0833, 112 dB
        constexpr float relaxedCoefs[3] { 0.07022405925320405f, 0.2850862804444172f, 0.6845413589235864f }; // tbw 0.25, 89 dB

        // scipy.signal.ellip (11, 0.05, 95, ellipord-Wn), wp 0.4167 / ws 0.5 of the Nyquist: { b0, b1, b2, a1, a2 }
        constexpr double ellipSos[6][5] {
            { 0.001735320602656005, 0.002572001947754186, 0.0017353206026560044, -0.5681057664377767, 0.0 },
            { 1.0, 0.6957411066027798, 1.0000000000000002, -1.0395427835883473, 0.40607124592273447 },
            { 1.0, 1.0, 0.0, -0.8337200819619389, 0.5854658219706824 },
            { 1.0, 0.20554497573200486, 1.0, -0.6466125998409783, 0.7551035155627321 },
            { 1.0, -0.03692442840250392, 1.0000000000000002, -0.5294544242168896, 0.8770541228428506 },
            { 1.0, -0.1341542789864973, 0.9999999999999999, -0.4821652191564928, 0.9626865294440597 },
        };
    }

    double SatOversampler::Elliptic::process (double x) noexcept
    {
        for (int s = 0; s < sections; ++s)
        {
            const auto& c = ellipSos[s];
            auto& st = z[(size_t) s];
            const double y = c[0] * x + st[0];
            st[0] = c[1] * x - c[3] * y + st[1];
            st[1] = c[2] * x - c[4] * y;
            x = y;
        }
        return x;
    }

    void SatOversampler::initCoefficients()
    {
        for (auto& f : upSteep)
            std::copy (std::begin (steepCoefs), std::end (steepCoefs), f.c.begin());
        for (auto* bank : { &upRelaxed, &downRelaxed })
            for (auto& f : *bank)
                std::copy (std::begin (relaxedCoefs), std::end (relaxedCoefs), f.c.begin());
    }

    void SatOversampler::prepare (double sampleRate, int maxBlockSize)
    {
        baseRate = sampleRate;
        factor = sampleRate < 60000.0 ? 4 : 2;
        maxBlock = std::max (1, maxBlockSize);
        initCoefficients();
        for (int ch = 0; ch < maxChannels; ++ch)
        {
            high[(size_t) ch].assign ((size_t) (maxBlock * factor), 0.0f);
            mid[(size_t) ch].assign ((size_t) (maxBlock * 2), 0.0f);
            highPtr[(size_t) ch] = high[(size_t) ch].data();
        }
        reset();

        // Phase delay of the linear chain at 1 kHz from its impulse response
        constexpr int len = 256;
        std::vector<float> imp ((size_t) len, 0.0f), out ((size_t) len, 0.0f);
        imp[0] = 1.0f;
        for (int start = 0; start < len; start += maxBlock)
        {
            const int n = std::min (maxBlock, len - start);
            const float* in[] { imp.data() + start };
            upsample (in, 1, n);
            float* o[] { out.data() + start };
            downsample (o, 1, n);
        }
        const double w = 2.0 * 3.14159265358979323846 * 1000.0 / sampleRate;
        std::complex<double> h {};
        for (int i = 0; i < len; ++i)
            h += (double) out[(size_t) i] * std::polar (1.0, -w * i);
        delayAt1k = -std::arg (h) / w;
        reset();
    }

    void SatOversampler::reset() noexcept
    {
        for (auto& f : upSteep) f.reset();
        for (auto& f : upRelaxed) f.reset();
        for (auto& f : downRelaxed) f.reset();
        for (auto& f : ellip) f.reset();
    }

    double SatOversampler::getDelayBaseSamples (double extraHighRateSamples) const noexcept
    {
        return delayAt1k + extraHighRateSamples / factor;
    }

    float* const* SatOversampler::upsample (const float* const* in, int numChannels, int n) noexcept
    {
        jassert (n <= maxBlock);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* x = in[ch];
            auto& steep = upSteep[(size_t) ch];
            float* h = high[(size_t) ch].data();
            if (factor == 2)
            {
                for (int i = 0; i < n; ++i)
                    steep.up (x[i], h[2 * i], h[2 * i + 1]);
                continue;
            }

            float* m = mid[(size_t) ch].data();
            for (int i = 0; i < n; ++i)
                steep.up (x[i], m[2 * i], m[2 * i + 1]);

            auto& relaxed = upRelaxed[(size_t) ch];
            for (int i = 0; i < 2 * n; ++i)
                relaxed.up (m[i], h[2 * i], h[2 * i + 1]);
        }
        return highPtr.data();
    }

    void SatOversampler::downsample (float* const* out, int numChannels, int n) noexcept
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* h = high[(size_t) ch].data();
            const float* m = h;   // the 2x-rate signal (the high buffer itself when the factor is 2)
            if (factor == 4)
            {
                float* md = mid[(size_t) ch].data();
                auto& relaxed = downRelaxed[(size_t) ch];
                for (int i = 0; i < 2 * n; ++i)
                    md[i] = relaxed.down (h[2 * i], h[2 * i + 1]);
                m = md;
            }

            auto& e = ellip[(size_t) ch];
            float* y = out[ch];
            for (int i = 0; i < n; ++i)
            {
                y[i] = (float) e.process (m[2 * i]);
                e.process (m[2 * i + 1]);
            }
        }
    }
}
