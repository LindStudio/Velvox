#include "SmoothModule.h"

namespace velvox::dsp
{
    namespace
    {
        constexpr float lowestHz = 120.0f, highestHz = 18000.0f;
        constexpr int controlInterval = 16;          // samples between filter updates
        constexpr int nearSkip = 1;                  // direct neighbours share the peak skirt - not part of the reference
        constexpr int neighbourhood = 5;             // reference = bands 2..5 away on either side
        constexpr float detectorQ = 8.0f;            // narrow: a resonance does not leak into its reference
        constexpr float silenceDb = -72.0f;          // bands quieter than this are never treated
    }

    float SmoothModule::bandFrequency (int band) noexcept
    {
        return lowestHz * std::pow (highestHz / lowestHz, (float) band / (float) (numBands - 1));
    }

    void SmoothModule::prepare (double sampleRate, int)
    {
        fs = sampleRate;
        activeBands = 0;
        while (activeBands < numBands && bandFrequency (activeBands) < fs * 0.45)
            ++activeBands;

        // Constant-peak band-pass (0 dB at the centre): b = alpha*(1, 0, -1)
        for (int b = 0; b < activeBands; ++b)
        {
            const double w = 2.0 * juce::MathConstants<double>::pi * bandFrequency (b) / fs;
            const double alpha = std::sin (w) / (2.0 * detectorQ);
            const double a0 = 1.0 + alpha;
            BiquadCoeffs c;
            c.b0 = alpha / a0;
            c.b1 = 0.0;
            c.b2 = -alpha / a0;
            c.a1 = -2.0 * std::cos (w) / a0;
            c.a2 = (1.0 - alpha) / a0;
            detectors[(size_t) b].setCoeffs (c);
        }

        // Envelope time per band: long enough for a steady estimate in narrow low bands
        for (int b = 0; b < numBands; ++b)
        {
            const float bandwidthHz = bandFrequency (b) / detectorQ;
            powerCoeff[(size_t) b] = timeConstantCoeff (std::max (8.0f, 4000.0f / bandwidthHz), fs);
        }
        for (auto* s : { &enabled, &outputGain, &deltaMix })
        {
            s->reset (sampleRate, 0.03);
            s->setCurrentAndTargetValue (s->getTargetValue());
        }
        setSettings (settings);
        reset();
        snapNext = true;
    }

    void SmoothModule::reset()
    {
        for (int b = 0; b < numBands; ++b)
        {
            detectors[(size_t) b].reset();
            cutters[(size_t) b].reset();
            if (b < activeBands)
                cutters[(size_t) b].setCoeffs (design::peak (fs, bandFrequency (b), 2.5, 0.0));
            power[(size_t) b] = 0.0f;
            cut[(size_t) b] = 0.0f;
            appliedCut[(size_t) b] = 0.0f;
            bandCutDb[(size_t) b].store (0.0f, std::memory_order_relaxed);
        }
        controlCounter = 0;
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

        // Control-rate time constants (the envelope runs every controlInterval samples)
        const double controlRate = fs / controlInterval;
        const float hardFactor = s.mode == 1 ? 0.6f : 1.0f;
        attackCoeff = timeConstantCoeff (juce::jlimit (0.5f, 100.0f, s.attackMs) * hardFactor, controlRate);
        releaseCoeff = timeConstantCoeff (juce::jlimit (5.0f, 1000.0f, s.releaseMs) * hardFactor, controlRate);

        // Soft edges of the frequency range (1/4 octave fade on each side)
        const float lo = juce::jlimit (20.0f, 20000.0f, std::min (s.lowHz, s.highHz));
        const float hi = juce::jlimit (20.0f, 20000.0f, std::max (s.lowHz, s.highHz));
        for (int b = 0; b < numBands; ++b)
        {
            const float f = bandFrequency (b);
            const float below = std::log2 (f / lo) * 4.0f + 1.0f;   // 0 at 1/4 oct below lo, 1 at lo
            const float above = std::log2 (hi / f) * 4.0f + 1.0f;
            rangeWeight[(size_t) b] = juce::jlimit (0.0f, 1.0f, std::min (below, above));
        }
    }

