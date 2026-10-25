// Diagnostic only (runs when VELVOX_PITCH_SCAN is set): accuracy of the
// Signalsmith Stretch engine versus block size, used to choose the quality modes.

#include "TestHelpers.h"
#include "DSP/FormantCorrector.h"
#include "DSP/PitchTracker.h"
#include "DSP/SpliceShifter.h"
#include <juce_dsp/juce_dsp.h>

#if defined (_MSC_VER)
 #pragma warning (push, 0)
#endif
#include "signalsmith-stretch.h"
#if defined (_MSC_VER)
 #pragma warning (pop)
#endif

using namespace velvox::test;

namespace
{
    double peakFreq (const std::vector<float>& x, int start, double fs, double minHz)
    {
        constexpr int order = 15, size = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> data ((size_t) size * 2, 0.0f);
        for (int i = 0; i < size; ++i)
            data[(size_t) i] = x[(size_t) (start + i)] * (0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * i / (size - 1)));
        fft.performFrequencyOnlyForwardTransform (data.data());
        int k = (int) (minHz * size / fs);
        for (int i = k; i < size / 2 - 1; ++i)
            if (data[(size_t) i] > data[(size_t) k]) k = i;
        const double a = std::log (data[(size_t) k - 1] + 1e-12), b = std::log (data[(size_t) k] + 1e-12), c = std::log (data[(size_t) k + 1] + 1e-12);
        return (k + 0.5 * (a - c) / (a - 2.0 * b + c)) * fs / size;
    }
}

class PitchDiagnostics final : public juce::UnitTest
{
public:
    PitchDiagnostics() : juce::UnitTest ("Velvox pitch engine scan (diagnostic)", "Diagnostics") {}

