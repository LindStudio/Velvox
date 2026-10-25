#pragma once

#include "CheckFixPlanner.h"
#include "CheckTargets.h"
#include "../DSP/VocalAnalyzer.h"
#include <map>
#include <memory>

// VOCAL CHECK for the UI (message thread): the visible findings, the score, "Anwenden" /
// "Alles anwenden" (one undo step each), "Ignorieren" / restore (persisted with the project)
// and the genre the targets are taken from.
namespace velvox::check
{
    class VocalCheckController
    {
    public:
        // What the controller needs from the processor
        struct Host
        {
            std::function<const dsp::AnalysisResult&()> latest;          // latest analysis (with handling)
            std::function<void()> refresh;                                // re-run the analysis now
            std::function<dsp::Routing()> routing;
            std::function<bool (dsp::ModuleId, int)> audible;
            UndoHistory* history = nullptr;                               // the global undo ("Anwenden" = one step)
        };

        VocalCheckController (juce::AudioProcessorValueTreeState& state, dsp::VocalAnalyzer& analyzer, Host host);

        // ---- findings (the latest analysis; confidence >= showThreshold already)
        std::vector<const dsp::Finding*> visibleFindings() const;   // not ignored, in display order
        std::vector<const dsp::Finding*> ignoredFindings() const;   // ignored ones of the latest analysis ("x ignoriert")
        const dsp::Finding* finding (const juce::String& id) const;
        int score() const;                                          // 0..100 over the visible findings, -1 = not enough material

        // ---- Anwenden
        bool canApply (const juce::String& id) const;               // visible, has a fix that changes something, not applied on this measurement
        ResolvedFix preview (const juce::String& id) const;         // what apply() would write
        bool apply (const juce::String& id);                        // one undo step
        int applyAll();                                             // every applicable visible finding, one undo step; returns how many
        // Applied on the current measurement and still in place (an undo makes it applicable again)
        bool wasApplied (const juce::String& id) const;

        // ---- Ignorieren (ids persist in the plugin state, also for findings not present now)
        void ignore (const juce::String& id);
        void restore (const juce::String& id);
        void restoreAll();
        bool isIgnored (const juce::String& id) const;
        juce::StringArray getIgnoredIds() const;

        // ---- targets
        void setGenre (Genre genre);
        Genre getGenre() const;

        // The EQ track's resolver replaces the legacy one after the merge
        void setEqResolver (std::unique_ptr<EqBandResolver> resolver);
        const EqBandResolver& getEqResolver() const noexcept { return *eqResolver; }

    private:
        FixContext context() const;
        void write (const ResolvedFix& fix, const std::vector<juce::String>& ids);
        void setIgnored (const juce::StringArray& ids);

        juce::AudioProcessorValueTreeState& apvts;
        dsp::VocalAnalyzer& analyzer;
        Host host;
        std::unique_ptr<EqBandResolver> eqResolver;
        // finding id -> the measurement it was applied on and what the fix left in the state. While
        // that is still in place, a second apply on the same measurement is blocked (no double cuts);
        // an undo puts the old values back and makes the finding applicable again.
        struct Applied
        {
            std::uint64_t generation = 0;
            std::vector<std::pair<juce::String, float>> values;   // full id -> normalised value after the fix
            juce::String routing;                                 // empty: the fix left the routing alone
        };
        std::map<juce::String, Applied> applied;
    };
}
