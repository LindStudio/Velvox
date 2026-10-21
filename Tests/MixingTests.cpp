// Future smart-mixing groundwork (no AI): proposal compare / accept / reject,
// parameter schema completeness.

#include "TestHelpers.h"
#include "Parameters/RoutingMigration.h"
#include "Mixing/ProposalSession.h"
#include "Mixing/MixingEngine.h"

using namespace velvox;
using namespace velvox::test;

class MixingTests final : public juce::UnitTest
{
public:
    MixingTests() : juce::UnitTest ("Velvox mixing groundwork", "Velvox") {}

    void runTest() override
    {
        const auto proposal = [] {
            for (auto& p : getFactoryPresets())
                if (p.name == "Wide Festival Vocal")
                    return p;
            return ParameterSnapshot();
        }();

        beginTest ("Proposal: compare with the original, then reject restores exactly");
        {
            Harness h;
            h.set (pid::compRatio, 2.0f);
            auto r = dsp::Routing::defaults();
            r.insertAt (0, dsp::ModuleId::sat, 0);
            h.proc.setRouting (r);
            const auto before = ParameterSnapshot::capture (h.proc.getState());

            mixing::ProposalSession session (h.proc.getState());
            session.begin (proposal);
            expect (session.isActive() && session.isShowingProposal());
            expectWithinAbsoluteError (h.get (pid::compRatio), 3.5f, 0.01f);
            // The factory preset has no 0.4 routing: every module it uses, in the usual order
            expect (h.proc.getRouting() == dsp::Routing::fromString (migrateRoutingString (proposal.routing, h.proc.getState())),
                    "proposal routing applied: " + h.proc.getRouting().toString());
            expect (h.proc.getRouting().contains (dsp::ModuleId::doubler), "the proposal's doubler is in the chain");

            session.showOriginal();
            expectWithinAbsoluteError (h.get (pid::compRatio), 2.0f, 0.01f);
            expect (h.proc.getRouting() == r);
            session.showProposal();
            expectWithinAbsoluteError (h.get (pid::compRatio), 3.5f, 0.01f);

            session.reject();
            const auto after = ParameterSnapshot::capture (h.proc.getState());
            // tolerance: skewed ranges lose ~1e-6 in the normalise / denormalise round trip
            for (const auto& [id, v] : before.values)
                expectWithinAbsoluteError (after.values.at (id), v, 1.0e-3f, id);
            expect (h.proc.getRouting() == r, "original routing restored");
            expect (! session.isActive());
        }

        beginTest ("Proposal: tweaks made while comparing are kept on accept");
        {
            Harness h;
            mixing::ProposalSession session (h.proc.getState());
            session.begin (proposal);
            h.set (pid::reverbMix, 12.0f);      // user adjusts the proposal
            session.showOriginal();
            session.showProposal();
            expectWithinAbsoluteError (h.get (pid::reverbMix), 12.0f, 0.01f);
            session.accept();
            expect (! session.isActive());
            expectWithinAbsoluteError (h.get (pid::reverbMix), 12.0f, 0.01f);
            expectWithinAbsoluteError (h.get (pid::doublerMix), 55.0f, 0.01f);
        }

        beginTest ("Parameter schema covers every parameter incl. the new modules");
        {
            const auto schema = createParameterSchema();
            juce::StringArray ids;
            for (const auto& entry : *schema.getArray())
                ids.add (entry.getProperty ("id", {}).toString());
            for (auto* id : { pid::doublerMix, pid::pitchSemitones, pid::chopPattern, pid::reverbDuckAmount,
                              pid::stereoWidth, pid::levelMatchMode, pid::macroSpace })
                expect (ids.contains (id), juce::String ("schema misses ") + id);
            Harness h;
            expectEquals (ids.size(), h.proc.getParameters().size());
        }
    }
};

static MixingTests mixingTests;
