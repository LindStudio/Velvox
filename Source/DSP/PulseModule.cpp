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
            table[(size_t) i] = evaluate (settings, (float) i / (float) tableSize);
    }

    void PulseModule::setSettings (const PulseSettings& s) noexcept
    {
        const bool curveChanged = s.x != settings.x || s.y != settings.y || s.interpolation != settings.interpolation
                                  || s.tension != settings.tension;
        settings = s;
        settings.rate = juce::jlimit (0, rateNames().size() - 1, s.rate);
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
        const float depth = juce::jlimit (0.0f, 1.0f, settings.depth);
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
