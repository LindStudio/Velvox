// RAUMRAUSCHEN (0.7, plan docs/superpowers/plans/2026-10-27-denoise-breath-modules.md, Task 2):
// lowers steady room noise in the gaps, leaves the voice alone, is transparent on a clean vocal,
// learns the noise also from a voice that never pauses, and is independent of block size and rate.

#include "TestHelpers.h"
#include "SyntheticVocal.h"
#include "DSP/DenoiseModule.h"

using namespace velvox;
using namespace velvox::test;
using dsp::DenoiseModule;

namespace
{
    // Pink-ish noise (Paul Kellet's filter) at an RMS in dBFS
    std::vector<float> pinkNoise (int n, float rmsDb, unsigned seed)
    {
        juce::Random rnd ((juce::int64) seed);
        std::vector<float> out ((size_t) n);
        float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        double sum = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const float w = rnd.nextFloat() * 2.0f - 1.0f;
            b0 = 0.99886f * b0 + w * 0.0555179f; b1 = 0.99332f * b1 + w * 0.0750759f; b2 = 0.96900f * b2 + w * 0.1538520f;
            b3 = 0.86650f * b3 + w * 0.3104856f; b4 = 0.55000f * b4 + w * 0.5329522f; b5 = -0.7616f * b5 - w * 0.0168980f;
            out[(size_t) i] = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f;
            b6 = w * 0.115926f;
            sum += out[(size_t) i] * out[(size_t) i];
        }
        const float scale = juce::Decibels::decibelsToGain (rmsDb) / (float) std::sqrt (sum / n);
        for (auto& x : out)
            x *= scale;
        return out;
    }

    std::vector<float> run (std::vector<float> x, double fs, float amount, int block = 512)
    {
        DenoiseModule m;
        m.prepare (fs, std::max (1, block));
        m.setSettings ({ true, amount });
        for (int start = 0; start < (int) x.size(); start += block)
        {
            float* ch[] { x.data() + start };
            m.process (ch, 1, std::min (block, (int) x.size() - start));
        }
        return x;
    }

    double rmsDb (const std::vector<float>& x, int from, int len)
    {
        double s = 0.0;
        for (int i = from; i < from + len; ++i)
            s += x[(size_t) i] * x[(size_t) i];
        return 10.0 * std::log10 (s / len + 1.0e-20);
    }

    struct Frames { std::vector<int> gaps, voiced; };

    // 20 ms frames of the clean vocal: gaps (< -70 dBFS, at least 100 ms away from any singing) and voiced (> -30 dBFS)
    Frames classify (const std::vector<float>& clean, double fs, int skip)
    {
        const int len = (int) (0.02 * fs), guard = (int) (0.1 * fs);
        Frames f;
        for (int s = skip; s + len + guard < (int) clean.size(); s += len)
        {
            const double l = rmsDb (clean, s, len);
            if (l > -30.0)
                f.voiced.push_back (s);
            else if (rmsDb (clean, std::max (0, s - guard), len + 2 * guard) < -70.0)
                f.gaps.push_back (s);
        }
        return f;
    }

    double meanDb (const std::vector<float>& x, const std::vector<int>& frames, int len)
    {
        double e = 0.0;
        for (int s : frames)
            e += std::pow (10.0, rmsDb (x, s, len) / 10.0);
        return 10.0 * std::log10 (e / std::max<size_t> (1, frames.size()));
    }
}

class DenoiseTests final : public juce::UnitTest
{
public:
    DenoiseTests() : juce::UnitTest ("Raumrauschen (denoise)", "Headliner") {}

