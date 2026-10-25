// 0.3 modules: PULSE, VOCAL SMASH, CHOIR, SMOOTH, VOCAL CHECK, LEVEL RIDE.
// Each module is measured on its own with test signals, then all of them
// together through the real processor.

#include "TestHelpers.h"
#include "DSP/PulseModule.h"
#include "DSP/SmashModule.h"
#include "DSP/ChoirModule.h"
#include "DSP/SmoothModule.h"
#include "DSP/RideModule.h"
#include "DSP/VocalAnalyzer.h"

using namespace velvox;
using namespace velvox::test;
using namespace velvox::dsp;

namespace
{
    constexpr int chunk = 256;

    // Runs a module over a stereo buffer in chunks (like the chain does)
    template <typename Module>
    void runModule (Module& m, juce::AudioBuffer<float>& b, std::function<void (int start)> beforeChunk = {})
    {
        for (int start = 0; start < b.getNumSamples(); start += chunk)
        {
            const int n = std::min (chunk, b.getNumSamples() - start);
            if (beforeChunk)
                beforeChunk (start);
            float* ch[2] { b.getWritePointer (0, start), b.getWritePointer (std::min (1, b.getNumChannels() - 1), start) };
            m.process (ch, b.getNumChannels(), n);
        }
    }

    juce::AudioBuffer<float> constant (int channels, int n, float v)
    {
        juce::AudioBuffer<float> b (channels, n);
        for (int c = 0; c < channels; ++c)
            juce::FloatVectorOperations::fill (b.getWritePointer (c), v, n);
        return b;
    }

    juce::AudioBuffer<float> noiseBuffer (int channels, int n, float amp, int seed)
    {
        juce::Random r (seed);
        juce::AudioBuffer<float> b (channels, n);
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < n; ++i)
                b.setSample (c, i, amp * (r.nextFloat() * 2.0f - 1.0f));
        return b;
    }

    double maxAbsDiff (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        double d = 0.0;
        for (int c = 0; c < a.getNumChannels(); ++c)
            for (int i = 0; i < a.getNumSamples(); ++i)
                d = std::max (d, (double) std::abs (a.getSample (c, i) - b.getSample (c, i)));
        return d;
    }

    float rms (const juce::AudioBuffer<float>& b, int start, int len)
    {
        double s = 0.0;
        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int i = start; i < start + len; ++i)
                s += (double) b.getSample (c, i) * b.getSample (c, i);
        return (float) std::sqrt (s / (len * b.getNumChannels()));
    }

    float db (float lin) { return juce::Decibels::gainToDecibels (lin, -200.0f); }

    double correlation (const juce::AudioBuffer<float>& a, int chA, const juce::AudioBuffer<float>& b, int chB, int start, int len)
    {
        double ab = 0, aa = 0, bb = 0;
        for (int i = start; i < start + len; ++i)
        {
            const double x = a.getSample (chA, i), y = b.getSample (chB, i);
            ab += x * y; aa += x * x; bb += y * y;
        }
        return ab / std::sqrt (std::max (1.0e-20, aa * bb));
    }

    // Vowel-like tone: harmonic series on a note that changes every 250 ms
    void addVowel (juce::AudioBuffer<float>& b, int start, int len, float amp, double fs, float f0, double& phase)
    {
        for (int i = start; i < start + len && i < b.getNumSamples(); ++i)
        {
            float s = 0.0f;
            for (int h = 1; h <= 20; ++h)
            {
                const double f = f0 * h;
                if (f > fs * 0.45)
                    break;
                // Formant-ish envelope: strong 300 Hz - 1.5 kHz, falling above
                const double env = (f < 300 ? 0.6 : f < 1500 ? 1.0 : 1500.0 / f) / h;
                s += (float) (env * std::sin (phase * h));
            }
            phase += 2.0 * pi * f0 / fs;
            for (int c = 0; c < b.getNumChannels(); ++c)
                b.addSample (c, i, amp * s);
        }
    }

    void addHissing (juce::AudioBuffer<float>& b, int start, int len, float amp, double fs, juce::Random& r)
    {
        Biquad hp;
        hp.setCoeffs (design::highPass (fs, 5000.0, 0.7071));
        Biquad hp2;
        hp2.setCoeffs (design::highPass (fs, 5000.0, 0.7071));
        for (int i = start; i < start + len && i < b.getNumSamples(); ++i)
        {
            const float s = hp2.process (hp.process (r.nextFloat() * 2.0f - 1.0f, 0), 0);
            for (int c = 0; c < b.getNumChannels(); ++c)
                b.addSample (c, i, amp * s);
        }
    }

    // Normalises a region of a buffer to the given RMS in dBFS
    void setRms (juce::AudioBuffer<float>& b, int start, int len, float dbfs)
    {
        const float now = rms (b, start, len);
        if (now <= 0.0f)
            return;
        const float g = juce::Decibels::decibelsToGain (dbfs) / now;
        for (int c = 0; c < b.getNumChannels(); ++c)
            b.applyGain (c, start, len, g);
    }

    juce::AudioBuffer<float> vocalLike (double fs, double seconds, float vowelDb, float sDb, int seed)
    {
        const int n = (int) (seconds * fs);
        juce::AudioBuffer<float> b (2, n);
        b.clear();
        juce::Random r (seed);
        const float notes[] { 196.0f, 220.0f, 246.9f, 261.6f, 293.7f, 329.6f, 220.0f, 174.6f };
        double phase = 0.0;
        int pos = 0, k = 0;
        while (pos < n)
        {
            const int vowelLen = (int) (0.30 * fs), sLen = (int) (0.12 * fs), gapLen = (int) (0.08 * fs);
            addVowel (b, pos, vowelLen, 1.0f, fs, notes[k++ % 8], phase);
            setRms (b, pos, std::min (vowelLen, n - pos), vowelDb);
            pos += vowelLen;
            if (pos >= n) break;
            addHissing (b, pos, sLen, 1.0f, fs, r);
            setRms (b, pos, std::min (sLen, n - pos), sDb);
            pos += sLen + gapLen;
        }
        return b;
    }

    AnalysisResult analyseBuffer (const juce::AudioBuffer<float>& b, double fs)
    {
        CheckModule check;
        check.setEnabled (true);
        VocalAnalyzer analyzer (check);
        analyzer.setSampleRate (fs);
        for (int start = 0; start < b.getNumSamples(); start += 512)
        {
            const int n = std::min (512, b.getNumSamples() - start);
            const float* ch[2] { b.getReadPointer (0, start), b.getReadPointer (1, start) };
            check.process (ch, 2, n);
            analyzer.pump();
        }
        return analyzer.analyse();
    }

    bool hasFinding (const AnalysisResult& r, std::optional<ModuleId> module, bool sureOnly)
    {
        for (auto& f : r.findings)
            if (f.module == module && (! sureOnly || f.certainty == Finding::Certainty::sure))
                return true;
        return false;
    }
}

