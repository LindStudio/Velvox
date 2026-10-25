// A/B is gone (spec 2.2, 8 Q3). What stays: the automatic mix keeps a hidden snapshot of the state
// before it, and "Vorher / Nachher" switches between that and the mix. Projects saved by 0.5 with
// A/B slots load the slot that was active (= the saved top-level state) and are saved without slots.

#include "TestHelpers.h"
#include "SyntheticVocal.h"

using namespace velvox;
using namespace velvox::test;
using dsp::ModuleId;

namespace
{
    juce::ValueTree stateTree (Harness& h)
    {
        juce::MemoryBlock block;
        h.proc.getStateInformation (block);
        const auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
        return xml != nullptr ? juce::ValueTree::fromXml (*xml) : juce::ValueTree();
    }

    juce::MemoryBlock toBlock (const juce::ValueTree& tree)
    {
        juce::MemoryBlock block;
        if (auto xml = tree.createXml())
            juce::AudioProcessor::copyXmlToBinary (*xml, block);
        return block;
    }

    void runMix (Harness& h)
    {
        auto& am = h.proc.getAutoMix();
        am.dismissPrompt();
        VocalSpec s;
        s.seconds = 14.0;
        s.channels = 2;
        s.sampleRate = h.fs;
        am.applyResult (am.runSynchronously (makeVocal (s)));
    }
}

class CompareTests final : public juce::UnitTest
{
public:
    CompareTests() : juce::UnitTest ("Headliner compare (A/B removed)", "Headliner") {}

