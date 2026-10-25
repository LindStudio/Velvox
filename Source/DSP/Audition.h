#pragma once

#include "DspUtilities.h"
#include "EqDesign.h"
#include <array>
#include <cmath>

// EQ band solo (0.6.1 item 1, "Kopfhörer wie Pro-Q 4"): while a band is auditioned the EQ output is
// replaced by the region the band acts on - taken from the EQ's INPUT, so the band's own gain is
// not applied - level-matched to the input, through a short crossfade. A monitoring state: the
// request is an atomic in the module (EqualizerModule::setAuditionBand), never a parameter.
namespace velvox::dsp
{
    enum class AuditionShape { bandPass = 0, lowPass, highPass };

    struct AuditionRegion
    {
        AuditionShape shape = AuditionShape::bandPass;
        float freq = 1000.0f;   // Hz
        float q = 1.0f;         // band pass only (low / high pass: Butterworth)
        bool operator== (const AuditionRegion&) const = default;
    };

    // The region a band acts on: bell, notch, band pass = a band pass at its frequency with its Q;
    // low shelf / low cut = everything below the frequency, high shelf / high cut = everything above;
    // tilt = a broad band pass around its pivot.
    inline AuditionRegion auditionRegionFor (const EqBand& b) noexcept
    {
        const float f = juce::jlimit (eqdesign::minFreq, eqdesign::maxFreq, std::isfinite (b.freq) ? b.freq : 1000.0f);
        switch (b.bandType())
        {
            case EqBandType::lowShelf:
            case EqBandType::lowCut:    return { AuditionShape::lowPass, f, 0.7071f };
            case EqBandType::highShelf:
            case EqBandType::highCut:   return { AuditionShape::highPass, f, 0.7071f };
            case EqBandType::tilt:      return { AuditionShape::bandPass, f, 0.4f };
            case EqBandType::bell:
            case EqBandType::notch:
            case EqBandType::bandPass:  break;
        }
        return { AuditionShape::bandPass, f, juce::jlimit (0.2f, eqdesign::maxQ, std::isfinite (b.q) ? b.q : 1.0f) };
    }

    // The listening path: TPT state-variable filters (band pass with 0 dB peak; low / high pass as two
    // Butterworth sections = 24 dB/oct), frequency and Q glide (20 ms) so a dragged band follows without
    // zipper, an adaptive level match (slow RMS of input vs. filtered, at most +24 dB) and the crossfade
    // (15 ms) between the normal output and the region. Real-time safe, no allocation.
    class BandAudition
    {
    public:
        static constexpr float maxGainDb = 24.0f;
        static constexpr double fadeSeconds = 0.015;

        void prepare (double sampleRate) noexcept
        {
            fs = sampleRate > 0.0 ? sampleRate : 48000.0;
            fade.reset (fs, fadeSeconds);
            fade.setCurrentAndTargetValue (0.0f);
            const double chunkSeconds = (double) chunk / fs;
            glide = 1.0 - std::exp (-chunkSeconds / 0.02);
            envCoeff = 1.0 - std::exp (-chunkSeconds / 0.3);
            gainCoeff = 1.0 - std::exp (-chunkSeconds / 0.1);
            reset();
        }

        void reset() noexcept
        {
            for (auto& ch : state)
                for (auto& s : ch)
                    s = {};
            envIn = envOut = 0.0;
        }

        // Starts a new region from scratch (filters, level match); called while the path is silent
        void start (const AuditionRegion& r) noexcept
        {
            region = target = r;
            logFreq = std::log ((double) r.freq);
            logQ = std::log ((double) r.q);
            reset();
            gain = initialGain (r);
            design();
        }

        // Same shape, new frequency / Q (glides there)
        void follow (const AuditionRegion& r) noexcept { target = r; }

        void setOn (bool on) noexcept { fade.setTargetValue (on ? 1.0f : 0.0f); }
        bool isSilent() const noexcept { return fade.getCurrentValue() <= 0.0f && ! fade.isSmoothing(); }
        AuditionShape getShape() const noexcept { return region.shape; }
        float getGain() const noexcept { return (float) gain; }

