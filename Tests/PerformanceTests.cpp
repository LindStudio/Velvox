// Worst-case CPU load: every module on at once (offline estimate, Release build).

#include "TestHelpers.h"

using namespace velvox;
using namespace velvox::test;

class PerformanceTests final : public juce::UnitTest
{
public:
    PerformanceTests() : juce::UnitTest ("Velvox performance", "Velvox") {}

    void runTest() override
    {
        for (double fs : { 48000.0, 96000.0 })
        {
            beginTest ("All modules on at " + juce::String (fs / 1000.0, 1) + " kHz");

            Harness h (fs, 256);
            for (auto& p : getFactoryPresets())
                if (p.name == "Hypertechno Lead")
                    p.applyTo (h.proc.getState());
            for (auto* id : { pid::eqOn, pid::compOn, pid::deessOn, pid::satOn, pid::doublerOn, pid::pitchOn,
                              pid::chopOn, pid::delayOn, pid::reverbOn, pid::lpfOn,
                              pid::pulseOn, pid::smashOn, pid::choirOn, pid::smoothOn, pid::checkOn, pid::rideOn })
                h.set (id, 1.0f);
            h.set (pid::pitchSemitones, -12.0f);
            h.set (pid::choirVoices, 3.0f);   // 8 voices
            h.set (pid::levelMatchMode, 2.0f);
            h.set (pid::stereoWidth, 140.0f);
            h.set (pid::monoBelow, 150.0f);
            h.proc.updateHostLatency();

            // Best of three runs: the cost of the DSP without interference from other work
            // on the machine (earlier tests, background threads)
            const double seconds = 10.0;
            const auto in = h.noise (0.3f, seconds, 3);
            juce::AudioBuffer<float> out;
            double ms = 1.0e12;
            for (int run = 0; run < 3; ++run)
            {
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                out = h.run (in, 256);
                ms = std::min (ms, juce::Time::getMillisecondCounterHiRes() - t0);
            }
            const double share = ms / (seconds * 1000.0);

            logMessage ("Worst case @ " + juce::String (fs / 1000.0, 1) + " kHz, 256-sample blocks: "
                        + juce::String (ms, 0) + " ms for " + juce::String (seconds, 0) + " s stereo = "
                        + juce::String (share * 100.0, 1) + " % of one core");
            expect (allFinite (out));
            expectLessThan (share, 0.25, "must leave plenty of headroom");
        }
    }
};

static PerformanceTests performanceTests;
