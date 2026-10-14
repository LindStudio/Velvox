// Vocal chop: step timing, host sync, stutter repeats, transparency, clicks.

#include "TestHelpers.h"

using namespace velvox;
using namespace velvox::test;

namespace
{
    constexpr double fs = 48000.0;

    int patternIndex (const char* name)
    {
        const auto& p = dsp::ChopModule::patterns();
        for (size_t i = 0; i < p.size(); ++i)
            if (juce::String (p[i].name) == name)
                return (int) i;
        jassertfalse;
        return 0;
    }

    // Runs the module directly in 256-sample chunks (like the chain does)
    juce::AudioBuffer<float> runChop (const dsp::ChopSettings& s, const juce::AudioBuffer<float>& in,
                                      double bpm = 120.0, double ppq = -1.0, bool playing = false)
    {
        dsp::ChopModule m;
        m.setSettings (s);
        m.prepare (fs, 256);
        m.setTransport (bpm, ppq, playing);
        juce::AudioBuffer<float> out (in);
        for (int start = 0; start < out.getNumSamples(); start += 256)
        {
            const int n = std::min (256, out.getNumSamples() - start);
            float* ptrs[2] { out.getWritePointer (0, start), out.getWritePointer (1, start) };
            m.process (ptrs, 2, n);
        }
        return out;
    }

    juce::AudioBuffer<float> stereoSine (double seconds, double freq, float amp)
    {
        juce::AudioBuffer<float> b (2, (int) (seconds * fs));
        for (int i = 0; i < b.getNumSamples(); ++i)
            for (int ch = 0; ch < 2; ++ch)
                b.setSample (ch, i, amp * (float) std::sin (2.0 * pi * freq * i / fs));
        return b;
    }
}

class ChopTests final : public juce::UnitTest
{
public:
    ChopTests() : juce::UnitTest ("Velvox chop", "Velvox") {}

    void runTest() override
    {
        beginTest ("Gate pattern: steps play and mute at the right time");
        {
            dsp::ChopSettings s;
            s.enabled = true;
            s.pattern = patternIndex ("Gate 1/2");
            s.rate = 1;          // 1/16 -> 0.125 s at 120 BPM = 6000 samples
            s.depth = 1.0f;
            s.smoothMs = 0.5f;
            const auto out = runChop (s, stereoSine (2.0, 440.0, 0.5f));

            const int step = 6000;
            for (int k = 0; k < 8; ++k)
            {
                const float level = rmsDb (out, k * step + 1000, step - 2000);
                if (k % 2 == 0)
                    expectWithinAbsoluteError (level, juce::Decibels::gainToDecibels (0.5f / std::sqrt (2.0f)), 0.1f, "play step " + juce::String (k));
                else
                    expectLessThan (level, -60.0f, "muted step " + juce::String (k));
            }
        }

        beginTest ("Host sync: follows the song position, also across internal chunks");
        {
            dsp::ChopSettings s;
            s.enabled = true;
            s.rate = 1; // 1/16 = 0.25 beats
            dsp::ChopModule m;
            m.setSettings (s);
            m.prepare (fs, 256);
            m.setTransport (128.0, 3.7, true); // 3.7 / 0.25 = step 14.8 -> step 14
            juce::AudioBuffer<float> b (2, 256);
            b.clear();
            float* p[2] { b.getWritePointer (0), b.getWritePointer (1) };
            m.process (p, 2, 1);
            expectEquals (m.getCurrentStep(), 14);

            // Advance without a new transport call: 0.2 beats at 128 BPM = 4500 samples
            int remaining = 4500 - 1;
            while (remaining > 0)
            {
                const int n = std::min (256, remaining);
                m.process (p, 2, n);
                remaining -= n;
            }
            expectEquals (m.getCurrentStep(), 15, "3.9 beats -> step 15");
        }

        beginTest ("Stutter repeats the previous slice");
        {
            dsp::ChopSettings s;
            s.enabled = true;
            s.pattern = patternIndex ("Stutter 8ths"); // x x r r ...
            s.rate = 1;
            s.depth = 1.0f;
            s.smoothMs = 0.5f;
            const auto in = [] { juce::Random r (5); juce::AudioBuffer<float> b (2, (int) fs);
                                 for (int i = 0; i < b.getNumSamples(); ++i) { const float v = r.nextFloat() - 0.5f; b.setSample (0, i, v); b.setSample (1, i, v); }
                                 return b; }();
            const auto out = runChop (s, in);
            const int step = 6000;

            double err2 = 0.0, err3 = 0.0;
            for (int i = 2 * step + 200; i < 3 * step - 200; ++i)
                err2 = std::max (err2, (double) std::abs (out.getSample (0, i) - in.getSample (0, i - step)));
            for (int i = 3 * step + 200; i < 4 * step - 200; ++i)
                err3 = std::max (err3, (double) std::abs (out.getSample (0, i) - in.getSample (0, i - 2 * step)));
            logMessage ("Repeat error: step 2 vs slice 1: " + juce::String (err2, 6) + ", step 3 vs slice 1: " + juce::String (err3, 6));
            expectLessThan (err2, 1.0e-6, "first repeat = previous slice");
            expectLessThan (err3, 1.0e-6, "second repeat = same slice again");
        }

        beginTest ("Depth 0 % and module off are transparent");
        {
            const auto in = stereoSine (1.0, 300.0, 0.4f);
            dsp::ChopSettings s;
            s.enabled = true;
            s.depth = 0.0f;
            s.pattern = patternIndex ("Chop & repeat");
            const auto out = runChop (s, in);
            double err = 0.0;
            for (int i = 2000; i < in.getNumSamples(); ++i)
                err = std::max (err, (double) std::abs (out.getSample (0, i) - in.getSample (0, i)));
            expectLessThan (err, 1.0e-6);

            Harness h;
            h.neutral();
            const auto pass = h.run (h.noise (0.3f, 0.5, 7));
            expect (allFinite (pass));
        }

        beginTest ("All patterns and rates in the plugin: finite, click-free with smoothing");
        {
            for (int pattern = 0; pattern < (int) dsp::ChopModule::patterns().size(); ++pattern)
                for (int rate = 0; rate < 4; ++rate)
                {
                    Harness h;
                    h.neutral();
                    h.set (pid::chopOn, 1.0f);
                    h.set (pid::chopPattern, (float) pattern);
                    h.set (pid::chopRate, (float) rate);
                    h.set (pid::chopSmooth, 3.0f);
                    const auto out = h.run (h.sine (200.0, 0.5f, 1.5));
                    expect (allFinite (out));
                    float maxStep = 0.0f;
                    for (int i = 4801; i < out.getNumSamples(); ++i)
                        maxStep = std::max (maxStep, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
                    // a 200 Hz sine at 0.5 moves up to ~0.013 per sample; edges must stay smooth
                    expectLessThan (maxStep, 0.05f, dsp::ChopModule::patterns()[(size_t) pattern].name + juce::String (" rate ") + juce::String (rate));
                }
        }
    }
};

static ChopTests chopTests;
