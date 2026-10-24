#pragma once

// The EQ of version 0.5 (fixed HPF / low / mid / high / LPF bands, RBJ bilinear designs), kept
// verbatim ONLY as the null-test reference for the 0.6 band EQ and its migration. Never used by the plugin.

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>
#include <complex>

namespace velvox::legacy::v05
{
    inline constexpr int maxChannels = 2;

    inline float dbToGain (float db) noexcept       { return std::pow (10.0f, db * 0.05f); }
    inline float gainToDb (float g) noexcept        { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }

    // Replaces NaN/Inf with silence. Returns true if anything had to be fixed.
    

    // Normalised second-order section (a0 == 1)
    struct BiquadCoeffs
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    };

    // Coefficient designs (Robert Bristow-Johnson "Audio EQ Cookbook")
    namespace design
    {
    }

    // Transposed direct form II biquad with independent state per channel.
    class Biquad
    {
    public:
        void setCoeffs (const BiquadCoeffs& newCoeffs) noexcept { c = newCoeffs; }
        const BiquadCoeffs& getCoeffs() const noexcept         { return c; }

        void reset() noexcept
        {
            s1.fill (0.0);
            s2.fill (0.0);
        }

        inline float process (float x, int channel) noexcept
        {
            const double in = x;
            const double y = c.b0 * in + s1[(size_t) channel];
            s1[(size_t) channel] = c.b1 * in - c.a1 * y + s2[(size_t) channel];
            s2[(size_t) channel] = c.b2 * in - c.a2 * y;
            return (float) y;
        }

        // Guards against a corrupted state (e.g. after invalid input)
        bool isStateFinite() const noexcept
        {
            for (int ch = 0; ch < maxChannels; ++ch)
                if (! std::isfinite (s1[(size_t) ch]) || ! std::isfinite (s2[(size_t) ch]))
                    return false;
            return true;
        }

    private:
        BiquadCoeffs c;
        std::array<double, maxChannels> s1 {}, s2 {};
    };

    // Simple one-pole smoothing coefficient for time constant in ms
    inline float timeConstantCoeff (float ms, double sampleRate) noexcept
    {
        if (ms <= 0.0f)
            return 0.0f;
        return (float) std::exp (-1.0 / (0.001 * (double) ms * sampleRate));
    }
    namespace design
    {
        namespace
        {
            constexpr double pi = 3.14159265358979323846;

            struct Prep
            {
                double cosw, sinw, alpha;
            };

            inline Prep prepare (double sampleRate, double freq, double q)
            {
                const double f = std::clamp (freq, 5.0, 0.49 * sampleRate);
                const double w0 = 2.0 * pi * f / sampleRate;
                const double qq = std::max (q, 0.05);
                return { std::cos (w0), std::sin (w0), std::sin (w0) / (2.0 * qq) };
            }

            inline BiquadCoeffs normalise (double b0, double b1, double b2, double a0, double a1, double a2)
            {
                const double inv = 1.0 / a0;
                return { b0 * inv, b1 * inv, b2 * inv, a1 * inv, a2 * inv };
            }
        }

        inline BiquadCoeffs lowPass (double sampleRate, double freq, double q)
        {
            const auto p = prepare (sampleRate, freq, q);
            const double b1 = 1.0 - p.cosw;
            return normalise (b1 * 0.5, b1, b1 * 0.5, 1.0 + p.alpha, -2.0 * p.cosw, 1.0 - p.alpha);
        }

        inline BiquadCoeffs highPass (double sampleRate, double freq, double q)
        {
            const auto p = prepare (sampleRate, freq, q);
            const double b0 = (1.0 + p.cosw) * 0.5;
            return normalise (b0, -(1.0 + p.cosw), b0, 1.0 + p.alpha, -2.0 * p.cosw, 1.0 - p.alpha);
        }

        inline BiquadCoeffs peak (double sampleRate, double freq, double q, double gainDb)
        {
            const auto p = prepare (sampleRate, freq, q);
            const double A = std::pow (10.0, gainDb / 40.0);
            return normalise (1.0 + p.alpha * A, -2.0 * p.cosw, 1.0 - p.alpha * A,
                              1.0 + p.alpha / A, -2.0 * p.cosw, 1.0 - p.alpha / A);
        }

        inline BiquadCoeffs lowShelf (double sampleRate, double freq, double q, double gainDb)
        {
            const auto p = prepare (sampleRate, freq, q);
            const double A = std::pow (10.0, gainDb / 40.0);
            const double sq = 2.0 * std::sqrt (A) * p.alpha;

            return normalise (A * ((A + 1.0) - (A - 1.0) * p.cosw + sq),
                              2.0 * A * ((A - 1.0) - (A + 1.0) * p.cosw),
                              A * ((A + 1.0) - (A - 1.0) * p.cosw - sq),
                              (A + 1.0) + (A - 1.0) * p.cosw + sq,
                              -2.0 * ((A - 1.0) + (A + 1.0) * p.cosw),
                              (A + 1.0) + (A - 1.0) * p.cosw - sq);
        }

        inline BiquadCoeffs highShelf (double sampleRate, double freq, double q, double gainDb)
        {
            const auto p = prepare (sampleRate, freq, q);
            const double A = std::pow (10.0, gainDb / 40.0);
            const double sq = 2.0 * std::sqrt (A) * p.alpha;

            return normalise (A * ((A + 1.0) + (A - 1.0) * p.cosw + sq),
                              -2.0 * A * ((A - 1.0) + (A + 1.0) * p.cosw),
                              A * ((A + 1.0) + (A - 1.0) * p.cosw - sq),
                              (A + 1.0) - (A - 1.0) * p.cosw + sq,
                              2.0 * ((A - 1.0) - (A + 1.0) * p.cosw),
                              (A + 1.0) - (A - 1.0) * p.cosw - sq);
        }

        inline double magnitude (const BiquadCoeffs& c, double freq, double sampleRate)
        {
            const double w = 2.0 * pi * freq / sampleRate;
            const std::complex<double> z1 = std::polar (1.0, -w);
            const std::complex<double> z2 = z1 * z1;
            const auto num = c.b0 + c.b1 * z1 + c.b2 * z2;
            const auto den = 1.0 + c.a1 * z1 + c.a2 * z2;
            return std::abs (num / den);
        }
    }

    struct EqSettings
    {
        bool hpfOn = true;
        float hpfFreq = 80.0f;
        int hpfSlope = 1;          // 0 = 12, 1 = 24, 2 = 48 dB/oct
        int lowType = 0;           // 0 = shelf, 1 = bell
        float lowFreq = 150.0f, lowGain = 0.0f, lowQ = 0.7071f;
        float midFreq = 1500.0f, midGain = 0.0f, midQ = 1.0f;
        int highType = 0;          // 0 = shelf, 1 = bell
        float highFreq = 10000.0f, highGain = 0.0f, highQ = 0.7071f;
        bool lpfOn = false;
        float lpfFreq = 18000.0f;
        int lpfSlope = 1;          // 0 = 12, 1 = 24, 2 = 48 dB/oct
    };

    // Low cut (Butterworth, up to 8th order) -> low band (shelf/bell) -> mid bell
    // -> high band (shelf/bell) -> high cut (Butterworth, up to 8th order).
    class EqualizerModule
    {
    public:
        static constexpr int maxCutSections = 4; // 48 dB/oct
        enum Section
        {
            hp1 = 0, hp2, hp3, hp4,
            lowBand, midBell, highBand,
            lp1, lp2, lp3, lp4,
            numSections
        };
        using Sections = std::array<BiquadCoeffs, numSections>;

        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        void setSettings (const EqSettings& s);
        void process (float* const* channels, int numChannels, int numSamples) noexcept;

        // Shared with the UI so the drawn curve is exactly what is processed
        static Sections designSections (const EqSettings& s, double sampleRate);
        static double responseDb (const EqSettings& s, double freq, double sampleRate);

    private:
        void updateCoefficients (bool snap) noexcept;

        double fs = 44100.0;
        EqSettings target;
        int currentHpSlope = 1, currentLpSlope = 1;

        using Mult = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>;
        Mult hpfFreq { 80.0f }, lowFreq { 150.0f }, midFreq { 1500.0f }, highFreq { 10000.0f }, lpfFreq { 18000.0f },
             midQ { 1.0f }, lowQ { 0.7071f }, highQ { 0.7071f };
        juce::SmoothedValue<float> lowGain, midGain, highGain, hpfAmount, lpfAmount;

        std::array<Biquad, numSections> filters;
        static constexpr int updateInterval = 16;
    };

    namespace detail
    {
        // Butterworth section Q values for 2nd, 4th and 8th order
        constexpr double bwQ[3][4] {
            { 0.70710678, 0, 0, 0 },
            { 0.54119610, 1.30656296, 0, 0 },
            { 0.50979558, 0.60134489, 0.89997622, 2.56291545 },
        };
        constexpr int sectionsForSlope[3] { 1, 2, 4 };

        template <typename Fn>
        void designCut (EqualizerModule::Sections& out, int first, int slope, Fn&& design)
        {
            slope = juce::jlimit (0, 2, slope);
            for (int k = 0; k < EqualizerModule::maxCutSections; ++k)
                out[(size_t) (first + k)] = k < sectionsForSlope[slope] ? design (bwQ[slope][k]) : BiquadCoeffs {};
        }

        template <typename SV>
        bool anySmoothing (std::initializer_list<SV*> values)
        {
            for (auto* v : values)
                if (v->isSmoothing())
                    return true;
            return false;
        }
    }

    inline void EqualizerModule::prepare (double sampleRate, int)
    {
        fs = sampleRate;

        for (auto* s : { &hpfFreq, &lowFreq, &midFreq, &highFreq, &lpfFreq, &midQ, &lowQ, &highQ })
            s->reset (sampleRate, 0.05);
        for (auto* s : { &lowGain, &midGain, &highGain })
            s->reset (sampleRate, 0.05);
        hpfAmount.reset (sampleRate, 0.03);
        lpfAmount.reset (sampleRate, 0.03);

        setSettings (target);
        hpfAmount.setCurrentAndTargetValue (target.hpfOn ? 1.0f : 0.0f);
        lpfAmount.setCurrentAndTargetValue (target.lpfOn ? 1.0f : 0.0f);
        updateCoefficients (true);
        reset();
    }

    inline void EqualizerModule::reset()
    {
        for (auto& f : filters)
            f.reset();
    }

    inline void EqualizerModule::setSettings (const EqSettings& s)
    {
        target = s;
        hpfFreq.setTargetValue (s.hpfFreq);
        lowFreq.setTargetValue (s.lowFreq);
        midFreq.setTargetValue (s.midFreq);
        highFreq.setTargetValue (s.highFreq);
        lpfFreq.setTargetValue (s.lpfFreq);
        midQ.setTargetValue (s.midQ);
        lowQ.setTargetValue (s.lowQ);
        highQ.setTargetValue (s.highQ);
        lowGain.setTargetValue (s.lowGain);
        midGain.setTargetValue (s.midGain);
        highGain.setTargetValue (s.highGain);
        hpfAmount.setTargetValue (s.hpfOn ? 1.0f : 0.0f);
        lpfAmount.setTargetValue (s.lpfOn ? 1.0f : 0.0f);
    }

    inline EqualizerModule::Sections EqualizerModule::designSections (const EqSettings& s, double sr)
    {
        Sections out;

        detail::designCut (out, hp1, s.hpfSlope, [&] (double q) { return design::highPass (sr, s.hpfFreq, q); });
        detail::designCut (out, lp1, s.lpfSlope, [&] (double q) { return design::lowPass (sr, s.lpfFreq, q); });

        out[lowBand]  = s.lowType == 1 ? design::peak (sr, s.lowFreq, s.lowQ, s.lowGain)
                                       : design::lowShelf (sr, s.lowFreq, s.lowQ, s.lowGain);
        out[midBell]  = design::peak (sr, s.midFreq, s.midQ, s.midGain);
        out[highBand] = s.highType == 1 ? design::peak (sr, s.highFreq, s.highQ, s.highGain)
                                        : design::highShelf (sr, s.highFreq, s.highQ, s.highGain);
        return out;
    }

    inline double EqualizerModule::responseDb (const EqSettings& s, double freq, double sampleRate)
    {
        const auto sections = designSections (s, sampleRate);
        double mag = 1.0;

        for (int i = 0; i < numSections; ++i)
        {
            if (i >= hp1 && i <= hp4 && ! s.hpfOn)
                continue;
            if (i >= lp1 && i <= lp4 && ! s.lpfOn)
                continue;
            mag *= design::magnitude (sections[(size_t) i], freq, sampleRate);
        }

        return 20.0 * std::log10 (std::max (mag, 1.0e-9));
    }

    inline void EqualizerModule::updateCoefficients (bool snap) noexcept
    {
        if (snap)
        {
            for (auto* s : { &hpfFreq, &lowFreq, &midFreq, &highFreq, &lpfFreq, &midQ, &lowQ, &highQ })
                s->setCurrentAndTargetValue (s->getTargetValue());
            for (auto* s : { &lowGain, &midGain, &highGain })
                s->setCurrentAndTargetValue (s->getTargetValue());
        }

        EqSettings cur = target;
        cur.hpfFreq  = hpfFreq.getCurrentValue();
        cur.lowFreq  = lowFreq.getCurrentValue();
        cur.midFreq  = midFreq.getCurrentValue();
        cur.highFreq = highFreq.getCurrentValue();
        cur.lpfFreq  = lpfFreq.getCurrentValue();
        cur.midQ     = midQ.getCurrentValue();
        cur.lowQ     = lowQ.getCurrentValue();
        cur.highQ    = highQ.getCurrentValue();
        cur.lowGain  = lowGain.getCurrentValue();
        cur.midGain  = midGain.getCurrentValue();
        cur.highGain = highGain.getCurrentValue();

        // Changing the filter topology: clear the affected state to avoid bursts
        if (cur.hpfSlope != currentHpSlope)
        {
            for (int i = hp1; i <= hp4; ++i)
                filters[(size_t) i].reset();
            currentHpSlope = cur.hpfSlope;
        }
        if (cur.lpfSlope != currentLpSlope)
        {
            for (int i = lp1; i <= lp4; ++i)
                filters[(size_t) i].reset();
            currentLpSlope = cur.lpfSlope;
        }

        const auto sections = designSections (cur, fs);
        for (size_t i = 0; i < sections.size(); ++i)
            filters[i].setCoeffs (sections[i]);
    }

    inline void EqualizerModule::process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        const int hpSections = detail::sectionsForSlope[juce::jlimit (0, 2, target.hpfSlope)];
        const int lpSections = detail::sectionsForSlope[juce::jlimit (0, 2, target.lpfSlope)];

        for (int start = 0; start < numSamples; start += updateInterval)
        {
            const int n = std::min (updateInterval, numSamples - start);

            const bool smoothing = detail::anySmoothing<Mult> ({ &hpfFreq, &lowFreq, &midFreq, &highFreq, &lpfFreq, &midQ, &lowQ, &highQ })
                                || detail::anySmoothing<juce::SmoothedValue<float>> ({ &lowGain, &midGain, &highGain })
                                || target.hpfSlope != currentHpSlope || target.lpfSlope != currentLpSlope;

            if (smoothing)
            {
                for (auto* s : { &hpfFreq, &lowFreq, &midFreq, &highFreq, &lpfFreq, &midQ, &lowQ, &highQ })
                    s->skip (n);
                for (auto* s : { &lowGain, &midGain, &highGain })
                    s->skip (n);
                updateCoefficients (false);
            }

            const bool hpRamping = hpfAmount.isSmoothing(), lpRamping = lpfAmount.isSmoothing();
            const float hpStatic = hpfAmount.getCurrentValue(), lpStatic = lpfAmount.getCurrentValue();

            for (int i = start; i < start + n; ++i)
            {
                const float hpAmt = hpRamping ? hpfAmount.getNextValue() : hpStatic;
                const float lpAmt = lpRamping ? lpfAmount.getNextValue() : lpStatic;

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    float x = channels[ch][i];

                    if (hpAmt > 0.0f)
                    {
                        float y = x;
                        for (int k = 0; k < hpSections; ++k)
                            y = filters[(size_t) (hp1 + k)].process (y, ch);
                        x += (y - x) * hpAmt;
                    }

                    x = filters[lowBand].process (x, ch);
                    x = filters[midBell].process (x, ch);
                    x = filters[highBand].process (x, ch);

                    if (lpAmt > 0.0f)
                    {
                        float y = x;
                        for (int k = 0; k < lpSections; ++k)
                            y = filters[(size_t) (lp1 + k)].process (y, ch);
                        x += (y - x) * lpAmt;
                    }

                    channels[ch][i] = x;
                }
            }
        }

        for (auto& f : filters)
            if (! f.isStateFinite())
            {
                reset();
                break;
            }
    }
}