        // io = io + fade * (gain * region (in) - io); `in` may be the same buffers as `io`
        void process (const float* const* in, float* const* io, int numChannels, int numSamples) noexcept
        {
            numChannels = std::min (numChannels, maxChannels);
            for (int start = 0; start < numSamples; start += chunk)
            {
                const int n = std::min (chunk, numSamples - start);
                std::array<std::array<float, chunk>, maxChannels> x;
                for (int ch = 0; ch < numChannels; ++ch)
                    std::copy (in[ch] + start, in[ch] + start + n, x[(size_t) ch].begin());

                // Glide towards the band's current frequency / Q
                const double tf = std::log ((double) target.freq), tq = std::log ((double) target.q);
                if (std::abs (tf - logFreq) > 1.0e-6 || std::abs (tq - logQ) > 1.0e-6)
                {
                    logFreq += (tf - logFreq) * glide;
                    logQ += (tq - logQ) * glide;
                    if (std::abs (tf - logFreq) < 1.0e-5) logFreq = tf;
                    if (std::abs (tq - logQ) < 1.0e-5) logQ = tq;
                    design();
                }

                std::array<float, chunk> f;
                for (int i = 0; i < n; ++i)
                    f[(size_t) i] = fade.getNextValue();

                // Level match: the gain moves linearly over the chunk to its new value
                const double g0 = gain;
                if (envIn > 1.0e-9)
                {
                    const double want = juce::jlimit (0.5, maxGain(), std::sqrt (envIn / std::max (envOut, 1.0e-14)));
                    gain += (want - gain) * gainCoeff;
                }
                const double g1 = gain;

                double sumIn = 0.0, sumOut = 0.0;
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    auto& st = state[(size_t) ch];
                    float* d = io[ch] + start;
                    for (int i = 0; i < n; ++i)
                    {
                        const double xi = x[(size_t) ch][(size_t) i];
                        const double y = filter (st, xi);
                        sumIn += xi * xi;
                        sumOut += y * y;
                        const double g = g0 + (g1 - g0) * (double) (i + 1) / (double) n;
                        d[i] += f[(size_t) i] * ((float) (g * y) - d[i]);
                    }
                }

                const double count = (double) std::max (1, n * numChannels);
                envIn += (sumIn / count - envIn) * envCoeff;
                envOut += (sumOut / count - envOut) * envCoeff;
            }

            for (auto& ch : state)
                for (auto& s : ch)
                    if (! std::isfinite (s.ic1) || ! std::isfinite (s.ic2))
                    {
                        reset();
                        return;
                    }
        }

        // Starting gain before the level match has measured anything: the share of a pink-ish
        // spectrum (8 octaves) the region holds
        static double initialGain (const AuditionRegion& r) noexcept
        {
            double octaves = 8.0;
            if (r.shape == AuditionShape::bandPass)
                octaves = 2.0 * std::asinh (1.0 / (2.0 * (double) r.q)) / std::log (2.0);
            else if (r.shape == AuditionShape::lowPass)
                octaves = std::log2 ((double) r.freq / 60.0);
            else
                octaves = std::log2 (16000.0 / (double) r.freq);
            const double share = juce::jlimit (0.01, 1.0, octaves / 8.0);
            return juce::jlimit (1.0, maxGain(), 1.0 / std::sqrt (share));
        }

    private:
        static constexpr int chunk = 16;
        static double maxGain() noexcept { return std::pow (10.0, (double) maxGainDb / 20.0); }

        struct Svf { double ic1 = 0.0, ic2 = 0.0; };
        struct Coeffs { double k = 1.0, a1 = 1.0, a2 = 0.0, a3 = 0.0; };

        void design() noexcept
        {
            const double f = juce::jlimit (10.0, 0.45 * fs, std::exp (logFreq));
            const double g = std::tan (juce::MathConstants<double>::pi * f / fs);
            c.k = region.shape == AuditionShape::bandPass ? 1.0 / juce::jlimit (0.05, 40.0, std::exp (logQ)) : std::sqrt (2.0);
            c.a1 = 1.0 / (1.0 + g * (g + c.k));
            c.a2 = g * c.a1;
            c.a3 = g * c.a2;
        }

        double tick (Svf& s, double x, double& band, double& high) const noexcept
        {
            const double v3 = x - s.ic2;
            const double v1 = c.a1 * s.ic1 + c.a2 * v3;
            const double v2 = s.ic2 + c.a2 * s.ic1 + c.a3 * v3;
            s.ic1 = 2.0 * v1 - s.ic1;
            s.ic2 = 2.0 * v2 - s.ic2;
            band = v1;
            high = x - c.k * v1 - v2;
            return v2;
        }

        double filter (std::array<Svf, 2>& st, double x) const noexcept
        {
            double band = 0.0, high = 0.0;
            switch (region.shape)
            {
                case AuditionShape::bandPass:
                    tick (st[0], x, band, high);
                    return c.k * band;
                case AuditionShape::lowPass:
                {
                    const double l = tick (st[0], x, band, high);
                    return tick (st[1], l, band, high);
                }
                case AuditionShape::highPass:
                {
                    tick (st[0], x, band, high);
                    const double h = high;
                    tick (st[1], h, band, high);
                    return high;
                }
            }
            return x;
        }

        double fs = 48000.0;
        AuditionRegion region, target;
        double logFreq = std::log (1000.0), logQ = 0.0;
        Coeffs c;
        std::array<std::array<Svf, 2>, maxChannels> state {};
        juce::SmoothedValue<float> fade { 0.0f };
        double gain = 1.0, envIn = 0.0, envOut = 0.0;
        double glide = 0.1, envCoeff = 0.01, gainCoeff = 0.01;
    };
}
