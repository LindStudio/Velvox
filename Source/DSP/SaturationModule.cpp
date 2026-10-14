#include "SaturationModule.h"

namespace velvox::dsp
{
    namespace
    {
        constexpr float bias = 0.2f;     // adds even harmonics
        constexpr int updateInterval = 16;
    }

    float SaturationModule::shape (float x, float drive, int character) noexcept
    {
        const float u = drive * x;
        switch (character)
        {
            case 1: // Tape: symmetric and gentle (odd harmonics, soft knee)
                return 0.63661977f * std::atan (1.5707963f * u);

            case 2: // Tube: stronger asymmetry (more even harmonics)
            {
                constexpr float tubeBias = 0.5f;
                static const float tanhTubeBias = std::tanh (tubeBias);
                return std::tanh (u + tubeBias) - tanhTubeBias;
            }

            case 3: // Hard: cubic soft clipper with a firm ceiling
            {
                const float c = juce::jlimit (-1.0f, 1.0f, u);
                return 1.5f * (c - c * c * c / 3.0f);
            }

            default: // Warm: mildly asymmetric tanh (v0.1 curve)
            {
                static const float tanhBias = std::tanh (bias);
                return std::tanh (u + bias) - tanhBias;
            }
        }
    }

    // Keeps perceived level roughly constant: matches the RMS of a -12 dBFS sine
    // before and after the curve.
    float SaturationModule::compensationFor (float drive, int character) noexcept
    {
        constexpr int points = 48;
        constexpr float amp = 0.25f;
        double inSum = 0.0, outSum = 0.0, outMean = 0.0;
        float out[points];

        for (int i = 0; i < points; ++i)
        {
            const float x = amp * std::sin (6.2831853f * (float) i / (float) points);
            out[i] = shape (x, drive, character);
            outMean += out[i];
            inSum += (double) x * x;
        }

        outMean /= points;
        for (int i = 0; i < points; ++i)
            outSum += (out[i] - outMean) * (out[i] - outMean);

        return outSum > 1.0e-12 ? (float) std::sqrt (inSum / outSum) : 1.0f;
    }

    void SaturationModule::prepare (double sampleRate, int maxBlockSize)
    {
        oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
            (size_t) maxChannels, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, false, true);
        oversampler->initProcessing ((size_t) maxBlockSize);
        latency = (int) std::lround (oversampler->getLatencyInSamples());

        dry.setSize (maxChannels, maxBlockSize, false, true, false);
        dryDelay.prepare (std::max (1, latency));
        dryDelay.setDelay (latency);

        driveDb.reset (sampleRate, 0.05);
        outGain.reset (sampleRate, 0.05);
        amount.reset (sampleRate, 0.03);
        amount.setCurrentAndTargetValue (amountTarget);

        driveLin = dbToGain (driveDb.getTargetValue());
        compensation = compensationFor (driveLin, character);
        compensationCharacter = character;
        dcR = (float) (1.0 - 2.0 * 3.14159265 * 10.0 / sampleRate);
        reset();
    }

    void SaturationModule::reset()
    {
        if (oversampler != nullptr)
            oversampler->reset();
        dryDelay.reset();
        dcX.fill (0.0f);
        dcY.fill (0.0f);
        needsReset = false;
    }

    void SaturationModule::setSettings (const SatSettings& s) noexcept
    {
        character = juce::jlimit (0, numCharacters - 1, s.character);
        driveDb.setTargetValue (s.driveDb);
        outGain.setTargetValue (dbToGain (s.outputDb));
        amountTarget = s.enabled ? juce::jlimit (0.0f, 1.0f, s.mix) : 0.0f;
        amount.setTargetValue (amountTarget);
    }

    void SaturationModule::process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        // Aligned dry copy
        for (int ch = 0; ch < numChannels; ++ch)
            dry.copyFrom (ch, 0, channels[ch], numSamples);
        dryDelay.process (dry.getArrayOfWritePointers(), numChannels, numSamples);

        if (amountTarget <= 0.0f && ! amount.isSmoothing())
        {
            for (int ch = 0; ch < numChannels; ++ch)
                juce::FloatVectorOperations::copy (channels[ch], dry.getReadPointer (ch), numSamples);
            needsReset = true;
            return;
        }

        if (needsReset)
        {
            oversampler->reset();
            dcX.fill (0.0f);
            dcY.fill (0.0f);
            needsReset = false;
        }

        // --- Wet path, 2x oversampled ---
        juce::dsp::AudioBlock<float> block (channels, (size_t) numChannels, (size_t) numSamples);
        auto up = oversampler->processSamplesUp (block);
        const int upSamples = (int) up.getNumSamples();
        const int factor = upSamples / std::max (1, numSamples);

        for (int start = 0; start < numSamples; start += updateInterval)
        {
            const int n = std::min (updateInterval, numSamples - start);

            if (driveDb.isSmoothing() || character != compensationCharacter)
            {
                driveDb.skip (n);
                driveLin = dbToGain (driveDb.getCurrentValue());
                compensation = compensationFor (driveLin, character);
                compensationCharacter = character;
            }

            const float d = driveLin;
            const float c = compensation;

            for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
            {
                auto* s = up.getChannelPointer (ch);
                for (int i = start * factor; i < (start + n) * factor; ++i)
                    s[i] = shape (s[i], d, character) * c;
            }
        }

        oversampler->processSamplesDown (block);

        // DC blocker, output gain, dry/wet
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = outGain.getNextValue();
            const float a = amount.getNextValue();

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float x = channels[ch][i];
                const float y = x - dcX[(size_t) ch] + dcR * dcY[(size_t) ch];
                dcX[(size_t) ch] = x;
                dcY[(size_t) ch] = y;

                const float wet = y * g;
                const float d = dry.getSample (ch, i);
                channels[ch][i] = d + (wet - d) * a;
            }
        }

        for (auto& v : dcY)
            if (! std::isfinite (v))
                reset();
    }
}
