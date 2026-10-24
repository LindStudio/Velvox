// Diagnostic only (runs when VELVOX_PITCH_SCAN is set): accuracy of the
// Signalsmith Stretch engine versus block size, used to choose the quality modes.

#include "TestHelpers.h"
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
