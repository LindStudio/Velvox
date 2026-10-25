#include "RideModule.h"

namespace velvox::dsp
{
    namespace
    {
        constexpr int controlInterval = 32;
        constexpr float hysteresisDb = 3.0f;
        constexpr float transientStartDb = 6.0f;   // fast above syllable level -> transient
    }

    void RideModule::prepare (double sampleRate, int)
    {
        fs = sampleRate;
        bandHpf.setCoeffs (design::highPass (fs, 100.0, 0.7071));
        bandLpf.setCoeffs (design::lowPass (fs, std::min (8000.0, fs * 0.45), 0.7071));
        fastCoeff = timeConstantCoeff (5.0f, fs);
        syllableCoeff = timeConstantCoeff (syllableMs, fs);
        enabled.reset (sampleRate, 0.05);
        enabled.setCurrentAndTargetValue (enabled.getTargetValue());
        latency = (int) std::lround (lookaheadMs * 0.001 * sampleRate);
        fadeInLength = std::max (1, (int) std::lround (0.01 * sampleRate));
        lookahead.prepare (std::max (1, latency));
        lookahead.setDelay (latency);
        reset();
        snapNext = true;
    }

    void RideModule::reset()
    {
        lookahead.reset();
        // The emptied delay line releases silence for `latency` samples: the audio that
        // follows fades in, so joining the chain never starts with a hard edge
        warmupSamples = latency;
        fadeInPos = latency > 0 ? 0 : fadeInLength;
        resetControl();
    }

    void RideModule::applyWarmup (float* const* ch, int numChannels, int n) noexcept
    {
        for (int i = 0; i < n && fadeInPos < fadeInLength; ++i)
        {
            float g = 0.0f;
            if (warmupSamples > 0)
                --warmupSamples;
            else
                g = (float) ++fadeInPos / (float) fadeInLength;
            for (int c = 0; c < numChannels; ++c)
                ch[c][i] *= g;
        }
    }

    void RideModule::resetControl() noexcept
    {
        bandHpf.reset();
        bandLpf.reset();
        fastPower = syllablePower = 0.0f;
        phraseDb = -100.0f;
        phrasePower = 0.0f;
        freezeSamples = 0;
        gainDb = 0.0f;
        rampFrom = rampTo = 1.0f;
        rampPos = controlInterval;
        voiced = false;
        voicedSamples = pauseSamples = 0;
        controlCounter = 0;
        currentGainDb.store (0.0f, std::memory_order_relaxed);
    }

    void RideModule::setSettings (const RideSettings& s) noexcept
    {
        settings = s;
        enabled.setTargetValue (s.enabled ? 1.0f : 0.0f);
        if (snapNext)
        {
            snapNext = false;
            enabled.setCurrentAndTargetValue (enabled.getTargetValue());
        }

        // Transient-robust level: an instantaneous power above the syllable
        // level by more than this factor is clipped before averaging, so a
        // short consonant cannot lift the measured level for the next 100 ms.
        // Preserve 0 % -> +30 dB (no effect), 100 % -> +6 dB.
        const float clampDb = 6.0f + 24.0f * (1.0f - juce::jlimit (0.0f, 1.0f, s.transient));
        transientClamp = std::pow (10.0f, clampDb / 10.0f);
    }

