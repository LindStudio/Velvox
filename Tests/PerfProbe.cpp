// Diagnostic (run with: VelvoxTests --test "perf probe"): does anything a test leaves
// behind slow down later processing in the same process? Measures the worst-case chain,
// runs one suspect action, measures again.

#include "TestHelpers.h"
#include "DSP/ReverbModule.h"
#include "DSP/DelayModule.h"

using namespace velvox;
using namespace velvox::test;

namespace
{
    double worstCaseMs();
    double reverbOnlyMs();
    double delayOnlyMs();

    double worstCaseMs()
    {
        Harness h (48000.0, 256);
        for (auto& p : getFactoryPresets())
            if (p.name == "Hypertechno Lead")
                p.applyTo (h.proc.getState());
        for (auto* id : { pid::eqOn, pid::compOn, pid::deessOn, pid::satOn, pid::doublerOn,
                          pid::delayOn, pid::reverbOn, pid::lpfOn,
                          pid::smashOn, pid::smoothOn, pid::checkOn, pid::rideOn })
            h.set (id, 1.0f);
        const auto in = h.noise (0.3f, 4.0, 3);
        double best = 1.0e12;
        for (int run = 0; run < 3; ++run)
        {
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            h.run (in, 256);
            best = std::min (best, juce::Time::getMillisecondCounterHiRes() - t0);
        }
        return best;
    }

    double reverbOnlyMs()
    {
        dsp::ReverbModule rv;
        dsp::ReverbSettings s;
        s.type = 3;
        rv.setSettings (s);
        rv.prepare (48000.0, 256);
        juce::AudioBuffer<float> in (2, 256), out (2, 256);
        juce::Random r (1);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 256; ++i)
                in.setSample (ch, i, r.nextFloat() - 0.5f);
        double best = 1.0e12;
        for (int run = 0; run < 3; ++run)
        {
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            for (int b = 0; b < 750; ++b)   // 4 s
                rv.process (in.getArrayOfReadPointers(), out.getArrayOfWritePointers(), 2, 256);
            best = std::min (best, juce::Time::getMillisecondCounterHiRes() - t0);
        }
        return best;
    }

    double delayOnlyMs()
    {
        dsp::DelayModule d;
        dsp::DelaySettings s;
        s.feedback = 0.5f;
        d.setSettings (s);
        d.prepare (48000.0, 256);
        juce::AudioBuffer<float> in (2, 256), out (2, 256);
        in.clear();
        in.setSample (0, 0, 1.0f);
        double best = 1.0e12;
        for (int run = 0; run < 3; ++run)
        {
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            for (int b = 0; b < 750; ++b)
                d.process (in.getArrayOfReadPointers(), out.getArrayOfWritePointers(), 2, 256);
            best = std::min (best, juce::Time::getMillisecondCounterHiRes() - t0);
        }
        return best;
    }
}

class PerfProbe final : public juce::UnitTest
{
public:
    PerfProbe() : juce::UnitTest ("Perf probe (diagnostic)", "Diagnostics") {}

    void runTest() override
    {
        beginTest ("Order effects on the processing speed");
        const double base = worstCaseMs();
        logMessage ("baseline: " + juce::String (base, 0) + " ms");
        logMessage ("reverb only (large hall, 4 s): " + juce::String (reverbOnlyMs(), 1) + " ms");
        logMessage ("delay only (4 s): " + juce::String (delayOnlyMs(), 1) + " ms");

        expect (true);
    }
};

static PerfProbe perfProbe;
