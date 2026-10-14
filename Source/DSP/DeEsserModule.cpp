#include "DeEsserModule.h"
#include "CompressorModule.h"

namespace velvox::dsp
{
    namespace
    {
        constexpr double butterworthQ = 0.70710678;
        constexpr int updateInterval = 16;
    }

    void DeEsserModule::prepare (double sampleRate, int)
    {
        fs = sampleRate;
        freq.reset (sampleRate, 0.05);
        freq.setCurrentAndTargetValue (settings.freq);
        attackCoeff = timeConstantCoeff (0.5f, fs);
        releaseCoeff = timeConstantCoeff (60.0f, fs);
        reset();
    }

    void DeEsserModule::reset()
    {
        detector.setCoeffs (design::highPass (fs, freq.getCurrentValue(), butterworthQ));
        detector.reset();
        shelf.reset();
        envDb = 0.0f;
        updateShelf (0.0f);
    }

    void DeEsserModule::setSettings (const DeEssSettings& s) noexcept
    {
        settings = s;
        freq.setTargetValue (s.freq);
    }

    void DeEsserModule::updateShelf (float reductionDb) noexcept
    {
        shelfDb = reductionDb;
        shelf.setCoeffs (design::highShelf (fs, freq.getCurrentValue(), butterworthQ, -reductionDb));
    }

    void DeEsserModule::process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        float maxReduction = 0.0f;

        for (int start = 0; start < numSamples; start += updateInterval)
        {
            const int n = std::min (updateInterval, numSamples - start);
            bool freqChanged = false;

            if (freq.isSmoothing())
            {
                freq.skip (n);
                detector.setCoeffs (design::highPass (fs, freq.getCurrentValue(), butterworthQ));
                freqChanged = true;
            }

            // 1) Detector over this sub-block
            float wideGains[updateInterval];
            for (int i = start; i < start + n; ++i)
            {
                float peak = 0.0f;
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const float band = detector.process (channels[ch][i], ch);
                    peak = std::max (peak, std::abs (band));
                    if (settings.listen)
                        channels[ch][i] = band; // solo the detection band
                }

                const float inDb = gainToDb (peak);
                const float outDb = CompressorModule::computeGainDb (inDb, settings.thresholdDb, ratio, kneeDb);
                const float targetReduction = std::min (settings.rangeDb, std::max (0.0f, inDb - outDb));

                const float coeff = targetReduction > envDb ? attackCoeff : releaseCoeff;
                envDb = targetReduction + coeff * (envDb - targetReduction);
                wideGains[i - start] = dbToGain (-envDb);
            }

            maxReduction = std::max (maxReduction, envDb);

            if (settings.listen)
                continue;

            if (settings.mode == 1)
            {
                // Wideband: the whole voice dips briefly on each S
                for (int ch = 0; ch < numChannels; ++ch)
                    for (int i = start; i < start + n; ++i)
                        channels[ch][i] *= wideGains[i - start];
                continue;
            }

            // 2) Dynamic shelf follows the envelope
            if (freqChanged || std::abs (envDb - shelfDb) > 0.01f)
                updateShelf (envDb);

            if (shelfDb < 0.001f)
            {
                // A 0 dB shelf is an identity filter whose state is zero, so
                // skipping it is exact and the next onset starts consistently.
                shelf.reset();
                continue;
            }

            for (int ch = 0; ch < numChannels; ++ch)
                for (int i = start; i < start + n; ++i)
                    channels[ch][i] = shelf.process (channels[ch][i], ch);
        }

        if (! detector.isStateFinite() || ! shelf.isStateFinite() || ! std::isfinite (envDb))
            reset();

        gainReductionDb.push (maxReduction);
    }
}
