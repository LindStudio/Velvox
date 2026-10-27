// ATEM-ABSENKER (0.7, plan docs/superpowers/plans/2026-10-27-denoise-breath-modules.md, Task 3):
// breaths between the lines get quieter by the amount, vowels and S sounds stay, a clean vocal passes
// unchanged, the block size does not matter.

#include "TestHelpers.h"
#include "SyntheticVocal.h"
#include "DSP/BreathModule.h"

using namespace velvox;
using namespace velvox::test;
using dsp::BreathModule;

namespace
{
    constexpr double twoPi = 6.283185307179586;

    // Vowel: a harmonic stack with a small vibrato, 20 ms fades
    void addVowel (std::vector<float>& x, double fs, int start, int len, float amp)
    {
        double phase = 0.0;
        const int fade = (int) (0.02 * fs);
        for (int i = 0; i < len; ++i)
        {
            phase += 200.0 * (1.0 + 0.01 * std::sin (twoPi * 5.0 * i / fs)) / fs;
            float v = 0.0f;
            for (int h = 1; h <= 25; ++h)
                v += (float) (std::sin (twoPi * h * phase) / (h * 0.7 + 0.3));
            const float env = (float) std::min ({ 1.0, i / (double) fade, (len - 1 - i) / (double) fade });
            x[(size_t) (start + i)] += amp * env * v;
        }
    }

    // Noise between two cutoffs (one-pole high pass + two one-pole low passes), raised-cosine envelope, RMS in dB
    void addNoise (std::vector<float>& x, double fs, int start, int len, float rmsDbValue, double loHz, double hiHz, unsigned seed)
    {
        juce::Random rnd ((juce::int64) seed);
        std::vector<float> n ((size_t) len);
        const float aHp = (float) std::exp (-twoPi * loHz / fs), aLp = (float) std::exp (-twoPi * hiHz / fs);
        float hpIn = 0.0f, hpOut = 0.0f, lp1 = 0.0f, lp2 = 0.0f;
        double sum = 0.0;
        for (int i = 0; i < len; ++i)
        {
            const float w = rnd.nextFloat() * 2.0f - 1.0f;
            hpOut = aHp * (hpOut + w - hpIn);
            hpIn = w;
            lp1 = lp1 + (1.0f - aLp) * (hpOut - lp1);
            lp2 = lp2 + (1.0f - aLp) * (lp1 - lp2);
            const float env = (float) (0.5 - 0.5 * std::cos (twoPi * i / (len - 1)));
            n[(size_t) i] = lp2 * env;
            sum += n[(size_t) i] * n[(size_t) i];
        }
        const float scale = juce::Decibels::decibelsToGain (rmsDbValue) / (float) std::sqrt (sum / len + 1.0e-20);
        for (int i = 0; i < len; ++i)
            x[(size_t) (start + i)] += n[(size_t) i] * scale;
    }

    double rmsDb (const std::vector<float>& x, int from, int len)
    {
        double s = 0.0;
        for (int i = from; i < from + len; ++i)
            s += x[(size_t) i] * x[(size_t) i];
        return 10.0 * std::log10 (s / len + 1.0e-20);
    }

    std::vector<float> run (std::vector<float> x, double fs, float amount, int block = 512)
    {
        BreathModule m;
        m.prepare (fs, std::max (1, block));
        m.setSettings ({ true, amount });
        for (int start = 0; start < (int) x.size(); start += block)
        {
            float* ch[] { x.data() + start };
            m.process (ch, 1, std::min (block, (int) x.size() - start));
        }
        return x;
    }

    // Lines of singing with a breath between them: vowel 0.8 s, 0.1 s pause, breath 0.3 s, 0.1 s pause, ...
    struct Take
    {
        std::vector<float> x;
        std::vector<std::pair<int, int>> vowels, breaths;
    };

    Take take (double fs, float breathBelowVowelDb)
    {
        Take t;
        t.x.assign ((size_t) (8.0 * fs), 0.0f);
        const float vowelAmp = 0.1f;
        int at = (int) (0.2 * fs);
        for (int line = 0; line < 5; ++line)
        {
            const int vLen = (int) (0.8 * fs);
            addVowel (t.x, fs, at, vLen, vowelAmp);
            t.vowels.push_back ({ at, vLen });
            at += vLen + (int) (0.1 * fs);
            const int bLen = (int) (0.3 * fs);
            const double vowelDb = rmsDb (t.x, t.vowels.back().first + (int) (0.1 * fs), (int) (0.5 * fs));
            addNoise (t.x, fs, at, bLen, (float) (vowelDb - breathBelowVowelDb), 400.0, 5000.0, 17u + (unsigned) line);
            t.breaths.push_back ({ at, bLen });
            at += bLen + (int) (0.1 * fs);
        }
        return t;
    }
}

class BreathTests final : public juce::UnitTest
{
public:
    BreathTests() : juce::UnitTest ("Atem-Absenker (breath)", "Headliner") {}

