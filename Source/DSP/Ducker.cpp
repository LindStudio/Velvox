#include "Ducker.h"

namespace velvox::dsp
{
    void Ducker::prepare (double sampleRate)
    {
        fs = sampleRate;
        setSettings (settings);
        levelCoeff = timeConstantCoeff (20.0f, fs); // detector peak decay
        reset();
    }

    void Ducker::reset()
    {
        level = 0.0f;
        envDb = 0.0f;
    }

    void Ducker::setSettings (const DuckSettings& s) noexcept
    {
        settings = s;
        settings.amountDb = juce::jlimit (0.0f, 48.0f, s.amountDb);
        attackCoeff = timeConstantCoeff (std::max (0.1f, s.attackMs), fs);
        releaseCoeff = timeConstantCoeff (std::max (1.0f, s.releaseMs), fs);
    }

    void Ducker::computeGains (const float* const* detector, int numChannels, int numSamples, float* gains) noexcept
    {
        float maxReduction = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            float peak = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                peak = std::max (peak, std::abs (detector[ch][i]));

            // Instant-attack peak follower, so short consonants are caught too
            level = peak > level ? peak : peak + levelCoeff * (level - peak);

            const float over = gainToDb (level) - settings.thresholdDb;
            const float target = juce::jlimit (0.0f, settings.amountDb, over);

            const float coeff = target > envDb ? attackCoeff : releaseCoeff;
            envDb = target + coeff * (envDb - target);

            gains[i] = dbToGain (-envDb);
            maxReduction = std::max (maxReduction, envDb);
        }

        if (! std::isfinite (envDb) || ! std::isfinite (level))
            reset();

        gainReductionDb.push (maxReduction);
    }
}
