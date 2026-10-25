#include "PitchModule.h"

namespace velvox::dsp
{
    void PitchModule::prepare (double sampleRate, int maxBlockSize)
    {
        fs = sampleRate;
        maxBlock = std::max (1, maxBlockSize);

        tracker.prepare (sampleRate);
        shifter.prepare (sampleRate, maxBlock);
        corrector.prepare (sampleRate, shifter.getLatency(), maxBlock);

        dryDelay.prepare (getLatencySamples() + 1);
        dry.setSize (maxChannels, maxBlock, false, true, false);
        monoIn.assign ((size_t) maxBlock, 0.0f);
        period.assign ((size_t) maxBlock, 0.0f);

        mix.reset (sampleRate, 0.03);
        mix.setCurrentAndTargetValue (mix.getTargetValue());
        setSettings (settings);
        activate();
    }

    void PitchModule::activate() noexcept
    {
        tracker.reset();
        shifter.reset();
        corrector.reset();
        // a module that (re)joins the chain starts at its Mix (the chain fades it in)
        mix.setCurrentAndTargetValue (mix.getTargetValue());
        dryDelay.setDelay (getLatencySamples());
        dryDelay.reset();
    }

    void PitchModule::reset()
    {
        activate();
    }

    void PitchModule::setSettings (const PitchSettings& s) noexcept
    {
        settings = s;
        mix.setTargetValue (juce::jlimit (0.0f, 1.0f, s.mix));

        const float transpose = juce::jlimit (-24.0f, 24.0f, s.semitones) + juce::jlimit (-100.0f, 100.0f, s.cents) * 0.01f;
        const float ratio = std::pow (2.0f, transpose / 12.0f);
        const float formant = std::pow (2.0f, juce::jlimit (-12.0f, 12.0f, s.formantShift) / 12.0f);
        shifter.setRatio (ratio);
        // Preserve: the input's formants stay where they were (times the creative shift);
        // otherwise they move with the pitch (classic "chipmunk"), plus the creative shift
        corrector.setWarp (ratio, s.preserveFormants ? formant : ratio * formant);
    }

    void PitchModule::process (float* const* ch, int numChannels, int n) noexcept
    {
        numChannels = std::min (numChannels, (int) maxChannels);
        for (int start = 0; start < n; start += maxBlock)
        {
            const int len = std::min (maxBlock, n - start);
            float* view[maxChannels] {};
            for (int c = 0; c < numChannels; ++c)
                view[c] = ch[c] + start;
            processChunk (view, numChannels, len);
        }
    }

    void PitchModule::processChunk (float* const* ch, int numChannels, int n) noexcept
    {
        for (int c = 0; c < numChannels; ++c)
            dry.copyFrom (c, 0, ch[c], n);
        dryDelay.process (dry.getArrayOfWritePointers(), numChannels, n);

        const float norm = 1.0f / (float) numChannels;
        for (int i = 0; i < n; ++i)
        {
            float m = 0.0f;
            for (int c = 0; c < numChannels; ++c)
                m += ch[c][i];
            m *= norm;
            if (! std::isfinite (m)) m = 0.0f;
            monoIn[(size_t) i] = m;
            tracker.push (m);
            period[(size_t) i] = tracker.getPeriod();
        }

        shifter.process (ch, numChannels, n, period.data());
        corrector.process (monoIn.data(), ch, numChannels, n, period.data());

        for (int i = 0; i < n; ++i)
        {
            const float m = mix.getNextValue();
            for (int c = 0; c < numChannels; ++c)
            {
                const float d = dry.getSample (c, i);
                ch[c][i] = d + (ch[c][i] - d) * m;
            }
        }

        if (! std::isfinite (ch[0][0]) || ! std::isfinite (ch[numChannels - 1][n - 1]))
            reset();
    }
}