    void runTest() override
    {
        beginTest ("A 0.5 session saved with slot B active loads slot B and is saved without slots");
        {
            // Slot B (active) = top-level state of the blob: ratio 10, second EQ instance
            Harness b;
            b.set (pid::compRatio, 10.0f);
            auto r = b.proc.getRouting();
            r.insertAt (1, ModuleId::eq, 1);
            b.proc.setRouting (r);
            auto tree = stateTree (b);

            // ... and the A/B tree of 0.5: slot A different (ratio 2, default chain), B active
            Harness a;
            a.set (pid::compRatio, 2.0f);
            juce::ValueTree ab ("ABSlots");
            ab.setProperty ("active", 1, nullptr);
            auto slotA = ParameterSnapshot::capture (a.proc.getState(), "A").toValueTree();
            slotA.setProperty ("slot", 0, nullptr);
            ab.appendChild (slotA, nullptr);
            auto slotB = ParameterSnapshot::capture (b.proc.getState(), "B").toValueTree();
            slotB.setProperty ("slot", 1, nullptr);
            ab.appendChild (slotB, nullptr);
            tree.appendChild (ab, nullptr);
            const auto blob = toBlock (tree);

            Harness h;
            h.proc.setStateInformation (blob.getData(), (int) blob.getSize());
            expectWithinAbsoluteError (h.get (pid::compRatio), 10.0f, 0.01f, "the active slot B is what loads");
            expect (h.proc.getRouting().contains (ModuleId::eq, 1), "with its chain");
            expect (! stateTree (h).getChildWithName ("ABSlots").isValid(), "saved again without A/B slots");

            // The same with slot A active: the top-level state is slot A's
            Harness h2;
            auto treeA = stateTree (a);
            juce::ValueTree abA ("ABSlots");
            abA.setProperty ("active", 0, nullptr);
            slotB.setProperty ("slot", 1, nullptr);
            abA.appendChild (slotB.createCopy(), nullptr);
            treeA.appendChild (abA, nullptr);
            const auto blobA = toBlock (treeA);
            h2.proc.setStateInformation (blobA.getData(), (int) blobA.getSize());
            expectWithinAbsoluteError (h2.get (pid::compRatio), 2.0f, 0.01f, "slot A active -> slot A");
        }

        beginTest ("Auto mix: hidden before snapshot, Vorher / Nachher switches the real parameters");
        {
            Harness h;
            h.set (pid::compRatio, 2.0f);
            auto& am = h.proc.getAutoMix();
            expect (! am.hasPrevious(), "nothing to compare before an analysis");
            runMix (h);
            expect (am.hasMix() && am.hasPrevious());
            expect (! am.isShowingPrevious(), "the mix is active after the analysis");
            const float mixRatio = h.get (pid::compRatio);
            am.showPrevious (true);
            expect (am.isShowingPrevious());
            expectWithinAbsoluteError (h.get (pid::compRatio), 2.0f, 0.01f, "Vorher = the state before the analysis");
            am.showPrevious (false);
            expectWithinAbsoluteError (h.get (pid::compRatio), mixRatio, 0.01f, "Nachher = the mix");
            expectEquals (h.proc.getUndo().getNumSteps(), 1, "the mix is one undo step, the switch none");
        }

        beginTest ("Switching Vorher / Nachher while playing never clicks");
        {
            Harness h;
            // No slope pin any more: the 0.6 EQ crossfades slope / type changes (see the test below)
            runMix (h);
            const auto in = h.sine (180.0, 0.3f, 4.0);
            juce::MidiBuffer midi;
            const int bs = 256;
            float maxStep = 0.0f, last = 0.0f, steadyMax = 0.0f;
            int switches = 0;
            auto& am = h.proc.getAutoMix();
            for (int start = 0; start + bs <= in.getNumSamples(); start += bs)
            {
                if (start > 0 && start % 12000 < bs)
                {
                    am.showPrevious (! am.isShowingPrevious());
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
            logMessage ("switches: " + juce::String (switches) + ", largest step " + juce::String (maxStep, 4)
                        + " (steady state " + juce::String (steadyMax, 4) + ")");
            expectGreaterThan (switches, 10);
            expectLessThan (maxStep, std::max (0.12f, 1.6f * steadyMax), "no jump beyond the signal's own movement");
        }

        beginTest ("Switching the low-cut slope while playing never jumps (legacy hpfSlope and band slope)");
        {
            // Shell finding: with the 0.5 EQ a slope switch stepped the filter state (~0.16 on a 0.3 sine).
            for (int variant = 0; variant < 2; ++variant)
            {
                Harness h;
                dsp::Routing r;
                r.insertAt (0, ModuleId::eq, 0, true);
                h.proc.setRouting (r);
                h.set (pid::eqOn, 1.0f);
                h.set (pid::hpfOn, 1.0f);
                h.set (pid::hpfFreq, 100.0f);   // the bridge writes the low-cut role band
                const auto slopeId = pid::eqBandId (1, pid::eqband::slope);
                const auto setSlope = [&] (bool steep)
                {
                    if (variant == 0)
                        h.set (pid::hpfSlope, steep ? 2.0f : 0.0f);   // 12 <-> 48 dB/oct (0.5 IDs)
                    else
                        h.set (slopeId.toRawUTF8(), steep ? 5.0f : 0.0f);   // 6 <-> 48 dB/oct
                };
                setSlope (false);
                expectEquals ((int) h.get (pid::eqBandId (1, pid::eqband::role).toRawUTF8()), (int) dsp::EqRole::lowCut, "role band 1");

                const auto in = h.sine (130.0, 0.3f, 3.0);
                juce::MidiBuffer midi;
                const int bs = 256;
                float maxStep = 0.0f, steadyMax = 0.0f, last = 0.0f;
                int switches = 0;
                for (int start = 0; start + bs <= in.getNumSamples(); start += bs)
                {
                    if (start > 0 && start % 12032 < bs)
                        setSlope (++switches % 2 == 1);
                    juce::AudioBuffer<float> block (2, bs);
                    for (int ch = 0; ch < 2; ++ch)
                        block.copyFrom (ch, 0, in, ch, start, bs);
                    h.proc.processBlock (block, midi);
                    for (int i = 0; i < bs; ++i)
                    {
                        const float y = block.getSample (0, i);
                        if (start + i > 4800)
                        {
                            maxStep = std::max (maxStep, std::abs (y - last));
                            if (start % 12032 > 7000)
                                steadyMax = std::max (steadyMax, std::abs (y - last));
                        }
                        last = y;
                    }
                }
                logMessage ((variant == 0 ? "hpfSlope" : "eqBand1Slope") + juce::String (": switches ") + juce::String (switches)
                            + ", largest step " + juce::String (maxStep, 4) + " (steady " + juce::String (steadyMax, 4) + ")");
                expectGreaterThan (switches, 8);
                expectLessThan (maxStep, std::max (0.012f, 1.6f * steadyMax), "no jump beyond the signal's own movement");
            }
        }

        beginTest ("The before snapshot is saved with the project");
        {
            Harness h;
            h.set (pid::compRatio, 2.0f);
            runMix (h);
            juce::MemoryBlock block;
            h.proc.getStateInformation (block);
            Harness h2;
            h2.proc.setStateInformation (block.getData(), (int) block.getSize());
            expect (h2.proc.getAutoMix().hasPrevious());
            h2.proc.getAutoMix().showPrevious (true);
            expectWithinAbsoluteError (h2.get (pid::compRatio), 2.0f, 0.01f);
        }

        beginTest ("An edit while Vorher is shown ends the compare and keeps the edit; undo returns to Nachher exactly");
        {
            Harness h;
            h.set (pid::compRatio, 2.0f);
            h.set (pid::compThreshold, -30.0f);
            runMix (h);
            auto& am = h.proc.getAutoMix();
            auto& undo = h.proc.getUndo();
            const auto nachher = ParameterSnapshot::capture (h.proc.getState());
            am.showPrevious (true);
            expectWithinAbsoluteError (h.get (pid::compRatio), 2.0f, 0.01f);
            const int stepsBefore = undo.getNumSteps();

            auto* p = h.proc.getState().getParameter (pid::compRatio);   // the user edits "Vorher"
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (7.0f));
            p->endChangeGesture();
            expect (! am.isShowingPrevious(), "the edit ends the compare");
            expectWithinAbsoluteError (h.get (pid::compRatio), 7.0f, 0.01f, "the edit is kept");
            expectWithinAbsoluteError (h.get (pid::compThreshold), -30.0f, 0.01f, "on top of the Vorher state");
            am.showPrevious (false);   // "Nachher" now: nothing to switch back to, nothing is discarded
            expectWithinAbsoluteError (h.get (pid::compRatio), 7.0f, 0.01f, "Nachher does not discard the edit");
            expectEquals (undo.getNumSteps(), stepsBefore + 1, "one step");

            expect (undo.undo());
            expect (! nachher.differsFrom (h.proc.getState()), "undo returns to the Nachher state exactly");
            expect (! am.isShowingPrevious());
            expect (undo.redo());
            expectWithinAbsoluteError (h.get (pid::compRatio), 7.0f, 0.01f, "redo: the edit again");
            expectWithinAbsoluteError (h.get (pid::compThreshold), -30.0f, 0.01f);

            // The compare still works afterwards: Vorher = before the mix, Nachher = the edited state
            am.showPrevious (true);
            expectWithinAbsoluteError (h.get (pid::compRatio), 2.0f, 0.01f);
            am.showPrevious (false);
            expectWithinAbsoluteError (h.get (pid::compRatio), 7.0f, 0.01f);

            // Undo of the Auto-Mix step itself stays consistent too: back to the state before it
            undo.undo();
            undo.undo();
            expectWithinAbsoluteError (h.get (pid::compRatio), 2.0f, 0.01f);
        }
    }
};

static CompareTests compareTests;
