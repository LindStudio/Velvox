// Creative pitch shifter (Signalsmith Stretch): accuracy, latency, formants, CPU.

#include "TestHelpers.h"
#include <juce_dsp/juce_dsp.h>

using namespace velvox;
using namespace velvox::test;

namespace
{
    double frequencyOf (const float* x, int n, double fs)
    {
        double first = -1.0, last = -1.0;
        int count = 0;
        for (int i = 1; i < n; ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            {
                const double t = (i - 1) + x[i - 1] / (double) (x[i - 1] - x[i]);
                if (first < 0.0) first = t;
                last = t;
                ++count;
            }
        return count > 1 ? (count - 1) * fs / (last - first) : 0.0;
    }

    // Frequency of the strongest FFT bin, refined by parabolic interpolation
    double peakFrequency (const juce::AudioBuffer<float>& b, int start, double fs)
    {
        constexpr int order = 15, size = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> data ((size_t) size * 2, 0.0f);
        for (int i = 0; i < size; ++i)
        {
            const float w = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * i / (size - 1));
            data[(size_t) i] = b.getSample (0, start + i) * w;
        }
        fft.performFrequencyOnlyForwardTransform (data.data());
        int k = 1;
        for (int i = 2; i < size / 2 - 1; ++i)
            if (data[(size_t) i] > data[(size_t) k]) k = i;
        const double a = std::log (data[(size_t) k - 1] + 1e-12), bb = std::log (data[(size_t) k] + 1e-12), c = std::log (data[(size_t) k + 1] + 1e-12);
        const double offset = 0.5 * (a - c) / (a - 2.0 * bb + c);
        return (k + offset) * fs / size;
    }

    void pitchOnly (Harness& h, float semitones)
    {
        h.neutral();
        h.set (pid::pitchOn, 1.0f);
        h.set (pid::pitchSemitones, semitones);
        h.set (pid::pitchCents, 0.0f);
        h.set (pid::pitchMix, 100.0f);
        h.proc.updateHostLatency();
    }

    // Harmonic "voice": sawtooth-like tone through a fixed formant resonance
    juce::AudioBuffer<float> buzz (double fs, double seconds, double f0)
    {
        juce::AudioBuffer<float> b (2, (int) (seconds * fs));
        dsp::Biquad formant;
        formant.setCoeffs (dsp::design::peak (fs, 900.0, 3.0, 18.0));
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            float s = 0.0f;
            for (int k = 1; k * f0 < 0.45 * fs && k <= 40; ++k)
                s += (float) (std::sin (2.0 * pi * k * f0 * i / fs) / k);
            s = 0.1f * formant.process (s, 0);
            b.setSample (0, i, s);
            b.setSample (1, i, s);
        }
        return b;
    }

    double spectralCentroid (const juce::AudioBuffer<float>& b, int start, double fs)
    {
        constexpr int order = 14, size = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> data ((size_t) size * 2, 0.0f);
        for (int i = 0; i < size; ++i)
        {
            const float w = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * i / (size - 1));
            data[(size_t) i] = b.getSample (0, start + i) * w;
        }
        fft.performFrequencyOnlyForwardTransform (data.data());
        double num = 0.0, den = 0.0;
        for (int k = 1; k < size / 2; ++k)
        {
            num += k * fs / size * data[(size_t) k];
            den += data[(size_t) k];
        }
        return num / den;
    }
}

class PitchTests final : public juce::UnitTest
{
public:
    PitchTests() : juce::UnitTest ("Velvox pitch", "Velvox") {}

