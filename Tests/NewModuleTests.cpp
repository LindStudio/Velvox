// 0.3 modules: PULSE, VOCAL SMASH, CHOIR, SMOOTH, VOCAL CHECK, LEVEL RIDE.
// Each module is measured on its own with test signals, then all of them
// together through the real processor.

#include "TestHelpers.h"
#include "SyntheticVocal.h"
#include "PopScan.h"
#include <juce_dsp/juce_dsp.h>
#include <complex>
#include <thread>
#include "DSP/PulseModule.h"
#include "DSP/SmashModule.h"
#include "DSP/ChoirModule.h"
#include "SyntheticVocal.h"
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

        // 0.6 quality (QUALITY_BASELINE #7): the default choir was narrow (L/R 0.94 vs 0.79 Waves Doubler4)
        // and rough (AM 2.12 vs 1.62 dB). Proxies of the bench metrics on a synthetic vocal / a 440 Hz tone.
        beginTest ("CHOIR: wide image at the default settings (vocal)");
        {
            VocalSpec vs;
            vs.seconds = 8.0;
            const auto voice = makeVocal (vs);
            ChoirModule m;
            m.prepare (fs, chunk);
            ChoirSettings s;
            s.enabled = true;   // defaults: 4 voices, detune 10, timing 18, humanize 50 %, width 80 %, density 50 %, mix 35 %
            m.setSettings (s);
            juce::AudioBuffer<float> b (2, voice.numSamples());
            for (int c = 0; c < 2; ++c)
                b.copyFrom (c, 0, voice.vocal[0].data(), voice.numSamples());
            runModule (m, b);
            const double corr = correlation (b, 0, b, 1, (int) fs, b.getNumSamples() - (int) fs);
            logMessage ("default choir on a vocal: L/R correlation " + juce::String (corr, 3));
            expectLessThan (corr, 0.88);
        }

        beginTest ("CHOIR: low AM roughness at the default settings (6 tones)");
        {
            // One tone alone depends on the beat phases the voices happen to have (same levels: 0.7-3.1 dB),
            // so the mean over six tones is pinned (0.5 engine: 2.02 dB in the bench, Doubler4 2.57 dB)
            double total = 0.0;
            for (double f : { 196.0, 262.0, 330.0, 523.0, 659.0, 880.0 })
            {
                ChoirModule m;
                m.prepare (fs, chunk);
                ChoirSettings s;
                s.enabled = true;
                m.setSettings (s);
                juce::AudioBuffer<float> b (2, (int) (4.5 * fs));
                for (int i = 0; i < b.getNumSamples(); ++i)
                    for (int c = 0; c < 2; ++c)
                        b.setSample (c, i, 0.25f * (float) std::sin (2.0 * pi * f * i / fs));
                runModule (m, b);
                std::vector<double> e;
                const int w = (int) (0.02 * fs);
                for (int st = (int) (0.5 * fs); st + w < b.getNumSamples(); st += w / 2)
                {
                    double sum = 0.0;
                    for (int i = st; i < st + w; ++i)
                    {
                        const double mono = 0.5 * (b.getSample (0, i) + b.getSample (1, i));
                        sum += mono * mono;
                    }
                    e.push_back (10.0 * std::log10 (sum / w + 1e-20));
                }
                double mean = 0.0, q = 0.0;
                for (auto x : e) mean += x;
                mean /= (double) e.size();
                for (auto x : e) q += (x - mean) * (x - mean);
                total += std::sqrt (q / (double) e.size());
            }
            const double rough = total / 6.0;
            logMessage ("default choir: AM roughness " + juce::String (rough, 3) + " dB (mean of 6 tones)");
            expectLessThan (rough, 2.0);
        }

        beginTest ("CHOIR: a solo voice is a clean detune (no shifter AM)");
        {
            ChoirModule m;
            m.prepare (fs, chunk);
            ChoirSettings s;
            s.enabled = true; s.voices = 2; s.detuneCents = 20.0f; s.humanize = 0.0f; s.width = 0.0f; s.mix = 1.0f; s.density = 1.0f;
            m.setSettings (s);
            juce::AudioBuffer<float> b (2, (int) (4.0 * fs));
            for (int i = 0; i < b.getNumSamples(); ++i)
                for (int c = 0; c < 2; ++c)
                    b.setSample (c, i, 0.25f * (float) std::sin (2.0 * pi * 440.0 * i / fs));
            runModule (m, b);
            // Each voice's tone must keep its level (a dual-tap shifter's crossfade combs it up and down)
            const double fLo = 440.0 * std::pow (2.0, -20.0 / 1200.0), fHi = 440.0 * std::pow (2.0, 18.0 / 1200.0);
            for (double f : { fLo, fHi })
            {
                double lo = 1.0e9, hi = 0.0;
                for (int st = (int) (0.5 * fs); st + (int) fs <= b.getNumSamples(); st += (int) (0.125 * fs))
                {
                    const double a = toneLevel (b, st, (int) fs, f, fs);
                    lo = std::min (lo, a);
                    hi = std::max (hi, a);
                }
                logMessage ("voice at " + juce::String (f, 2) + " Hz: level " + juce::String (lo, 4) + " .. " + juce::String (hi, 4));
                expectLessThan (hi / lo, 1.15, "steady voice level");
            }
        }

        beginTest ("CHOIR: UI positions follow the voices (half-circle stage)");
        {
            ChoirSettings s;
            s.voices = 4; s.width = 0.5f; s.timingMs = 30.0f;
            for (int v = 0; v < 4; ++v)
            {
                const auto p = ChoirModule::voicePosition (v, s);
                expectWithinAbsoluteError (p.pan, ChoirModule::stagePan (ChoirModule::shapes()[(size_t) v].pan, 0.5f), 1.0e-6f);
                expect (p.pan * ChoirModule::shapes()[(size_t) v].pan > 0.0f, "on its own side");
                expectGreaterOrEqual (p.depth, 0.0f);
                expectLessOrEqual (p.depth, 1.0f);
                expectGreaterThan (p.level, 0.0f);
            }
            expectEquals (ChoirModule::voicePosition (5, s).level, 0.0f, "voices beyond the count are silent");
            expect (std::abs (ChoirModule::shapes()[0].pan) >= 0.95f && std::abs (ChoirModule::shapes()[1].pan) >= 0.95f, "outer voices at the edges");
            expect (ChoirModule::shapes()[0].pan * ChoirModule::shapes()[1].pan < 0.0f, "alternating sides");
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

        // ---- 0.6 STFT engine: constant latency, exact transparency, phase-coherent reduction
        beginTest ("SMOOTH: constant latency = FFT size (44.1/48/96/192 kHz)");
        {
            expectEquals (SmoothModule::fftSizeFor (44100.0), 1024);
            expectEquals (SmoothModule::fftSizeFor (48000.0), 1024);
            expectEquals (SmoothModule::fftSizeFor (96000.0), 2048);
            expectEquals (SmoothModule::fftSizeFor (192000.0), 4096);
            SmoothModule m;
            m.prepare (48000.0, chunk);
            expectEquals (m.getLatencySamples(), 1024);
            m.prepare (96000.0, chunk);
            expectEquals (m.getLatencySamples(), 2048, "follows a sample-rate change");
        }

        beginTest ("SMOOTH: Amount 0 and off are an exact delayed copy");
        {
            for (int variant = 0; variant < 2; ++variant)
            {
                SmoothModule m;
                m.prepare (fs, chunk);
                SmoothSettings s;
                s.enabled = variant == 0;
                s.amount = variant == 0 ? 0.0f : 1.0f;
                m.setSettings (s);
                const auto in = makeSignal (true, 31);
                auto out = in;
                runModule (m, out);
                const int L = m.getLatencySamples();
                double worst = 0.0;
                for (int c = 0; c < 2; ++c)
                    for (int i = L; i < in.getNumSamples(); ++i)
                        worst = std::max (worst, (double) std::abs (out.getSample (c, i) - in.getSample (c, i - L)));
                expectLessThan (worst, 1.0e-6, variant == 0 ? "Amount 0" : "switched off");
            }
        }

        beginTest ("SMOOTH: the reduction keeps the phase (aligned with the latency)");
        {
            SmoothModule m;
            m.prepare (fs, chunk);
            SmoothSettings s;
            s.enabled = true; s.amount = 0.8f; s.sensitivity = 0.6f; s.lowHz = 500.0f; s.highHz = 15000.0f;
            m.setSettings (s);
            const auto in = makeSignal (true, 33);
            auto out = in;
            runModule (m, out);
            const int L = m.getLatencySamples();
            const auto dft = [&] (const juce::AudioBuffer<float>& b, int start, int phaseRef)
            {
                std::complex<double> z;
                for (int i = 0; i < (int) fs; ++i)
                    z += (double) b.getSample (0, start + i) * std::polar (1.0, -2.0 * pi * 3000.0 * (phaseRef + i) / fs);
                return z;
            };
            const auto zin = dft (in, 2 * (int) fs, 0), zout = dft (out, 2 * (int) fs + L, 0);
            const double phase = std::abs (std::arg (zout / zin));
            const double gainDb = 20.0 * std::log10 (std::abs (zout) / std::abs (zin));
            logMessage ("3 kHz through SMOOTH: " + juce::String (gainDb, 1) + " dB, phase " + juce::String (phase, 3) + " rad");
            expectLessThan (gainDb, -2.0, "reduced");
            expectLessThan (phase, 0.05, "zero-phase gain, aligned to the reported latency");
        }

        beginTest ("SMOOTH: switching on and off while playing is click-free");
        {
            SmoothModule m;
            m.prepare (fs, chunk);
            SmoothSettings s;
            s.enabled = false; s.amount = 1.0f; s.sensitivity = 0.8f; s.lowHz = 500.0f; s.highHz = 15000.0f;
            m.setSettings (s);
            juce::AudioBuffer<float> in (2, (int) fs * 3);
            for (int i = 0; i < in.getNumSamples(); ++i)
                for (int c = 0; c < 2; ++c)
                    in.setSample (c, i, 0.25f * (float) std::sin (2.0 * pi * 3000.0 * i / fs) + 0.3f * (float) std::sin (2.0 * pi * 200.0 * i / fs));
            auto out = in;
            runModule (m, out, [&] (int start)
            {
                s.enabled = start >= (int) fs && start < 2 * (int) fs;
                m.setSettings (s);
            });
            double inStep = 0.0, outStep = 0.0;
            for (int i = 1; i < in.getNumSamples(); ++i)
            {
                inStep = std::max (inStep, (double) std::abs (in.getSample (0, i) - in.getSample (0, i - 1)));
                outStep = std::max (outStep, (double) std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
            }
            expectLessThan (outStep, inStep * 1.05, "no step larger than the signal's own");
        }

        // Change of the resonance region and of the rest of 1-10 kHz (long-term spectra, Welch 8192,
        // output aligned by the latency) for one input at Amount 75 %, defaults otherwise
        const auto resonanceAndRest = [&] (const juce::AudioBuffer<float>& in, float resonanceHz)
        {
            const int n = in.getNumSamples();
            SmoothModule m;
            m.prepare (fs, chunk);
            SmoothSettings s;
            s.enabled = true; s.amount = 0.75f;
            m.setSettings (s);
            auto out = in;
            runModule (m, out);
            const int L = m.getLatencySamples();

            constexpr int order = 13, size = 1 << order;
            juce::dsp::FFT fftw (order);
            juce::dsp::WindowingFunction<float> win ((size_t) size, juce::dsp::WindowingFunction<float>::hann, false);
            const auto ltas = [&] (const juce::AudioBuffer<float>& b, int offset)
            {
                std::vector<double> p ((size_t) size / 2 + 1, 0.0);
                std::vector<float> d ((size_t) size * 2);
                for (int start = (int) fs; start + size + offset < n; start += size / 2)
                {
                    std::fill (d.begin(), d.end(), 0.0f);
                    std::copy (b.getReadPointer (0, start + offset), b.getReadPointer (0, start + offset) + size, d.begin());
                    win.multiplyWithWindowingTable (d.data(), (size_t) size);
                    fftw.performFrequencyOnlyForwardTransform (d.data(), true);
                    for (size_t k = 0; k < p.size(); ++k)
                        p[k] += (double) d[k] * d[k];
                }
                return p;
            };
            const auto pin = ltas (in, 0), pout = ltas (out, L);
            const auto bandChange = [&] (double lo, double hi, double exLo, double exHi)
            {
                double a = 0.0, b = 0.0;
                for (size_t k = 0; k < pin.size(); ++k)
                {
                    const double f = (double) k * fs / size;
                    if (f >= lo && f <= hi && ! (f > exLo && f < exHi))
                    {
                        a += pin[k];
                        b += pout[k];
                    }
                }
                return 10.0 * std::log10 (b / a);
            };
            return std::pair<double, double> { bandChange (resonanceHz / 1.12, resonanceHz * 1.12, 0.0, 0.0),
                                               bandChange (1000.0, 10000.0, resonanceHz / 1.414, resonanceHz * 1.414) };
        };

        beginTest ("SMOOTH: a narrow resonance on a steady voice is cut, the harmonics around it stay");
        {
            // Sawtooth-like voice at 150 Hz with vibrato, a +10 dB Q 12 resonance at 3.2 kHz (a boxy mic /
            // room ring). The 0.5 engine did nothing here (its threshold sat above such peaks).
            juce::AudioBuffer<float> in (2, (int) fs * 6);
            Biquad res;
            res.setCoeffs (design::peak (fs, 3200.0, 12.0, 10.0));
            double phase = 0.0;
            for (int i = 0; i < in.getNumSamples(); ++i)
            {
                phase += 2.0 * pi * 150.0 * (1.0 + 0.01 * std::sin (2.0 * pi * 5.0 * i / fs)) / fs;
                double v = 0.0;
                for (int k = 1; k * 150 < 16000; ++k)
                    v += std::sin (k * phase) / k;
                const float y = 0.08f * res.process ((float) v, 0);
                in.setSample (0, i, y);
                in.setSample (1, i, y);
            }
            const auto [atResonance, elsewhere] = resonanceAndRest (in, 3200.0f);
            logMessage ("Harmonic voice, +10 dB Q 12 at 3.2 kHz: resonance " + juce::String (atResonance, 2) + " dB, 1-10 kHz elsewhere "
                        + juce::String (elsewhere, 2) + " dB");
            expectLessThan (atResonance, -0.35, "the resonance is reduced");
            expectGreaterThan (elsewhere, -0.1, "the harmonics elsewhere are left alone");
        }

        beginTest ("SMOOTH: on a voice it removes the resonance, not broadband (no dulling)");
        {
            velvox::test::VocalSpec spec;
            spec.seconds = 8.0;
            spec.resonanceHz = 3200.0f;
            spec.resonanceDb = 10.0f;
            const auto voice = velvox::test::makeVocal (spec);
            juce::AudioBuffer<float> in (2, voice.numSamples());
            for (int c = 0; c < 2; ++c)
                in.copyFrom (c, 0, voice.vocal[0].data(), voice.numSamples());
            const auto [atResonance, elsewhere] = resonanceAndRest (in, 3200.0f);
            logMessage ("Synthetic voice, +10 dB at 3.2 kHz: resonance " + juce::String (atResonance, 2) + " dB, 1-10 kHz elsewhere "
                        + juce::String (elsewhere, 2) + " dB");
            expectLessThan (atResonance, -0.5, "the resonance is reduced");
            expectGreaterThan (elsewhere, -0.6, "the rest of the presence range is (almost) untouched");
            expectLessThan (atResonance - elsewhere, -0.3, "more at the resonance than elsewhere");
        }

        beginTest ("SMOOTH: weighting curve (flat default, through the points, no overshoot)");
        {
            SmoothSettings s;
            for (float hz : { 20.0f, 100.0f, 1000.0f, 5000.0f, 20000.0f })
                expectEquals (SmoothModule::weightingDbAt (s, hz), 0.0f, "flat by default");
            s.weightGain = { 0.0f, 0.0f, 6.0f, -12.0f, 3.0f, 0.0f };
            for (int i = 0; i < SmoothSettings::numWeights; ++i)
                expectWithinAbsoluteError (SmoothModule::weightingDbAt (s, s.weightFreq[(size_t) i]), s.weightGain[(size_t) i], 1.0e-3f,
                                           "passes through point " + juce::String (i + 1));
            expectEquals (SmoothModule::weightingDbAt (s, 20.0f), 0.0f, "flat below the first point");
            expectEquals (SmoothModule::weightingDbAt (s, 20000.0f), 0.0f, "flat above the last point");
            bool monotone = true;
            float prev = SmoothModule::weightingDbAt (s, 1000.0f);
            for (float hz = 1000.0f; hz <= 2500.0f; hz *= 1.01f)
            {
                const float v = SmoothModule::weightingDbAt (s, hz);
                monotone = monotone && v <= prev + 1.0e-4f && v >= -12.0f - 1.0e-4f && v <= 6.0f + 1.0e-4f;
                prev = v;
            }
            expect (monotone, "between two points the curve stays between their values");

            // Points dragged onto each other or out of order: finite, within the range of the gains
            s.weightFreq = { 3000.0f, 3000.0f, 200.0f, 3000.0f, 50.0f, 18000.0f };
            s.weightGain = { 12.0f, -12.0f, 4.0f, 0.0f, -3.0f, 8.0f };
            bool sane = true;
            for (float hz = 20.0f; hz < 20000.0f; hz *= 1.05f)
            {
                const float v = SmoothModule::weightingDbAt (s, hz);
                sane = sane && std::isfinite (v) && v >= -12.0f - 1.0e-3f && v <= 12.0f + 1.0e-3f;
            }
            expect (sane, "degenerate point sets stay finite and bounded");
        }

        beginTest ("SMOOTH: the weighting curve steers where it works");
        {
            const auto reductionWith = [&] (float weightAt3k)
            {
                SmoothModule m;
                m.prepare (fs, chunk);
                SmoothSettings s;
                s.enabled = true; s.amount = 0.8f; s.sensitivity = 0.6f; s.lowHz = 500.0f; s.highHz = 15000.0f;
                s.weightFreq[3] = 3000.0f;
                s.weightGain[3] = weightAt3k;
                m.setSettings (s);
                const auto in = makeSignal (true, 17);
                auto out = in;
                runModule (m, out);
                const int start = (int) fs * 2, len = (int) fs * 2;
                return 20.0 * std::log10 (toneLevel (in, start, len, 3000.0, fs) / toneLevel (out, start, len, 3000.0, fs));
            };
            const double flat = reductionWith (0.0f), less = reductionWith (-12.0f), more = reductionWith (12.0f);
            logMessage ("3 kHz resonance with weighting 0 / -12 / +12 dB: " + juce::String (flat, 1) + " / " + juce::String (less, 1)
                        + " / " + juce::String (more, 1) + " dB");
            expectLessThan (less, flat - 2.0, "-12 dB weight: much less reduction");
            expectGreaterThan (more, flat + 1.0, "+12 dB weight: more reduction");
        }

        beginTest ("SMOOTH: panel feed - spectrum and reduction per bin, wait-free from another thread");
        {
            SmoothModule m;
            m.prepare (fs, chunk);
            SmoothSettings s;
            s.enabled = true; s.amount = 0.8f; s.sensitivity = 0.6f; s.lowHz = 500.0f; s.highHz = 15000.0f;
            m.setSettings (s);
            const auto in = makeSignal (true, 17);
            auto out = in;

            std::atomic<bool> done { false };
            std::atomic<int> reads { 0 }, torn { 0 };
            std::thread reader ([&]
            {
                auto frame = std::make_unique<SmoothSpectrum>();
                while (! done.load())
                    if (m.getSpectrum (*frame))
                    {
                        ++reads;
                        bool ok = frame->numBins == 0 || (frame->numBins == 513 && std::abs (frame->binHz - 46.875f) < 1.0e-3f);
                        for (int k = 0; k < frame->numBins && ok; ++k)
                            ok = std::isfinite (frame->inputDb[(size_t) k]) && frame->reductionDb[(size_t) k] >= 0.0f;
                        if (! ok)
                            ++torn;
                    }
            });
            runModule (m, out);
            done = true;
            reader.join();
            expectGreaterThan (reads.load(), 0);
            expectEquals (torn.load(), 0, "every frame read is complete");

            auto frame = std::make_unique<SmoothSpectrum>();
            expect (m.getSpectrum (*frame));
            const int k3 = (int) std::lround (3000.0 / frame->binHz), k1 = (int) std::lround (1000.0 / frame->binHz);
            expectGreaterThan (frame->reductionDb[(size_t) k3], 3.0f, "the reduction shows at the resonance");
            expectLessThan (frame->reductionDb[(size_t) k1], 0.5f, "and not elsewhere");
            expectGreaterThan (frame->inputDb[(size_t) k3], frame->inputDb[(size_t) k1] + 10.0f, "input spectrum shows the tone");

            const auto [attack, release] = SmoothModule::speedToTimes (0.5f);
            expectWithinAbsoluteError (attack, 5.0f, 0.01f, "Speed 50 % = the default attack");
            expectWithinAbsoluteError (release, 80.0f, 0.1f, "and release");
            expectLessThan (SmoothModule::speedToTimes (1.0f).second, SmoothModule::speedToTimes (0.0f).second, "faster = shorter");
        }

        beginTest ("SMOOTH: silence and full-scale input stay finite");
        {
            SmoothModule m;
            m.prepare (fs, chunk);
            SmoothSettings s;
            s.enabled = true; s.amount = 1.0f; s.mode = 1; s.sensitivity = 1.0f;
            m.setSettings (s);
            juce::AudioBuffer<float> b (2, (int) fs * 2);
            b.clear();
            for (int i = (int) fs; i < b.getNumSamples(); ++i)
                for (int c = 0; c < 2; ++c)
                    b.setSample (c, i, (i / 37) % 2 == 0 ? 1.0f : -1.0f);   // full-scale square after 1 s of silence
            runModule (m, b);
            bool finite = true;
            float peak = 0.0f;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    finite = finite && std::isfinite (b.getSample (c, i));
                    peak = std::max (peak, std::abs (b.getSample (c, i)));
                }
            expect (finite);
            expectLessThan (peak, 2.0f, "no blow-up");
        }

        beginTest ("SMOOTH: hard mode cuts a strong resonance deeper");
        {
            SmoothModule m;
            m.prepare (fs, chunk);
            SmoothSettings s;
            s.enabled = true; s.amount = 0.8f; s.sensitivity = 0.6f; s.lowHz = 500.0f; s.highHz = 15000.0f; s.mode = 1;
            m.setSettings (s);
            const auto in = makeSignal (true, 17);
            auto out = in;
            runModule (m, out);
            const int start = (int) fs * 2, len = (int) fs * 2;
            const double reduction = 20.0 * std::log10 (toneLevel (in, start, len, 3000.0, fs) / toneLevel (out, start, len, 3000.0, fs));
            logMessage ("Hard mode: 3 kHz resonance reduced by " + juce::String (reduction, 1) + " dB");
            expectGreaterThan (reduction, 4.0);
        }

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
            expectEquals (maxAbsDiff (in, out), 0.0);
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
            expect (! r.findings.empty() && r.findings.front().id == "clipping"
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

        beginTest ("SMOOTH weighting parameters: 6 points per instance, flat defaults, read into the settings");
        {
            Harness h;
            const float freqs[] { 150.0f, 400.0f, 1000.0f, 2500.0f, 6000.0f, 14000.0f };
            for (int i = 0; i < 6; ++i)
                for (int k = 0; k < 2; ++k)
                {
                    const auto f = instanceParamId (pid::smoothWFreq[i], k), g = instanceParamId (pid::smoothWGain[i], k);
                    expect (h.proc.getState().getParameter (f) != nullptr && h.proc.getState().getParameter (g) != nullptr, f + " / " + g);
                    expectWithinAbsoluteError (h.proc.getState().getRawParameterValue (f)->load(), freqs[i], 0.5f);
                    expectEquals (h.proc.getState().getRawParameterValue (g)->load(), 0.0f);
                }
            h.set (pid::smoothWGain[3], -7.0f);
            h.set (pid::smoothWFreq[3], 3300.0f);
            h.set (instanceParamId (pid::smoothWGain[1], 1).toRawUTF8(), 5.0f);
            velvox::SettingsReader reader;
            reader.bind ([&] (const char* id) { return h.proc.getState().getRawParameterValue (id); });
            const auto s = reader.read (Routing::defaults(), false);
            expectWithinAbsoluteError (s.smooth.weightGain[3], -7.0f, 1.0e-3f);
            expectWithinAbsoluteError (s.smooth.weightFreq[3], 3300.0f, 1.0f);
            expectWithinAbsoluteError (s.extra[0].smooth.weightGain[1], 5.0f, 1.0e-3f, "second instance has its own curve");
        }

        beginTest ("Stutter / Pulse after a latent SMOOTH stay on the host grid");
        {
            // The host shifts the output back by the reported latency, so a tempo-locked gate after
            // SMOOTH must gate the delayed signal at the delayed song position: "smooth, chop" has to
            // sound like "chop, smooth"
            const auto render = [&] (const char* routing, const char* gateOn)
            {
                Harness h;
                h.neutral();
                velvox::test::ScanPlayHead head;
                head.playing = true;
                head.bpm = 120.0;
                h.proc.setPlayHead (&head);
                h.set (pid::smoothOn, 1.0f);
                h.set (pid::smoothAmount, 0.0f);
                h.set (gateOn, 1.0f);
                h.proc.setRouting (Routing::fromString (routing));
                const auto in = h.sine (300.0, 0.3f, 2.0);
                juce::AudioBuffer<float> out (in);
                juce::MidiBuffer midi;
                for (int start = 0; start < out.getNumSamples(); start += 512)
                {
                    head.ppq = start / h.fs * head.bpm / 60.0;
                    juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, start, std::min (512, out.getNumSamples() - start));
                    h.proc.processBlock (view, midi);
                }
                h.proc.setPlayHead (nullptr);
                return out;
            };
            for (auto* gate : { pid::chopOn, pid::pulseOn })
            {
                const bool chop = juce::String (gate) == pid::chopOn;
                const auto before = render (chop ? "smooth,chop" : "smooth,pulse", gate);
                const auto after = render (chop ? "chop,smooth" : "pulse,smooth", gate);
                double d = 0.0;
                for (int i = 2048; i < before.getNumSamples(); ++i)
                    d = std::max (d, (double) std::abs (before.getSample (0, i) - after.getSample (0, i)));
                logMessage (juce::String (gate) + ": max difference smooth-first vs gate-first " + juce::String (d, 6));
                expectLessThan (d, 1.0e-3, gate);
            }
        }

        beginTest ("SMOOTH in the chain: constant latency reported, output and bypass aligned");
        {
            for (double rate : { 48000.0, 96000.0 })
            {
                Harness h (rate);
                h.neutral();
                const int fft = SmoothModule::fftSizeFor (rate);
                h.set (pid::smoothOn, 1.0f);
                h.set (pid::smoothAmount, 0.0f);
                expectEquals (h.proc.getLatencySamples(), fft, "one instance");
                const auto in = h.sine (440.0, 0.3f, 1.0);
                const auto out = h.run (in);
                double worst = 0.0;
                for (int i = in.getNumSamples() / 2; i < in.getNumSamples(); ++i)
                    worst = std::max (worst, (double) std::abs (out.getSample (0, i) - in.getSample (0, i - fft)));
                expectLessThan (worst, 1.0e-4, "output = input delayed by the reported latency");

                h.set (pid::smoothOn, 0.0f);
                expectEquals (h.proc.getLatencySamples(), fft, "unchanged while the instance stays in the chain");
                h.set (pid::bypass, 1.0f);
                const auto byp = h.run (in);
                worst = 0.0;
                for (int i = in.getNumSamples() / 2; i < in.getNumSamples(); ++i)
                    worst = std::max (worst, (double) std::abs (byp.getSample (0, i) - in.getSample (0, i - fft)));
                expectLessThan (worst, 1.0e-4, "global bypass is latency aligned");
                h.set (pid::bypass, 0.0f);

                h.set (instanceParamId (pid::smoothOn, 1).toRawUTF8(), 1.0f);
                expectEquals (h.proc.getLatencySamples(), 2 * fft, "two instances add up");
            }
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
