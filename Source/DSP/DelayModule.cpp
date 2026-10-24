#include "DelayModule.h"
#include "../Parameters/ParameterLayout.h"

namespace velvox::dsp
{
    namespace
    {
        constexpr float maxFeedback = 0.9f;
        constexpr int updateInterval = 32;
        constexpr double crossfadeSeconds = 0.06;
        constexpr double glideSeconds = 0.05;       // tape-style follow time for small moves
        constexpr float maxGlideRate = 0.08f;       // |d delay / dt|: about +-1.4 semitones
        constexpr double jumpSeconds = 0.015;       // farther away than this: crossfade instead
        constexpr double twoPi = 6.283185307179586;
        constexpr float diffuserMs[maxChannels][3] { { 2.1f, 3.7f, 5.3f }, { 2.4f, 3.3f, 5.9f } };
        constexpr float maxModMs = 2.5f;

        // Exactly linear up to 0.8 (0.6: the old plain tanh added -50 dB THD at -10 dBFS),
        // then a C1-continuous soft knee that is bounded to +-1.5 like before
        inline float loopLimiter (float x) noexcept
        {
            constexpr float knee = 0.8f, range = 1.5f - knee;
            const float a = std::abs (x);
            if (a <= knee)
                return x;
            return std::copysign (knee + range * std::tanh ((a - knee) * (1.0f / range)), x);
        }
    }

    float DelayModule::timeForTempo (double bpm, int divisionIndex) noexcept
    {
        if (! (bpm > 1.0 && bpm < 1000.0))
            return -1.0f;
        const double ms = 60000.0 / bpm * delaydiv::beatsForIndex (divisionIndex);
        return (float) std::min ((double) maxDelayMs, ms);
    }

    void DelayModule::prepare (double sampleRate, int)
    {
        fs = sampleRate;
        glideCoeff = (float) std::exp (-1.0 / (glideSeconds * sampleRate));
        maxGlideStep = maxGlideRate;
        jumpSamples = (float) (jumpSeconds * sampleRate);

        for (int ch = 0; ch < maxChannels; ++ch)
        {
            int total = 0;
            for (int k = 0; k < numDiffusers; ++k)
            {
                const int len = std::max (1, (int) std::lround (diffuserMs[ch][k] * 0.001 * sampleRate));
                diffusers[(size_t) ch][(size_t) k].buffer.assign ((size_t) len, 0.0f);
                total += len;
            }
            diffuserLatency[(size_t) ch] = total;
        }

        size = (int) std::ceil ((maxDelayMs + maxSpreadMs + maxModMs * 2.0f) * 0.001 * sampleRate) + 8;
        for (auto& l : lines)
            l.assign ((size_t) size, 0.0f);

        feedback.reset (sampleRate, 0.05);
        spread.reset (sampleRate, 0.15);
        drive.reset (sampleRate, 0.05);
        diffusion.reset (sampleRate, 0.05);
        modulation.reset (sampleRate, 0.1);
        lowCut.reset (sampleRate, 0.05);
        highCut.reset (sampleRate, 0.05);

        setSettings (settings);
        for (auto* s : { &feedback, &spread, &drive, &diffusion, &modulation })
            s->setCurrentAndTargetValue (s->getTargetValue());
        snapTime = true;
        updateFilters (true);
        reset();
    }

    void DelayModule::reset()
    {
        for (auto& l : lines)
            std::fill (l.begin(), l.end(), 0.0f);
        for (auto& side : diffusers)
            for (auto& d : side)
            {
                std::fill (d.buffer.begin(), d.buffer.end(), 0.0f);
                d.pos = 0;
            }
        writePos = 0;
        hp.reset();
        lp.reset();
        // After a reset nothing is in the line: jump straight to the requested time
        snapTime = true;
    }

    void DelayModule::setSettings (const DelaySettings& s) noexcept
    {
        settings = s;
        const float ms = juce::jlimit (1.0f, maxDelayMs, s.timeMs);
        targetDelay = (float) (ms * 0.001 * fs);
        feedback.setTargetValue (juce::jlimit (0.0f, maxFeedback, s.feedback));
        spread.setTargetValue ((float) (juce::jlimit (0.0f, maxSpreadMs, s.spreadMs) * 0.001 * fs));
        drive.setTargetValue (juce::jlimit (0.0f, 1.0f, s.drive));
        diffusion.setTargetValue (juce::jlimit (0.0f, 1.0f, s.diffusion));
        modulation.setTargetValue (juce::jlimit (0.0f, 1.0f, s.modulation));
        lowCut.setTargetValue (juce::jlimit (20.0f, 20000.0f, s.lowCutHz));
        highCut.setTargetValue (juce::jlimit (200.0f, 20000.0f, s.highCutHz));
    }

    void DelayModule::updateFilters (bool snap) noexcept
    {
        if (snap)
        {
            lowCut.setCurrentAndTargetValue (lowCut.getTargetValue());
            highCut.setCurrentAndTargetValue (highCut.getTargetValue());
        }

        hp.setCoeffs (design::highPass (fs, lowCut.getCurrentValue(), 0.70710678));
        lp.setCoeffs (design::lowPass (fs, std::min ((double) highCut.getCurrentValue(), 0.45 * fs), 0.70710678));
    }