class NewModuleTests final : public juce::UnitTest
{
public:
    NewModuleTests() : juce::UnitTest ("Velvox 0.3 modules", "Velvox") {}

    void runTest() override
    {
        pulseTests();
        smashTests();
        choirTests();
        smoothTests();
        rideTests();
        checkTests();
        integrationTests();
    }

    //==========================================================================
    void pulseTests()
    {
        beginTest ("PULSE: curve evaluation (linear, smooth, step, wrap-around)");
        {
            PulseSettings s;
            s.x = { 0.0f, 0.25f, 0.5f, 0.75f, 0.9f, 0.95f };
            s.y = { 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f };
            s.interpolation = 0;
            expectWithinAbsoluteError (PulseModule::evaluate (s, 0.125f), 0.5f, 1.0e-4f);
            expectWithinAbsoluteError (PulseModule::evaluate (s, 0.4f), 1.0f, 1.0e-4f);
            expectWithinAbsoluteError (PulseModule::evaluate (s, 0.625f), 0.5f, 1.0e-4f);
            s.interpolation = 2;
            expectWithinAbsoluteError (PulseModule::evaluate (s, 0.2f), 0.0f, 1.0e-4f);
            s.interpolation = 1;
            expectWithinAbsoluteError (PulseModule::evaluate (s, 0.125f), 0.5f, 1.0e-4f);
            expectLessThan (PulseModule::evaluate (s, 0.05f), 0.2f * 0.5f + 0.05f);
            // Unsorted points are sorted internally; the curve wraps from the last point to the first
            s.x = { 0.8f, 0.2f, 0.5f, 0.6f, 0.7f, 0.9f };
            s.y = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
            s.interpolation = 0;
            // phase 0.05 lies between (0.9, 1) and (0.2 + 1, 0): halfway -> 0.5
            expectWithinAbsoluteError (PulseModule::evaluate (s, 0.05f), 0.5f, 1.0e-3f);
        }

        beginTest ("PULSE: curve shapes the gain in sync with the host position");
        {
            for (double fs : { 44100.0, 48000.0, 96000.0 })
            {
                PulseModule m;
                m.prepare (fs, chunk);
                PulseSettings s;
                s.enabled = true;
                s.depth = 1.0f;
                s.rate = 2;               // 1/4 -> one cycle per beat
                s.interpolation = 0;
                s.attackMs = 0.5f; s.releaseMs = 0.5f; s.smoothMs = 0.0f;
                s.x = { 0.0f, 0.45f, 0.5f, 0.95f, 0.97f, 0.99f };
                s.y = { 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f };
                m.setSettings (s);

                const double bpm = 120.0;
                auto b = constant (2, (int) (fs * 2.0), 1.0f);
                runModule (m, b, [&] (int start) { m.setTransport (bpm, start / fs * bpm / 60.0, true); });

                // Beat = 0.5 s: first half loud, second half silent
                const int beat = (int) (fs * 0.5);
                const float high = b.getSample (0, beat + beat / 4);
                const float low = b.getSample (0, beat + beat * 3 / 4);
                expectWithinAbsoluteError (high, 1.0f, 0.01f, "curve high at " + juce::String (fs));
                expectWithinAbsoluteError (low, 0.0f, 0.01f, "curve low at " + juce::String (fs));
            }
        }

        beginTest ("PULSE: follows loops / position jumps");
        {
            PulseModule m;
            m.prepare (48000.0, chunk);
            PulseSettings s;
            s.enabled = true; s.depth = 1.0f; s.rate = 2; s.interpolation = 0;
            s.attackMs = 0.5f; s.releaseMs = 0.5f; s.smoothMs = 0.0f;
            s.x = { 0.0f, 0.45f, 0.5f, 0.95f, 0.97f, 0.99f };
            s.y = { 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f };
            m.setSettings (s);
            auto b = constant (2, 48000, 1.0f);
            // Host loops: jumps to beat position 7.75 (second half of a beat -> silent) at every block
            runModule (m, b, [&] (int start) { m.setTransport (120.0, 7.75 + 0.0 * start, true); });
            expectLessThan (b.getSample (0, 40000), 0.05f, "position from the host, not free running");
        }

        beginTest ("PULSE: manual BPM when the host sends no tempo");
        {
            const double fs = 48000.0;
            PulseModule m;
            m.prepare (fs, chunk);
            PulseSettings s;
            s.enabled = true; s.depth = 1.0f; s.rate = 2; s.interpolation = 0; s.manualBpm = 150.0f;
            s.attackMs = 0.5f; s.releaseMs = 0.5f; s.smoothMs = 0.0f;
            s.x = { 0.0f, 0.45f, 0.5f, 0.95f, 0.97f, 0.99f };
            s.y = { 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f };
            m.setSettings (s);
            auto b = constant (2, (int) fs * 3, 1.0f);
            runModule (m, b, [&] (int) { m.setTransport (0.0, -1.0, false); });

            // Falling edges below 0.5 are one beat apart: 60 / 150 = 0.4 s
            std::vector<int> edges;
            for (int i = 1; i < b.getNumSamples(); ++i)
                if (b.getSample (0, i - 1) >= 0.5f && b.getSample (0, i) < 0.5f)
                    edges.push_back (i);
            expectGreaterThan ((int) edges.size(), 5);
            const double period = (edges.back() - edges.front()) / (double) (edges.size() - 1) / fs;
            expectWithinAbsoluteError (period, 0.4, 0.002);
        }

        beginTest ("PULSE: hard step curve stays click free; mix 0 is transparent");
        {
            const double fs = 48000.0;
            PulseModule m;
            m.prepare (fs, chunk);
            PulseSettings s;
            s.enabled = true; s.depth = 1.0f; s.rate = 4; s.interpolation = 2;  // 1/16 hard steps
            s.attackMs = 1.0f; s.releaseMs = 3.0f; s.smoothMs = 2.0f;
            s.y = { 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f };
            s.x = { 0.0f, 0.16f, 0.33f, 0.5f, 0.66f, 0.83f };
            m.setSettings (s);
            auto b = constant (2, (int) fs * 2, 1.0f);
            runModule (m, b, [&] (int start) { m.setTransport (174.0, start / fs * 174.0 / 60.0, true); });
            float maxStep = 0.0f;
            for (int i = 1; i < b.getNumSamples(); ++i)
                maxStep = std::max (maxStep, std::abs (b.getSample (0, i) - b.getSample (0, i - 1)));
            logMessage ("Largest gain step per sample: " + juce::String (maxStep, 4));
            expectLessThan (maxStep, 0.03f, "no hard gain jumps");

            PulseModule m2;
            m2.prepare (fs, chunk);
            s.mix = 0.0f;
            m2.setSettings (s);
            const auto in = noiseBuffer (2, 48000, 0.5f, 3);
            auto out = in;
            runModule (m2, out, [&] (int start) { m2.setTransport (120.0, start / fs * 2.0, true); });
            expectEquals (maxAbsDiff (in, out), 0.0, "mix 0 = untouched");
        }
    }

