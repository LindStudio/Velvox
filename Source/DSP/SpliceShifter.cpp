#include "SpliceShifter.h"

#include <cmath>

namespace velvox::dsp
{
    namespace
    {
        int nextPow2 (int n)
        {
            int p = 1;
            while (p < n) p <<= 1;
            return p;
        }
    }

    void SpliceShifter::prepare (double sampleRate, int maxBlockSize)
    {
        juce::ignoreUnused (maxBlockSize);
        fs = sampleRate;
        guard = 4;
        latency = (int) std::ceil (fs / 70.0) + 16;
        maxJump = latency - guard - 2;

        const int maxWindow = (int) std::ceil (0.016 * fs);
        size = nextPow2 (latency + 3 * maxJump + maxWindow + 64);
        mask = size - 1;
        for (auto& l : lines)
            l.assign ((size_t) size, 0.0f);
        mono.assign ((size_t) size, 0.0f);
        designAntiAlias();
        reset();
    }

    void SpliceShifter::reset() noexcept
    {
        for (auto& l : lines)
            std::fill (l.begin(), l.end(), 0.0f);
        std::fill (mono.begin(), mono.end(), 0.0f);
        for (auto& f : aa)
            f.reset();
        pos = 0;
        delayA = delayB = latency;
        fading = false;
        fadePos = 0.0;
    }

    void SpliceShifter::setRatio (float r) noexcept
    {
        r = juce::jlimit (0.25f, 4.0f, std::isfinite (r) ? r : 1.0f);
        if (r == ratio)
            return;
        ratio = r;
        designAntiAlias();
    }

    void SpliceShifter::designAntiAlias() noexcept
    {
        // Reading faster than real time folds everything above fs / (2 ratio): low-pass the input
        const bool want = ratio > 1.0001f;
        if (want && ! antiAlias)
            for (auto& f : aa)
                f.reset();
        antiAlias = want;
        if (! want)
            return;
        const double fc = std::min (0.45 * fs, 0.40 * fs / ratio);
        constexpr int order = 2 * aaSections;
        for (int k = 0; k < aaSections; ++k)
        {
            const double q = 1.0 / (2.0 * std::cos ((2.0 * k + 1.0) * juce::MathConstants<double>::pi / (2.0 * order)));
            aa[(size_t) k].setCoeffs (design::lowPass (fs, fc, q));
        }
    }

    float SpliceShifter::read (const std::vector<float>& buf, double delay) const noexcept
    {
        const int whole = (int) delay;
        const float t = (float) (delay - whole);
        const int i0 = pos - whole;
        const float pm1 = buf[(size_t) ((i0 + 1) & mask)];
        const float p0 = buf[(size_t) (i0 & mask)];
        const float p1 = buf[(size_t) ((i0 - 1) & mask)];
        const float p2 = buf[(size_t) ((i0 - 2) & mask)];
        const float c1 = 0.5f * (p1 - pm1);
        const float c2 = pm1 - 2.5f * p0 + 2.0f * p1 - 0.5f * p2;
        const float c3 = 0.5f * (p2 - pm1) + 1.5f * (p0 - p1);
        return ((c3 * t + c2) * t + c1) * t + p0;
    }

    SpliceShifter::Splice SpliceShifter::findSplice (double fromDelay, double nominal, int direction, float period) const noexcept
    {
        const bool voiced = period > 0.0f;
        const double scale = fs / 48000.0;
        const int window = voiced ? juce::jlimit ((int) (128 * scale), (int) (768 * scale), (int) std::lround (1.5 * period))
                                  : (int) (256 * scale);
        const int range = voiced ? (int) std::ceil (0.1 * period + 2.0) : (int) (nominal / 3.0);
        const int a = pos - (int) std::lround (fromDelay);

        const auto ncc = [&] (int jump)
        {
            const int b = a - direction * jump;
            double xy = 0.0, xx = 1.0e-20, yy = 1.0e-20;
            for (int u = 0; u < window; ++u)
            {
                const float x = mono[(size_t) ((a - u) & mask)];
                const float y = mono[(size_t) ((b - u) & mask)];
                xy += (double) x * y;
                xx += (double) x * x;
                yy += (double) y * y;
            }
            return (float) (xy / std::sqrt (xx * yy));
        };

        const int lo = std::max (1, (int) std::lround (nominal) - range);
        const int hi = std::min (direction > 0 ? maxJump + range : (int) std::floor (fromDelay) - guard - 2, (int) std::lround (nominal) + range);
        if (hi <= lo)
            return { nominal, 0.0f };

        // coarse grid, then the neighbours of the best coarse point
        const int step = std::max (1, (hi - lo) / 24);
        int best = lo;
        float bestV = -2.0f;
        for (int j = lo; j <= hi; j += step)
        {
            const float v = ncc (j);
            if (v > bestV) { bestV = v; best = j; }
        }
        if (step > 1)
        {
            const int c = best;
            for (int j = std::max (lo, c - step + 1); j <= std::min (hi, c + step - 1); ++j)
            {
                if (j == c) continue;
                const float v = ncc (j);
                if (v > bestV) { bestV = v; best = j; }
            }
        }

        double jump = best;
        if (best > lo && best < hi)
        {
            const float y0 = ncc (best - 1), y2 = ncc (best + 1);
            const float den = y0 - 2.0f * bestV + y2;
            if (den < 0.0f)
                jump += juce::jlimit (-0.5f, 0.5f, 0.5f * (y0 - y2) / den);
        }
        return { jump, bestV };
    }

