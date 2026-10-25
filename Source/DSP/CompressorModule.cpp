#include "CompressorModule.h"

namespace velvox::dsp
{
    void CompressorModule::prepare (double sampleRate, int)
    {
        fs = sampleRate;
        history.prepare (sampleRate);
        peakHold.prepare ((int) std::ceil (holdMs * 0.001 * sampleRate));
        makeup.reset (sampleRate, 0.05);
        dualMix.reset (sampleRate, 0.05);
        setSettings (settings);
        makeup.setCurrentAndTargetValue (makeup.getTargetValue());
        dualMix.setCurrentAndTargetValue (dualMix.getTargetValue());
        reset();
    }

    void CompressorModule::reset()
    {
        envDb = fastDb = slowDb = grDb = 0.0f;
        rmsState = 0.0f;
        sidechainHpf.reset();
        peakHold.reset();
    }

    void CompressorModule::setSettings (const CompSettings& s) noexcept
    {
        settings = s;
        settings.ratio = std::max (1.0f, s.ratio);
        attackCoeff = timeConstantCoeff (std::max (0.05f, s.attackMs), fs);
        releaseCoeff = timeConstantCoeff (std::max (1.0f, s.releaseMs), fs);
        const float r = std::max (1.0f, s.releaseMs);
        fastReleaseCoeff = timeConstantCoeff (fastReleaseFactor * r, fs);
        slowAttackCoeff = timeConstantCoeff (slowAttackFactor * r, fs);
        slowReleaseCoeff = timeConstantCoeff (slowReleaseFactor * r, fs);
        dualMix.setTargetValue (s.autoRelease ? 1.0f : 0.0f);
        makeup.setTargetValue (dbToGain (s.makeupDb + (s.autoMakeup ? autoMakeupDb (settings) : 0.0f)));
        rmsCoeff = timeConstantCoeff (10.0f, fs);

        const bool wantHpf = s.sidechainHpfHz > 20.5f;
        if (wantHpf)
            sidechainHpf.setCoeffs (design::highPass (fs, juce::jlimit (20.0f, 1000.0f, s.sidechainHpfHz), 0.70710678));
        if (wantHpf && ! hpfActive)
            sidechainHpf.reset();
        hpfActive = wantHpf;
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

    float CompressorModule::autoMakeupDb (const CompSettings& s) noexcept
    {
        const float ratio = std::max (1.0f, s.ratio);
        return autoMakeupLevelDb - computeGainDb (autoMakeupLevelDb, s.thresholdDb, ratio, s.kneeDb);
    }

    float CompressorModule::transferDb (const CompSettings& s, float inputDb) noexcept
    {
        return computeGainDb (inputDb, s.thresholdDb, std::max (1.0f, s.ratio), s.kneeDb)
               + s.makeupDb + (s.autoMakeup ? autoMakeupDb (s) : 0.0f);
    }

    void CompressorModule::process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        float maxReduction = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            float peak = 0.0f, power = 0.0f, inAbs = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                inAbs = std::max (inAbs, std::abs (channels[ch][i]));
                const float d = hpfActive ? sidechainHpf.process (channels[ch][i], ch) : channels[ch][i];
                peak = std::max (peak, std::abs (d));
                power = std::max (power, d * d);
            }

            float level = peakHold.push (peak);
            if (settings.detector == 1)
            {
                // RMS: mean square (10 ms), scaled so a sine reads its peak level
                rmsState = power + rmsCoeff * (rmsState - power);
                level = std::sqrt (2.0f * rmsState);
            }

            const float inDb = gainToDb (level);
            const float outDb = computeGainDb (inDb, settings.thresholdDb, settings.ratio, settings.kneeDb);
            const float targetReduction = std::max (0.0f, inDb - outDb);

            // Branching one-pole smoother: attack when reduction rises
            const float coeff = targetReduction > envDb ? attackCoeff : releaseCoeff;
            envDb = targetReduction + coeff * (envDb - targetReduction);
            grDb = envDb;

            // Program-dependent (dual) release: short peaks recover with the fast branch,
            // sustained compression charges the slow branch and recovers slowly
            const float dual = dualMix.getNextValue();
            if (dual > 0.0f || dualMix.isSmoothing())
            {
                fastDb = targetReduction + (targetReduction > fastDb ? attackCoeff : fastReleaseCoeff) * (fastDb - targetReduction);
                slowDb = targetReduction + (targetReduction > slowDb ? slowAttackCoeff : slowReleaseCoeff) * (slowDb - targetReduction);
                grDb = envDb + dual * (std::max (fastDb, slowDb) - envDb);
            }
            else
            {
                fastDb = envDb;
                slowDb = 0.0f;
            }

            const float gain = dbToGain (-grDb) * makeup.getNextValue();

            for (int ch = 0; ch < numChannels; ++ch)
                channels[ch][i] *= gain;
            history.add (inAbs, inAbs * gain, grDb);

            maxReduction = std::max (maxReduction, grDb);
        }

        if (! std::isfinite (envDb) || ! std::isfinite (grDb) || ! std::isfinite (rmsState) || ! sidechainHpf.isStateFinite())
            reset();

        gainReductionDb.push (maxReduction);
        currentGainReductionDb.store (grDb, std::memory_order_relaxed);
    }
}