    //==========================================================================
    void smashTests()
    {
        const double fs = 48000.0;

        beginTest ("SMASH: blend 0 is bit-exact, the original is never altered");
        {
            SmashModule m;
            m.prepare (fs, chunk);
            SmashSettings s;
            s.enabled = true; s.amount = 1.0f; s.blend = 0.0f;
            m.setSettings (s);
            const auto in = noiseBuffer (2, 48000, 0.5f, 5);
            auto out = in;
            runModule (m, out);
            expectEquals (maxAbsDiff (in, out), 0.0);
        }

        beginTest ("SMASH: lifts quiet passages, keeps loud ones, same polarity, controlled level");
        {
            SmashModule m;
            m.prepare (fs, chunk);
            SmashSettings s;
            s.enabled = true; s.amount = 0.7f; s.blend = 0.6f; s.tone = 0.0f;
            m.setSettings (s);

            // 1 s quiet (-30 dBFS RMS) / 1 s loud (-12 dBFS RMS), four times
            const int sec = (int) fs;
            auto in = noiseBuffer (2, sec * 8, 1.0f, 7);
            for (int k = 0; k < 8; ++k)
                setRms (in, k * sec, sec, k % 2 == 0 ? -30.0f : -12.0f);
            auto out = in;
            runModule (m, out);

            const float inDiff = db (rms (in, sec * 7 + sec / 2, sec / 2)) - db (rms (in, sec * 6 + sec / 2, sec / 2));
            const float outDiff = db (rms (out, sec * 7 + sec / 2, sec / 2)) - db (rms (out, sec * 6 + sec / 2, sec / 2));
            logMessage ("Loud - quiet: input " + juce::String (inDiff, 1) + " dB, output " + juce::String (outDiff, 1) + " dB");
            expectLessThan (outDiff, inDiff - 4.0f, "denser");
            expectGreaterThan (outDiff, 6.0f, "loud parts stay loud - dynamics are not flattened");

            const double corr = correlation (in, 0, out, 0, sec * 6, sec * 2);
            expectGreaterThan (corr, 0.95, "no polarity or timing problems");

            const float loudChange = db (rms (out, sec * 7 + sec / 2, sec / 2)) - db (rms (in, sec * 7 + sec / 2, sec / 2));
            logMessage ("Level change on loud parts: " + juce::String (loudChange, 2) + " dB");
            expectWithinAbsoluteError (loudChange, 0.0f, 3.0f, "controlled output level");
            expectGreaterThan (m.gainReductionDb.pull(), 6.0f, "squashed path really compresses");
        }

        beginTest ("SMASH: breaths and room noise are not pulled up");
        {
            SmashModule m;
            m.prepare (fs, chunk);
            SmashSettings s;
            s.enabled = true; s.amount = 1.0f; s.blend = 1.0f;
            m.setSettings (s);
            auto in = noiseBuffer (2, (int) fs * 2, 1.0f, 9);
            setRms (in, 0, in.getNumSamples(), -62.0f);
            auto out = in;
            runModule (m, out);
            const float change = db (rms (out, (int) fs, (int) fs)) - db (rms (in, (int) fs, (int) fs));
            logMessage ("Change on -62 dBFS noise at full blend: " + juce::String (change, 2) + " dB");
            expectLessThan (change, 1.0f);
        }
    }