    void runTest() override
    {
        constexpr int L = DenoiseModule::latency;

        beginTest ("Noise in the gaps drops by at least 12 dB at 100 %, the voice stays within 1 dB");
        {
            for (double fs : { 44100.0, 48000.0, 96000.0 })
            {
                VocalSpec spec; spec.sampleRate = fs; spec.seconds = 12.0;
                const auto clean = makeVocal (spec).vocal[0];
                const auto noise = pinkNoise ((int) clean.size(), -45.0f, 7);
                std::vector<float> in (clean.size());
                for (size_t i = 0; i < in.size(); ++i)
                    in[i] = clean[i] + noise[i];
                const auto out = run (in, fs, 100.0f);

                // Compare the output at t + L with the input at t, after 2 s of learning
                const auto frames = classify (clean, fs, (int) (2.0 * fs));
                const int len = (int) (0.02 * fs);
                std::vector<float> outAligned (out.begin() + L, out.end());
                outAligned.resize (out.size(), 0.0f);
                const double noiseIn = meanDb (in, frames.gaps, len), noiseOut = meanDb (outAligned, frames.gaps, len);
                const double voiceIn = meanDb (in, frames.voiced, len), voiceOut = meanDb (outAligned, frames.voiced, len);
                logMessage ("  " + juce::String (fs / 1000.0, 1) + " kHz: gaps " + juce::String (noiseIn, 1) + " -> " + juce::String (noiseOut, 1)
                            + " dB, voice " + juce::String (voiceIn, 2) + " -> " + juce::String (voiceOut, 2) + " dB ("
                            + juce::String ((int) frames.gaps.size()) + " gap / " + juce::String ((int) frames.voiced.size()) + " voiced frames)");
                expect (frames.gaps.size() > 20 && frames.voiced.size() > 100, "enough gaps and singing in the test vocal");
                expectGreaterOrEqual (noiseIn - noiseOut, 12.0, "noise reduction at " + juce::String (fs));
                expectLessOrEqual (std::abs (voiceIn - voiceOut), 1.0, "voice level at " + juce::String (fs));
            }
        }

        beginTest ("A clean vocal passes unchanged (delayed by the latency)");
        {
            VocalSpec spec; spec.seconds = 6.0;
            const auto clean = makeVocal (spec).vocal[0];
            const auto out = run (clean, 48000.0, 100.0f);
            double err = 0.0, sig = 0.0;
            for (size_t i = (size_t) (2 * L); i + L < clean.size(); ++i)
            {
                const double d = out[i + L] - clean[i];
                err += d * d;
                sig += (double) clean[i] * clean[i];
            }
            const double errDb = 10.0 * std::log10 (err / sig + 1.0e-30);
            logMessage ("  error " + juce::String (errDb, 1) + " dB");
            expectLessThan (errDb, -60.0);
        }

        beginTest ("A voice that never pauses keeps its level (the floor is learned under the voice too)");
        {
            const double fs = 48000.0;
            const int n = (int) (10.0 * fs);
            std::vector<float> voice ((size_t) n);
            double phase = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const double f0 = 220.0 * (1.0 + 0.01 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.0 * i / fs));
                phase += f0 / fs;
                float v = 0.0f;
                for (int h = 1; h <= 20; ++h)
                    v += (float) (std::sin (2.0 * juce::MathConstants<double>::pi * h * phase) / h);
                voice[(size_t) i] = 0.1f * v;
            }
            const auto noise = pinkNoise (n, -50.0f, 3);
            std::vector<float> in ((size_t) n);
            for (int i = 0; i < n; ++i)
                in[(size_t) i] = voice[(size_t) i] + noise[(size_t) i];
            const auto out = run (in, fs, 100.0f);
            const double before = rmsDb (in, (int) (3.0 * fs), (int) (6.0 * fs));
            const double after = rmsDb (out, (int) (3.0 * fs) + L, (int) (6.0 * fs));
            logMessage ("  sustained voice " + juce::String (before, 2) + " -> " + juce::String (after, 2) + " dB");
            expectLessOrEqual (before - after, 1.0);
        }

        beginTest ("Block sizes 1, 64 and 2048 give the same output");
        {
            VocalSpec spec; spec.seconds = 3.0;
            const auto clean = makeVocal (spec).vocal[0];
            const auto noise = pinkNoise ((int) clean.size(), -45.0f, 11);
            std::vector<float> in (clean.size());
            for (size_t i = 0; i < in.size(); ++i)
                in[i] = clean[i] + noise[i];
            const auto a = run (in, 48000.0, 70.0f, 1), b = run (in, 48000.0, 70.0f, 64), c = run (in, 48000.0, 70.0f, 2048);
            float worst = 0.0f;
            for (size_t i = 0; i < a.size(); ++i)
                worst = std::max ({ worst, std::abs (a[i] - b[i]), std::abs (a[i] - c[i]) });
            expectLessThan (worst, 1.0e-4f);
        }

        beginTest ("Off = the dry delayed by the latency; switching on and off is click free");
        {
            VocalSpec spec; spec.seconds = 4.0;
            auto in = makeVocal (spec).vocal[0];
            const auto noise = pinkNoise ((int) in.size(), -40.0f, 5);
            for (size_t i = 0; i < in.size(); ++i)
                in[i] += noise[i];
            DenoiseModule m;
            m.prepare (48000.0, 512);
            auto out = in;
            float maxStep = 0.0f, maxStepIn = 0.0f;
            for (int start = 0, block = 0; start < (int) out.size(); start += 512, ++block)
            {
                m.setSettings ({ (block / 40) % 2 == 1, 100.0f });
                float* ch[] { out.data() + start };
                m.process (ch, 1, std::min (512, (int) out.size() - start));
            }
            for (size_t i = 1; i < out.size(); ++i)
            {
                maxStep = std::max (maxStep, std::abs (out[i] - out[i - 1]));
                maxStepIn = std::max (maxStepIn, std::abs (in[i] - in[i - 1]));
            }
            expectLessThan (maxStep, maxStepIn * 1.2f + 1.0e-4f, "no step larger than the input's own");
            // First 40 blocks: off -> exactly the delayed input (after the window fill)
            float offErr = 0.0f;
            for (int i = 2 * L; i < 40 * 512 - L; ++i)
                offErr = std::max (offErr, std::abs (out[(size_t) (i + L)] - in[(size_t) i]));
            expectLessThan (offErr, 1.0e-4f);
        }
    }
};

static DenoiseTests denoiseTests;
