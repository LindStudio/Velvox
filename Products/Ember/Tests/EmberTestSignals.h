#pragma once
// Synthetic test signals shared by EmberTests.cpp and EmberQaTests.cpp (moved here from EmberTests.cpp, 2026-11-23).
#include "EmberEngine.h"

namespace ember::testsig
{
    // ------------------------------------------------------------ signals
    struct Biq
    {
        vocalypse::dsp::Biquad b;
        float run (float x) { return b.process (x, 0); }
    };

    // A synthetic take: vowels (harmonics of 180 Hz, falling 9 dB/oct above 1 kHz), S bursts (noise 5-11 kHz),
    // pauses. Returns the sample ranges of the S bursts and vowels.
    struct Take
    {
        std::vector<float> x;
        std::vector<std::pair<int, int>> sRanges, vRanges;
    };

    inline Take makeTake (double fs, double seconds, float sLevelDb = -16.0f, float vowelPeakDb = -12.0f, bool withS = true, unsigned seed = 7)
    {
        Take t;
        const int n = (int) (seconds * fs);
        t.x.assign ((size_t) n, 0.0f);
        juce::Random rnd ((juce::int64) seed);
        Biq hp1, hp2, lp1, lp2;
        hp1.b.setCoeffs (vocalypse::dsp::design::highPass (fs, 5000.0, 0.54));
        hp2.b.setCoeffs (vocalypse::dsp::design::highPass (fs, 5000.0, 1.31));
        lp1.b.setCoeffs (vocalypse::dsp::design::lowPass (fs, 11000.0, 0.54));
        lp2.b.setCoeffs (vocalypse::dsp::design::lowPass (fs, 11000.0, 1.31));

        int i = 0, phrase = 0;
        double phase = 0.0;
        while (i < n)
        {
            const int vowelLen = (int) (fs * (0.25 + 0.1 * (phrase % 3)));
            const int sLen = (int) (fs * (0.08 + 0.04 * (phrase % 2)));
            const int pause = (int) (fs * 0.07);
            const float f0 = 170.0f + 25.0f * (float) (phrase % 4);
            const float vGain = vocalypse::dsp::dbToGain (vowelPeakDb - 3.0f * (float) (phrase % 3)) * 0.35f;

            const int v0 = i;
            for (int k = 0; k < vowelLen && i < n; ++k, ++i)
            {
                phase += f0 / fs;
                float s = 0.0f;
                for (int h = 1; h * f0 < 5000.0f; ++h)
                {
                    const float hz = h * f0;
                    const float amp = hz < 1000.0f ? 1.0f / h : (1.0f / h) * std::pow (1000.0f / hz, 1.5f);
                    s += amp * (float) std::sin (juce::MathConstants<double>::twoPi * phase * h);
                }
                const float env = std::min (1.0f, std::min (k, vowelLen - k) / (float) (fs * 0.02));
                t.x[(size_t) i] = s * vGain * env;
            }
            t.vRanges.push_back ({ v0 + (int) (fs * 0.03), i - (int) (fs * 0.03) });

            if (withS)
            {
                const int s0 = i;
                const float sGain = vocalypse::dsp::dbToGain (sLevelDb + 2.0f * (float) (phrase % 3)) * 2.2f;
                for (int k = 0; k < sLen && i < n; ++k, ++i)
                {
                    const float w = rnd.nextFloat() * 2.0f - 1.0f;
                    const float b = lp2.run (lp1.run (hp2.run (hp1.run (w))));
                    const float env = std::min (1.0f, std::min (k, sLen - k) / (float) (fs * 0.01));
                    t.x[(size_t) i] = b * sGain * env;
                }
                t.sRanges.push_back ({ s0 + (int) (fs * 0.012), i - (int) (fs * 0.012) });
            }
            i += pause;
            ++phrase;
        }
        return t;
    }

}
