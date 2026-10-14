#include "StereoStage.h"

namespace velvox::dsp
{
    void StereoStage::prepare (double sampleRate, int)
    {
        fs = sampleRate;
        width.reset (sampleRate, 0.05);
        monoBelow.reset (sampleRate, 0.05);
        bassMonoAmount.reset (sampleRate, 0.03);
        width.setCurrentAndTargetValue (width.getTargetValue());
        monoBelow.setCurrentAndTargetValue (monoBelow.getTargetValue());
        bassMonoAmount.setCurrentAndTargetValue (bassMonoAmount.getTargetValue());
        updateFilter();
        reset();
    }

    void StereoStage::reset()
    {
        sideHighPass1.reset();
        sideHighPass2.reset();
    }

    void StereoStage::setSettings (const StereoSettings& s) noexcept
    {
        width.setTargetValue (juce::jlimit (0.0f, maxWidth, s.width));
        const bool bassMono = s.monoBelowHz > monoBelowOffHz;
        bassMonoAmount.setTargetValue (bassMono ? 1.0f : 0.0f);
        if (bassMono)
            monoBelow.setTargetValue (juce::jlimit (monoBelowOffHz, 1000.0f, s.monoBelowHz));
    }

    void StereoStage::updateFilter() noexcept
    {
        const float f = monoBelow.getCurrentValue();
        sideHighPass1.setCoeffs (design::highPass (fs, f, 0.70710678));
        sideHighPass2.setCoeffs (design::highPass (fs, f, 0.70710678));
    }

    void StereoStage::process (float* const* ch, int numChannels, int numSamples) noexcept
    {
        if (numChannels < 2)
            return;

        const bool idle = ! width.isSmoothing() && std::abs (width.getCurrentValue() - 1.0f) < 1.0e-6f
                       && ! bassMonoAmount.isSmoothing() && bassMonoAmount.getCurrentValue() <= 0.0f;
        if (idle)
        {
            reset(); // keep filter state clean for the next activation
            return;
        }

        constexpr int updateInterval = 32;
        for (int start = 0; start < numSamples; start += updateInterval)
        {
            const int n = std::min (updateInterval, numSamples - start);
            if (monoBelow.isSmoothing())
            {
                monoBelow.skip (n);
                updateFilter();
            }

            for (int i = start; i < start + n; ++i)
            {
                const float l = ch[0][i], r = ch[1][i];
                const float mid = 0.5f * (l + r);
                float side = 0.5f * (l - r);

                const float bm = bassMonoAmount.getNextValue();
                if (bm > 0.0f)
                {
                    const float hp = sideHighPass2.process (sideHighPass1.process (side, 0), 0);
                    side += (hp - side) * bm;
                }

                side *= width.getNextValue();
                ch[0][i] = mid + side;
                ch[1][i] = mid - side;
            }
        }

        if (! sideHighPass1.isStateFinite() || ! sideHighPass2.isStateFinite())
            reset();
    }

    //==========================================================================
    void CorrelationMeter::prepare (double sampleRate, float tau)
    {
        coeff = std::exp (-1.0 / ((double) tau * sampleRate));
        reset();
    }

    void CorrelationMeter::reset()
    {
        lr = ll = rr = 0.0;
    }

    void CorrelationMeter::process (const float* left, const float* right, int numSamples) noexcept
    {
        const double c = coeff;
        for (int i = 0; i < numSamples; ++i)
        {
            const double l = left[i], r = right[i];
            lr = l * r + c * (lr - l * r);
            ll = l * l + c * (ll - l * l);
            rr = r * r + c * (rr - r * r);
        }
        if (! std::isfinite (lr + ll + rr))
            reset();
    }

    float CorrelationMeter::getCorrelation() const noexcept
    {
        const double denom = std::sqrt (ll * rr);
        if (denom < 1.0e-12)
            return 1.0f; // silence: report "in phase"
        return (float) juce::jlimit (-1.0, 1.0, lr / denom);
    }
}
