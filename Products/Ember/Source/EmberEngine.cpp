#include "EmberEngine.h"

namespace ember
{
    namespace
    {
        // 4th-order Butterworth high-pass as two sections
        constexpr double q1 = 0.54119610, q2 = 1.30656296;
        constexpr double shelfQ = 0.70710678;
        constexpr int subBlock = 16;
    }

    float staticReductionDb (float inDb, float thresholdDb) noexcept
    {
        const float over = inDb - thresholdDb;
        const float slope = 1.0f - 1.0f / ratio;
        if (over <= -kneeDb * 0.5f)
            return 0.0f;
        if (over >= kneeDb * 0.5f)
            return slope * over;
        const float x = over + kneeDb * 0.5f;
        return slope * x * x / (2.0f * kneeDb);
    }

    // ---------------------------------------------------------------- Detector
    void Detector::prepare (double sampleRate)
    {
        fs = sampleRate;
        fullRelease = timeConstantCoeff (250.0f, fs);
        envRelease = timeConstantCoeff (30.0f, fs);
        bodyHp.setCoeffs (design::highPass (fs, 250.0, 0.7071));
        bodyLp.setCoeffs (design::lowPass (fs, std::min (3000.0, fs * 0.45), 0.7071));
        reset();
    }

    void Detector::reset()
    {
        hp1.reset();
        hp2.reset();
        bodyHp.reset();
        bodyLp.reset();
        fullEnv = sEnv = bodyEnv = 0.0f;
        indexDb = -60.0f;
        weight = 0.0f;
    }

    float Detector::weightForIndex (float idx) noexcept
    {
        const float t = juce::jlimit (0.0f, 1.0f, (idx - smartNoneDb) / (smartFullDb - smartNoneDb));
        return t * t * (3.0f - 2.0f * t);
    }

    void Detector::setFrequency (float hz) noexcept
    {
        const double f = juce::jlimit (1000.0, fs * 0.45, (double) hz);
        hp1.setCoeffs (design::highPass (fs, f, q1));
        hp2.setCoeffs (design::highPass (fs, f, q2));
    }

    float Detector::process (const float* frame, int numChannels, bool relative, float* band) noexcept
    {
        float peak = 0.0f, full = 0.0f, body = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            full = std::max (full, std::abs (frame[ch]));
            const float b = hp2.process (hp1.process (frame[ch], ch), ch);
            peak = std::max (peak, std::abs (b));
            body = std::max (body, std::abs (bodyLp.process (bodyHp.process (frame[ch], ch), ch)));
            if (band != nullptr)
                band[ch] = b;
        }

        // Sibilance index for smart detection (instant attack, 30 ms release on both sides)
        sEnv = peak > sEnv ? peak : peak + envRelease * (sEnv - peak);
        bodyEnv = body > bodyEnv ? body : body + envRelease * (bodyEnv - body);
        indexDb = gainToDb (sEnv) - gainToDb (bodyEnv);
        weight = weightForIndex (indexDb);

