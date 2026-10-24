// 0.6 dynamics quality track: Compressor, De-Esser, Vocal Smash, Level Ride.
// Measurable quality targets (QUALITY_BASELINE gaps #6 and #8), the new options and the
// lock-free UI data feeds of the module panels.

#include "TestHelpers.h"
#include "DSP/DynamicsFeed.h"

using namespace velvox;
using namespace velvox::test;
using namespace velvox::dsp;

namespace
{
    constexpr double twoPi = 6.283185307179586;
}

//==============================================================================
class DynamicsFeedTests final : public juce::UnitTest
{
public:
    DynamicsFeedTests() : juce::UnitTest ("Dynamics feed", "Dynamics") {}

    void runTest() override
    {
        beginTest ("FrameFifo: order kept");
        {
            FrameFifo<int, 8> f;
            for (int i = 0; i < 5; ++i)
                expect (f.push (i));
            int out[8] {};
            expectEquals (f.pull (out, 3), 3);
            expectEquals (out[0], 0);
            expectEquals (out[2], 2);
            for (int i = 5; i < 9; ++i)
                expect (f.push (i));
            expectEquals (f.pull (out, 8), 6);
            expectEquals (out[0], 3);
            expectEquals (out[5], 8);
        }

        beginTest ("Dynamics feed: frames carry the peak of each frame");
        {
            const double fs = 48000.0;
            DynamicsHistory h;
            h.prepare (fs, 5.0);
            const int n = (int) fs;   // 1 s
            for (int i = 0; i < n; ++i)
            {
                const float x = 0.5f * (float) std::sin (twoPi * 1000.0 * i / fs);
                h.add (std::abs (x), std::abs (0.5f * x), 6.0f);
            }
            std::vector<DynamicsFrame> frames (DynamicsHistory::capacity);
            const int got = h.pull (frames.data(), (int) frames.size());
            expectEquals (got, n / juce::roundToInt (fs * 0.005));
            expectWithinAbsoluteError (h.frameSeconds(), 0.005, 1.0e-9);
            const auto& last = frames[(size_t) got - 1];
            expectWithinAbsoluteError (last.inputDb, -6.02f, 0.1f);
            expectWithinAbsoluteError (last.outputDb, -12.04f, 0.1f);
            expectWithinAbsoluteError (last.gainReductionDb, 6.0f, 1.0e-6f);
        }

        beginTest ("Dynamics feed: never blocks, drops when full");
        {
            DynamicsHistory h;
            h.prepare (48000.0, 1.0);
            const int samplesPerFrame = 48;
            for (int i = 0; i < 10 * DynamicsHistory::capacity * samplesPerFrame; ++i)
                h.add (0.1f, 0.1f, 0.0f);
            std::vector<DynamicsFrame> frames (DynamicsHistory::capacity * 2);
            expectEquals (h.pull (frames.data(), (int) frames.size()), DynamicsHistory::capacity);
            expectEquals (frames[0].inputDb, juce::Decibels::gainToDecibels (0.1f, -120.0f));
            // Silence reads as the floor, never -inf
            h.reset();
            for (int i = 0; i < samplesPerFrame; ++i)
                h.add (0.0f, 0.0f, 0.0f);
            expectEquals (h.pull (frames.data(), 4), 1);
            expectEquals (frames[0].inputDb, -120.0f);
        }
    }
};

static DynamicsFeedTests dynamicsFeedTests;
