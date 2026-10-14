#include "DoublerModule.h"

namespace velvox::dsp
{
    namespace
    {
        constexpr double twoPi = 6.283185307179586;
        // Slow, unrelated rates so the two voices never move together
        constexpr double rates[2][2] { { 0.31, 0.113 }, { 0.47, 0.171 } };
        constexpr float voiceLowCutHz = 120.0f;
        constexpr int updateInterval = 32;
    }

    double DoublerModule::depthForCents (double cents, double rateHz, double sampleRate) noexcept
    {
        // Delay D(t) = D0 + A sin(2 pi f t)  ->  pitch ratio 1 - dD/dt, peak |dD/dt| = A 2 pi f / fs
        const double ratio = std::pow (2.0, cents / 1200.0);
        return (ratio - 1.0) * sampleRate / (twoPi * rateHz);
    }

    void DoublerModule::prepare (double sampleRate, int)
    {
        fs = sampleRate;

        // Longest delay: right voice base (1.37x) + modulation headroom
        const double maxDepth = depthForCents (maxDetuneCents, rates[0][0], sampleRate) * 1.4;
        const int size = (int) std::ceil (maxDelayMs * 1.37 * 0.001 * sampleRate + maxDepth) + 8;
        buffer.assign ((size_t) size, 0.0f);

        voices[0].baseFactor = 1.0f;
        voices[1].baseFactor = 1.37f;
        for (int v = 0; v < 2; ++v)
        {
            voices[(size_t) v].inc1 = twoPi * rates[v][0] / sampleRate;
            voices[(size_t) v].inc2 = twoPi * rates[v][1] / sampleRate;
        }
        voices[1].phase1 = 1.7; // start out of phase
        voices[1].phase2 = 0.6;

        for (auto* s : { &amount, &width, &monoSafe, &delayMs, &detune })
        {
            s->reset (sampleRate, s == &delayMs ? 0.2 : 0.05);
            s->setCurrentAndTargetValue (s->getTargetValue());
        }
        tone.reset (sampleRate, 0.05);
        tone.setCurrentAndTargetValue (tone.getTargetValue());

        updateFilters();
        reset();
    }

    void DoublerModule::reset()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
        for (auto& v : voices)
        {
            v.lowCut.reset();
            v.highCut.reset();
        }
    }

    void DoublerModule::setSettings (const DoublerSettings& s) noexcept
    {
        amount.setTargetValue (s.enabled ? juce::jlimit (0.0f, 1.0f, s.mix) : 0.0f);
        width.setTargetValue (juce::jlimit (0.0f, 1.0f, s.width));
        monoSafe.setTargetValue (juce::jlimit (0.0f, 1.0f, s.monoSafe));
        delayMs.setTargetValue (juce::jlimit (1.0f, maxDelayMs, s.delayMs));
        detune.setTargetValue (juce::jlimit (0.0f, maxDetuneCents, s.detuneCents));
        tone.setTargetValue (juce::jlimit (1000.0f, 20000.0f, s.toneHz));
    }

    void DoublerModule::updateFilters() noexcept
    {
        for (auto& v : voices)
        {
            v.lowCut.setCoeffs (design::highPass (fs, voiceLowCutHz, 0.70710678));
            v.highCut.setCoeffs (design::lowPass (fs, tone.getCurrentValue(), 0.70710678));
        }
    }

    float DoublerModule::read (float delay) const noexcept
    {
        const int size = (int) buffer.size();
        float pos = (float) writePos - delay;
        while (pos < 0.0f)
            pos += (float) size;
        const int i0 = (int) pos;
        const float frac = pos - (float) i0;
        const int i1 = i0 + 1 >= size ? 0 : i0 + 1;
        return buffer[(size_t) i0] + frac * (buffer[(size_t) i1] - buffer[(size_t) i0]);
    }

    void DoublerModule::process (float* const* ch, int numChannels, int numSamples) noexcept
    {
        const bool active = amount.getTargetValue() > 0.0f || amount.isSmoothing();
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

        const int size = (int) buffer.size();
        const float maxDelay = (float) (size - 4);

        for (int start = 0; start < numSamples; start += updateInterval)
        {
            const int n = std::min (updateInterval, numSamples - start);
            if (tone.isSmoothing())
            {
                tone.skip (n);
                updateFilters();
            }

            // Modulation depths per sub-block (the detune control is smoothed)
            detune.skip (n);
            const float cents = detune.getCurrentValue();
            double depth1[2], depth2[2];
            for (int k = 0; k < 2; ++k)
            {
                // Main LFO sets the detune; a slower one adds irregular drift
                depth1[k] = depthForCents (cents, rates[k][0], fs);
                depth2[k] = depthForCents (cents * 0.35, rates[k][1], fs);
            }

            for (int i = start; i < start + n; ++i)
            {
                const float in = numChannels > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
                buffer[(size_t) writePos] = in;

                const float base = delayMs.getNextValue() * 0.001f * (float) fs;

                float v[2];
                for (int k = 0; k < 2; ++k)
                {
                    auto& voice = voices[(size_t) k];
                    voice.phase1 += voice.inc1;
                    voice.phase2 += voice.inc2;
                    if (voice.phase1 > twoPi) voice.phase1 -= twoPi;
                    if (voice.phase2 > twoPi) voice.phase2 -= twoPi;

                    const double mod = depth1[k] * std::sin (voice.phase1) + depth2[k] * std::sin (voice.phase2);
                    const float d = juce::jlimit (2.0f, maxDelay,
                                                  base * voice.baseFactor + (float) (depth1[k] + depth2[k] + mod));

                    float y = read (d);
                    y = voice.lowCut.process (y, 0);
                    y = voice.highCut.process (y, 0);
                    v[k] = y;
                }

                if (++writePos >= size)
                    writePos = 0;

                // Pan the voices, then limit how much of them reaches the mono sum
                const float w = width.getNextValue();
                const float left  = v[0] * 0.5f * (1.0f + w) + v[1] * 0.5f * (1.0f - w);
                const float right = v[0] * 0.5f * (1.0f - w) + v[1] * 0.5f * (1.0f + w);
                const float mid = 0.5f * (left + right) * (1.0f - monoSafe.getNextValue());
                const float side = 0.5f * (left - right);
                const float a = amount.getNextValue();

                if (numChannels > 1)
                {
                    ch[0][i] += a * (mid + side);
                    ch[1][i] += a * (mid - side);
                }
                else
                {
                    ch[0][i] += a * mid;
                }
            }
        }

        for (auto& voice : voices)
            if (! voice.lowCut.isStateFinite() || ! voice.highCut.isStateFinite())
            {
                reset();
                break;
            }
    }
}
