#include "CompressorModule.h"

namespace velvox::dsp
{
    void CompressorModule::prepare (double sampleRate, int)
    {
        fs = sampleRate;
        makeup.reset (sampleRate, 0.05);
        setSettings (settings);
        makeup.setCurrentAndTargetValue (dbToGain (settings.makeupDb));
        reset();
    }

    void CompressorModule::reset()
    {
        envDb = 0.0f;
    }

    void CompressorModule::setSettings (const CompSettings& s) noexcept
    {
        settings = s;
        settings.ratio = std::max (1.0f, s.ratio);
        attackCoeff = timeConstantCoeff (std::max (0.05f, s.attackMs), fs);
        releaseCoeff = timeConstantCoeff (std::max (1.0f, s.releaseMs), fs);
        makeup.setTargetValue (dbToGain (s.makeupDb));
    }

    float CompressorModule::computeGainDb (float x, float t, float ratio, float knee) noexcept
    {
        const float slope = 1.0f / ratio - 1.0f;
        const float over = x - t;

        if (knee > 0.0f && 2.0f * std::abs (over) <= knee)
        {
            const float k = over + knee * 0.5f;
            return x + slope * k * k / (2.0f * knee);
        }

        if (over > 0.0f)
            return t + over / ratio;

        return x;
    }

    void CompressorModule::process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        float maxReduction = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            float peak = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                peak = std::max (peak, std::abs (channels[ch][i]));

            const float inDb = gainToDb (peak);
            const float outDb = computeGainDb (inDb, settings.thresholdDb, settings.ratio, settings.kneeDb);
            const float targetReduction = std::max (0.0f, inDb - outDb);

            // Branching one-pole smoother: attack when reduction rises
            const float coeff = targetReduction > envDb ? attackCoeff : releaseCoeff;
            envDb = targetReduction + coeff * (envDb - targetReduction);

            const float gain = dbToGain (-envDb) * makeup.getNextValue();

            for (int ch = 0; ch < numChannels; ++ch)
                channels[ch][i] *= gain;

            maxReduction = std::max (maxReduction, envDb);
        }

        if (! std::isfinite (envDb))
            envDb = 0.0f;

        gainReductionDb.push (maxReduction);
    }
}
