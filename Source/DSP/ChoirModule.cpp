#include "ChoirModule.h"

namespace velvox::dsp
{
    namespace
    {
        constexpr float baseDelayMs = 6.0f;       // nobody sings exactly on top of the lead
        constexpr float maxDriftMs = 4.0f;
        constexpr float maxDriftCents = 6.0f;
        constexpr int controlInterval = 32;
        // Humanize: every voice drifts at its own, unrelated speeds (Hz)
        constexpr double pitchRates[ChoirModule::maxVoices] { 0.23, 0.31, 0.17, 0.41, 0.27, 0.13, 0.37, 0.21 };
        constexpr double timeRates[ChoirModule::maxVoices]  { 0.11, 0.19, 0.29, 0.15, 0.23, 0.09, 0.17, 0.27 };
    }

    const std::array<ChoirModule::VoiceShape, ChoirModule::maxVoices>& ChoirModule::shapes()
    {
        // detune (x Detune), timing (x Timing), pan (x Width), level (dB). Alternating sides, the
        // first pair at the edges; uneven amounts and levels so pairs never mirror each other and
        // no two equal voices beat against each other.
        static const std::array<VoiceShape, maxVoices> s { {
            { -1.00f, 0.35f, -1.00f,  0.0f }, { +0.90f, 0.60f, +1.00f, -0.8f },
            { -0.55f, 0.15f, -0.85f, -1.6f }, { +0.60f, 0.85f, +0.90f, -0.4f },
            { -0.80f, 0.50f, -0.95f, -1.2f }, { +0.35f, 1.00f, +0.80f, -2.0f },
            { -0.25f, 0.25f, -0.70f, -0.6f }, { +0.75f, 0.70f, +0.75f, -1.4f },
        } };
        return s;
    }

    float ChoirModule::stagePan (float shapePan, float width) noexcept
    {
        // Width scales the position; the curve keeps the voices near the sides they stand on
        // (little crosstalk into the other channel) instead of pulling them towards the lead
        const float p = juce::jlimit (-1.0f, 1.0f, shapePan * juce::jlimit (0.0f, 1.0f, width));
        const float a = 1.0f - std::abs (p);
        return std::copysign (1.0f - a * a, p);
    }

    namespace
    {
        // Level of voice v among n: its dB offset, normalised so the voices together keep the
        // energy of equal voices (the Density law sets the overall level)
        float shapedGain (int v, int n) noexcept
        {
            const auto& sh = ChoirModule::shapes();
            double sum = 0.0;
            for (int k = 0; k < n; ++k)
                sum += std::pow (10.0, sh[(size_t) k].gainDb / 10.0);
            return (float) (std::pow (10.0, sh[(size_t) v].gainDb / 20.0) / std::sqrt (sum / n));
        }

        float densityGain (int n, float density) noexcept
        {
            // Density: 0 -> the voices together carry the energy of one voice (soft, behind the
            // lead); 1 -> each voice close to full level
            return std::pow ((float) n, -0.5f + 0.32f * juce::jlimit (0.0f, 1.0f, density));
        }
    }

    ChoirModule::VoicePosition ChoirModule::voicePosition (int voice, const ChoirSettings& s) noexcept
    {
        VoicePosition p;
        const int n = juce::jlimit (2, maxVoices, s.voices);
        if (voice < 0 || voice >= maxVoices)
            return p;
        const auto& sh = shapes()[(size_t) voice];
        p.pan = stagePan (sh.pan, s.width);
        p.depth = juce::jlimit (0.0f, 1.0f, sh.timing * juce::jlimit (0.0f, maxTimingMs, s.timingMs) / maxTimingMs);
        p.level = voice < n ? shapedGain (voice, n) * densityGain (n, s.density) : 0.0f;
        return p;
    }

    void ChoirModule::prepare (double sampleRate, int)
    {
        fs = sampleRate;
        splice = SpliceConfig::forRate (fs);
        line.setSize ((int) ((baseDelayMs + maxTimingMs + maxDriftMs) * 0.001 * fs + splice.headroom()));

        sourceHpf.setCoeffs (design::highPass (fs, 150.0, 0.7071));
        sourceLpf.setCoeffs (design::lowPass (fs, std::min (11000.0, fs * 0.45), 0.7071));

        for (int v = 0; v < maxVoices; ++v)
        {
            auto& vo = voices[(size_t) v];
            vo.pitchRate = pitchRates[v];
            vo.timeRate = timeRates[v];
            vo.level.reset (sampleRate, 0.05);
            vo.delayMs.reset (sampleRate, 0.25);
        }
        for (auto* s : { &enabled, &mix, &width, &voiceGain })
            s->reset (sampleRate, 0.05);

        setSettings (settings);
        reset();
        snapNext = true;
    }

    void ChoirModule::reset()
    {
        line.clear();
        sourceHpf.reset();
        sourceLpf.reset();
        for (int v = 0; v < maxVoices; ++v)
        {
            auto& vo = voices[(size_t) v];
            vo.shifter.reset();
            // Fixed seeds: reproducible renders, independent voices
            vo.pitchWalk.reset (0x3a17 + 7919 * v, v % 2 == 0, 0.5 * fs / vo.pitchRate);
            vo.timeWalk.reset (0x6c02 + 104729 * v, v % 3 == 0, 0.5 * fs / vo.timeRate);
            vo.driftCents = vo.driftMs = 0.0f;
        }
        sumLR = sumLL = sumRR = 0.0;
        updateCounter = 0;
    }