    // 4-point, 3rd order Hermite interpolation `delay` samples behind the write position
    float DelayModule::readInterpolated (const std::vector<float>& buf, float delay) const noexcept
    {
        // >= 2: the newer neighbour of the interpolation must already be written
        const float d = juce::jlimit (2.0f, (float) (size - 4), delay);
        const int whole = (int) d;
        const float frac = d - (float) whole;

        // Integer wrap (see fractionalIndex): i1 = writePos - whole, i2 one sample older
        int i1 = (writePos - whole) % size;
        if (i1 < 0)
            i1 += size;
        const int i0 = i1 + 1 >= size ? i1 + 1 - size : i1 + 1;   // newer
        const int i2 = i1 == 0 ? size - 1 : i1 - 1;               // older
        const int i3 = i2 == 0 ? size - 1 : i2 - 1;

        const float y0 = buf[(size_t) i0], y1 = buf[(size_t) i1], y2 = buf[(size_t) i2], y3 = buf[(size_t) i3];
        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }

    void DelayModule::process (const float* const* input, float* const* wetOut, int numChannels, int numSamples) noexcept
    {
        const bool pingPong = settings.pingPong && numChannels > 1;
        const float fadeIncrement = (float) (1.0 / (crossfadeSeconds * fs));

        if (snapTime)
        {
            nextDelay = targetDelay;
            currentDelay = targetDelay;
            fade = 1.0f;
            fadeStep = 0.0f;
            snapTime = false;
        }

        for (int start = 0; start < numSamples; start += updateInterval)
        {
            const int n = std::min (updateInterval, numSamples - start);

            if (lowCut.isSmoothing() || highCut.isSmoothing())
            {
                lowCut.skip (n);
                highCut.skip (n);
                updateFilters (false);
            }

            diffusion.skip (n);
            const float apGain = 0.62f * diffusion.getCurrentValue();
            drive.skip (n);
            const float k = 1.0f + 5.0f * drive.getCurrentValue();
            const bool saturate = k > 1.001f;

            // Slow wow (0.6 Hz) plus a faster flutter component (4.7 Hz), evaluated per
            // sample: a stepped read position would add zipper sidebands
            const double inc1 = twoPi * 0.6 / fs, inc2 = twoPi * 4.7 / fs;
            const float modScale = maxModMs * 0.001f * (float) fs;

            for (int i = start; i < start + n; ++i)
            {
                const float fb = feedback.getNextValue();
                const float sp = spread.getNextValue();

                float mod = 0.0f;
                const float modAmount = modulation.getNextValue();
                modPhase1 += inc1;
                modPhase2 += inc2;
                if (modPhase1 > twoPi) modPhase1 -= twoPi;
                if (modPhase2 > twoPi) modPhase2 -= twoPi;
                if (modAmount > 0.0f)
                    mod = modAmount * modScale * (float) (0.5 * (1.0 + std::sin (modPhase1)) + 0.08 * std::sin (modPhase2));

                // Time: glide toward small moves, crossfade to jumps (one fade at a time)
                if (fadeStep == 0.0f)
                {
                    const double gap = (double) targetDelay - currentDelay;
                    if (std::abs (gap) > (double) jumpSamples)
                    {
                        nextDelay = targetDelay;
                        fade = 0.0f;
                        fadeStep = fadeIncrement;
                    }
                    else if (gap != 0.0)
                    {
                        const double step = juce::jlimit (-(double) maxGlideStep, (double) maxGlideStep, gap * (1.0 - (double) glideCoeff));
                        currentDelay = std::abs (gap) < 1.0e-4 ? (double) targetDelay : currentDelay + step;
                    }
                }

                float x = 1.0f;
                if (fadeStep != 0.0f)
                {
                    fade += fadeStep;
                    if (fade >= 1.0f)
                    {
                        fade = 1.0f;
                        fadeStep = 0.0f;
                        currentDelay = nextDelay;
                    }
                    x = fade;
                }

                float taps[maxChannels] {};
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    // The diffusion all-passes add their length: read that much earlier
                    const float offset = (ch == 1 ? sp : 0.0f) + mod - (float) diffuserLatency[(size_t) ch];
                    float t = readInterpolated (lines[(size_t) ch], (float) currentDelay + offset);
                    if (fadeStep != 0.0f || x < 1.0f)
                        t += x * (readInterpolated (lines[(size_t) ch], nextDelay + offset) - t);

                    for (auto& ap : diffusers[(size_t) ch])
                        t = ap.process (t, apGain);
                    t = lp.process (hp.process (t, ch), ch);
                    if (saturate)
                        t = std::tanh (k * t) / k;
                    taps[ch] = t;
                    wetOut[ch][i] = t;
                }

                if (pingPong)
                {
                    // Mono input enters on the left; repeats bounce L -> R -> L
                    const float in = 0.5f * (input[0][i] + input[1][i]);
                    lines[0][(size_t) writePos] = loopLimiter (in + taps[1] * fb);
                    lines[1][(size_t) writePos] = loopLimiter (taps[0] * fb);
                }
                else
                {
                    for (int ch = 0; ch < numChannels; ++ch)
                        lines[(size_t) ch][(size_t) writePos] = loopLimiter (input[ch][i] + taps[ch] * fb);
                }

                if (++writePos >= size)
                    writePos = 0;
            }
        }

        displayTimeMs.store ((float) ((fadeStep != 0.0f ? (double) nextDelay : currentDelay) * 1000.0 / fs), std::memory_order_relaxed);

        if (! hp.isStateFinite() || ! lp.isStateFinite())
            reset();
    }
}