    void runTest() override
    {
        beginTest ("Block size scan");
        if (juce::SystemStats::getEnvironmentVariable ("VELVOX_PITCH_SCAN", {}).isEmpty())
        {
            logMessage ("skipped (set VELVOX_PITCH_SCAN=1 to run)");
            return;
        }

        constexpr double fs = 48000.0;
        const int n = (int) (3.0 * fs);

        // CPU per component of the 0.6 engine (10 s stereo harmonic tone, +7 st, preserve on)
        {
            using namespace velvox::dsp;
            PitchTracker tr; SpliceShifter sh; FormantCorrector fc;
            tr.prepare (fs); sh.prepare (fs, 512); fc.prepare (fs, sh.getLatency(), 512);
            sh.setRatio (1.4983f); fc.setWarp (1.4983f, 1.0f);
            std::vector<float> l (512), r (512), mono (512), per (512);
            double tT = 0, tS = 0, tF = 0;
            for (int b = 0; b < (int) (10.0 * fs / 512); ++b)
            {
                for (int i = 0; i < 512; ++i)
                {
                    const double t = (b * 512 + i) / fs;
                    l[(size_t) i] = r[(size_t) i] = mono[(size_t) i] = (float) (0.2 * std::sin (2 * pi * 180 * t) + 0.1 * std::sin (2 * pi * 360 * t) + 0.05 * std::sin (2 * pi * 540 * t));
                }
                auto t0 = juce::Time::getHighResolutionTicks();
                for (int i = 0; i < 512; ++i) { tr.push (mono[(size_t) i]); per[(size_t) i] = tr.getPeriod(); }
                auto t1 = juce::Time::getHighResolutionTicks();
                float* io[2] { l.data(), r.data() };
                sh.process (io, 2, 512, per.data());
                auto t2 = juce::Time::getHighResolutionTicks();
                fc.process (mono.data(), io, 2, 512, per.data());
                auto t3 = juce::Time::getHighResolutionTicks();
                tT += (double) (t1 - t0); tS += (double) (t2 - t1); tF += (double) (t3 - t2);
            }
            const double tick = 1.0 / (double) juce::Time::getHighResolutionTicksPerSecond();
            logMessage ("CPU share of real time: tracker " + juce::String (tT * tick * 10.0, 2) + " %, shifter " + juce::String (tS * tick * 10.0, 2)
                        + " %, corrector " + juce::String (tF * tick * 10.0, 2) + " %");
        }

        // Root-cause scan of the 0.5 configuration (120 ms / 15 ms): error per input frequency,
        // shift and formant setting, pure sine (one spectral peak)
        for (bool compensate : { false, true })
            for (double f0 : { 220.0, 233.0, 440.0, 311.1 })
            {
                juce::String detail;
                for (double st : { 12.0, 7.0, 1.0, -5.0, -12.0 })
                {
                    signalsmith::stretch::SignalsmithStretch<float> s (0x5eed);
                    s.configure (1, (int) (0.120 * fs), (int) (0.015 * fs));
                    s.setTransposeSemitones ((float) st);
                    s.setFormantSemitones (0.0f, compensate);
                    s.setFormantBase ((float) (220.0 / fs));
                    std::vector<float> in ((size_t) n), out ((size_t) n);
                    for (int i = 0; i < n; ++i)
                        in[(size_t) i] = 0.25f * (float) std::sin (2.0 * pi * f0 * i / fs);
                    for (int pos = 0; pos < n; pos += 512)
                    {
                        const int len = std::min (512, n - pos);
                        const float* ip[1] { in.data() + pos };
                        float* op[1] { out.data() + pos };
                        s.process (ip, len, op, len);
                    }
                    const double expected = f0 * std::pow (2.0, st / 12.0);
                    const double f = peakFreq (out, (int) fs, fs, expected * 0.8);
                    detail << juce::String (st, 0) << ":" << juce::String (1200.0 * std::log2 (f / expected), 2) << "ct ";
                }
                logMessage ("sine " + juce::String (f0, 1) + " Hz" + (compensate ? " formant comp" : " no comp") + "  " + detail);
            }

        for (double blockMs : { 40.0, 60.0, 80.0, 120.0 })
            for (double ratio : { 4.0, 8.0 })
                for (bool compensate : { false, true })
                {
                    double worst = 0.0;
                    juce::String detail;
                    for (double st : { 12.0, 7.0, -5.0, -12.0 })
                        for (double f0 : { 440.0, 180.0 })
                        {
                            signalsmith::stretch::SignalsmithStretch<float> s (1234);
                            const int block = (int) (blockMs * 0.001 * fs);
                            s.configure (1, block, (int) (block / ratio));
                            s.setTransposeSemitones ((float) st);
                            s.setFormantSemitones (0.0f, compensate);
                            s.setFormantBase ((float) (220.0 / fs));

                            // harmonic tone (fundamental strongest)
                            std::vector<float> in ((size_t) n), out ((size_t) n);
                            for (int i = 0; i < n; ++i)
                                in[(size_t) i] = 0.3f * (float) (std::sin (2.0 * pi * f0 * i / fs) + 0.3 * std::sin (2.0 * pi * 2 * f0 * i / fs));
                            for (int pos = 0; pos < n; pos += 256)
                            {
                                const int len = std::min (256, n - pos);
                                const float* ip[1] { in.data() + pos };
                                float* op[1] { out.data() + pos };
                                s.process (ip, len, op, len);
                            }
                            const double expected = f0 * std::pow (2.0, st / 12.0);
                            const double f = peakFreq (out, (int) fs, fs, expected * 0.7);
                            const double cents = 1200.0 * std::log2 (f / expected);
                            worst = std::max (worst, std::abs (cents));
                            detail << juce::String (cents, 1) << " ";
                        }
                    logMessage ("block " + juce::String (blockMs, 0) + " ms, interval 1/" + juce::String (ratio, 0)
                                + (compensate ? ", formant comp" : ", no comp") + ": worst " + juce::String (worst, 1)
                                + " ct  [" + detail + "] (values near 1200 = octave picked by the peak search)");
                }
    }
};

static PitchDiagnostics pitchDiagnostics;