    //==========================================================================
    void choirTests()
    {
        const double fs = 48000.0;

        beginTest ("CHOIR: mix 0 is transparent");
        {
            ChoirModule m;
            m.prepare (fs, chunk);
            ChoirSettings s;
            s.enabled = true; s.mix = 0.0f; s.voices = 8;
            m.setSettings (s);
            const auto in = noiseBuffer (2, 48000, 0.5f, 11);
            auto out = in;
            runModule (m, out);
            expectEquals (maxAbsDiff (in, out), 0.0);
        }

        beginTest ("CHOIR: voices are really detuned (constant pitch offsets, not a wobble)");
        {
            ChoirModule m;
            m.prepare (fs, chunk);
            ChoirSettings s;
            s.enabled = true; s.voices = 2; s.detuneCents = 30.0f; s.timingMs = 0.0f; s.humanize = 0.0f;
            s.width = 0.0f; s.mix = 1.0f; s.density = 1.0f;
            m.setSettings (s);

            juce::AudioBuffer<float> b (2, (int) fs * 6);
            for (int i = 0; i < b.getNumSamples(); ++i)
                for (int c = 0; c < 2; ++c)
                    b.setSample (c, i, 0.3f * (float) std::sin (2.0 * pi * 440.0 * i / fs));
            runModule (m, b);

            const int start = (int) fs, len = (int) fs * 4;
            const double down = toneLevel (b, start, len, 440.0 * std::pow (2.0, -30.0 / 1200.0), fs);
            const double up = toneLevel (b, start, len, 440.0 * std::pow (2.0, 27.0 / 1200.0), fs);
            const double orig = toneLevel (b, start, len, 440.0, fs);
            logMessage ("Voice levels: -30 ct " + juce::String (down, 4) + ", +27 ct " + juce::String (up, 4) + ", 440 Hz " + juce::String (orig, 4));
            expectGreaterThan (down, orig * 2.0, "lower voice at its own pitch");
            expectGreaterThan (up, orig * 2.0, "upper voice at its own pitch");
        }

        beginTest ("CHOIR: width spreads the voices; humanize keeps them moving");
        {
            const auto in = noiseBuffer (2, (int) fs * 3, 0.3f, 13);
            for (float w : { 0.0f, 1.0f })
            {
                ChoirModule m;
                m.prepare (fs, chunk);
                ChoirSettings s;
                s.enabled = true; s.voices = 8; s.width = w; s.mix = 1.0f;
                m.setSettings (s);
                // Mono source so only the choir creates stereo
                auto b = in;
                b.copyFrom (1, 0, b, 0, 0, b.getNumSamples());
                runModule (m, b);
                const double corr = correlation (b, 0, b, 1, (int) fs, (int) fs * 2);
                logMessage ("Width " + juce::String (w) + ": L/R correlation " + juce::String (corr, 3));
                if (w == 0.0f) expectGreaterThan (corr, 0.98);
                else           expectLessThan (corr, 0.8);
                expectWithinAbsoluteError ((double) m.outputCorrelation.load(), corr, 0.15, "mono meter reports it");
            }

            ChoirModule m;
            m.prepare (fs, chunk);
            ChoirSettings s;
            s.enabled = true; s.voices = 4; s.humanize = 1.0f;
            m.setSettings (s);
            auto b = noiseBuffer (2, (int) fs * 4, 0.3f, 15);
            float minC = 1.0e9f, maxC = -1.0e9f;
            runModule (m, b, [&] (int) { const float c = m.voiceCents[0].load(); minC = std::min (minC, c); maxC = std::max (maxC, c); });
            logMessage ("Voice 1 pitch drift over 4 s: " + juce::String (maxC - minC, 2) + " ct");
            expectGreaterThan (maxC - minC, 1.0f);
        }

        beginTest ("CHOIR: voice count changes during playback without clicks");
        {
            ChoirModule m;
            m.prepare (fs, chunk);
            ChoirSettings s;
            s.enabled = true; s.mix = 0.5f;
            auto b = juce::AudioBuffer<float> (2, (int) fs * 2);
            for (int i = 0; i < b.getNumSamples(); ++i)
                for (int c = 0; c < 2; ++c)
                    b.setSample (c, i, 0.3f * (float) std::sin (2.0 * pi * 220.0 * i / fs));
            int k = 0;
            runModule (m, b, [&] (int) { s.voices = 2 + 2 * ((k++ / 20) % 4); m.setSettings (s); });
            float maxStep = 0.0f;
            for (int i = 1; i < b.getNumSamples(); ++i)
                maxStep = std::max (maxStep, std::abs (b.getSample (0, i) - b.getSample (0, i - 1)));
            // A 220 Hz sine at 0.3 changes by at most ~0.009 per sample; voices add similar slopes
            expectLessThan (maxStep, 0.06f);
            expect (allFinite (b));
        }
    }

