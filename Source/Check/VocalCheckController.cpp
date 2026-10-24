#include "VocalCheckController.h"
#include "CheckRules.h"
#include "../StateKeys.h"

namespace velvox::check
{
    VocalCheckController::VocalCheckController (juce::AudioProcessorValueTreeState& state, dsp::VocalAnalyzer& a, Host h)
        : apvts (state), analyzer (a), host (std::move (h)), eqResolver (std::make_unique<EqBandsResolver>())
    {
    }

    std::vector<const dsp::Finding*> VocalCheckController::visibleFindings() const
    {
        std::vector<const dsp::Finding*> v;
        for (auto& f : host.latest().findings)
            if (! isIgnored (f.id))
                v.push_back (&f);
        return v;
    }

    std::vector<const dsp::Finding*> VocalCheckController::ignoredFindings() const
    {
        std::vector<const dsp::Finding*> v;
        for (auto& f : host.latest().findings)
            if (isIgnored (f.id))
                v.push_back (&f);
        return v;
    }

    const dsp::Finding* VocalCheckController::finding (const juce::String& id) const
    {
        for (auto& f : host.latest().findings)
            if (f.id == id)
                return &f;
        return nullptr;
    }

    int VocalCheckController::score() const
    {
        return scoreOf (host.latest(), [this] (const dsp::Finding& f) { return ! isIgnored (f.id); });
    }

    FixContext VocalCheckController::context() const
    {
        FixContext c;
        c.value = [this] (const juce::String& id)
        {
            if (auto* p = apvts.getParameter (id))
                return p->convertFrom0to1 (p->getValue());
            return 0.0f;
        };
        c.routing = host.routing();
        c.audible = host.audible;
        c.eq = eqResolver.get();
        return c;
    }

    ResolvedFix VocalCheckController::preview (const juce::String& id) const
    {
        if (const auto* f = finding (id))
            return planFix (f->action, "Vocal Check: " + f->problem, context());
        return {};
    }

    bool VocalCheckController::canApply (const juce::String& id) const
    {
        const auto* f = finding (id);
        if (f == nullptr || isIgnored (id) || f->action.empty())
            return false;
        // Applied on this very measurement already: wait for a fresh one (no double cuts)
        const auto it = appliedInGeneration.find (id);
        if (it != appliedInGeneration.end() && it->second == host.latest().generation)
            return false;
        return ! preview (id).empty();
    }

    bool VocalCheckController::apply (const juce::String& id)
    {
        if (! canApply (id))
            return false;
        applyResolvedFix (apvts, preview (id));
        afterApply ({ id });
        return true;
    }

    int VocalCheckController::applyAll()
    {
        std::vector<std::pair<FixAction, juce::String>> fixes;
        std::vector<juce::String> ids;
        for (const auto* f : visibleFindings())
            if (canApply (f->id))
            {
                fixes.emplace_back (f->action, f->problem);
                ids.push_back (f->id);
            }
        if (fixes.empty())
            return 0;
        const auto fix = planFixes (fixes, context(), juce::String::fromUTF8 ("Vocal Check: Alles anwenden"));
        if (fix.empty())
            return 0;
        applyResolvedFix (apvts, fix);
        afterApply (ids);
        return (int) ids.size();
    }

    void VocalCheckController::afterApply (const std::vector<juce::String>& ids)
    {
        const auto generation = host.latest().generation;
        for (auto& id : ids)
            appliedInGeneration[id] = generation;
        // The chain changed: in continuous mode measure afresh (the old frames do not contain
        // the fix). A finished capture keeps its measurement; the generation guard above
        // stops a second apply on it.
        if (analyzer.getMode() == dsp::VocalAnalyzer::Mode::continuous)
            analyzer.clear();
        if (host.refresh)
            host.refresh();
    }

    //==========================================================================
    juce::StringArray VocalCheckController::getIgnoredIds() const
    {
        juce::StringArray ids;
        ids.addTokens (apvts.state.getProperty (statekeys::checkIgnored).toString(), ",", {});
        ids.trim();
        ids.removeEmptyStrings();
        return ids;
    }

    bool VocalCheckController::isIgnored (const juce::String& id) const
    {
        return getIgnoredIds().contains (id);
    }

    void VocalCheckController::setIgnored (const juce::StringArray& ids)
    {
        // A view setting of the analysis: not part of the undo history
        apvts.state.setProperty (statekeys::checkIgnored, ids.joinIntoString (","), nullptr);
    }

    void VocalCheckController::ignore (const juce::String& id)
    {
        auto ids = getIgnoredIds();
        if (id.isNotEmpty() && ! ids.contains (id))
        {
            ids.add (id);
            setIgnored (ids);
        }
    }

    void VocalCheckController::restore (const juce::String& id)
    {
        auto ids = getIgnoredIds();
        ids.removeString (id);
        setIgnored (ids);
    }

    void VocalCheckController::restoreAll()
    {
        setIgnored ({});
    }

    void VocalCheckController::setGenre (Genre genre)
    {
        apvts.state.setProperty (statekeys::checkGenre, (int) genre, nullptr);
        if (host.refresh)
            host.refresh();
    }

    Genre VocalCheckController::getGenre() const
    {
        return (Genre) juce::jlimit (0, 2, (int) apvts.state.getProperty (statekeys::checkGenre, 0));
    }

    void VocalCheckController::setEqResolver (std::unique_ptr<EqBandResolver> resolver)
    {
        if (resolver != nullptr)
            eqResolver = std::move (resolver);
    }
}