        float inDb = gainToDb (peak);
        if (relative)
        {
            // Instant attack, 250 ms release: the band can never read louder relative to a voice whose
            // onset the envelope has not caught yet
            fullEnv = full > fullEnv ? full : full + fullRelease * (fullEnv - full);
            const float fullDb = gainToDb (fullEnv);
            // Near-silence is never de-essed (a quiet onset would otherwise read as a loud S)
            inDb = fullDb < fullFloorDb ? -200.0f : inDb + referenceDb - fullDb;
        }
        return inDb;
    }

    bool Detector::isFinite() const noexcept
    {
        return hp1.isStateFinite() && hp2.isStateFinite() && bodyHp.isStateFinite() && bodyLp.isStateFinite()
            && std::isfinite (fullEnv) && std::isfinite (sEnv) && std::isfinite (bodyEnv);
    }

    // ------------------------------------------------------------------ Engine
    void Engine::prepare (double sampleRate, int maxBlockSize)
    {
        fs = sampleRate;
        latency = latencyFor (sampleRate);
        delaySize = latency + 1;
        for (auto& d : audioDelay)
            d.assign ((size_t) delaySize, 0.0f);
        envDelay.assign ((size_t) delaySize, 0.0f);
        for (int ch = 0; ch < maxChannels; ++ch)
        {
            dryScratch[ch].assign ((size_t) subBlock, 0.0f);
            wetScratch[ch].assign ((size_t) subBlock, 0.0f);
        }
        juce::ignoreUnused (maxBlockSize);

        detector.prepare (sampleRate);
        freq.reset (sampleRate, 0.05);
        mixSm.reset (sampleRate, 0.02);
        outSm.reset (sampleRate, 0.02);
        bypassSm.reset (sampleRate, 0.02);
        setSettings (settings);
        freq.setCurrentAndTargetValue (settings.freq);
        mixSm.setCurrentAndTargetValue (settings.mix);
        outSm.setCurrentAndTargetValue (dbToGain (settings.outputDb));
        bypassSm.setCurrentAndTargetValue (settings.bypass ? 1.0f : 0.0f);
        reset();
    }

    void Engine::reset()
    {
        detector.reset();
        detector.setFrequency (freq.getCurrentValue());
        listenHp1.setCoeffs (design::highPass (fs, juce::jlimit (1000.0, fs * 0.45, (double) freq.getCurrentValue()), q1));
        listenHp2.setCoeffs (design::highPass (fs, juce::jlimit (1000.0, fs * 0.45, (double) freq.getCurrentValue()), q2));
        listenHp1.reset();
        listenHp2.reset();
        shelf.reset();
        for (auto& d : audioDelay)
            std::fill (d.begin(), d.end(), 0.0f);
        std::fill (envDelay.begin(), envDelay.end(), 0.0f);
        writePos = 0;
        envDb = 0.0f;
        updateShelf (0.0f);
        currentReductionDb.store (0.0f, std::memory_order_relaxed);
    }

    void Engine::setSettings (const Settings& s) noexcept
    {
        settings = s;
        settings.freq = juce::jlimit (1000.0f, 20000.0f, s.freq);
        settings.amount = juce::jlimit (0.0f, 1.0f, s.amount);
        settings.lookaheadMs = juce::jlimit (0.0f, maxLookaheadMs, s.lookaheadMs);
        freq.setTargetValue (settings.freq);
        mixSm.setTargetValue (juce::jlimit (0.0f, 1.0f, settings.mix));
        outSm.setTargetValue (dbToGain (settings.outputDb));
        bypassSm.setTargetValue (settings.bypass ? 1.0f : 0.0f);
        attackCoeff = timeConstantCoeff (std::max (0.01f, settings.attackMs), fs);
        releaseCoeff = timeConstantCoeff (std::max (1.0f, settings.releaseMs), fs);
    }

    void Engine::updateShelf (float reductionDb) noexcept
    {
        shelfDb = reductionDb;
        const double f = juce::jlimit (1000.0, fs * 0.45, (double) freq.getCurrentValue());
        shelf.setCoeffs (design::highShelf (fs, f, shelfQ, -reductionDb));
    }

    void Engine::process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        numChannels = std::min (numChannels, maxChannels);
        if (numChannels <= 0 || numSamples <= 0)
            return;

        inputTap.push (channels, numChannels, numSamples);
        {
            float pk = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                for (int i = 0; i < numSamples; ++i)
                    pk = std::max (pk, std::abs (channels[ch][i]));
            inputPeak.push (pk);
        }

        const int lookahead = juce::jlimit (0, latency, (int) std::lround (settings.lookaheadMs * 0.001 * fs));
        const int envLag = latency - lookahead;
        const float amountScale = settings.amount * 2.0f;
        float maxReduction = 0.0f, outPk = 0.0f, maxDet = -200.0f, weightAtMax = 0.0f;
        float envAt[subBlock];

        for (int start = 0; start < numSamples; start += subBlock)
        {
            const int n = std::min (subBlock, numSamples - start);
            bool freqChanged = false;
            if (freq.isSmoothing())
            {
                freq.skip (n);
                const float f = freq.getCurrentValue();
                detector.setFrequency (f);
                const double fl = juce::jlimit (1000.0, fs * 0.45, (double) f);
                listenHp1.setCoeffs (design::highPass (fs, fl, q1));
                listenHp2.setCoeffs (design::highPass (fs, fl, q2));
                freqChanged = true;
            }

            // 1) detection on the undelayed input, delay audio and envelope
            float blockMaxEnv = 0.0f;
            for (int k = 0; k < n; ++k)
            {
                const int i = start + k;
                float frame[maxChannels] {};
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    frame[ch] = channels[ch][i];
                    audioDelay[(size_t) ch][(size_t) writePos] = frame[ch];
                }

                const float inDb = detector.process (frame, numChannels, settings.relative || settings.smart);
                const float smartWeight = settings.smart ? detector.getSibilanceWeight() : 1.0f;
                if (inDb > maxDet)
                {
                    maxDet = inDb;
                    weightAtMax = smartWeight;
                }
                const float target = std::min (maxReductionDb,
                                               std::min (settings.rangeDb, staticReductionDb (inDb, settings.thresholdDb)) * amountScale * smartWeight);
                const float coeff = target > envDb ? attackCoeff : releaseCoeff;
                envDb = target + coeff * (envDb - target);
                envDelay[(size_t) writePos] = envDb;

                const int readAudio = (writePos - latency + delaySize) % delaySize;
                const int readEnv = (writePos - envLag + delaySize) % delaySize;
                for (int ch = 0; ch < numChannels; ++ch)
                    dryScratch[ch][(size_t) k] = audioDelay[(size_t) ch][(size_t) readAudio];
                envAt[k] = envDelay[(size_t) readEnv];
                blockMaxEnv = std::max (blockMaxEnv, envAt[k]);
                writePos = (writePos + 1) % delaySize;
            }
            maxReduction = std::max (maxReduction, blockMaxEnv);

            // 2) the processed (wet) signal of the delayed audio
            if (settings.mode == 1)
            {
                for (int ch = 0; ch < numChannels; ++ch)
                    for (int k = 0; k < n; ++k)
                        wetScratch[ch][(size_t) k] = dryScratch[ch][(size_t) k] * dbToGain (-envAt[k]);
            }
            else
            {
                if (freqChanged || std::abs (blockMaxEnv - shelfDb) > 0.01f)
                    updateShelf (blockMaxEnv);

                if (shelfDb < 0.001f)
                {
                    // A 0 dB shelf is an identity filter whose state is zero: skipping it is exact
                    shelf.reset();
                    for (int ch = 0; ch < numChannels; ++ch)
                        std::copy_n (dryScratch[ch].data(), n, wetScratch[ch].data());
                }
                else
                {
                    for (int ch = 0; ch < numChannels; ++ch)
                        for (int k = 0; k < n; ++k)
                            wetScratch[ch][(size_t) k] = shelf.process (dryScratch[ch][(size_t) k], ch);
                }
            }

            // 3) listen / mix / output / bypass
            for (int k = 0; k < n; ++k)
            {
                const float mix = mixSm.getNextValue();
                const float outGain = outSm.getNextValue();
                const float byp = bypassSm.getNextValue();
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const float dry = dryScratch[ch][(size_t) k];
                    const float wet = wetScratch[ch][(size_t) k];
                    float y;
                    if (settings.listen == 1)
                        y = listenHp2.process (listenHp1.process (dry, ch), ch);
                    else if (settings.listen == 2)
                        y = (dry - wet) * mix;
                    else
                        y = dry + mix * (wet - dry);
                    y *= outGain;
                    y += byp * (dry - y);
                    channels[ch][start + k] = y;
                    outPk = std::max (outPk, std::abs (y));
                }
            }
        }

        if (! detector.isFinite() || ! shelf.isStateFinite() || ! std::isfinite (envDb)
            || ! listenHp1.isStateFinite() || ! listenHp2.isStateFinite())
        {
            reset();
            for (int ch = 0; ch < numChannels; ++ch)
                std::fill_n (channels[ch], numSamples, 0.0f);
            outPk = 0.0f;
        }

        // Mono input on a stereo bus: nothing to do for the second channel (host copies)
        gainReductionDb.push (settings.bypass ? 0.0f : maxReduction);
        currentReductionDb.store (settings.bypass ? 0.0f : envDb, std::memory_order_relaxed);
        detectorDb.store (std::max (-100.0f, maxDet), std::memory_order_relaxed);
        detectorWeight.store (weightAtMax, std::memory_order_relaxed);
        outputPeak.push (outPk);
        outputTap.push (channels, numChannels, numSamples);
    }
}