    void runTest() override
    {
        beginTest ("Transposition accuracy (with and without formant preservation)");
        {
            for (bool keepFormants : { true, false })
                for (float st : { 12.0f, 7.0f, -5.0f, -12.0f })
                {
                    Harness h;
                    pitchOnly (h, st);
                    h.set (pid::pitchPreserveFormants, keepFormants ? 1.0f : 0.0f);
                    const auto out = h.run (h.sine (440.0, 0.3f, 2.5));
                    const double f = peakFrequency (out, (int) h.fs, h.fs);
                    const double fz = frequencyOf (out.getReadPointer (0, (int) h.fs), (int) (0.5 * h.fs), h.fs);
                    const double expected = 440.0 * std::pow (2.0, st / 12.0);
                    const double errCents = 1200.0 * std::log2 (f / expected);
                    logMessage ((keepFormants ? "keep formants " : "move formants ") + juce::String (st, 0) + " st: FFT " + juce::String (f, 2)
                                + " Hz, zero-crossing " + juce::String (fz, 2) + " Hz (expected " + juce::String (expected, 2)
                                + ", error " + juce::String (errCents, 2) + " ct)");
                    // Measured worst case of the engine: ~8 ct with formant preservation
                    expectLessThan (std::abs (errCents), keepFormants ? 10.0 : 8.0);
                    expect (allFinite (out));
                }
        }

        beginTest ("Reported latency matches the actual delay");
        {
            Harness h;
            pitchOnly (h, 0.0f);
            const int reported = h.proc.getLatencySamples();
            const int expected = h.proc.getChain().getLatencyFor (true);
            expectEquals (reported, expected);

            // Aperiodic input: a periodic tone would make the correlation ambiguous
            const auto in = h.noise (0.3f, 2.0, 77);
            const auto out = h.run (in);

            // Cross-correlation peak between input and output
            const int from = (int) (0.5 * h.fs), len = (int) (0.5 * h.fs);
            int bestLag = 0;
            double best = -1.0e9;
            for (int lag = 0; lag < reported + 400; ++lag)
            {
                double acc = 0.0;
                for (int i = from; i < from + len; ++i)
                    acc += (double) in.getSample (0, i - lag) * out.getSample (0, i);
                if (acc > best) { best = acc; bestLag = lag; }
            }
            logMessage ("Reported latency " + juce::String (reported) + " samples ("
                        + juce::String (1000.0 * reported / h.fs, 1) + " ms), measured " + juce::String (bestLag));
            expectLessOrEqual (std::abs (bestLag - reported), 2);
            expectLessThan (1000.0 * reported / h.fs, 130.0, "latency stays around 120 ms");
        }

        beginTest ("Mix 0 % gives the latency-aligned dry signal");
        {
            Harness h;
            pitchOnly (h, 7.0f);
            h.set (pid::pitchMix, 0.0f);
            const auto in = h.noise (0.3f, 1.5, 21);
            const auto out = h.run (in);
            const int L = h.proc.getLatencySamples();
            double err = 0.0;
            for (int i = (int) h.fs; i < in.getNumSamples(); ++i)
                err = std::max (err, (double) std::abs (out.getSample (0, i) - in.getSample (0, i - L)));
            expectLessThan (err, 1.0e-5);
        }

        beginTest ("Keep formants: shifted voice keeps its spectral envelope");
        {
            const auto centroid = [] (bool keep)
            {
                Harness h;
                pitchOnly (h, 7.0f);
                h.set (pid::pitchPreserveFormants, keep ? 1.0f : 0.0f);
                const auto out = h.run (buzz (h.fs, 2.0, 150.0));
                return spectralCentroid (out, (int) h.fs, h.fs);
            };
            Harness ref;
            const double original = spectralCentroid (buzz (ref.fs, 2.0, 150.0), (int) ref.fs, ref.fs);
            const double kept = centroid (true), moved = centroid (false);
            logMessage ("Spectral centroid: original " + juce::String (original, 0) + " Hz, keep formants "
                        + juce::String (kept, 0) + " Hz, formants moved " + juce::String (moved, 0) + " Hz");
            expectGreaterThan (moved, kept, "without preservation the timbre moves up");
            expectLessThan (std::abs (kept - original), std::abs (moved - original));
        }

        beginTest ("Latency is reported to the host when pitch is switched");
        {
            Harness h;
            h.neutral();
            const int base = h.proc.getLatencySamples();
            h.set (pid::pitchOn, 1.0f);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50); // async latency update
            const int withPitch = h.proc.getLatencySamples();
            h.set (pid::pitchOn, 0.0f);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            logMessage ("Latency: pitch off " + juce::String (base) + ", pitch on " + juce::String (withPitch) + " samples");
            expectEquals (base, 3);
            expectGreaterThan (withPitch, base);
            expectEquals (h.proc.getLatencySamples(), base);
        }

        beginTest ("Switching pitch on/off while playing is click-free");
        {
            Harness h;
            h.neutral();
            h.set (pid::pitchSemitones, -12.0f);
            const auto in = h.sine (200.0, 0.4f, 3.0);
            juce::MidiBuffer midi;
            float maxStep = 0.0f, last = 0.0f;
            for (int start = 0; start < in.getNumSamples(); start += 256)
            {
                if (start % 24000 == 0 && start > 0)
                    h.set (pid::pitchOn, (start / 24000) % 2 == 1 ? 1.0f : 0.0f);
                juce::AudioBuffer<float> block (2, 256);
                for (int ch = 0; ch < 2; ++ch)
                    block.copyFrom (ch, 0, in, ch, start, 256);
                h.proc.processBlock (block, midi);
                for (int i = 0; i < 256; ++i)
                {
                    if (start + i > 4800)
                        maxStep = std::max (maxStep, std::abs (block.getSample (0, i) - last));
                    last = block.getSample (0, i);
                }
            }
            logMessage ("Largest sample step across pitch switches: " + juce::String (maxStep, 4));
            expectLessThan (maxStep, 0.1f);
        }

        beginTest ("CPU load (offline estimate)");
        {
            Harness h;
            pitchOnly (h, 5.0f);
            const auto in = h.noise (0.3f, 10.0, 31);
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            const auto out = h.run (in);
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            const double realtimeShare = ms / 10000.0;
            logMessage ("10 s stereo with pitch processed in " + juce::String (ms, 0) + " ms ("
                        + juce::String (realtimeShare * 100.0, 1) + " % of real time, whole chain)");
            expect (allFinite (out));
            expectLessThan (realtimeShare, 0.5, "must run far faster than real time");
        }
    }
};

static PitchTests pitchTests;