    //==========================================================================
    void smoothTests()
    {
        const double fs = 48000.0;
        const auto makeSignal = [&] (bool withResonance, int seed)
        {
            auto b = noiseBuffer (2, (int) fs * 4, 0.2f, seed);
            if (withResonance)
                for (int i = 0; i < b.getNumSamples(); ++i)
                    for (int c = 0; c < 2; ++c)
                        b.addSample (c, i, 0.25f * (float) std::sin (2.0 * pi * 3000.0 * i / fs));
            return b;
        };

        beginTest ("SMOOTH: reduces a ringing resonance, leaves the rest of the spectrum");
        {
            SmoothModule m;
            m.prepare (fs, chunk);
            SmoothSettings s;
            s.enabled = true; s.amount = 0.8f; s.sensitivity = 0.6f; s.lowHz = 500.0f; s.highHz = 15000.0f;
            m.setSettings (s);
            const auto in = makeSignal (true, 17);
            auto out = in;
            float maxCut = 0.0f;
            int maxBand = -1;
            runModule (m, out, [&] (int start)
            {
                if (start > (int) fs * 3)
                    for (int bnd = 0; bnd < SmoothModule::numBands; ++bnd)
                        if (m.bandCutDb[(size_t) bnd].load() > maxCut)
                        {
                            maxCut = m.bandCutDb[(size_t) bnd].load();
                            maxBand = bnd;
                        }
            });

            const int start = (int) fs * 2, len = (int) fs * 2;
            const double toneIn = toneLevel (in, start, len, 3000.0, fs), toneOut = toneLevel (out, start, len, 3000.0, fs);
            const double reduction = 20.0 * std::log10 (toneIn / toneOut);
            logMessage ("3 kHz resonance reduced by " + juce::String (reduction, 1) + " dB; strongest band "
                        + juce::String (SmoothModule::bandFrequency (maxBand), 0) + " Hz at " + juce::String (maxCut, 1) + " dB");
            expectGreaterThan (reduction, 4.0);
            expect (maxBand >= 0 && std::abs (std::log2 (SmoothModule::bandFrequency (maxBand) / 3000.0f)) < 0.35f,
                    "the display shows the cut where the resonance is");
        }

        beginTest ("SMOOTH: no resonance -> (almost) nothing happens, no overall dulling");
        {
            SmoothModule m;
            m.prepare (fs, chunk);
            SmoothSettings s;
            s.enabled = true; s.amount = 1.0f; s.sensitivity = 1.0f; s.lowHz = 100.0f; s.highHz = 18000.0f;
            m.setSettings (s);
            const auto in = makeSignal (false, 19);
            auto out = in;
            runModule (m, out);
            const float change = db (rms (out, (int) fs * 2, (int) fs * 2)) - db (rms (in, (int) fs * 2, (int) fs * 2));

            // High band energy (above 6 kHz) must not drop noticeably
            const auto highRms = [&] (const juce::AudioBuffer<float>& b)
            {
                Biquad hp;
                hp.setCoeffs (design::highPass (fs, 6000.0, 0.7071));
                double sum = 0.0;
                for (int i = 0; i < (int) fs * 4; ++i)
                {
                    const float y = hp.process (b.getSample (0, i), 0);
                    if (i > (int) fs * 2)
                        sum += (double) y * y;
                }
                return 10.0 * std::log10 (sum);
            };
            const double highChange = highRms (out) - highRms (in);
            logMessage ("Flat noise at maximum settings: level " + juce::String (change, 2) + " dB, above 6 kHz " + juce::String (highChange, 2) + " dB");
            expectWithinAbsoluteError (change, 0.0f, 1.0f);
            expectGreaterThan (highChange, -1.5);
        }

        beginTest ("SMOOTH: frequency range is respected; delta plays only what is removed");
        {
            SmoothModule m;
            m.prepare (fs, chunk);
            SmoothSettings s;
            s.enabled = true; s.amount = 1.0f; s.sensitivity = 0.6f; s.lowHz = 6000.0f; s.highHz = 14000.0f;
            m.setSettings (s);
            const auto in = makeSignal (true, 21);
            auto out = in;
            runModule (m, out);
            const int start = (int) fs * 2, len = (int) fs * 2;
            const double reduction = 20.0 * std::log10 (toneLevel (in, start, len, 3000.0, fs) / toneLevel (out, start, len, 3000.0, fs));
            expectLessThan (reduction, 1.0, "3 kHz is outside 6-14 kHz");

            SmoothModule d;
            d.prepare (fs, chunk);
            s.lowHz = 500.0f; s.delta = true;
            d.setSettings (s);
            auto delta = in;
            runModule (d, delta);
            const double toneDelta = toneLevel (delta, start, len, 3000.0, fs);
            const double removedDb = 20.0 * std::log10 (toneDelta / toneLevel (in, start, len, 3000.0, fs));
            logMessage ("Delta signal at 3 kHz: " + juce::String (removedDb, 1) + " dB relative to the input tone");
            expectGreaterThan (removedDb, -12.0, "delta contains the removed resonance");
            expectLessThan (db (rms (delta, start, len)), db (rms (in, start, len)) - 3.0f, "but not the whole signal");
        }
    }

