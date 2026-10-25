#include "PulseModule.h"

namespace velvox::dsp
{
    const juce::StringArray& PulseModule::rateNames()
    {
        static const juce::StringArray n { "1/1", "1/2", "1/4", "1/8", "1/16", "1/2T", "1/4T", "1/8T", "1/4D", "1/8D" };
        return n;
    }

    double PulseModule::beatsPerCycle (int rateIndex) noexcept
    {
        static const double beats[] { 4.0, 2.0, 1.0, 0.5, 0.25, 4.0 / 3.0, 2.0 / 3.0, 1.0 / 3.0, 1.5, 0.75 };
        return beats[juce::jlimit (0, (int) std::size (beats) - 1, rateIndex)];
    }

    const juce::StringArray& PulseModule::shapeNames()
    {
        static const juce::StringArray n { "Custom", "Classic", "Punch", "Swell", "Gate" };
        return n;
    }

    namespace
    {
        inline float smoothstep (float t) noexcept
        {
            t = juce::jlimit (0.0f, 1.0f, t);
            return t * t * (3.0f - 2.0f * t);
        }

        // Curated pump shapes: 0 = ducked, 1 = open. Each one closes with a short
        // raised-cosine fade into the next beat's duck (no step at the cycle edge).
        float pumpShape (int shape, float p) noexcept
        {
            constexpr float closeStart = 0.975f;
            float c = 1.0f;
            switch (shape)
            {
                case 1:   // Classic: S-shaped recovery over 60 % of the beat - the sidechain-pump sound
                    c = smoothstep (p / 0.6f);
                    break;
                case 2:   // Punch: a short hard duck, quick fast-starting release (tight, rhythmic)
                {
                    const float t = juce::jlimit (0.0f, 1.0f, (p - 0.04f) / 0.26f);
                    c = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
                    break;
                }
                case 3:   // Swell: slow, breathing recovery across the whole beat
                {
                    const float t = juce::jlimit (0.0f, 1.0f, p / 0.95f);
                    c = t * t;
                    break;
                }
                case 4:   // Gate: held down to the off-beat, then opens fast (choppy pump)
                    c = smoothstep ((p - 0.22f) / 0.08f);
                    break;
                default:
                    break;
            }
            if (p > closeStart)
                c *= 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * (p - closeStart) / (1.0f - closeStart));
            return c;
        }
    }

    float PulseModule::evaluateShape (const PulseSettings& s, float phase) noexcept
    {
        if (s.shape <= 0 || s.shape >= shapeNames().size())
            return evaluate (s, phase);
        return pumpShape (s.shape, phase - std::floor (phase));
    }

    float PulseModule::effectiveDepth (float depth, float amount) noexcept
    {
        depth = juce::jlimit (0.0f, 1.0f, depth);
        amount = juce::jlimit (0.0f, 1.0f, amount);
        if (amount <= 0.5f)
            return depth * (amount * 2.0f);   // exact at 0.5: old states keep their depth bit for bit
        return depth + (1.0f - depth) * ((amount - 0.5f) * 2.0f);
    }

    float PulseModule::gainAt (const PulseSettings& s, float phase) noexcept
    {
        return 1.0f - effectiveDepth (s.depth, s.amount) * (1.0f - evaluateShape (s, phase));
    }

    float PulseModule::evaluate (const PulseSettings& s, float phase) noexcept
    {
        constexpr int n = PulseSettings::numPoints;

        // Sort the points by time (tiny fixed-size insertion sort, no allocation)
        std::array<int, n> order {};
        for (int i = 0; i < n; ++i)
            order[(size_t) i] = i;
        for (int i = 1; i < n; ++i)
            for (int j = i; j > 0 && s.x[(size_t) order[(size_t) j]] < s.x[(size_t) order[(size_t) j - 1]]; --j)
                std::swap (order[(size_t) j], order[(size_t) j - 1]);

        phase = phase - std::floor (phase);

        // Segment containing the phase; the curve wraps around the cycle
        int k = n - 1;
        for (int i = 0; i < n; ++i)
            if (s.x[(size_t) order[(size_t) i]] <= phase)
                k = i;
            else
                break;

        const int a = order[(size_t) k];
        const int b = order[(size_t) ((k + 1) % n)];
        float x0 = juce::jlimit (0.0f, 1.0f, s.x[(size_t) a]);
        float x1 = juce::jlimit (0.0f, 1.0f, s.x[(size_t) b]);
        float p = phase;
        if (x1 <= x0)            // wrap: last point -> first point of the next cycle
        {
            x1 += 1.0f;
            if (p < x0)
                p += 1.0f;
        }

        const float y0 = juce::jlimit (0.0f, 1.0f, s.y[(size_t) a]);
        const float y1 = juce::jlimit (0.0f, 1.0f, s.y[(size_t) b]);
        if (s.interpolation == 2)
            return y0;

        float t = x1 - x0 > 1.0e-6f ? juce::jlimit (0.0f, 1.0f, (p - x0) / (x1 - x0)) : 1.0f;

        // Tension bends the segment: > 0 starts slowly (exponent > 1), < 0 starts fast
        const float tension = juce::jlimit (-1.0f, 1.0f, s.tension);
        if (std::abs (tension) > 1.0e-3f)
        {
            const float e = 1.0f + 3.0f * std::abs (tension);
            t = tension > 0.0f ? std::pow (t, e) : 1.0f - std::pow (1.0f - t, e);
        }
        if (s.interpolation == 1)
            t = t * t * (3.0f - 2.0f * t);   // smoothstep: S-shaped, zero slope at the points

        return y0 + (y1 - y0) * t;
    }

    void PulseModule::prepare (double sampleRate, int)
    {
        fs = sampleRate;
        amount.reset (sampleRate, 0.03);
        amount.setCurrentAndTargetValue (amount.getTargetValue());
        setSettings (settings);
        reset();
        snapNext = true;
    }

    void PulseModule::reset()
    {
        freePpq = 0.0;
        slewed = smoothed = 1.0f;
    }

    void PulseModule::rebuildTable() noexcept
    {
        for (int i = 0; i <= tableSize; ++i)
            table[(size_t) i] = evaluateShape (settings, (float) i / (float) tableSize);
    }

    void PulseModule::setSettings (const PulseSettings& s) noexcept
    {
        const bool curveChanged = s.x != settings.x || s.y != settings.y || s.interpolation != settings.interpolation
                                  || s.tension != settings.tension || s.shape != settings.shape;
        settings = s;
        settings.rate = juce::jlimit (0, rateNames().size() - 1, s.rate);
        settings.shape = juce::jlimit (0, shapeNames().size() - 1, s.shape);
        if (curveChanged)
            rebuildTable();

        attackCoeff = timeConstantCoeff (juce::jlimit (0.05f, 100.0f, s.attackMs), fs);
        releaseCoeff = timeConstantCoeff (juce::jlimit (0.5f, 500.0f, s.releaseMs), fs);
        smoothCoeff = timeConstantCoeff (juce::jlimit (0.0f, 50.0f, s.smoothMs), fs);
        amount.setTargetValue (s.enabled ? juce::jlimit (0.0f, 1.0f, s.mix) : 0.0f);
        if (snapNext)
        {
            snapNext = false;
            amount.setCurrentAndTargetValue (amount.getTargetValue());
        }
    }

    void PulseModule::setTransport (double newBpm, double ppq, bool playing) noexcept
    {
        hostTempoKnown = newBpm > 20.0 && newBpm < 999.0;
        bpm = hostTempoKnown ? newBpm : juce::jlimit (20.0, 400.0, (double) settings.manualBpm);
        displayBpm.store ((float) bpm, std::memory_order_relaxed);
        displayHostTempo.store (hostTempoKnown, std::memory_order_relaxed);
        hostPpq = ppq;
        hostPlaying = playing && ppq >= 0.0;
    }

    void PulseModule::process (float* const* ch, int numChannels, int n) noexcept
    {
        const bool active = amount.getTargetValue() > 0.0f || amount.isSmoothing();
        if (! active)
        {
            wasActive = false;
            displayPhase.store (-1.0f, std::memory_order_relaxed);
            return;
        }
        if (! wasActive)
        {
            wasActive = true;
            rebuildTable();
            // Start from the curve value so switching on never jumps
            reset();
        }

        const double cycleBeats = beatsPerCycle (settings.rate);
        const double ppqPerSample = bpm / 60.0 / fs;
        const float depth = effectiveDepth (settings.depth, settings.amount);
        double ppq = hostPlaying ? hostPpq : freePpq;
        float phase = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            const double cyclePos = ppq / cycleBeats;
            phase = (float) (cyclePos - std::floor (cyclePos));

            // Table lookup with linear interpolation
            const float tp = phase * (float) tableSize;
            const int i0 = juce::jlimit (0, tableSize - 1, (int) tp);
            const float curve = table[(size_t) i0] + (table[(size_t) i0 + 1] - table[(size_t) i0]) * (tp - (float) i0);
            const float target = 1.0f - depth * (1.0f - curve);

            // Slew: attack for drops, release for rises, then final rounding
            const float c = target < slewed ? attackCoeff : releaseCoeff;
            slewed = target + c * (slewed - target);
            smoothed = slewed + smoothCoeff * (smoothed - slewed);

            const float m = amount.getNextValue();
            const float g = 1.0f - m + m * smoothed;
            for (int k = 0; k < numChannels; ++k)
                ch[k][i] *= g;

            ppq += ppqPerSample;
        }

        displayPhase.store (phase, std::memory_order_relaxed);
        displayGain.store (smoothed, std::memory_order_relaxed);

        // Keep advancing across internal chunks; setTransport() re-syncs per host block
        if (hostPlaying)
            hostPpq = ppq;
        else
            freePpq = std::fmod (ppq, 192.0); // multiple of every cycle length
    }
}
