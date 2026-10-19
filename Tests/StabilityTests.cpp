// Audio stability: the occasional pop (out-of-bounds read in modulated delay
// lines), digital silence, discontinuities over long runs with FL-like block
// sizes, and the audio-thread diagnostics.

#include "TestHelpers.h"
#include "PopScan.h"

using namespace velvox;
using namespace velvox::test;

class StabilityTests final : public juce::UnitTest
{
public:
    StabilityTests() : juce::UnitTest ("Velvox audio stability", "Velvox") {}

    void runTest() override
    {
        beginTest ("Fractional delay index never leaves the buffer (the float wrap bug behind the pops)");
        {
            // The old code: pos = newest - delay; while (pos < 0) pos += size; i0 = (int) pos.
            // A delay a few ulps above `newest` makes pos + size round to exactly `size`.
            const float tiny = std::nextafter (0.0f, 1.0f) * 1000.0f;
            float pos = 0.0f - tiny;
            while (pos < 0.0f)
                pos += 3500.0f;
            expectEquals ((int) pos, 3500, "the old wrap really produced index == size");

            int checked = 0;
            for (int size : { 2, 7, 3500, 65536 })
                for (int newest : { 0, 1, size / 2, size - 1 })
                    for (int k : { 0, 1, size / 2, std::max (0, size - 3) })
                        for (float delta : { 0.0f, tiny, -tiny, 0.5f, 0.999999f, 1.0e-7f })
                        {
                            const float delay = (float) k + delta;
                            const auto f = dsp::fractionalIndex (newest, delay, size);
                            ++checked;
                            if (f.i0 < 0 || f.i0 >= size || f.i1 < 0 || f.i1 >= size || ! (f.frac >= 0.0f && f.frac < 1.0f))
                                expect (false, "out of range: size " + juce::String (size) + " newest " + juce::String (newest)
                                                + " delay " + juce::String (delay, 9));
                        }
            for (float bad : { -1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), 1.0e9f })
            {
                const auto f = dsp::fractionalIndex (10, bad, 64);
                expect (f.i0 >= 0 && f.i0 < 64 && f.i1 >= 0 && f.i1 < 64, "invalid delays are clamped");
            }
            expect (checked > 300);

            // Interpolation is still correct
            std::vector<float> buf { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f };
            expectWithinAbsoluteError (dsp::readFractional (buf, 4, 0.0f), 4.0f, 1.0e-6f);
            expectWithinAbsoluteError (dsp::readFractional (buf, 4, 1.5f), 2.5f, 1.0e-6f);
            expectWithinAbsoluteError (dsp::readFractional (buf, 0, 1.0f), 4.0f, 1.0e-6f);   // wraps to the end
        }

        beginTest ("Auto-mix chain, 90 s steady tone, FL-like block sizes: no discontinuities");
        {
            const auto r = runPopScan ("tone", true, false, 90.0, 44100.0, false, 3);
            logMessage ("    " + juce::String (r.discontinuities) + " discontinuities");
            for (auto& l : r.log)
                logMessage (l);
            expectEquals (r.discontinuities, 0);
        }

        beginTest ("Auto-mix chain on digital silence (stopped transport): exactly silent output");
        {
            const auto r = runPopScan ("silence", true, false, 60.0, 48000.0, false, 4);
            expectEquals (r.silentEvents, 0, "max " + juce::String (r.maxSilentDb, 1) + " dBFS");
        }

        beginTest ("Vocal with reverb and doubler (delay off), 60 s: no discontinuities");
        {
            // The delay is excluded: its echoes of S sounds in vocal pauses look like
            // curvature spikes to the detector without being discontinuities.
            const auto r = runPopScan ("vocal", true, false, 60.0, 44100.0, false, 5, "delayOn=0");
            for (auto& l : r.log)
                logMessage (l);
            expectEquals (r.discontinuities, 0);
        }

        beginTest ("Diagnostics: counters stay clean in normal operation, NaN input is contained");
        {
            Harness h;
            const auto out = h.run (h.sine (440.0, 0.2f, 2.0));
            expect (allFinite (out));
            auto& m = h.proc.getChain().meters;
            expectEquals (m.nonFiniteResets.load(), 0);
            expect (m.samplesProcessed.load() > 0);

            auto bad = h.sine (440.0, 0.2f, 0.1);
            bad.setSample (0, 100, std::numeric_limits<float>::quiet_NaN());
            const auto out2 = h.run (bad);
            expect (allFinite (out2), "NaN never reaches the output");
            expect (h.proc.getCallbackLoad().averageRatio.load() < 1.0f, "average callback load below real time");
        }
    }

};

static StabilityTests stabilityTests;
