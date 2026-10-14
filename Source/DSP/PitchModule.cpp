#include "PitchModule.h"

#if defined (_MSC_VER)
 #pragma warning (push, 0) // third-party header: keep our warning level clean
#endif
#include "signalsmith-stretch.h"
#if defined (_MSC_VER)
 #pragma warning (pop)
#endif

namespace velvox::dsp
{
    namespace
    {
        // 120 ms window, 1/8 interval: worst case ~7 ct error over +-12 st
        constexpr double blockSeconds = 0.120;
        constexpr double intervalSeconds = 0.015;
        constexpr double formantBaseHz = 220.0; // middle of a typical vocal register
    }

    struct PitchModule::Engine
    {
        signalsmith::stretch::SignalsmithStretch<float> stretch { 0x5eed }; // fixed seed: reproducible
    };

    PitchModule::PitchModule()
    {
        engine = std::make_unique<Engine>();
    }

    PitchModule::~PitchModule() = default;

    void PitchModule::prepare (double sampleRate, int maxBlockSize)
    {
        fs = sampleRate;

        auto& st = engine->stretch;
        st.configure (maxChannels, (int) (blockSeconds * sampleRate), (int) (intervalSeconds * sampleRate));
        st.setFormantBase ((float) (formantBaseHz / sampleRate));

        dryDelay.prepare (getLatencySamples() + 1);
        dry.setSize (maxChannels, maxBlockSize, false, true, false);
        wet.setSize (maxChannels, maxBlockSize, false, true, false);

        mix.reset (sampleRate, 0.03);
        mix.setCurrentAndTargetValue (mix.getTargetValue());
        lastTranspose = lastFormant = 1.0e9f;
        activate();
    }

    int PitchModule::getLatencySamples() const noexcept
    {
        return engine->stretch.inputLatency() + engine->stretch.outputLatency();
    }

    void PitchModule::activate() noexcept
    {
        engine->stretch.reset();
        dryDelay.setDelay (getLatencySamples());
        dryDelay.reset();
        lastTranspose = lastFormant = 1.0e9f; // re-apply settings to the fresh engine
    }

    void PitchModule::reset()
    {
        engine->stretch.reset();
        dryDelay.reset();
    }

    void PitchModule::setSettings (const PitchSettings& s) noexcept
    {
        settings = s;
        mix.setTargetValue (juce::jlimit (0.0f, 1.0f, s.mix));
    }

    void PitchModule::process (float* const* ch, int numChannels, int n) noexcept
    {
        auto& stretch = engine->stretch;

        const float transpose = juce::jlimit (-24.0f, 24.0f, settings.semitones) + juce::jlimit (-100.0f, 100.0f, settings.cents) * 0.01f;
        const float formant = juce::jlimit (-12.0f, 12.0f, settings.formantShift);
        if (transpose != lastTranspose || formant != lastFormant || settings.preserveFormants != lastPreserve)
        {
            stretch.setTransposeSemitones (transpose);
            // compensatePitch = keep the voice's formants where they were (natural);
            // otherwise formants move with the pitch (classic "chipmunk").
            stretch.setFormantSemitones (formant, settings.preserveFormants);
            lastTranspose = transpose;
            lastFormant = formant;
            lastPreserve = settings.preserveFormants;
        }

        for (int c = 0; c < numChannels; ++c)
            dry.copyFrom (c, 0, ch[c], n);
        dryDelay.process (dry.getArrayOfWritePointers(), numChannels, n);

        stretch.process (ch, n, wet.getArrayOfWritePointers(), n);

        for (int i = 0; i < n; ++i)
        {
            const float m = mix.getNextValue();
            for (int c = 0; c < numChannels; ++c)
            {
                const float d = dry.getSample (c, i);
                ch[c][i] = d + (wet.getSample (c, i) - d) * m;
            }
        }

        if (! std::isfinite (ch[0][0]) || ! std::isfinite (ch[numChannels - 1][n - 1]))
            reset();
    }
}