    void runTest() override
    {
        beginTest ("Strength: 50 % = -12 dB, 100 % = -30 dB, 0 % = untouched");
        expectEquals (BreathModule::breathGainDb (50.0f), -12.0f);
        expectEquals (BreathModule::breathGainDb (100.0f), -30.0f);
        expectEquals (BreathModule::breathGainDb (0.0f), 0.0f);

        beginTest ("Breaths get 12 dB quieter at 50 %, 30 dB at 100 %; vowels stay within 0.3 dB");
        {
            for (double fs : { 44100.0, 48000.0, 96000.0 })
                for (auto [amount, target, tol] : { std::tuple { 50.0f, -12.0, 2.0 }, { 100.0f, -30.0, 3.0 } })
                {
                    const auto t = take (fs, 28.0f);
                    const auto out = run (t.x, fs, amount);
                    const int L = BreathModule::latencyFor (fs);
                    // the middle of each breath (its fade-in / out are quiet anyway) and the body of each vowel
                    double breathChange = 0.0, worstVowel = 0.0;
                    for (size_t i = 1; i < t.breaths.size(); ++i)   // the first line only sets the phrase level
                    {
                        const auto [b, len] = t.breaths[i];
                        const int from = b + len / 4, n = len / 2;
                        breathChange += (rmsDb (out, from + L, n) - rmsDb (t.x, from, n)) / (double) (t.breaths.size() - 1);
                        const auto [v, vLen] = t.vowels[i];
                        worstVowel = std::max (worstVowel, std::abs (rmsDb (out, v + (int) (0.05 * fs) + L, vLen - (int) (0.1 * fs))
                                                                      - rmsDb (t.x, v + (int) (0.05 * fs), vLen - (int) (0.1 * fs))));
                    }
                    logMessage ("  " + juce::String (fs / 1000.0, 1) + " kHz, " + juce::String (amount, 0) + " %: breaths "
                                + juce::String (breathChange, 1) + " dB, vowels within " + juce::String (worstVowel, 2) + " dB");
                    expectWithinAbsoluteError (breathChange, target, tol);
                    expectLessOrEqual (worstVowel, 0.3);
                }
        }

        beginTest ("An S right after a vowel is not a breath");
        {
            const double fs = 48000.0;
            auto t = take (fs, 28.0f);
            // replace every breath by an S: high noise above 4 kHz, 120 ms, directly after the vowel, 10 dB under it
            std::fill (t.x.begin(), t.x.end(), 0.0f);
            std::vector<std::pair<int, int>> esses;
            for (auto [v, vLen] : t.vowels)
            {
                addVowel (t.x, fs, v, vLen, 0.1f);
                const double vowelDb = rmsDb (t.x, v + (int) (0.1 * fs), (int) (0.5 * fs));
                const int s = v + vLen, len = (int) (0.12 * fs);
                addNoise (t.x, fs, s, len, (float) (vowelDb - 10.0), 4500.0, 12000.0, 99u);
                esses.push_back ({ s, len });
            }
            const auto out = run (t.x, fs, 100.0f);
            const int L = BreathModule::latencyFor (fs);
            double worst = 0.0;
            for (auto [s, len] : esses)
                worst = std::max (worst, std::abs (rmsDb (out, s + L, len) - rmsDb (t.x, s, len)));
            logMessage ("  S sounds changed by up to " + juce::String (worst, 2) + " dB");
            expectLessOrEqual (worst, 1.0);
        }

        beginTest ("A clean vocal (no breaths) passes unchanged, delayed by the look-ahead");
        {
            VocalSpec spec; spec.seconds = 8.0;
            const auto clean = makeVocal (spec).vocal[0];
            const auto out = run (clean, 48000.0, 100.0f);
            const int L = BreathModule::latencyFor (48000.0);
            double err = 0.0, sig = 0.0;
            for (size_t i = 0; i + L < clean.size(); ++i)
            {
                const double d = out[i + (size_t) L] - clean[i];
                err += d * d;
                sig += (double) clean[i] * clean[i];
            }
            const double errDb = 10.0 * std::log10 (err / sig + 1.0e-30);
            logMessage ("  error " + juce::String (errDb, 1) + " dB");
            expectLessThan (errDb, -50.0, "inaudible");
        }

        beginTest ("Block sizes 1, 64 and 2048 give the same output; off = the delayed dry");
        {
            const auto t = take (48000.0, 28.0f);
            const auto a = run (t.x, 48000.0, 80.0f, 1), b = run (t.x, 48000.0, 80.0f, 64), c = run (t.x, 48000.0, 80.0f, 2048);
            float worst = 0.0f;
            for (size_t i = 0; i < a.size(); ++i)
                worst = std::max ({ worst, std::abs (a[i] - b[i]), std::abs (a[i] - c[i]) });
            expectLessThan (worst, 1.0e-5f);

            BreathModule m;
            m.prepare (48000.0, 512);
            m.setSettings ({ false, 100.0f });
            auto x = t.x;
            for (int start = 0; start < (int) x.size(); start += 512)
            {
                float* ch[] { x.data() + start };
                m.process (ch, 1, std::min (512, (int) x.size() - start));
            }
            const int L = BreathModule::latencyFor (48000.0);
            float offErr = 0.0f;
            for (size_t i = 0; i + (size_t) L < x.size(); ++i)
                offErr = std::max (offErr, std::abs (x[i + (size_t) L] - t.x[i]));
            expectLessThan (offErr, 1.0e-6f);
        }
    }
};

static BreathTests breathTests;
