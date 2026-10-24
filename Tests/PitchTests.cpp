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

namespace
{
    // Sustained "vowel": harmonics of f0 shaped by a known two-formant envelope (F1, F2)
    double vowelEnvelope (double f, double F1, double F2)
    {
        const auto res = [] (double x, double F, double B) { const double u = (x - F) / B; return 1.0 / (1.0 + u * u); };
        return (res (f, F1, 90.0) + 0.6 * res (f, F2, 120.0) + 0.01) / std::sqrt (std::max (1.0, f / 100.0));
    }

    juce::AudioBuffer<float> vowel (double fs, double seconds, double f0, double F1 = 700.0, double F2 = 1800.0, float amp = 0.3f)
    {
        juce::AudioBuffer<float> b (2, (int) (seconds * fs));
        b.clear();
        std::vector<double> a;
        for (int k = 1; k * f0 < std::min (0.45 * fs, 9000.0); ++k)
            a.push_back (vowelEnvelope (k * f0, F1, F2));
        double peak = 0.0;
        std::vector<double> v ((size_t) b.getNumSamples());
        for (size_t i = 0; i < v.size(); ++i)
        {
            double s = 0.0;
            for (size_t k = 0; k < a.size(); ++k)
                s += a[k] * std::sin (2.0 * pi * (double) (k + 1) * f0 * (double) i / fs + 0.7 * (double) k * (double) k);
            v[i] = s;
            peak = std::max (peak, std::abs (s));
        }
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample (ch, i, (float) (amp * v[(size_t) i] / peak));
        return b;
    }

    struct Spectrum
    {
        std::vector<float> mag;   // linear magnitude, size/2 bins
        double binHz = 1.0;
    };

    Spectrum spectrumOf (const juce::AudioBuffer<float>& b, int start, double fs, int order = 15)
    {
        const int size = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> data ((size_t) size * 2, 0.0f);
        for (int i = 0; i < size; ++i)
            data[(size_t) i] = b.getSample (0, start + i) * (0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * i / (size - 1)));
        fft.performFrequencyOnlyForwardTransform (data.data());
        Spectrum s;
        s.mag.assign (data.begin(), data.begin() + size / 2);
        s.binHz = fs / size;
        return s;
    }

    // Energy away from the expected partial (beyond +-25 Hz), relative to the total (dB)
    double spuriousDb (const juce::AudioBuffer<float>& b, int start, double fs, double expectedHz)
    {
        const auto s = spectrumOf (b, start, fs);
        double inside = 0.0, outside = 0.0;
        for (size_t k = 1; k < s.mag.size(); ++k)
        {
            const double p = (double) s.mag[k] * s.mag[k];
            (std::abs ((double) k * s.binHz - expectedHz) <= 25.0 ? inside : outside) += p;
        }
        return 10.0 * std::log10 ((outside + 1e-30) / (inside + outside + 1e-30));
    }

    // Formant estimate of a harmonic signal with known f0: the strongest harmonic in [lo, hi],
    // refined by a parabola through its neighbours (log amplitude over frequency)
    double formantPeak (const Spectrum& s, double f0, double lo, double hi)
    {
        std::vector<double> f, a;
        for (int k = 1; k * f0 < 6000.0; ++k)
        {
            const int c = (int) std::round (k * f0 / s.binHz);
            float m = 0.0f;
            for (int j = c - 4; j <= c + 4; ++j)
                if (j > 0 && j < (int) s.mag.size()) m = std::max (m, s.mag[(size_t) j]);
            f.push_back (k * f0);
            a.push_back (std::log (m + 1e-12));
        }
        int best = -1;
        for (int i = 0; i < (int) f.size(); ++i)
            if (f[(size_t) i] >= lo && f[(size_t) i] <= hi && (best < 0 || a[(size_t) i] > a[(size_t) best]))
                best = i;
        if (best <= 0 || best + 1 >= (int) f.size())
            return best >= 0 ? f[(size_t) best] : 0.0;
        const double y0 = a[(size_t) best - 1], y1 = a[(size_t) best], y2 = a[(size_t) best + 1];
        const double den = y0 - 2.0 * y1 + y2;
        const double off = den < 0.0 ? juce::jlimit (-0.5, 0.5, 0.5 * (y0 - y2) / den) : 0.0;
        return f[(size_t) best] + off * f0;
    }

    // Spread (std, dB) of the 10 ms RMS envelope: amplitude roughness / beating of a sustained note
    double envelopeSpreadDb (const juce::AudioBuffer<float>& b, int start, int len, double fs)
    {
        const int frame = (int) (0.010 * fs);
        std::vector<double> db;
        for (int s = start; s + frame <= start + len; s += frame)
            db.push_back (juce::Decibels::gainToDecibels ((double) b.getRMSLevel (0, s, frame), -200.0));
        double mean = 0.0, var = 0.0;
        for (auto v : db) mean += v;
        mean /= (double) db.size();
        for (auto v : db) var += (v - mean) * (v - mean);
        return std::sqrt (var / (double) db.size());
    }
}

