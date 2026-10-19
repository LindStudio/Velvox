#include "SmashModule.h"

namespace velvox::dsp
{
    namespace
    {
        constexpr float kneeDb = 8.0f;
        constexpr float expanderStartDb = 6.0f;  // full lift down to 6 dB under the threshold ...
        constexpr float expanderSpanDb = 12.0f;  // ... then it runs out over 12 dB
        constexpr float liftFloorDb = -46.0f;    // detector peak level below which nothing is lifted
        constexpr float detectorHpfHz = 120.0f;
    }

    float SmashModule::gainComputerDb (float x, float amount) noexcept
    {
        const float T = thresholdFor (amount), R = ratioFor (amount), W = kneeDb;
        float y;
        if (2.0f * (x - T) < -W)
            y = x;
        else if (2.0f * std::abs (x - T) <= W)
            y = x + (1.0f / R - 1.0f) * (x - T + W * 0.5f) * (x - T + W * 0.5f) / (2.0f * W);
        else
            y = T + (x - T) / R;
        return y - x;
    }

    float SmashModule::wetGainDb (float envDb, float amount) noexcept
    {
        const float makeup = makeupFor (amount);
        float g = gainComputerDb (envDb, amount);

        // Below the threshold the lift of the squashed path runs out: back to
        // 0 dB at 18 dB under the threshold and further down from there, so
        // breaths and room noise are never pulled up by the makeup gain
        const float expandFrom = thresholdFor (amount) - expanderStartDb;
        if (envDb < expandFrom)
            g -= std::min (makeup + 24.0f, (expandFrom - envDb) * makeup / expanderSpanDb);

        // Absolute floor: near the noise floor the squashed path may never be
        // louder than the original (0 dB lift at -46 dBFS peak, 2 dB less per dB below)
        g = std::min (g, (envDb - liftFloorDb) * 2.0f - makeup);
        return g + makeup;
    }

    void SmashModule::prepare (double sampleRate, int)
    {
        fs = sampleRate;
        detectorHpf.setCoeffs (design::highPass (fs, detectorHpfHz, 0.7071));
        for (auto* s : { &enabled, &blend, &outputGain, &amountSmoothed })
        {
            s->reset (sampleRate, 0.03);
            s->setCurrentAndTargetValue (s->getTargetValue());
        }
        // Level detector: short RMS-like window, independent of attack / release
        levelCoeff = timeConstantCoeff (5.0f, fs);
        setSettings (settings);
        appliedTone = 1000.0f; // force a coefficient update
        updateTone();
        reset();
        snapNext = true;       // the first real settings are taken over without a fade
    }

    void SmashModule::reset()
    {
        detectorHpf.reset();
        lowShelf.reset();
        highShelf.reset();
        envDb = -120.0f;
        grDb = 0.0f;
    }

    void SmashModule::updateTone() noexcept
    {
        const float t = juce::jlimit (-1.0f, 1.0f, settings.tone);
        if (std::abs (t - appliedTone) < 1.0e-4f)
            return;
        appliedTone = t;
        // Tilt around ~1 kHz: warmer = more body, softer top; brighter = presence and air
        lowShelf.setCoeffs (design::lowShelf (fs, 300.0, 0.7071, -4.0 * t));
        highShelf.setCoeffs (design::highShelf (fs, 3000.0, 0.7071, 5.0 * t));
    }

    void SmashModule::setSettings (const SmashSettings& s) noexcept
    {
        settings = s;
        enabled.setTargetValue (s.enabled ? 1.0f : 0.0f);
        blend.setTargetValue (juce::jlimit (0.0f, 1.0f, s.blend));
        outputGain.setTargetValue (dbToGain (juce::jlimit (-24.0f, 24.0f, s.outputDb)));
        amountSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, s.amount));
        attackCoeff = timeConstantCoeff (juce::jlimit (0.05f, 100.0f, s.attackMs), fs);
        releaseCoeff = timeConstantCoeff (juce::jlimit (5.0f, 2000.0f, s.releaseMs), fs);
        updateTone();

        if (snapNext)
        {
            snapNext = false;
            for (auto* v : { &enabled, &blend, &outputGain, &amountSmoothed })
                v->setCurrentAndTargetValue (v->getTargetValue());
        }
    }

    void SmashModule::process (float* const* ch, int numChannels, int n) noexcept
    {
        const bool active = enabled.getTargetValue() > 0.0f || enabled.isSmoothing();
        if (! active)
        {
            wasActive = false;
            blendLevelDb.store (-100.0f, std::memory_order_relaxed);
            return;
        }
        if (! wasActive)
        {
            reset();
            wasActive = true;
        }

        float maxGr = 0.0f, blendPeak = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            // Stereo-linked detector on the high-passed signal (plosives and bass do not pump it)
            float det = 0.0f;
            for (int c = 0; c < numChannels; ++c)
                det = std::max (det, std::abs (detectorHpf.process (ch[c][i], c)));
            const float levelDb = gainToDb (det);
            envDb = levelDb > envDb ? levelDb : levelDb + levelCoeff * (envDb - levelDb);

            // Static curve, smoothed with attack / release
            const float amt = amountSmoothed.getNextValue();
            const float makeup = makeupFor (amt);
            const float compression = gainComputerDb (envDb, amt);
            const float target = wetGainDb (envDb, amt) - makeup;
            grDb = target + (target < grDb ? attackCoeff : releaseCoeff) * (grDb - target);

            const float wetGain = dbToGain (grDb + makeup);
            const float b = blend.getNextValue();
            const float norm = 1.0f / (1.0f + 0.6f * b);
            const float on = enabled.getNextValue();
            const float out = outputGain.getNextValue();

            for (int c = 0; c < numChannels; ++c)
            {
                const float dry = ch[c][i];
                float wet = highShelf.process (lowShelf.process (dry * wetGain, c), c);
                // The squashed path never adds hard overs
                if (std::abs (wet) > 0.9f)
                    wet = std::copysign (0.9f + 0.1f * std::tanh ((std::abs (wet) - 0.9f) * 10.0f), wet);

                const float added = b * wet;
                blendPeak = std::max (blendPeak, std::abs (added));
                const float processed = (dry + added) * norm * out;
                ch[c][i] = dry + on * (processed - dry);
            }

            maxGr = std::max (maxGr, -compression); // meter: compression only, not the breath expander
        }

        gainReductionDb.push (maxGr);
        blendLevelDb.store (gainToDb (blendPeak), std::memory_order_relaxed);
    }
}