    //==========================================================================
    void rideTests()
    {
        const double fs = 48000.0;

        beginTest ("LEVEL RIDE: evens out quiet and loud phrases");
        {
            RideModule m;
            m.prepare (fs, chunk);
            RideSettings s;
            s.enabled = true;   // defaults: target -18, amount 70 %, +-8 dB
            s.phrase = 0.2f;    // mostly syllable level: the alternating test phrases are independent
            m.setSettings (s);
            const int phrase = (int) (fs * 2.0);
            auto in = noiseBuffer (2, phrase * 10, 1.0f, 23);
            for (int k = 0; k < 10; ++k)
                setRms (in, k * phrase, phrase, k % 2 == 0 ? -32.0f : -8.0f);   // quiet below, loud above the target
            auto out = in;
            float gainQuiet = 0.0f, gainLoud = 0.0f, levelQuiet = 0.0f, levelLoud = 0.0f;
            runModule (m, out, [&] (int start)
            {
                if (start / chunk == (8 * phrase + phrase * 3 / 4) / chunk) { gainQuiet = m.currentGainDb.load(); levelQuiet = m.detectedLevelDb.load(); }
                if (start / chunk == (9 * phrase + phrase * 3 / 4) / chunk) { gainLoud = m.currentGainDb.load(); levelLoud = m.detectedLevelDb.load(); }
            });
            logMessage ("Detected / gain: quiet " + juce::String (levelQuiet, 1) + " dB / " + juce::String (gainQuiet, 1)
                        + " dB, loud " + juce::String (levelLoud, 1) + " dB / " + juce::String (gainLoud, 1) + " dB");

            const auto level = [&] (const juce::AudioBuffer<float>& b, int k) { return db (rms (b, k * phrase + phrase / 2, phrase / 2)); };
            const float inDiff = level (in, 9) - level (in, 8), outDiff = level (out, 9) - level (out, 8);
            logMessage ("Loud - quiet phrase: input " + juce::String (inDiff, 1) + " dB, output " + juce::String (outDiff, 1) + " dB");
            expectLessThan (outDiff, inDiff - 6.0f);
            expectGreaterThan (outDiff, 0.0f, "the order of loud and quiet is kept - no inversion");
        }

        beginTest ("LEVEL RIDE: pauses are never boosted (breaths / room noise)");
        {
            RideModule m;
            m.prepare (fs, chunk);
            RideSettings s;
            s.enabled = true; s.maxBoostDb = 18.0f; s.amount = 1.0f;
            m.setSettings (s);
            const int sec = (int) fs;
            auto in = noiseBuffer (2, sec * 6, 1.0f, 25);
            setRms (in, 0, sec * 2, -28.0f);          // quiet singing -> boosted
            setRms (in, sec * 2, sec * 4, -66.0f);    // pause with room noise
            auto out = in;
            float maxGainInPause = -100.0f;
            runModule (m, out, [&] (int start) { if (start > sec * 3) maxGainInPause = std::max (maxGainInPause, m.currentGainDb.load()); });
            const float pauseChange = db (rms (out, sec * 4, sec * 2)) - db (rms (in, sec * 4, sec * 2));
            logMessage ("Gain in the pause: max " + juce::String (maxGainInPause, 2) + " dB, level change " + juce::String (pauseChange, 2) + " dB");
            expectLessThan (maxGainInPause, 0.5f);
            expectLessThan (pauseChange, 0.5f);
        }

        beginTest ("LEVEL RIDE: transients do not make it duck; amount 0 is transparent");
        {
            const int sec = (int) fs;
            auto in = noiseBuffer (2, sec * 6, 1.0f, 27);
            setRms (in, 0, in.getNumSamples(), -24.0f);
            // 5 ms bursts 18 dB louder every 250 ms (consonants)
            for (int pos = sec; pos < in.getNumSamples(); pos += sec / 4)
                for (int c = 0; c < 2; ++c)
                    in.applyGain (c, pos, (int) (0.005 * fs), 8.0f);

            float range[2] {};
            for (int t = 0; t < 2; ++t)
            {
                RideModule m;
                m.prepare (fs, chunk);
                RideSettings s;
                s.enabled = true; s.transient = t == 0 ? 0.0f : 1.0f; s.response = 1.0f; s.phrase = 0.0f;
                m.setSettings (s);
                auto out = in;
                float lo = 100.0f, hi = -100.0f;
                runModule (m, out, [&] (int start) { if (start > sec * 2) { lo = std::min (lo, m.currentGainDb.load()); hi = std::max (hi, m.currentGainDb.load()); } });
                range[t] = hi - lo;
            }
            logMessage ("Gain movement with bursts: unprotected " + juce::String (range[0], 2) + " dB, protected " + juce::String (range[1], 2) + " dB");
            expectLessThan (range[1], range[0] * 0.6f);

            RideModule m;
            m.prepare (fs, chunk);
            RideSettings s;
            s.enabled = true; s.amount = 0.0f;
            m.setSettings (s);
            auto out = in;
            runModule (m, out);
            // 0.6: the rider looks ahead (constant latency): amount 0 = the input, delayed,
            // after the 10 ms fade-in that follows the empty delay line of a fresh instance
            const int L = m.getLatencySamples(), settled = L + (int) (0.01 * fs) + 1;
            expectGreaterThan (L, 0);
            double d = 0.0;
            for (int c = 0; c < 2; ++c)
                for (int i = settled; i < in.getNumSamples(); ++i)
                    d = std::max (d, (double) std::abs (in.getSample (c, i - L) - out.getSample (c, i)));
            expectEquals (d, 0.0);
        }
    }