    void ChoirModule::setSettings (const ChoirSettings& s) noexcept
    {
        settings = s;
        const int n = juce::jlimit (2, maxVoices, s.voices);
        const auto& sh = shapes();
        for (int i = 0; i < maxVoices; ++i)
        {
            auto& v = voices[(size_t) i];
            v.level.setTargetValue (i < n ? 1.0f : 0.0f);
            v.gain = i < n ? shapedGain (i, n) : v.gain;
            v.delayMs.setTargetValue (baseDelayMs + juce::jlimit (0.0f, maxTimingMs, s.timingMs) * sh[(size_t) i].timing);
        }

        enabled.setTargetValue (s.enabled ? 1.0f : 0.0f);
        mix.setTargetValue (juce::jlimit (0.0f, 1.0f, s.mix));
        width.setTargetValue (juce::jlimit (0.0f, 1.0f, s.width));
        voiceGain.setTargetValue (densityGain (n, s.density));

        if (snapNext)
        {
            snapNext = false;
            for (auto& v : voices)
            {
                v.level.setCurrentAndTargetValue (v.level.getTargetValue());
                v.delayMs.setCurrentAndTargetValue (v.delayMs.getTargetValue());
            }
            for (auto* sv : { &enabled, &mix, &width, &voiceGain })
                sv->setCurrentAndTargetValue (sv->getTargetValue());
        }
    }

    void ChoirModule::process (float* const* ch, int numChannels, int n) noexcept
    {
        const bool active = enabled.getTargetValue() > 0.0f || enabled.isSmoothing();
        if (! active)
        {
            wasActive = false;
            return;
        }
        if (! wasActive)
        {
            reset();
            wasActive = true;
        }

        const auto& sh = shapes();
        const float detune = juce::jlimit (0.0f, maxDetuneCents, settings.detuneCents);
        const float human = juce::jlimit (0.0f, 1.0f, settings.humanize);
        const double msToSamples = 0.001 * fs;

        for (int i = 0; i < n; ++i)
        {
            // Control rate: drift and pitch-shift speed per voice
            if (updateCounter-- <= 0)
            {
                updateCounter = controlInterval - 1;
                for (int v = 0; v < maxVoices; ++v)
                {
                    auto& vo = voices[(size_t) v];
                    vo.pitchWalk.advance (controlInterval, 0.5 * fs / vo.pitchRate);
                    vo.timeWalk.advance (controlInterval, 0.5 * fs / vo.timeRate);
                    vo.driftCents = human * maxDriftCents * (float) vo.pitchWalk.value();
                    vo.driftMs = human * maxDriftMs * (float) vo.timeWalk.value();

                    const float cents = detune * sh[(size_t) v].detune + vo.driftCents;
                    voiceCents[(size_t) v].store (cents, std::memory_order_relaxed);
                    vo.drift = 1.0 - std::pow (2.0, (double) cents / 1200.0);
                }
            }

            // Mono, band-limited source for the voices (no rumble, no fizz)
            float mono = 0.0f;
            for (int c = 0; c < numChannels; ++c)
                mono += ch[c][i];
            mono /= (float) numChannels;
            line.write (sourceLpf.process (sourceHpf.process (mono, 0), 0));

            const float w = width.getNextValue();
            const float g = voiceGain.getNextValue();
            float wetL = 0.0f, wetR = 0.0f;

            for (int v = 0; v < maxVoices; ++v)
            {
                auto& vo = voices[(size_t) v];
                const float level = vo.level.getNextValue();
                const double base = (vo.delayMs.getNextValue() + vo.driftMs) * msToSamples;
                if (level <= 0.0f)
                    continue;

                const float s = vo.shifter.process (line, std::max (splice.minDelay, base), vo.drift, splice);

                // Constant-power pan
                const float pan = stagePan (sh[(size_t) v].pan, w);
                const float angle = (pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
                const float vg = s * level * g * vo.gain;
                wetL += vg * std::cos (angle);
                wetR += vg * std::sin (angle);
            }
            line.advance();

            const float m = mix.getNextValue();
            const float on = enabled.getNextValue();
            const float wet[2] { wetL, wetR };
            float outL = 0.0f, outR = 0.0f;
            for (int c = 0; c < numChannels; ++c)
            {
                const float dry = ch[c][i];
                const float w2 = numChannels > 1 ? wet[c] : 0.7071f * (wetL + wetR);
                const float processed = dry * (1.0f - m) + w2 * m;
                ch[c][i] = dry + on * (processed - dry);
                (c == 0 ? outL : outR) = ch[c][i];
            }
            if (numChannels > 1)
            {
                sumLR = sumLR * 0.99995 + outL * outR;
                sumLL = sumLL * 0.99995 + outL * outL;
                sumRR = sumRR * 0.99995 + outR * outR;
            }
        }

        const float w = width.getCurrentValue(), g = voiceGain.getCurrentValue();
        for (int v = 0; v < maxVoices; ++v)
        {
            const auto& vo = voices[(size_t) v];
            voicePan[(size_t) v].store (stagePan (sh[(size_t) v].pan, w), std::memory_order_relaxed);
            voiceLevel[(size_t) v].store (vo.level.getCurrentValue() * g * vo.gain, std::memory_order_relaxed);
        }

        if (sumLL > 1.0e-9 && sumRR > 1.0e-9)
            outputCorrelation.store ((float) (sumLR / std::sqrt (sumLL * sumRR)), std::memory_order_relaxed);
    }
}
