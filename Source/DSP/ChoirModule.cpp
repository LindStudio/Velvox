#include "ChoirModule.h"

namespace velvox::dsp
{
    namespace
    {
        constexpr double twoPi = 6.283185307179586;
        constexpr float baseDelayMs = 6.0f;       // nobody sings exactly on top of the lead
        constexpr float windowMs = 40.0f;         // pitch shifter grain length
        constexpr float maxDriftMs = 4.0f;
        constexpr float maxDriftCents = 6.0f;
        constexpr int controlInterval = 32;
    }

    const std::array<ChoirModule::VoiceShape, ChoirModule::maxVoices>& ChoirModule::shapes()
    {
        // detune (x Detune), timing (x Timing), pan (x Width). Alternating sides,
        // uneven amounts: pairs never mirror each other exactly.
        static const std::array<VoiceShape, maxVoices> s { {
            { -1.00f, 0.35f, -0.95f }, { +0.90f, 0.60f, +0.90f },
            { -0.55f, 0.15f, -0.45f }, { +0.60f, 0.85f, +0.55f },
            { -0.80f, 0.50f, -0.75f }, { +0.35f, 1.00f, +0.70f },
            { -0.25f, 0.25f, -0.20f }, { +0.75f, 0.70f, +0.25f },
        } };
        return s;
    }

    void ChoirModule::prepare (double sampleRate, int)
    {
        fs = sampleRate;
        windowSamples = (float) (windowMs * 0.001 * sampleRate);

        const double longest = (baseDelayMs + maxTimingMs + maxDriftMs + windowMs) * 0.001 * sampleRate + 8.0;
        int size = 1;
        while (size < (int) longest)
            size <<= 1;
        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;

        sourceHpf.setCoeffs (design::highPass (fs, 150.0, 0.7071));
        sourceLpf.setCoeffs (design::lowPass (fs, std::min (11000.0, fs * 0.45), 0.7071));

        juce::Random rnd (1234);
        for (auto& v : voices)
        {
            // Slow, unrelated drift rates (0.05 .. 0.3 Hz)
            v.driftIncA = twoPi * (0.05 + 0.25 * rnd.nextDouble()) / sampleRate;
            v.driftIncB = twoPi * (0.04 + 0.20 * rnd.nextDouble()) / sampleRate;
            v.driftPhaseA = twoPi * rnd.nextDouble();
            v.driftPhaseB = twoPi * rnd.nextDouble();
            v.level.reset (sampleRate, 0.05);
            v.delayMs.reset (sampleRate, 0.25);
        }
        for (auto* s : { &enabled, &mix, &width, &voiceGain })
            s->reset (sampleRate, 0.05);

        setSettings (settings);
        reset();
        snapNext = true;
    }

    void ChoirModule::reset()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
        sourceHpf.reset();
        sourceLpf.reset();
        for (int i = 0; i < maxVoices; ++i)
            voices[(size_t) i].shiftPhase = (float) i / (float) maxVoices; // staggered grains
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
            v.delayMs.setTargetValue (baseDelayMs + juce::jlimit (0.0f, maxTimingMs, s.timingMs) * sh[(size_t) i].timing);
        }

        enabled.setTargetValue (s.enabled ? 1.0f : 0.0f);
        mix.setTargetValue (juce::jlimit (0.0f, 1.0f, s.mix));
        width.setTargetValue (juce::jlimit (0.0f, 1.0f, s.width));

        // Density: 0 -> the voices together carry the energy of one voice
        // (soft, behind the lead); 1 -> each voice close to full level
        const float d = juce::jlimit (0.0f, 1.0f, s.density);
        voiceGain.setTargetValue (std::pow ((float) n, -0.5f + 0.32f * d));

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

    float ChoirModule::readHermite (float delay) const noexcept
    {
        const float pos = (float) writePos - delay;
        const int i1 = (int) std::floor (pos);
        const float t = pos - (float) i1;
        const float y0 = buffer[(size_t) ((i1 - 1) & mask)], y1 = buffer[(size_t) (i1 & mask)];
        const float y2 = buffer[(size_t) ((i1 + 1) & mask)], y3 = buffer[(size_t) ((i1 + 2) & mask)];
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
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

        for (int i = 0; i < n; ++i)
        {
            // Control rate: drift and pitch-shift speed per voice
            if (updateCounter-- <= 0)
            {
                updateCounter = controlInterval - 1;
                for (int v = 0; v < maxVoices; ++v)
                {
                    auto& vo = voices[(size_t) v];
                    vo.driftPhaseA += vo.driftIncA * controlInterval;
                    vo.driftPhaseB += vo.driftIncB * controlInterval;
                    const float a = (float) std::sin (vo.driftPhaseA), b = (float) std::sin (vo.driftPhaseB);
                    vo.driftCents = human * maxDriftCents * (0.6f * a + 0.4f * b);
                    vo.driftMs = human * maxDriftMs * (0.5f * b - 0.5f * a);

                    const float cents = detune * sh[(size_t) v].detune + vo.driftCents;
                    voiceCents[(size_t) v].store (cents, std::memory_order_relaxed);
                    // Read pointer speed = pitch ratio; the window phase moves by (ratio - 1) / window
                    shiftInc[(size_t) v] = (std::pow (2.0f, cents / 1200.0f) - 1.0f) / windowSamples;
                }
            }

            // Mono, band-limited source for the voices (no rumble, no fizz)
            float mono = 0.0f;
            for (int c = 0; c < numChannels; ++c)
                mono += ch[c][i];
            mono /= (float) numChannels;
            buffer[(size_t) writePos] = sourceLpf.process (sourceHpf.process (mono, 0), 0);

            const float w = width.getNextValue();
            const float g = voiceGain.getNextValue();
            float wetL = 0.0f, wetR = 0.0f;

            for (int v = 0; v < maxVoices; ++v)
            {
                auto& vo = voices[(size_t) v];
                const float level = vo.level.getNextValue();
                const float base = (vo.delayMs.getNextValue() + vo.driftMs) * 0.001f * (float) fs;
                if (level <= 0.0f)
                    continue;

                // Two read heads half a window apart, complementary Hann gains
                vo.shiftPhase -= shiftInc[(size_t) v];
                vo.shiftPhase -= std::floor (vo.shiftPhase);
                const float pA = vo.shiftPhase;
                const float pB = pA + 0.5f >= 1.0f ? pA - 0.5f : pA + 0.5f;
                const float gA = 0.5f - 0.5f * std::cos ((float) twoPi * pA);
                const float s = gA * readHermite (base + pA * windowSamples)
                              + (1.0f - gA) * readHermite (base + pB * windowSamples);

                // Constant-power pan
                const float pan = juce::jlimit (-1.0f, 1.0f, sh[(size_t) v].pan * w);
                const float angle = (pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
                const float vg = s * level * g;
                wetL += vg * std::cos (angle);
                wetR += vg * std::sin (angle);
            }

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

            writePos = (writePos + 1) & mask;
        }

        if (sumLL > 1.0e-9 && sumRR > 1.0e-9)
            outputCorrelation.store ((float) (sumLR / std::sqrt (sumLL * sumRR)), std::memory_order_relaxed);
    }
}