    void RideModule::control() noexcept
    {
        const auto& s = settings;
        const double controlRate = fs / controlInterval;
        const float fastDb = 10.0f * std::log10 (std::max (fastPower, 1.0e-12f));
        const float syllableDb = 10.0f * std::log10 (std::max (syllablePower, 1.0e-12f));

        // Voice activity with hysteresis
        if (voiced && syllableDb < s.silenceDb - hysteresisDb)
            voiced = false;
        else if (! voiced && syllableDb > s.silenceDb)
        {
            voiced = true;
            voicedSamples = 0;
        }
        if (voiced)
        {
            voicedSamples += controlInterval;
            pauseSamples = 0;
        }
        else
            pauseSamples += controlInterval;

        const float response = juce::jlimit (0.0f, 1.0f, s.response);
        const float phrase = juce::jlimit (0.0f, 1.0f, s.phrase);

        // Phrase level: slow average that only moves while singing
        if (voiced)
        {
            const float phraseMs = 150.0f + 2350.0f * phrase;
            const float c = timeConstantCoeff (phraseMs, controlRate);
            phraseDb = phraseDb < -90.0f ? syllableDb : syllableDb + c * (phraseDb - syllableDb);
            phrasePower = std::pow (10.0f, phraseDb * 0.1f);
        }

        float target;
        if (voiced)
        {
            const float level = syllableDb + phrase * (phraseDb - syllableDb);
            target = juce::jlimit (-std::max (0.0f, s.maxCutDb), std::max (0.0f, s.maxBoostDb),
                                   effectiveAmount (s.amount) * (s.targetDb - level));
        }
        else if (pauseSamples < (int) (pauseHoldMs * 0.001 * fs))
        {
            // Gap between words: hold the gain, the next word continues where this one ended
            target = gainDb;
        }
        else
        {
            // Pause: keep reductions, let boosts go - nothing quiet is pulled up
            target = std::min (gainDb, 0.0f);
        }

        // Speeds: lowering is faster than lifting; both scale with Response (log mapping)
        const float downMs = downSlowMs * std::pow (downFastMs / downSlowMs, response);
        const float upMs = upSlowMs * std::pow (upFastMs / upSlowMs, response);
        float coeff = timeConstantCoeff (target < gainDb ? downMs : upMs, controlRate);

        // Transient protection: short peaks over the syllable level freeze the adaptation
        // and keep it frozen a little longer while the burst dies away
        // (not at the start of a word: a word onset is the level the rider has to follow)
        const bool inWord = voicedSamples > (int) (onsetGraceMs * 0.001 * fs);
        const float transientness = inWord ? juce::jlimit (0.0f, 1.0f, (fastDb - syllableDb - transientStartDb) / 6.0f) : 0.0f;
        if (transientness > 0.5f)
            freezeSamples = (int) (0.06 * fs);
        else
            freezeSamples = std::max (0, freezeSamples - controlInterval);
        const float freeze = (freezeSamples > 0 ? 1.0f : transientness) * juce::jlimit (0.0f, 1.0f, s.transient);
        coeff = coeff + (1.0f - coeff) * freeze;

        gainDb = target + coeff * (gainDb - target);

        rampFrom = rampTo;
        rampTo = dbToGain (gainDb * enabled.getCurrentValue());
        rampPos = 0;

        currentGainDb.store (gainDb * enabled.getCurrentValue(), std::memory_order_relaxed);
        detectedLevelDb.store (syllableDb, std::memory_order_relaxed);
        voiceActive.store (voiced, std::memory_order_relaxed);
    }

    void RideModule::process (float* const* ch, int numChannels, int n) noexcept
    {
        const bool active = enabled.getTargetValue() > 0.0f || enabled.isSmoothing();
        if (! active)
        {
            if (wasActive)
                resetControl();
            wasActive = false;
            lookahead.process (ch, numChannels, n);   // constant latency, on or off
            applyWarmup (ch, numChannels, n);
            return;
        }
        if (! wasActive)
        {
            resetControl();
            wasActive = true;
        }

        for (int start = 0; start < n; start += chunkSize)
        {
            const int len = std::min (chunkSize, n - start);
            float* part[maxChannels] {};
            for (int c = 0; c < numChannels; ++c)
                part[c] = ch[c] + start;
            analyse (part, numChannels, len);
            // The gain was found on the undelayed input: applied to the audio lookaheadMs
            // later, it is in place before each word (and each onset) reaches the output
            lookahead.process (part, numChannels, len);
            applyWarmup (part, numChannels, len);
            for (int c = 0; c < numChannels; ++c)
                juce::FloatVectorOperations::multiply (part[c], gains.data(), len);
        }
    }

    void RideModule::analyse (float* const* ch, int numChannels, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            float mid = 0.0f;
            for (int c = 0; c < numChannels; ++c)
                mid += ch[c][i];
            mid /= (float) numChannels;
            const float v = bandLpf.process (bandHpf.process (mid, 0), 0);
            const float p = v * v;
            fastPower = p + fastCoeff * (fastPower - p);

            // Spikes far above the usual singing level do not count for the syllable level
            const float reference = std::max (syllablePower, phrasePower);
            const float limited = reference > 1.0e-10f ? std::min (p, reference * transientClamp) : p;
            syllablePower = limited + syllableCoeff * (syllablePower - limited);

            enabled.getNextValue();
            if (--controlCounter <= 0)
            {
                controlCounter = controlInterval;
                control();
            }

            // Per-sample linear ramp between control points: no zipper, no steps
            const float t = (float) std::min (rampPos, controlInterval) / (float) controlInterval;
            gains[(size_t) i] = rampFrom + (rampTo - rampFrom) * t;
            ++rampPos;
        }
    }
}