// 0.6 quality targets (spec 5.10, QUALITY_BASELINE gap 3)
class PitchQualityTests final : public juce::UnitTest
{
public:
    PitchQualityTests() : juce::UnitTest ("Velvox pitch quality", "Velvox") {}

    void runTest() override
    {
        beginTest ("Exact transposition ratio on tones (any input frequency, any shift, both formant modes)");
        {
            double worst = 0.0;
            for (bool keep : { true, false })
                for (double f0 : { 220.0, 233.0, 311.1, 440.0 })
                    for (float st : { 12.0f, 7.0f, 1.0f, -5.0f, -12.0f })
                    {
                        Harness h;
                        pitchOnly (h, st);
                        h.set (pid::pitchPreserveFormants, keep ? 1.0f : 0.0f);
                        const auto out = h.run (h.sine (f0, 0.3f, 2.5));
                        const double expected = f0 * std::pow (2.0, st / 12.0);
                        const double err = 1200.0 * std::log2 (peakFrequency (out, (int) h.fs, h.fs) / expected);
                        if (std::abs (err) > 1.0)
                            logMessage ("  " + juce::String (f0, 1) + " Hz " + juce::String (st, 0) + " st" + (keep ? " keep" : " move")
                                        + ": " + juce::String (err, 2) + " ct");
                        worst = std::max (worst, std::abs (err));
                        expectLessThan (std::abs (err), 1.0);
                    }
            logMessage ("worst tone error " + juce::String (worst, 3) + " ct");
        }

        beginTest ("Accuracy at 44.1 and 96 kHz");
        for (double fs : { 44100.0, 96000.0 })
        {
            Harness h (fs);
            pitchOnly (h, 12.0f);
            const auto out = h.run (h.sine (220.0, 0.3f, 2.5));
            const double err = 1200.0 * std::log2 (peakFrequency (out, (int) fs, fs) / 440.0);
            logMessage (juce::String (fs, 0) + " Hz: " + juce::String (err, 3) + " ct, latency "
                        + juce::String (1000.0 * h.proc.getLatencySamples() / fs, 1) + " ms");
            expectLessThan (std::abs (err), 1.0);
            expectLessOrEqual (1000.0 * h.proc.getLatencySamples() / fs, 30.0);
        }

        beginTest ("Latency <= 30 ms and the same for every setting");
        {
            int first = -1;
            for (float st : { -12.0f, 0.0f, 3.0f, 12.0f, 24.0f })
                for (bool keep : { true, false })
                {
                    Harness h;
                    pitchOnly (h, st);
                    h.set (pid::pitchPreserveFormants, keep ? 1.0f : 0.0f);
                    h.set (pid::pitchFormant, keep ? 0.0f : 5.0f);
                    h.proc.updateHostLatency();
                    const int L = h.proc.getLatencySamples();
                    if (first < 0) first = L;
                    expectEquals (L, first);
                }
            logMessage ("latency " + juce::String (first) + " samples = " + juce::String (first / 48.0, 1) + " ms");
            expectLessOrEqual (first, 1440);
            expectGreaterThan (first, 0);
        }

        beginTest ("Spurious energy on a shifted sine <= -60 dB");
        for (float st : { 12.0f, -5.0f, 7.0f })
        {
            Harness h;
            pitchOnly (h, st);
            const auto out = h.run (h.sine (220.0, 0.25f, 2.5));
            const double sp = spuriousDb (out, (int) h.fs, h.fs, 220.0 * std::pow (2.0, st / 12.0));
            logMessage (juce::String (st, 0) + " st: spurious " + juce::String (sp, 1) + " dB");
            expectLessThan (sp, -60.0);
        }

        beginTest ("Keep formants: F1/F2 stay within 1/3 octave at +-12 st");
        {
            Harness ref;
            const double F1 = 700.0, F2 = 1800.0;
            for (float st : { 12.0f, -12.0f, 7.0f, -5.0f })
            {
                Harness h;
                pitchOnly (h, st);
                h.set (pid::pitchPreserveFormants, 1.0f);
                const double f0 = 150.0, f0out = f0 * std::pow (2.0, st / 12.0);
                const auto out = h.run (vowel (h.fs, 2.5, f0, F1, F2));
                const auto s = spectrumOf (out, (int) h.fs, h.fs);
                const double o1 = formantPeak (s, f0out, 350.0, 1150.0), o2 = formantPeak (s, f0out, 1250.0, 3000.0);
                const double d1 = std::log2 (o1 / F1), d2 = std::log2 (o2 / F2);
                logMessage (juce::String (st, 0) + " st: F1 " + juce::String (o1, 0) + " Hz (" + juce::String (d1, 2) + " oct), F2 "
                            + juce::String (o2, 0) + " Hz (" + juce::String (d2, 2) + " oct)");
                expectLessThan (std::abs (d1), 1.0 / 3.0);
                expectLessThan (std::abs (d2), 1.0 / 3.0);
            }
            // Formants off: they move with the pitch (an octave up)
            Harness h;
            pitchOnly (h, 12.0f);
            h.set (pid::pitchPreserveFormants, 0.0f);
            const auto s = spectrumOf (h.run (vowel (h.fs, 2.5, 150.0, F1, F2)), (int) h.fs, h.fs);
            const double moved = formantPeak (s, 300.0, 350.0, 2000.0);
            logMessage ("formants moved: F1 " + juce::String (moved, 0) + " Hz");
            expectGreaterThan (std::log2 (moved / F1), 2.0 / 3.0);
        }

        beginTest ("Formant control moves the formants without changing the pitch");
        {
            Harness h;
            pitchOnly (h, 0.0f);
            h.set (pid::pitchPreserveFormants, 1.0f);
            h.set (pid::pitchFormant, 5.0f);
            const auto out = h.run (vowel (h.fs, 2.5, 150.0, 700.0, 1800.0));
            const auto s = spectrumOf (out, (int) h.fs, h.fs);
            const double o1 = formantPeak (s, 150.0, 350.0, 1500.0);
            const double expected = 700.0 * std::pow (2.0, 5.0 / 12.0);
            logMessage ("formant +5 st: F1 " + juce::String (o1, 0) + " Hz (expected " + juce::String (expected, 0) + ")");
            expectLessThan (std::abs (std::log2 (o1 / expected)), 1.0 / 6.0);
            const double f = peakFrequency (out, (int) h.fs, h.fs);
            expectLessThan (std::abs (1200.0 * std::log2 (f / (150.0 * std::round (f / 150.0)))), 2.0, "partials stay harmonics of 150 Hz");
        }

        beginTest ("Sustained note: no roughness / phasiness (envelope stays steady)");
        for (float st : { 7.0f, -12.0f, 12.0f })
        {
            Harness h;
            pitchOnly (h, st);
            const auto in = vowel (h.fs, 3.5, 180.0);
            const auto out = h.run (in);
            const double dIn = envelopeSpreadDb (in, (int) h.fs, (int) (2.0 * h.fs), h.fs);
            const double dOut = envelopeSpreadDb (out, (int) h.fs, (int) (2.0 * h.fs), h.fs);
            logMessage (juce::String (st, 0) + " st: 10 ms envelope spread in " + juce::String (dIn, 2) + " dB, out " + juce::String (dOut, 2) + " dB");
            expectLessThan (dOut, 0.5);
        }

        beginTest ("Noise / unvoiced input keeps its level (no pumping)");
        {
            Harness h;
            pitchOnly (h, 5.0f);
            const auto in = h.noise (0.2f, 3.0, 5);
            const auto out = h.run (in);
            const double spread = envelopeSpreadDb (out, (int) h.fs, (int) (1.5 * h.fs), h.fs);
            const double level = rmsDb (out, (int) h.fs, (int) (1.5 * h.fs)) - rmsDb (in, (int) h.fs, (int) (1.5 * h.fs));
            logMessage ("noise: level change " + juce::String (level, 2) + " dB, envelope spread " + juce::String (spread, 2) + " dB");
            expectLessThan (std::abs (level), 1.5);
            expectLessThan (spread, 1.5);
        }

        beginTest ("Silence, denormal and non-finite input stay finite");
        {
            Harness h;
            pitchOnly (h, 7.0f);
            auto in = h.silence (1.0);
            in.setSample (0, 1000, std::numeric_limits<float>::quiet_NaN());
            in.setSample (1, 2000, std::numeric_limits<float>::infinity());
            for (int i = 5000; i < 10000; ++i) in.setSample (0, i, 1.0e-39f);
            const auto out = h.run (in);
            auto tail = h.run (h.sine (300.0, 0.3f, 1.0));
            expect (allFinite (tail));
            expectGreaterThan (tail.getMagnitude (0, (int) (0.5 * h.fs), (int) (0.4 * h.fs)), 0.05f, "recovers after bad input");
            juce::ignoreUnused (out);
        }

        beginTest ("Ratio automation while playing: no clicks");
        {
            Harness h;
            pitchOnly (h, 0.0f);
            const auto in = vowel (h.fs, 3.0, 200.0);
            juce::MidiBuffer midi;
            float maxStep = 0.0f, last = 0.0f;
            for (int start = 0; start + 256 <= in.getNumSamples(); start += 256)
            {
                h.set (pid::pitchSemitones, (float) std::round (12.0 * std::sin (start / 20000.0)));
                juce::AudioBuffer<float> block (2, 256);
                for (int ch = 0; ch < 2; ++ch) block.copyFrom (ch, 0, in, ch, start, 256);
                h.proc.processBlock (block, midi);
                for (int i = 0; i < 256; ++i)
                {
                    if (start + i > 4800) maxStep = std::max (maxStep, std::abs (block.getSample (0, i) - last));
                    last = block.getSample (0, i);
                }
                expect (allFinite (block));
            }
            // the vowel's own largest sample step
            float inStep = 0.0f;
            for (int i = 1; i < in.getNumSamples(); ++i) inStep = std::max (inStep, std::abs (in.getSample (0, i) - in.getSample (0, i - 1)));
            logMessage ("largest step " + juce::String (maxStep, 4) + " (input " + juce::String (inStep, 4) + ")");
            expectLessThan (maxStep, 3.0f * inStep + 0.02f);
        }
    }
};

static PitchQualityTests pitchQualityTests;

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
                    expectLessThan (std::abs (errCents), 1.0);
                    expect (allFinite (out));
                }
        }

        beginTest ("Reported latency matches the actual delay");
        {
            Harness h;
            pitchOnly (h, 0.0f);
            const int reported = h.proc.getLatencySamples();
            const int expected = h.proc.getChain().getLatencyFor (h.proc.getRouting(), true);
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
            expectLessOrEqual (1000.0 * reported / h.fs, 30.0, "low latency (Graillon class)");
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
            expectEquals (base, 0);   // nothing latent in the default chain
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
