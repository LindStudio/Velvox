// Pro extensions: high cut, bell bands, saturation characters, wideband
// de-essing, RMS detector and sidechain filter of the compressor.

#include "TestHelpers.h"

using namespace velvox;
using namespace velvox::test;

namespace
{
    float gainAt (std::function<void (Harness&)> setup, double freq)
    {
        Harness h;
        h.neutral();
        setup (h);
        const auto in = h.sine (freq, 0.05f, 1.0);
        const auto out = h.run (in);
        return rmsDb (out, 24000, 24000) - rmsDb (in, 24000, 24000);
    }

    juce::AudioBuffer<float> twoTones (double fs, double f1, float a1, double f2, float a2, double seconds)
    {
        juce::AudioBuffer<float> b (2, (int) (seconds * fs));
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float v = a1 * (float) std::sin (2.0 * pi * f1 * i / fs) + a2 * (float) std::sin (2.0 * pi * f2 * i / fs);
            b.setSample (0, i, v);
            b.setSample (1, i, v);
        }
        return b;
    }
}

class ProExtensionTests final : public juce::UnitTest
{
public:
    ProExtensionTests() : juce::UnitTest ("Velvox pro extensions", "Velvox") {}

    void runTest() override
    {
        beginTest ("EQ high cut");
        {
            const auto hc = [] (Harness& h) { h.set (pid::eqOn, 1.0f); h.set (pid::lpfOn, 1.0f); h.set (pid::lpfFreq, 2000.0f); h.set (pid::lpfSlope, 1.0f); };
            const float at8k = gainAt (hc, 8000.0), at500 = gainAt (hc, 500.0);
            logMessage ("High cut 2 kHz/24 dB: 8 kHz " + juce::String (at8k, 1) + " dB, 500 Hz " + juce::String (at500, 2) + " dB");
            expectLessThan (at8k, -40.0f);
            expectWithinAbsoluteError (at500, 0.0f, 0.3f);
        }

        beginTest ("EQ low and high bands as bells");
        {
            const auto lowBell = [] (Harness& h) { h.set (pid::eqOn, 1.0f); h.set (pid::lowType, 1.0f); h.set (pid::lowFreq, 300.0f); h.set (pid::lowGain, 9.0f); h.set (pid::lowQ, 2.0f); };
            const float centre = gainAt (lowBell, 300.0), far = gainAt (lowBell, 60.0);
            logMessage ("Low bell +9 dB @300 Hz: at 300 Hz " + juce::String (centre, 2) + " dB, at 60 Hz " + juce::String (far, 2) + " dB");
            expectWithinAbsoluteError (centre, 9.0f, 0.3f);
            expectLessThan (far, 1.0f, "a bell must not lift the deep lows like a shelf");

            const auto highBell = [] (Harness& h) { h.set (pid::eqOn, 1.0f); h.set (pid::highType, 1.0f); h.set (pid::highFreq, 3000.0f); h.set (pid::highGain, 6.0f); h.set (pid::highQ, 1.5f); };
            expectWithinAbsoluteError (gainAt (highBell, 3000.0), 6.0f, 0.3f);
            expectLessThan (gainAt (highBell, 15000.0), 1.0f);
        }

        beginTest ("Saturation characters: distinct harmonics, level compensated");
        {
            double h2[4] {}, h3[4] {};
            for (int c = 0; c < 4; ++c)
            {
                Harness h;
                h.neutral();
                h.set (pid::satOn, 1.0f);
                h.set (pid::satDrive, 12.0f);
                h.set (pid::satCharacter, (float) c);
                const auto in = h.sine (1000.0, 0.25f, 1.0);
                const auto out = h.run (in);
                expect (allFinite (out));
                const double f1 = toneLevel (out, 24000, 24000, 1000.0, h.fs);
                h2[c] = 20.0 * std::log10 (toneLevel (out, 24000, 24000, 2000.0, h.fs) / f1);
                h3[c] = 20.0 * std::log10 (toneLevel (out, 24000, 24000, 3000.0, h.fs) / f1);
                const float levelChange = rmsDb (out, 24000, 24000) - rmsDb (in, 24000, 24000);
                logMessage ("Character " + juce::String (c) + ": H2 " + juce::String (h2[c], 1) + " dB, H3 "
                            + juce::String (h3[c], 1) + " dB, level change " + juce::String (levelChange, 2) + " dB");
                expectWithinAbsoluteError (levelChange, 0.0f, 1.5f, "level compensation");
            }
            expectLessThan (h2[1], -60.0, "tape is symmetric: no 2nd harmonic");
            expectGreaterThan (h2[2], h2[0], "tube has more even harmonics than warm");
        }

        beginTest ("De-esser wideband vs split band");
        {
            const auto lowToneChange = [] (int mode)
            {
                Harness h;
                h.neutral();
                h.set (pid::deessOn, 1.0f);
                h.set (pid::deessMode, (float) mode);
                h.set (pid::deessFreq, 5000.0f);
                h.set (pid::deessThreshold, -40.0f);
                h.set (pid::deessRange, 12.0f);
                const auto in = twoTones (h.fs, 200.0, 0.2f, 8000.0, 0.3f, 1.0);
                const auto out = h.run (in);
                return 20.0 * std::log10 (toneLevel (out, 24000, 24000, 200.0, h.fs) / toneLevel (in, 24000, 24000, 200.0, h.fs));
            };
            const double split = lowToneChange (0), wide = lowToneChange (1);
            logMessage ("200 Hz component: split band " + juce::String (split, 2) + " dB, wideband " + juce::String (wide, 2) + " dB");
            expectWithinAbsoluteError (split, 0.0, 0.3);
            expectLessThan (wide, -6.0);
        }

        beginTest ("Compressor RMS detector and sidechain filter");
        {
            const auto reduction = [] (int detector, float scHpf, const juce::AudioBuffer<float>& in)
            {
                Harness h;
                h.neutral();
                h.set (pid::compOn, 1.0f);
                h.set (pid::compThreshold, -24.0f);
                h.set (pid::compRatio, 4.0f);
                h.set (pid::compDetector, (float) detector);
                h.set (pid::compScHpf, scHpf);
                h.run (in);
                return h.proc.getChain().getCompressor().gainReductionDb.pull();
            };

            Harness ref;

            // Calibration: on a sine both detectors read the same level
            const auto sine = ref.sine (1000.0, 0.5f, 1.0);
            const float peakSine = reduction (0, 20.0f, sine), rmsSine = reduction (1, 20.0f, sine);
            logMessage ("Sine: peak detector " + juce::String (peakSine, 2) + " dB GR, RMS " + juce::String (rmsSine, 2) + " dB GR");
            // -6 dBFS sine, threshold -24 dB, 4:1 -> 18 dB over * (1 - 1/4) = 13.5 dB
            expectWithinAbsoluteError (rmsSine, 13.5f, 0.3f, "RMS detector calibrated to sine peak level");
            expectWithinAbsoluteError (peakSine, 13.5f, 1.5f);

            // RMS detection regulates more calmly: less gain ripple on noise
            const auto ripple = [&] (int detector)
            {
                Harness h;
                h.neutral();
                h.set (pid::compOn, 1.0f);
                h.set (pid::compThreshold, -30.0f);
                h.set (pid::compRatio, 8.0f);
                h.set (pid::compAttack, 1.0f);
                h.set (pid::compRelease, 30.0f);
                h.set (pid::compDetector, (float) detector);
                const auto in = h.noise (0.3f, 2.0, 3);
                const auto out = h.run (in);
                const int L = h.proc.getLatencySamples(), win = 240; // 5 ms
                juce::Array<double> gains;
                for (int s = 48000; s + win < in.getNumSamples(); s += win)
                {
                    double ei = 0.0, eo = 0.0;
                    for (int i = s; i < s + win; ++i)
                    {
                        ei += std::pow (in.getSample (0, i - L), 2.0);
                        eo += std::pow (out.getSample (0, i), 2.0);
                    }
                    gains.add (10.0 * std::log10 (eo / ei));
                }
                double mean = 0.0, var = 0.0;
                for (auto g : gains) mean += g;
                mean /= gains.size();
                for (auto g : gains) var += (g - mean) * (g - mean);
                return std::sqrt (var / gains.size());
            };
            const double peakRipple = ripple (0), rmsRipple = ripple (1);
            logMessage ("Gain ripple on noise: peak " + juce::String (peakRipple, 2) + " dB, RMS " + juce::String (rmsRipple, 2) + " dB");
            expectLessThan (rmsRipple, peakRipple);

            const auto bassy = twoTones (ref.fs, 60.0, 0.5f, 2000.0, 0.02f, 1.0);
            const float noFilter = reduction (0, 20.0f, bassy), filtered = reduction (0, 250.0f, bassy);
            logMessage ("Bass-heavy input: GR without SC filter " + juce::String (noFilter, 1) + " dB, with 250 Hz filter " + juce::String (filtered, 1) + " dB");
            expectGreaterThan (noFilter, 10.0f);
            expectLessThan (filtered, noFilter - 8.0f);
        }
    }
};

static ProExtensionTests proExtensionTests;