    //==========================================================================
    void checkTests()
    {
        const double fs = 48000.0;

        beginTest ("VOCAL CHECK: never changes the audio");
        {
            Harness a, b;
            for (auto* h : { &a, &b })
            {
                h->neutral();
                h->set (pid::compOn, 1.0f);
            }
            b.set (pid::checkOn, 1.0f);
            const auto in = a.noise (0.4f, 1.0, 29);
            expectEquals (maxAbsDiff (a.run (in), b.run (in)), 0.0);
        }

        beginTest ("VOCAL CHECK: too little material -> no diagnosis");
        {
            const auto r = analyseBuffer (vocalLike (fs, 2.0, -18.0f, -14.0f, 1), fs);
            expect (! r.enoughMaterial);
            expect (! hasFinding (r, ModuleId::deess, false));
            expect (! hasFinding (r, ModuleId::smooth, false));
        }

        beginTest ("VOCAL CHECK: clipping and missing headroom are detected");
        {
            auto b = vocalLike (fs, 6.0, -10.0f, -24.0f, 2);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < b.getNumSamples(); ++i)
                    b.setSample (c, i, juce::jlimit (-1.0f, 1.0f, b.getSample (c, i) * 6.0f));
            const auto r = analyseBuffer (b, fs);
            expectGreaterThan (r.clipEvents, 0);
            expect (! r.findings.empty() && r.findings.front().title.contains ("Clipping")
                    && r.findings.front().certainty == Finding::Certainty::sure, "clipping is the top finding");
        }

