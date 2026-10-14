#include "ProposalSession.h"

namespace velvox::mixing
{
    void ProposalSession::begin (const ParameterSnapshot& proposal)
    {
        if (active)
            reject(); // never stack proposals: return to a known state first

        original = ParameterSnapshot::capture (apvts, "Original");
        proposed = proposal;
        proposed.applyTo (apvts, true);
        active = true;
        showingProposal = true;
    }

    void ProposalSession::showOriginal()
    {
        if (! active || ! showingProposal)
            return;
        proposed = ParameterSnapshot::capture (apvts, proposed.name); // keep tweaks made while listening
        original.applyTo (apvts, true);
        showingProposal = false;
    }

    void ProposalSession::showProposal()
    {
        if (! active || showingProposal)
            return;
        proposed.applyTo (apvts, true);
        showingProposal = true;
    }

    void ProposalSession::accept()
    {
        if (! active)
            return;
        showProposal();
        active = false;
    }

    void ProposalSession::reject()
    {
        if (! active)
            return;
        original.applyTo (apvts, true);
        active = false;
        showingProposal = false;
    }
}
