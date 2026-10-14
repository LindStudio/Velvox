#include "Loudness.h"

namespace velvox::dsp
{
    namespace
    {
        constexpr double pi = 3.14159265358979323846;

        // ITU-R BS.1770 K-weighting expressed as analog prototypes, so it can be
        // re-derived for any sample rate (bilinear transform with pre-warping).
        constexpr double shelfFreq = 1681.974450955533;
        constexpr double shelfGainDb = 3.999843853973347;
        constexpr double shelfQ = 0.7071752369554196;
        constexpr double hpFreq = 38.13547087602444;
        constexpr double hpQ = 0.5003270373238773;
    }

    void LoudnessMeter::designKWeighting (double sampleRate, BiquadCoeffs& s, BiquadCoeffs& h)
    {
        {
            const double K = std::tan (pi * shelfFreq / sampleRate);
            const double Vh = std::pow (10.0, shelfGainDb / 20.0);
            const double Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / shelfQ + K * K;
            s.b0 = (Vh + Vb * K / shelfQ + K * K) / a0;
            s.b1 = 2.0 * (K * K - Vh) / a0;
            s.b2 = (Vh - Vb * K / shelfQ + K * K) / a0;
            s.a1 = 2.0 * (K * K - 1.0) / a0;
            s.a2 = (1.0 - K / shelfQ + K * K) / a0;
        }
        {
            const double K = std::tan (pi * hpFreq / sampleRate);
            const double a0 = 1.0 + K / hpQ + K * K;
            h.b0 = 1.0;
            h.b1 = -2.0;
            h.b2 = 1.0;
            h.a1 = 2.0 * (K * K - 1.0) / a0;
            h.a2 = (1.0 - K / hpQ + K * K) / a0;
        }
    }

    double LoudnessMeter::kWeightingGainDb (double freq, double sampleRate)
    {
        BiquadCoeffs s, h;
        designKWeighting (sampleRate, s, h);
        const double mag = design::magnitude (s, freq, sampleRate) * design::magnitude (h, freq, sampleRate);
        return 20.0 * std::log10 (std::max (mag, 1.0e-12));
    }

    float LoudnessMeter::integratedLufs (const float* const* channels, int numChannels, int numSamples,
                                         double sampleRate, int skip)
    {
        BiquadCoeffs s, h;
        designKWeighting (sampleRate, s, h);
        Biquad shelfFilter, highPassFilter;
        shelfFilter.setCoeffs (s);
        highPassFilter.setCoeffs (h);

        double sum = 0.0;
        for (int i = 0; i < numSamples; ++i)
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const double y = highPassFilter.process (shelfFilter.process (channels[ch][i], ch), ch);
                if (i >= skip)
                    sum += y * y;
            }

        const int count = numSamples - skip;
        if (count <= 0 || sum <= 0.0)
            return silenceLufs;
        return (float) (-0.691 + 10.0 * std::log10 (sum / count));
    }

    void LoudnessMeter::prepare (double sampleRate)
    {
        fs = sampleRate;
        BiquadCoeffs s, h;
        designKWeighting (sampleRate, s, h);
        shelf.setCoeffs (s);
        highPass.setCoeffs (h);
        setWindowSeconds (windowSeconds);
        reset();
    }

    void LoudnessMeter::reset()
    {
        shelf.reset();
        highPass.reset();
        meanSquare = 0.0;
    }

    void LoudnessMeter::setWindowSeconds (float seconds) noexcept
    {
        windowSeconds = juce::jlimit (0.05f, 60.0f, seconds);
        coeff = std::exp (-1.0 / ((double) windowSeconds * fs));
    }

    void LoudnessMeter::process (const float* const* channels, int numChannels, int numSamples) noexcept
    {
        double ms = meanSquare;
        const double c = coeff;

        for (int i = 0; i < numSamples; ++i)
        {
            double sum = 0.0;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const double y = highPass.process (shelf.process (channels[ch][i], ch), ch);
                sum += y * y;
            }
            ms = sum + c * (ms - sum);
        }

        meanSquare = std::isfinite (ms) ? ms : 0.0;
        if (! shelf.isStateFinite() || ! highPass.isStateFinite())
            reset();
    }

    float LoudnessMeter::getLufs() const noexcept
    {
        if (meanSquare <= 1.0e-13)
            return silenceLufs;
        return (float) (-0.691 + 10.0 * std::log10 (meanSquare));
    }

    //==========================================================================
    void LevelMatcher::prepare (double sampleRate)
    {
        fs = sampleRate;
        input.prepare (sampleRate);
        processed.prepare (sampleRate);
        setWindowSeconds (windowSeconds);
        reset();
    }

    void LevelMatcher::reset()
    {
        input.reset();
        processed.reset();
        correctionDb = 0.0f;
        warmupSeconds = 0.0f;
    }

    void LevelMatcher::setWindowSeconds (float seconds) noexcept
    {
        if (std::abs (seconds - windowSeconds) < 1.0e-4f)
            return;
        windowSeconds = seconds;
        input.setWindowSeconds (seconds);
        processed.setWindowSeconds (seconds);
    }

    void LevelMatcher::update (int n) noexcept
    {
        const float dt = (float) (n / fs);
        warmupSeconds += dt;

        const float in = input.getLufs();
        const float out = processed.getLufs();

        // Hold the last correction while there is nothing meaningful to measure,
        // and until the averages have seen enough signal.
        if (in < gateLufs || out < gateLufs - 20.0f || warmupSeconds < 0.25f * windowSeconds)
            return;

        const float target = juce::jlimit (-rangeDb, rangeDb, in - out);
        const float maxStep = slewDbPerSecond * dt;
        correctionDb += juce::jlimit (-maxStep, maxStep, target - correctionDb);
    }
}
