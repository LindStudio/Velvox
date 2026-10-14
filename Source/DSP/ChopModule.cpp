#include "ChopModule.h"

namespace velvox::dsp
{
    const std::vector<ChopModule::Pattern>& ChopModule::patterns()
    {
        // 'x' play, '.' mute, 'r' repeat previous slice. Original patterns.
        static const std::vector<Pattern> p {
            { "Gate 1/2",        "x.x.x.x.x.x.x.x." },
            { "Offbeat",         ".x.x.x.x.x.x.x.x" },
            { "Trance gate",     "x.xx.xx.x.xx.xx." },
            { "Hyper drive",     "xx.x.xx.xx.x.xx." },
            { "Gallop",          "x.xxx.xxx.xxx.xx" },
            { "Half bar",        "xxxxxxxx........" },
            { "Stutter end",     "xxxxxxxxxxxxrrrr" },
            { "Stutter 8ths",    "xxrrxxrrxxrrxxrr" },
            { "Build-up",        "x.x.x.x.xrxrxrrr" },
            { "Chop & repeat",   "x.r.x.r.xxr.x.rr" },
        };
        return p;
    }

    const juce::StringArray& ChopModule::patternNames()
    {
        static const juce::StringArray names = []
        {
            juce::StringArray n;
            for (auto& p : patterns())
                n.add (p.name);
            return n;
        }();
        return names;
    }

    const juce::StringArray& ChopModule::rateNames()
    {
        static const juce::StringArray n { "1/8", "1/16", "1/16T", "1/32" };
        return n;
    }

    double ChopModule::beatsPerStep (int rateIndex) noexcept
    {
        static const double beats[] { 0.5, 0.25, 1.0 / 6.0, 0.125 };
        return beats[juce::jlimit (0, 3, rateIndex)];
    }

    void ChopModule::prepare (double sampleRate, int)
    {
        fs = sampleRate;
        const int size = (int) std::ceil (maxRepeatSeconds * sampleRate) + 4;
        for (auto& h : history)
            h.assign ((size_t) size, 0.0f);
        fadeLength = juce::jmax (16, (int) (0.0015 * sampleRate));
        amount.reset (sampleRate, 0.03);
        amount.setCurrentAndTargetValue (amount.getTargetValue());
        setSettings (settings);
        reset();
    }

    void ChopModule::reset()
    {
        for (auto& h : history)
            std::fill (h.begin(), h.end(), 0.0f);
        writePos = 0;
        freePpq = 0.0;
        currentStep = -1;
        repeatCount = 0;
        gate = 1.0f;
        fadeRemaining = 0;
        prevDelay = curDelay = 0;
    }

    void ChopModule::setSettings (const ChopSettings& s) noexcept
    {
        settings = s;
        settings.pattern = juce::jlimit (0, (int) patterns().size() - 1, s.pattern);
        settings.rate = juce::jlimit (0, 3, s.rate);
        smoothCoeff = timeConstantCoeff (juce::jlimit (0.1f, 50.0f, s.smoothMs), fs);
        amount.setTargetValue (s.enabled ? juce::jlimit (0.0f, 1.0f, s.depth) : 0.0f);
    }

    void ChopModule::setTransport (double newBpm, double ppq, bool playing) noexcept
    {
        bpm = (newBpm > 20.0 && newBpm < 999.0) ? newBpm : fallbackBpm;
        hostPpq = ppq;
        hostPlaying = playing && ppq >= 0.0;
    }

    char ChopModule::stepType (int step) const noexcept
    {
        return patterns()[(size_t) settings.pattern].steps[step % numSteps];
    }

    void ChopModule::process (float* const* ch, int numChannels, int n) noexcept
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

        const double ppqPerSample = bpm / 60.0 / fs;
        const double stepBeats = beatsPerStep (settings.rate);
        const int stepSamples = juce::jmax (1, (int) std::lround (stepBeats * 60.0 / bpm * fs));
        const int size = (int) history[0].size();

        // Song position for this block: host while playing, otherwise free-running
        double ppq = hostPlaying ? hostPpq : freePpq;
        if (hostPlaying != wasPlaying)
        {
            currentStep = -1; // re-evaluate the step immediately
            wasPlaying = hostPlaying;
        }

        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < numChannels; ++c)
                history[(size_t) c][(size_t) writePos] = ch[c][i];

            // Step at this sample (pattern repeats every 16 steps)
            const double stepPos = ppq / stepBeats;
            const int step = (int) std::fmod (std::floor (stepPos), (double) numSteps);
            if (step != currentStep)
            {
                currentStep = step;
                const char type = stepType (step);
                repeatCount = type == 'r' ? repeatCount + 1 : 0;

                // Repeating k steps in a row replays the slice from k steps ago
                const int newDelay = type == 'r' ? juce::jmin (repeatCount * stepSamples, size - 2) : 0;
                if (newDelay != curDelay)
                {
                    prevDelay = curDelay;
                    curDelay = newDelay;
                    fadeRemaining = fadeLength;
                }
            }

            const char type = stepType (currentStep);
            const float target = type == '.' ? 0.0f : 1.0f;
            gate = target + smoothCoeff * (gate - target);

            const float depth = amount.getNextValue();
            const float gain = 1.0f - depth * (1.0f - gate);
            const float fade = fadeRemaining > 0 ? (float) fadeRemaining / (float) fadeLength : 0.0f;

            for (int c = 0; c < numChannels; ++c)
            {
                const auto& h = history[(size_t) c];
                const auto readAt = [&] (int delay)
                {
                    int p = writePos - delay;
                    if (p < 0)
                        p += size;
                    return h[(size_t) p];
                };

                float source = readAt (curDelay);
                if (fadeRemaining > 0)
                    source = source * (1.0f - fade) + readAt (prevDelay) * fade;

                // Depth also blends repeats with the live signal
                const float live = ch[c][i];
                const float chopped = live + (source - live) * depth;
                ch[c][i] = chopped * gain;
            }

            if (fadeRemaining > 0)
                --fadeRemaining;
            if (++writePos >= size)
                writePos = 0;
            ppq += ppqPerSample;
        }

        displayStep.store (currentStep, std::memory_order_relaxed);

        // Keep advancing across the chain's internal chunks; setTransport()
        // re-synchronises to the host once per host block.
        if (hostPlaying)
            hostPpq = ppq;
        else
            freePpq = std::fmod (ppq, 128.0); // multiple of every pattern length; keeps the value small
    }
}