    float SmoothModule::getMaxCutDb() const noexcept
    {
        float m = 0.0f;
        for (auto& c : bandCutDb)
            m = std::max (m, c.load (std::memory_order_relaxed));
        return m;
    }

    void SmoothModule::updateControl() noexcept
    {
        std::array<float, numBands> levelDb {};
        for (int b = 0; b < activeBands; ++b)
            levelDb[(size_t) b] = 10.0f * std::log10 (std::max (power[(size_t) b], 1.0e-12f));

        const float amount = juce::jlimit (0.0f, 1.0f, settings.amount);
        const bool hard = settings.mode == 1;
        const float threshold = thresholdFor (settings.sensitivity);
        const float maxCut = hard ? 6.0f + 18.0f * amount : 3.0f + 12.0f * amount;
        const float ratio = (0.35f + 0.65f * amount) * (hard ? 1.3f : 1.0f);
        const double q = hard ? 5.0 : 2.5;

        for (int b = 0; b < activeBands; ++b)
        {
            // Local reference: mean level of the neighbourhood (the band itself excluded)
            float sum = 0.0f;
            int count = 0;
            for (int j = std::max (0, b - neighbourhood); j <= std::min (activeBands - 1, b + neighbourhood); ++j)
                if (std::abs (j - b) > nearSkip)
                {
                    sum += levelDb[(size_t) j];
                    ++count;
                }
            const float reference = count > 0 ? sum / (float) count : levelDb[(size_t) b];
            const float excess = levelDb[(size_t) b] - reference - threshold;

            float target = 0.0f;
            if (excess > 0.0f && levelDb[(size_t) b] > silenceDb)
                target = std::min (maxCut, excess * ratio) * rangeWeight[(size_t) b];

            auto& c = cut[(size_t) b];
            c = target + (target > c ? attackCoeff : releaseCoeff) * (c - target);

            // Re-design the bell only when the reduction really moved
            if (std::abs (c - appliedCut[(size_t) b]) > 0.03f)
            {
                appliedCut[(size_t) b] = c;
                // A 0 dB bell is exactly neutral, so the filter state stays continuous
                cutters[(size_t) b].setCoeffs (design::peak (fs, bandFrequency (b), q, -c));
            }
            bandCutDb[(size_t) b].store (appliedCut[(size_t) b], std::memory_order_relaxed);
        }
    }

    void SmoothModule::process (float* const* ch, int numChannels, int n) noexcept
    {
        const bool active = enabled.getTargetValue() > 0.0f || enabled.isSmoothing();
        if (! active)
        {
            if (wasActive)
                reset();
            wasActive = false;
            return;
        }
        if (! wasActive)
        {
            reset();
            wasActive = true;
        }

        for (int i = 0; i < n; ++i)
        {
            // Detection on the mid signal (stereo linked: both sides get the same cuts)
            float mid = 0.0f;
            for (int c = 0; c < numChannels; ++c)
                mid += ch[c][i];
            mid /= (float) numChannels;
            for (int b = 0; b < activeBands; ++b)
            {
                const float y = detectors[(size_t) b].process (mid, 0);
                power[(size_t) b] = y * y + powerCoeff[(size_t) b] * (power[(size_t) b] - y * y);
            }

            if (--controlCounter <= 0)
            {
                controlCounter = controlInterval;
                updateControl();
            }

            const float on = enabled.getNextValue();
            const float out = outputGain.getNextValue();
            const float delta = deltaMix.getNextValue();
            for (int c = 0; c < numChannels; ++c)
            {
                const float dry = ch[c][i];
                float wet = dry;
                for (int b = 0; b < activeBands; ++b)
                    wet = cutters[(size_t) b].process (wet, c);

                // Delta: listen to exactly what is removed
                const float processed = (wet + delta * (dry - 2.0f * wet)) * out;
                ch[c][i] = dry + on * (processed - dry);
            }
        }
    }
}
