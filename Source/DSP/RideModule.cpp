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
        sibilantHpf.setCoeffs (design::highPass (fs, std::min (4500.0, fs * 0.3), 0.7071));
        sibilantLpf.setCoeffs (design::lowPass (fs, std::min (12000.0, fs * 0.45), 0.7071));
        bodyLpf.setCoeffs (design::lowPass (fs, 2000.0, 0.7071));
        fastCoeff = timeConstantCoeff (5.0f, fs);
        syllableCoeff = timeConstantCoeff (syllableMs, fs);
        enabled.reset (sampleRate, 0.05);
        enabled.setCurrentAndTargetValue (enabled.getTargetValue());
        latency = (int) std::lround (lookaheadMs * 0.001 * sampleRate);
        fadeInLength = std::max (1, (int) std::lround (0.01 * sampleRate));
        powerDelay.assign ((size_t) std::max (1, latency), 0.0f);
        segCoeff = timeConstantCoeff (10.0f, fs);
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
        std::fill (powerDelay.begin(), powerDelay.end(), 0.0f);
        powerPos = 0;
        outPos = 0;
        samplePosition.store (0, std::memory_order_relaxed);
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
        fastPower = syllablePower = sibilantPower = bodyPower = 0.0f;
        sibilantHpf.reset();
        sibilantLpf.reset();
        bodyLpf.reset();
        phraseDb = -100.0f;
        phrasePower = 0.0f;
        freezeSamples = 0;
        gainDb = 0.0f;
        rampFrom = rampTo = 1.0f;
        rampPos = controlInterval;
        voiced = false;
        voicedSamples = pauseSamples = gapRecentSamples = dipRun = 0;
        phraseWarmup = (int) (0.1 * fs);
        controlCounter = 0;
        segPower = 0.0f;
        segOpen = waitRise = false;
        segLen = segLoudLen = dipSamples = segTick = 0;
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
        // A rise out of a gap between words (fast level well under the syllable level shortly
        // before) is a word onset: the onset grace below restarts, so it is followed, not frozen
        if (fastDb < syllableDb - wordGapDb)
            gapRecentSamples = (int) (0.15 * fs);
        else if (gapRecentSamples > 0)
        {
            gapRecentSamples -= controlInterval;
            if (fastDb > syllableDb + transientStartDb)
            {
                voicedSamples = 0;
                gapRecentSamples = 0;
            }
        }
        if (voiced)
            voicedSamples += controlInterval;
        pauseSamples += controlInterval;   // reset below while singing

        const float response = juce::jlimit (0.0f, 1.0f, s.response);
        const float phrase = juce::jlimit (0.0f, 1.0f, s.phrase);

        // Short dips well under the phrase level (gaps and breaths between words, < pauseHoldMs)
        // are not singing: the gain holds and the phrase level ignores them. A dip that lasts
        // longer is a quieter passage and is ridden normally.
        const bool dip = phraseDb > -90.0f && fastDb < phraseDb - wordGapDb;
        dipRun = dip ? dipRun + controlInterval : 0;
        // S sounds (4.5 .. 12 kHz far above the body of the voice, 100 Hz .. 2 kHz) are not words to
        // be lifted: the gain holds through them, so the rider never pushes sibilance up
        const bool sibilant = sibilantPower > sibilantRatio * bodyPower;
        const bool singing = voiced && ! sibilant && ! (dip && dipRun < (int) (pauseHoldMs * 0.001 * fs));
        if (singing)
            pauseSamples = 0;

        // Phrase level: slow average that only moves while singing. It starts at the level of
        // the first word (not at the threshold crossing of its onset).
        if (singing)
        {
            const float phraseMs = 150.0f + 2350.0f * phrase;
            const float c = timeConstantCoeff (phraseMs, controlRate);
            if (phraseDb < -90.0f || phraseWarmup > 0)
            {
                phraseDb = std::max (phraseDb, syllableDb);
                phraseWarmup = std::max (0, phraseWarmup - controlInterval);
            }
            else
                phraseDb = syllableDb + c * (phraseDb - syllableDb);
            phrasePower = std::pow (10.0f, phraseDb * 0.1f);
        }
        else if (! voiced)
            phraseWarmup = (int) (0.1 * fs);

        float target;
        if (singing)
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
            outPos += n;
            samplePosition.store (outPos, std::memory_order_relaxed);
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

            // Word bars: the band power that reaches the output now (delayed like the audio)
            for (int i = 0; i < len; ++i)
            {
                float delayed = powers[(size_t) i];
                if (latency > 0)
                {
                    std::swap (delayed, powerDelay[(size_t) powerPos]);
                    if (++powerPos == latency)
                        powerPos = 0;
                }
                segmentSample (delayed, gains[(size_t) i]);
            }
        }
        samplePosition.store (outPos, std::memory_order_relaxed);
    }

    void RideModule::segmentSample (float p, float g) noexcept
    {
        segPower = p + segCoeff * (segPower - p);
        if (segOpen)
        {
            sumBefore += p;
            sumAfter += (double) p * g * g;
            ++segLen;
        }
        ++outPos;
        if (++segTick < controlInterval)
            return;
        segTick = 0;

        const float level = 10.0f * std::log10 (std::max (segPower, 1.0e-12f));
        const float silence = settings.silenceDb;

        if (! segOpen)
        {
            if (level < silence - hysteresisDb)
                waitRise = false;
            dipMinDb = std::min (dipMinDb, level);
            if (level > silence && (! waitRise || level > dipMinDb + segmentRiseDb))
            {
                segOpen = true;
                waitRise = false;
                segStart = outPos - controlInterval;
                segLen = segLoudLen = dipSamples = 0;
                sumBefore = sumAfter = loudBefore = loudAfter = 0.0;
                segPeakDb = level;
            }
            return;
        }

        segPeakDb = std::max (segPeakDb, level);
        if (level < silence - hysteresisDb)
        {
            closeSegment();
            waitRise = false;
            return;
        }
        if (level < segPeakDb - segmentGapDb)
        {
            dipSamples += controlInterval;
            if (dipSamples >= (int) (segmentGapMs * 0.001 * fs))
            {
                closeSegment();
                waitRise = true;
                dipMinDb = level;
            }
            return;
        }

        dipSamples = 0;
        segLoudLen = segLen;
        loudBefore = sumBefore;
        loudAfter = sumAfter;
        if (segLen >= (int) (segmentMaxMs * 0.001 * fs))
        {
            closeSegment();   // sustained note: the next bar continues seamlessly
            segOpen = true;
            segStart = outPos;
            segLen = segLoudLen = 0;
            sumBefore = sumAfter = loudBefore = loudAfter = 0.0;
            segPeakDb = level;
        }
    }

    void RideModule::closeSegment() noexcept
    {
        segOpen = false;
        if (segLoudLen <= 0)
            return;
        // The bar ends where the word was last at its level (not after the decay into the gap)
        const auto toDb = [] (double sum, int n) { return (float) (10.0 * std::log10 (std::max (sum / n, 1.0e-12))); };
        segments.push ({ segStart, segLoudLen, toDb (loudBefore, segLoudLen), toDb (loudAfter, segLoudLen) });
    }

    void RideModule::analyse (float* const* ch, int numChannels, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            float mid = 0.0f;
            for (int c = 0; c < numChannels; ++c)
                mid += ch[c][i];
            mid /= (float) numChannels;
            const float hp = bandHpf.process (mid, 0);
            const float v = bandLpf.process (hp, 0);
            const float body = bodyLpf.process (hp, 0);
            bodyPower = body * body + fastCoeff * (bodyPower - body * body);
            const float p = v * v;
            powers[(size_t) i] = p;
            fastPower = p + fastCoeff * (fastPower - p);
            const float hf = sibilantLpf.process (sibilantHpf.process (mid, 0), 0);
            sibilantPower = hf * hf + fastCoeff * (sibilantPower - hf * hf);

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
