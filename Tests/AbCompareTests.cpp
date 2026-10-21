// A/B comparison: two independent, complete states (parameters + chain with repeated
// instances). Identical states sound identical, different states differ, switching
// while audio runs never clicks, copy works in both directions, the auto mix fills
// A = before and B = mix.

#include "TestHelpers.h"
#include "SyntheticVocal.h"

using namespace velvox;
using namespace velvox::test;
using dsp::ModuleId;

namespace
{
    void setId (Harness& h, const juce::String& id, float v)
    {
        auto* p = h.proc.getState().getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (v));
    }

    // A very different second sound: bright EQ, hard compression, saturation, a second
    // EQ instance, reverb as insert
    void makeSoundB (Harness& h)
    {
        h.set (pid::highGain, 12.0f);
        h.set (pid::compThreshold, -40.0f);
        h.set (pid::compRatio, 12.0f);
        h.set (pid::compMakeup, 10.0f);
        h.set (pid::satOn, 1.0f);
        h.set (pid::satDrive, 18.0f);
        setId (h, "eqOn_2", 1.0f);
        setId (h, "lowGain_2", -10.0f);
        h.set (pid::reverbOn, 1.0f);
        h.set (pid::reverbMix, 40.0f);
        h.set (pid::reverbType, 5.0f);
        auto r = h.proc.getRouting();
        r.insertAt (1, ModuleId::eq, 1);
        r.setInsert (ModuleId::reverb, 0, true);
        h.proc.setRouting (r);
    }
}

class AbCompareTests final : public juce::UnitTest
{
public:
    AbCompareTests() : juce::UnitTest ("Headliner A/B comparison", "Headliner") {}

    void runTest() override
    {
        beginTest ("B starts as an explicit copy of A; identical states render identically");
        {
            Harness a, b;
            for (auto* h : { &a, &b })
                h->set (pid::compRatio, 5.0f);
            b.proc.selectSlot (1);
            expect (b.proc.isSlotFilled (1) && b.proc.slotsAreEqual(), "B = copy of A");
            const auto in = a.noise (0.3f, 1.0, 71);
            const auto outA = a.run (in), outB = b.run (in);
            double d = 0.0;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < in.getNumSamples(); ++i)
                    d = std::max (d, (double) std::abs (outA.getSample (ch, i) - outB.getSample (ch, i)));
            logMessage ("identical A and B: max difference " + juce::String (d, 9));
            expectLessThan (d, 1.0e-6);
        }

        beginTest ("Different states really change the rendered output");
        {
            Harness h;
            h.proc.selectSlot (1);
            makeSoundB (h);
            expect (! h.proc.slotsAreEqual(), "A and B differ");
            const auto in = h.noise (0.2f, 1.0, 73);
            const auto outB = h.run (in);
            h.proc.selectSlot (0);
            h.run (h.silence (0.3));
            const auto outA = h.run (in);
            const double diff = rmsDb (outB, 9600, 38400) - rmsDb (outA, 9600, 38400);
            logMessage ("B vs A level difference: " + juce::String (diff, 1) + " dB");
            expectGreaterThan (std::abs (diff), 3.0);
            expect (h.proc.getRouting() == dsp::Routing::defaults(), "A has the default chain");
            h.proc.selectSlot (1);
            expect (h.proc.getRouting().contains (ModuleId::eq, 1) && h.proc.getRouting().isInsert (ModuleId::reverb), "B has its own chain");
        }

        beginTest ("Switching A/B while playing never clicks, even between very different states");
        {
            Harness h;
            h.proc.selectSlot (1);
            makeSoundB (h);
            h.proc.selectSlot (0);
            const auto in = h.sine (180.0, 0.3f, 4.0);
            juce::MidiBuffer midi;
            const int bs = 256;
            float maxStep = 0.0f, last = 0.0f, steadyMax = 0.0f;
            int switches = 0;
            for (int start = 0; start + bs <= in.getNumSamples(); start += bs)
            {
                if (start > 0 && start % 12000 < bs)
                {
                    h.proc.selectSlot (1 - h.proc.getActiveSlot());
                    ++switches;
                }
                juce::AudioBuffer<float> block (2, bs);
                for (int ch = 0; ch < 2; ++ch)
                    block.copyFrom (ch, 0, in, ch, start, bs);
                h.proc.processBlock (block, midi);
                for (int i = 0; i < bs; ++i)
                {
                    const float y = block.getSample (0, i);
                    const float step = std::abs (y - last);
                    if (start + i > 4800)
                    {
                        maxStep = std::max (maxStep, step);
                        if (start % 12000 > 6000)
                            steadyMax = std::max (steadyMax, step);
                    }
                    last = y;
                }
            }
            logMessage ("A/B switches: " + juce::String (switches) + ", largest step " + juce::String (maxStep, 4)
                        + " (steady state " + juce::String (steadyMax, 4) + ")");
            expectGreaterThan (switches, 10);
            expectLessThan (maxStep, std::max (0.12f, 1.6f * steadyMax), "no jump beyond the signal's own movement");
        }

        beginTest ("Copy works in both directions and the slots are saved with the project");
        {
            Harness h;
            h.set (pid::midGain, 4.0f);
            h.proc.selectSlot (1);
            h.set (pid::midGain, -6.0f);
            h.proc.copyActiveSlotToOther();                 // B -> A
            h.proc.selectSlot (0);
            expectWithinAbsoluteError (h.get (pid::midGain), -6.0f, 0.01f, "B copied to A");
            h.set (pid::midGain, 2.0f);
            h.proc.copyActiveSlotToOther();                 // A -> B
            h.proc.selectSlot (1);
            expectWithinAbsoluteError (h.get (pid::midGain), 2.0f, 0.01f, "A copied to B");

            h.set (pid::midGain, 7.0f);
            juce::MemoryBlock state;
            h.proc.getStateInformation (state);
            Harness h2;
            h2.proc.setStateInformation (state.getData(), (int) state.getSize());
            expectEquals (h2.proc.getActiveSlot(), 1);
            expectWithinAbsoluteError (h2.get (pid::midGain), 7.0f, 0.01f);
            h2.proc.selectSlot (0);
            expectWithinAbsoluteError (h2.get (pid::midGain), 2.0f, 0.01f, "A restored from the project");
        }

        beginTest ("Auto mix fills the slots: A = before, B = the mix");
        {
            Harness h;
            h.set (pid::compRatio, 2.0f);
            auto& am = h.proc.getAutoMix();
            am.dismissPrompt();
            VocalSpec s;
            s.seconds = 14.0;
            s.channels = 2;
            s.sampleRate = h.fs;
            am.applyResult (am.runSynchronously (makeVocal (s)));
            expectEquals (h.proc.getActiveSlot(), 1, "B is the mix");
            const float mixRatio = h.get (pid::compRatio);
            h.proc.selectSlot (0);
            expectWithinAbsoluteError (h.get (pid::compRatio), 2.0f, 0.01f, "A is the state before the analysis");
            h.proc.selectSlot (1);
            expectWithinAbsoluteError (h.get (pid::compRatio), mixRatio, 0.01f);
        }
    }
};

static AbCompareTests abCompareTests;