        beginTest ("VOCAL CHECK: sibilance is reported only when S sounds really stick out");
        {
            const auto loudS = analyseBuffer (vocalLike (fs, 12.0, -20.0f, -18.0f, 3), fs);
            logMessage ("Loud S: S vs vowels " + juce::String (loudS.sibilanceVsVowelDb, 1) + " dB");
            expect (loudS.enoughMaterial);
            expect (hasFinding (loudS, ModuleId::deess, true), "sure de-esser finding");

            const auto softS = analyseBuffer (vocalLike (fs, 12.0, -20.0f, -32.0f, 4), fs);
            logMessage ("Soft S: S vs vowels " + juce::String (softS.sibilanceVsVowelDb, 1) + " dB");
            expect (! hasFinding (softS, ModuleId::deess, false), "no sibilance finding");
        }

        beginTest ("VOCAL CHECK: large level differences point to LEVEL RIDE");
        {
            auto b = vocalLike (fs, 16.0, -18.0f, -32.0f, 5);
            for (int k = 0; k < 8; ++k)
                if (k % 2 == 0)
                    for (int c = 0; c < 2; ++c)
                        b.applyGain (c, k * (int) (fs * 2), (int) (fs * 2), juce::Decibels::decibelsToGain (-22.0f));
            const auto r = analyseBuffer (b, fs);
            logMessage ("Loudness range: " + juce::String (r.loudnessRangeDb, 1) + " dB");
            expect (hasFinding (r, ModuleId::ride, true));
        }

        beginTest ("VOCAL CHECK: a balanced vocal gets no sure findings");
        {
            const auto r = analyseBuffer (vocalLike (fs, 14.0, -18.0f, -28.0f, 6), fs);
            juce::StringArray titles;
            for (auto& f : r.findings)
                titles.add (juce::String (f.certainty == Finding::Certainty::sure ? "[sure] " : "[possible] ") + f.title);
            logMessage ("Balanced vocal findings: " + (titles.isEmpty() ? juce::String ("none") : titles.joinIntoString ("; ")));
            for (auto& f : r.findings)
                expect (f.certainty != Finding::Certainty::sure, "unexpected sure finding: " + f.title);
        }
    }

    //==========================================================================
    void integrationTests()
    {
        beginTest ("All 0.3 modules together in the chain, mono and stereo, several rates and block sizes");
        {
            for (auto [fs, bs, channels] : { std::tuple<double, int, int> { 44100.0, 37, 2 }, { 48000.0, 512, 1 }, { 96000.0, 1024, 2 } })
            {
                Harness h (fs, bs, channels);
                for (auto* id : { pid::pulseOn, pid::smashOn, pid::choirOn, pid::smoothOn, pid::checkOn, pid::rideOn,
                                  pid::eqOn, pid::compOn, pid::deessOn, pid::reverbOn })
                    h.set (id, 1.0f);
                h.set (pid::smashBlend, 80.0f);
                h.set (pid::choirVoices, 3.0f);
                const auto in = h.noise (0.4f, 3.0, 31);
                const auto out = h.run (in, bs);
                expect (allFinite (out), "finite at " + juce::String (fs) + " / " + juce::String (bs));
                expectLessThan (peakOf (out, 0, out.getNumSamples()), 1.0f, "safety clip keeps it below 0 dBFS");
                h.proc.pumpAnalyzer();
            }
        }

        beginTest ("Switching the 0.3 modules on and off during playback is click free");
        {
            Harness h;
            h.neutral();
            const auto in = h.sine (300.0, 0.3f, 4.0);
            juce::AudioBuffer<float> out (in);
            juce::MidiBuffer midi;
            const char* ids[] { pid::pulseOn, pid::smashOn, pid::choirOn, pid::smoothOn, pid::rideOn };
            int block = 0;
            for (int start = 0; start < out.getNumSamples(); start += 512, ++block)
            {
                if (block % 10 == 0)
                    h.set (ids[(block / 10) % 5], (block / 50) % 2 == 0 ? 1.0f : 0.0f);
                juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, start, std::min (512, out.getNumSamples() - start));
                h.proc.processBlock (view, midi);
            }
            float maxStep = 0.0f;
            for (int i = 1; i < out.getNumSamples(); ++i)
                maxStep = std::max (maxStep, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
            logMessage ("Largest sample step while toggling modules: " + juce::String (maxStep, 4));
            // A 300 Hz sine at 0.3 moves at most 0.012 per sample
            expectLessThan (maxStep, 0.05f);
        }

        beginTest ("Old projects: the 0.3 modules stay off and change nothing");
        {
            Harness h;
            for (auto* id : { pid::pulseOn, pid::smashOn, pid::choirOn, pid::smoothOn, pid::checkOn, pid::rideOn })
                expectEquals (h.get (id), 0.0f);
            for (auto& p : getFactoryPresets())
                if (p.category == "Classic (v0.1)")
                {
                    p.applyTo (h.proc.getState());
                    for (auto* id : { pid::pulseOn, pid::smashOn, pid::choirOn, pid::smoothOn, pid::rideOn })
                        expectEquals (h.get (id), 0.0f, p.name + " keeps " + id + " off");
                }
        }
    }
};

static NewModuleTests newModuleTests;