    void SpliceShifter::process (float* const* io, int numChannels, int n, const float* period) noexcept
    {
        numChannels = std::min (numChannels, (int) maxChannels);
        const double slope = 1.0 - (double) ratio;      // delay change per sample
        const double speed = std::abs (slope);
        const double minJump = 0.004 * fs, unvoicedJump = 0.008 * fs, minFade = 0.010 * fs;
        const double maxDelay = latency + maxJump;

        for (int i = 0; i < n; ++i)
        {
            pos = (pos + 1) & mask;
            float m = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float x = io[ch][i];
                if (! std::isfinite (x)) x = 0.0f;
                if (antiAlias)
                    for (auto& f : aa)
                        x = f.process (x, ch);
                lines[(size_t) ch][(size_t) pos] = x;
                m += x;
            }
            mono[(size_t) pos] = m / (float) numChannels;

            if (! fading && speed > 1.0e-7)
            {
                const float p = period[i];
                const bool voiced = p > 0.0f && p <= (float) maxJump;
                double jump = unvoicedJump;
                if (voiced)
                {
                    int k = std::max (1, (int) std::ceil (minJump / p));
                    while (k > 1 && k * (double) p > maxJump) --k;
                    jump = k * (double) p;
                }
                const double fade = std::min (0.95 * jump / speed, std::max (jump, minFade));
                const bool trigger = ratio > 1.0f ? delayA <= latency - 0.5 * jump + 0.5 * speed * fade
                                                  : delayA >= latency + 0.5 * jump - 0.5 * speed * fade;
                if (trigger)
                {
                    const int direction = ratio > 1.0f ? 1 : -1;
                    const auto s = findSplice (delayA, jump, direction, voiced ? p : 0.0f);
                    delayB = juce::jlimit ((double) guard, maxDelay, delayA + direction * s.jump);
                    fading = true;
                    fadeCorr = juce::jlimit (0.0f, 1.0f, s.ncc);
                    fadePos = 0.0;
                    fadeInc = 1.0 / std::max (1.0, fade);
                }
            }

            if (fading)
            {
                // raised-cosine fade, normalised by the heads' correlation so partially similar
                // waveforms (real voices, noise) keep their level through the splice
                const float u = (float) fadePos;
                float gB = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * u);
                float gA = 1.0f - gB;
                const float norm = 1.0f / std::sqrt (std::max (1.0e-3f, gA * gA + gB * gB + 2.0f * fadeCorr * gA * gB));
                gA *= norm;
                gB *= norm;
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const auto& line = lines[(size_t) ch];
                    io[ch][i] = gA * read (line, delayA) + gB * read (line, delayB);
                }
                delayB = juce::jlimit ((double) guard, maxDelay, delayB + slope);
                fadePos += fadeInc;
                if (fadePos >= 1.0)
                {
                    fading = false;
                    delayA = delayB;
                }
                else
                    delayA = juce::jlimit ((double) guard, maxDelay, delayA + slope);
            }
            else
            {
                for (int ch = 0; ch < numChannels; ++ch)
                    io[ch][i] = read (lines[(size_t) ch], delayA);
                delayA = juce::jlimit ((double) guard, maxDelay, delayA + slope);
            }
        }

        for (auto& f : aa)
            if (! f.isStateFinite())
                f.reset();
    }
}
