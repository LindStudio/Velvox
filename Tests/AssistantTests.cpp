// Smart Assistant: text interpretation, planning, applying to the real
// parameters, undo, and the Assistant page. Everything runs offline.
// Source is compiled as UTF-8 (/utf-8), so German test sentences are plain literals.

#include "TestHelpers.h"
#include "PluginEditor.h"
#include "Assistant/IntentParser.h"
#include "Assistant/AssistantPlanner.h"
#include "Assistant/AssistantSession.h"
#include "Assistant/AssistantContextBuilder.h"
#include "Parameters/ParameterLayout.h"
#include "UI/UserSettings.h"

using namespace velvox;
using namespace velvox::test;
using namespace velvox::assistant;

namespace
{
    juce::String de (const char* utf8) { return juce::String::fromUTF8 (utf8); }

    bool hasIntent (const ParsedRequest& r, Goal g, int dir)
    {
        for (auto& i : r.intents)
            if (i.goal == g && i.direction == dir)
                return true;
        return false;
    }

    const Change* changeFor (const Plan& p, const char* id)
    {
        for (auto& c : p.changes)
            if (c.paramId == id)
                return &c;
        return nullptr;
    }

    bool noteContains (const Plan& p, const juce::String& text)
    {
        for (auto& n : p.notes)
            if (n.text.contains (text))
                return true;
        return false;
    }

    Plan planFor (Harness& h, const juce::String& text)
    {
        return AssistantPlanner::plan (IntentParser::parse (text), buildContext (h.proc));
    }

    float value (Harness& h, const char* id)
    {
        auto* p = h.proc.getState().getParameter (id);
        return p->convertFrom0to1 (p->getValue());
    }

    struct GestureCounter final : juce::AudioProcessorListener
    {
        int begins = 0, ends = 0, changes = 0;
        void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override { ++changes; }
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
        void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int) override { ++begins; }
        void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int) override { ++ends; }
    };
}

class AssistantTests final : public juce::UnitTest
{
public:
    AssistantTests() : juce::UnitTest ("Velvox Smart Assistant", "Velvox") {}

    // Every proposed value exists, is finite, inside its range and on its raster
    void expectValid (const Plan& plan, const juce::String& context)
    {
        for (auto& c : plan.changes)
        {
            const auto* spec = findParameterSpec (c.paramId);
            expect (spec != nullptr, context + ": known parameter " + c.paramId);
            if (spec == nullptr)
                continue;
            expect (std::isfinite (c.proposed) && c.proposed >= spec->minValue && c.proposed <= spec->maxValue,
                    context + ": " + c.paramId + " in range");
            expectWithinAbsoluteError (legaliseValue (c.paramId, c.proposed), c.proposed, 1.0e-4f, context + ": " + c.paramId + " legal");
            expect (std::abs (c.proposed - c.current) > 1.0e-5f, context + ": " + c.paramId + " actually changes");
            expect (c.reason.isNotEmpty() && c.title.isNotEmpty() && c.module.isNotEmpty(), context + ": explained");
        }
    }

    void runTest() override
    {
        beginTest ("Synonyms and phrasings map to the same sound target");
        {
            for (auto* t : { "Weniger Tiefen.", "weniger Bass", "Bass reduzieren", "Die Stimme klingt zu basslastig.", "less low end" })
                expect (hasIntent (IntentParser::parse (de (t)), Goal::bass, -1), de ("bass down: ") + de (t));
            for (auto* t : { "Mehr Hall", "mehr Reverb", "etwas mehr Raum", "more reverb" })
                expect (hasIntent (IntentParser::parse (de (t)), Goal::reverb, 1), de ("reverb up: ") + de (t));
            for (auto* t : { "Stimme nach vorne", "mehr Präsenz", "Vocal stärker im Fokus", "Mach die Stimme präsenter" })
                expect (hasIntent (IntentParser::parse (de (t)), Goal::presence, 1), de ("presence up: ") + de (t));
            // Related, but not identical: "setzt sich nicht durch" is about the beat
            const auto through = IntentParser::parse (de ("Die Vocal setzt sich nicht durch"));
            expect (hasIntent (through, Goal::beatMasking, 1), "cut-through is its own target");
            expect (! hasIntent (through, Goal::presence, 1), "... not just presence");
            expect (through.constraints.empty(), "the 'nicht' of 'setzt sich nicht durch' is no negation");

            expect (hasIntent (IntentParser::parse (de ("Die Stimme klingt dumpf")), Goal::brightness, 1), "dumpf -> more highs");
            expect (hasIntent (IntentParser::parse (de ("zu viel Hall")), Goal::reverb, -1), "zu viel Hall -> less reverb");
            expect (hasIntent (IntentParser::parse (de ("Die S-Laute zischen")), Goal::sibilance, -1), "zischen -> fewer S sounds");
            expect (hasIntent (IntentParser::parse (de ("Mehr Druck")), Goal::punch, 1), "Druck");
            expect (hasIntent (IntentParser::parse (de ("Mach sie wärmer")), Goal::warmth, 1), "waermer");
        }

        beginTest ("Intensity words scale the proposal");
        {
            Harness h;
            h.set (pid::reverbOn, 1.0f);
            const auto slight = planFor (h, de ("ein wenig mehr Reverb"));
            const auto strong = planFor (h, de ("deutlich mehr Reverb"));
            const auto* s = changeFor (slight, pid::reverbMix);
            const auto* l = changeFor (strong, pid::reverbMix);
            expect (s != nullptr && l != nullptr);
            if (s != nullptr && l != nullptr)
                expect (l->proposed - l->current > s->proposed - s->current, "deutlich > ein wenig");

            const auto notTooMuch = IntentParser::parse (de ("Mehr Reverb, aber nicht zu viel."));
            expect (notTooMuch.intents.size() == 1 && notTooMuch.intents[0].intensity == Intensity::slight, "'nicht zu viel' = slight, not a negation");
            expect (notTooMuch.constraints.empty());
        }

        beginTest ("Negations are limits, never the opposite wish");
        {
            Harness h;
            const auto parsed = IntentParser::parse (de ("Nicht mehr Höhen."));
            expect (! hasIntent (parsed, Goal::brightness, 1), "'nicht mehr Hoehen' does not raise the highs");
            expect (parsed.hasConstraint (Goal::brightness, 1), "... it forbids raising them");
            const auto plan = planFor (h, de ("Nicht mehr Höhen."));
            const auto* high = changeFor (plan, pid::highGain);
            expect (high == nullptr || high->proposed <= high->current, "highs are never raised");
        }

        beginTest ("1: Weniger Tiefen - applied to the real EQ parameters, undo restores");
        {
            Harness h;
            GestureCounter gestures;
            h.proc.addListener (&gestures);

            const auto plan = planFor (h, de ("Weniger Tiefen."));
            expectValid (plan, "Weniger Tiefen");
            const auto* low = changeFor (plan, pid::lowGain);
            const auto* hpf = changeFor (plan, pid::hpfFreq);
            expect (low != nullptr && low->proposed < low->current, "low shelf goes down");
            expect (hpf != nullptr && hpf->proposed > hpf->current, "low cut goes up");
            for (auto& c : plan.changes)
                expect (juce::String (findParameterSpec (c.paramId)->module) == "EQ", "only EQ parameters for a bass wish");

            AssistantSession session (h.proc.getState());
            const auto r = session.apply (plan);
            expect (r.status == AssistantSession::Result::Status::applied);
            for (auto& c : plan.changes)
                expectWithinAbsoluteError (value (h, c.paramId.toRawUTF8()), c.proposed, 0.01f, "applied " + c.paramId);
            expect (gestures.begins >= (int) plan.changes.size() && gestures.ends == gestures.begins, "host sees proper change gestures (automation)");

            // The processed settings follow (= audible when the EQ is on)
            const auto settings = h.proc.readSettings();
            expectWithinAbsoluteError (settings.eq.lowGain, low->proposed, 0.01f, "DSP reads the new low gain");

            const auto u = session.undo();
            expect (u.status == AssistantSession::Result::Status::applied);
            for (auto& c : plan.changes)
                expectWithinAbsoluteError (value (h, c.paramId.toRawUTF8()), c.current, 0.01f, "restored " + c.paramId);
            expect (! session.canUndo());
            h.proc.removeListener (&gestures);
        }

        beginTest ("2: Etwas mehr Reverb - switches the reverb on, the sound changes");
        {
            Harness a, b;
            const auto plan = planFor (b, de ("Etwas mehr Reverb."));
            expectValid (plan, "Etwas mehr Reverb");
            expect (changeFor (plan, pid::reverbOn) != nullptr, "reverb is off by default: explicit switch-on change");
            const auto* mix = changeFor (plan, pid::reverbMix);
            expect (mix != nullptr && mix->proposed <= 20.0f, "a modest amount");

            AssistantSession session (b.proc.getState());
            expect (session.apply (plan).status == AssistantSession::Result::Status::applied);
            expect (value (b, pid::reverbOn) > 0.5f);

            const auto input = a.noise (0.3f, 0.6, 5);
            const auto outA = a.run (input), outB = b.run (input);
            float diff = 0.0f;
            for (int ch = 0; ch < outA.getNumChannels(); ++ch)
                for (int i = 0; i < outA.getNumSamples(); ++i)
                    diff = std::max (diff, std::abs (outA.getSample (ch, i) - outB.getSample (ch, i)));
            expect (diff > 1.0e-3f, "the applied change is audible");
            expect (allFinite (outB));
        }

        beginTest ("3: Praesenter, aber nicht schaerfer");
        {
            Harness h;
            const auto parsed = IntentParser::parse (de ("Mach die Stimme präsenter, aber nicht schärfer."));
            expect (hasIntent (parsed, Goal::presence, 1));
            expect (parsed.hasConstraint (Goal::harshness, 1), "'nicht schaerfer' is a limit");
            const auto plan = AssistantPlanner::plan (parsed, buildContext (h.proc));
            expectValid (plan, "praesenter");
            const auto* mid = changeFor (plan, pid::midGain);
            expect (mid != nullptr && mid->proposed > mid->current, "presence band goes up");
            expect (mid != nullptr && mid->proposed <= 3.0f, "carefully");
            const auto* high = changeFor (plan, pid::highGain);
            expect (high == nullptr || high->proposed <= high->current, "highs are not raised");
        }

        beginTest ("4: Gegen den Beat durchsetzen - without sidechain: general rules, honest notes");
        {
            Harness h;
            const auto plan = planFor (h, de ("Die Vocal soll sich besser gegen den Beat durchsetzen."));
            expectValid (plan, "Beat");
            expect (plan.beatRequested);
            expect (plan.hasChanges(), "still general vocal proposals");
            expect (noteContains (plan, de ("Beat-Sidechain nicht verbunden")), "says the sidechain is missing");
            expect (noteContains (plan, de ("noch nicht implementiert")), "masking analysis is reported as not available");
            for (auto& c : plan.changes)
                expect (! c.measured, "nothing claimed as measured without measurements");
        }

        beginTest ("5: Breiter, aber mono-kompatibel");
        {
            Harness h;
            const auto parsed = IntentParser::parse (de ("Mach die Stimme breiter, aber mono-kompatibel."));
            expect (hasIntent (parsed, Goal::width, 1));
            expect (parsed.hasConstraint (Goal::width, 0), "mono constraint");
            const auto plan = AssistantPlanner::plan (parsed, buildContext (h.proc));
            expectValid (plan, "breiter");
            const auto* safe = changeFor (plan, pid::doublerMonoSafe);
            const auto* below = changeFor (plan, pid::monoBelow);
            expect (changeFor (plan, pid::doublerOn) != nullptr, "doubler creates the width");
            expect (safe != nullptr && safe->proposed >= 75.0f, "doubler mono safe raised");
            expect (below != nullptr && below->proposed >= 150.0f, "low end kept mono");
        }

        beginTest ("6: Weniger Kompression");
        {
            Harness h;
            const auto plan = planFor (h, de ("Weniger Kompression."));
            expectValid (plan, "Kompression");
            const auto* ratio = changeFor (plan, pid::compRatio);
            const auto* thr = changeFor (plan, pid::compThreshold);
            expect (ratio != nullptr && ratio->proposed < ratio->current, "ratio down");
            expect (thr != nullptr && thr->proposed > thr->current, "threshold up");
        }

        beginTest ("7: Mach alles extremer - too vague: a question, no changes");
        {
            Harness h;
            const auto parsed = IntentParser::parse (de ("Mach alles extremer."));
            const auto plan = AssistantPlanner::plan (parsed, buildContext (h.proc));
            expect (! plan.hasChanges(), "nothing is guessed");
            expect (! parsed.ambiguous.empty(), "asks back");
        }

        beginTest ("8: Empty input does nothing");
        {
            Harness h;
            const auto parsed = IntentParser::parse ("   \n ");
            expect (parsed.empty);
            const auto plan = AssistantPlanner::plan (parsed, buildContext (h.proc));
            expect (! plan.hasChanges() && plan.notes.empty());
        }

        beginTest ("9: Unknown input changes no parameter");
        {
            Harness h;
            const auto before = AssistantSession::readValues (h.proc.getState());
            const auto parsed = IntentParser::parse (de ("Bitte koche mir einen Kaffee."));
            const auto plan = AssistantPlanner::plan (parsed, buildContext (h.proc));
            expect (! plan.hasChanges());
            expect (! parsed.unrecognised.empty(), "reported as not understood");
            AssistantSession session (h.proc.getState());
            expect (session.apply (plan).status == AssistantSession::Result::Status::nothingSelected);
            expect (AssistantSession::readValues (h.proc.getState()) == before, "state untouched");
        }

        beginTest ("10: Contradictory input changes nothing for that target");
        {
            Harness h;
            h.set (pid::reverbOn, 1.0f);
            const auto parsed = IntentParser::parse (de ("Mehr Hall, aber weniger Hall."));
            expect (! parsed.conflicts.empty(), "conflict detected");
            const auto plan = AssistantPlanner::plan (parsed, buildContext (h.proc));
            expect (changeFor (plan, pid::reverbMix) == nullptr && changeFor (plan, pid::reverbOn) == nullptr);
            expect (noteContains (plan, de ("Widersprüchlich")));
        }

        beginTest ("Combined wishes: weniger Schaerfe, aber weiterhin klar; Beat + Druck");
        {
            Harness h;
            const auto parsed = IntentParser::parse (de ("Weniger Schärfe, aber die Stimme soll weiterhin klar klingen."));
            expect (hasIntent (parsed, Goal::harshness, -1));
            expect (parsed.hasConstraint (Goal::presence, -1), "clarity is kept");
            const auto plan = AssistantPlanner::plan (parsed, buildContext (h.proc));
            expectValid (plan, "Schaerfe");
            expect (changeFor (plan, pid::smoothOn) != nullptr, "SMOOTH handles the harshness");
            const auto* mid = changeFor (plan, pid::midGain);
            expect (mid == nullptr || mid->proposed >= mid->current, "presence not cut");

            const auto beat = IntentParser::parse (de ("Passe die Vocal an den Beat an und gib ihr mehr Druck."));
            expect (hasIntent (beat, Goal::beatMasking, 1) && hasIntent (beat, Goal::punch, 1));
            const auto beatPlan = AssistantPlanner::plan (beat, buildContext (h.proc));
            expect (changeFor (beatPlan, pid::smashOn) != nullptr, "Druck -> Vocal Smash");
        }

        beginTest ("Only selected changes are applied; values are clamped to their ranges");
        {
            Harness h;
            auto plan = planFor (h, de ("Weniger Tiefen."));
            expect (plan.changes.size() >= 2);
            plan.changes[0].selected = false;
            const auto skipped = plan.changes[0];
            AssistantSession session (h.proc.getState());
            expect (session.apply (plan).status == AssistantSession::Result::Status::applied);
            expectWithinAbsoluteError (value (h, skipped.paramId.toRawUTF8()), skipped.current, 0.01f, "unselected stays");

            // Pushing far beyond the range is clamped
            h.set (pid::lowGain, -17.0f);
            const auto again = planFor (h, de ("deutlich weniger Tiefen"));
            expectValid (again, "clamp");
            if (auto* low = changeFor (again, pid::lowGain))
                expect (low->proposed >= -18.0f);
        }

        beginTest ("Out-of-range or unknown values are rejected as a whole");
        {
            Harness h;
            Plan bad;
            Change ok;
            ok.paramId = pid::reverbMix; ok.current = value (h, pid::reverbMix); ok.proposed = 30.0f; ok.title = "x"; ok.module = "Reverb"; ok.reason = "x";
            Change wrong = ok;
            wrong.paramId = pid::reverbDecay; wrong.current = value (h, pid::reverbDecay); wrong.proposed = 99.0f;
            bad.changes = { ok, wrong };
            const auto before = AssistantSession::readValues (h.proc.getState());
            AssistantSession session (h.proc.getState());
            expect (session.apply (bad).status == AssistantSession::Result::Status::invalid);
            expect (AssistantSession::readValues (h.proc.getState()) == before, "nothing applied - no partial changes");

            Change unknown = ok;
            unknown.paramId = "doesNotExist";
            bad.changes = { unknown };
            expect (session.apply (bad).status == AssistantSession::Result::Status::invalid);
        }

        beginTest ("Edited after analysis: apply refuses (stale); undo keeps later user changes");
        {
            Harness h;
            const auto plan = planFor (h, de ("Weniger Tiefen."));
            h.set (pid::lowGain, 4.0f);   // the user turns the knob in between
            AssistantSession session (h.proc.getState());
            expect (session.apply (plan).status == AssistantSession::Result::Status::stale);
            expectWithinAbsoluteError (value (h, pid::lowGain), 4.0f, 0.01f, "user value kept");

            const auto fresh = planFor (h, de ("Weniger Tiefen."));
            expect (session.apply (fresh).status == AssistantSession::Result::Status::applied);
            h.set (pid::hpfFreq, 150.0f);   // user edits one of the changed values afterwards
            const auto u = session.undo();
            expectWithinAbsoluteError (value (h, pid::hpfFreq), 150.0f, 0.01f, "later user change survives undo");
            expectWithinAbsoluteError (value (h, pid::lowGain), 4.0f, 0.01f, "the rest is restored");
            expect (u.messages.size() >= 1, "and the user is told");
        }

        beginTest ("Presets and state still work after assistant changes");
        {
            Harness h;
            AssistantSession session (h.proc.getState());
            session.apply (planFor (h, de ("Mehr Druck und mehr Hall")));
            auto& pm = h.proc.getPresetManager();
            expect (pm.getEntries().size() > 0);
            pm.loadPreset (0);
            expect (! pm.isCurrentPresetModified(), "preset loads exactly");

            juce::MemoryBlock state;
            h.proc.getStateInformation (state);
            Harness other;
            other.proc.setStateInformation (state.getData(), (int) state.getSize());
            expect (AssistantSession::readValues (other.proc.getState()) == AssistantSession::readValues (h.proc.getState()), "project state round trip");
        }

        beginTest ("Plans for all test inputs are valid");
        {
            Harness h;
            for (auto* t : { "Weniger Tiefen.", "Etwas mehr Reverb.", "Mach die Stimme präsenter, aber nicht schärfer.",
                             "Die Vocal soll sich besser gegen den Beat durchsetzen.", "Mach die Stimme breiter, aber mono-kompatibel.",
                             "Weniger Kompression.", "Mach alles extremer.", "", "Bitte koche mir einen Kaffee.", "Mehr Hall, aber weniger Hall.",
                             "Mach die Vocal breiter, aber achte auf Mono-Kompatibilität.", "Weniger Schärfe, aber die Stimme soll weiterhin klar klingen.",
                             "Passe die Vocal an den Beat an und gib ihr mehr Druck.", "lauter", "weniger S-Laute", "mehr Wärme", "deutlich mehr Druck" })
                expectValid (planFor (h, de (t)), de (t));
        }

        beginTest ("Assistant page: analyse, apply selected, undo, reset, empty input");
        {
            Harness h;
            ui::settings::set (ui::settings::assistantAutoApply, false);   // review first
            ui::AssistantView view (h.proc);
            view.setBounds (0, 0, 1080, 664);
            expect (! view.getAutoApplyButton().getToggleState());
            expect (view.getStatus() == ui::AssistantView::Status::ready);
            expect (! view.getApplyButton().isEnabled(), "apply disabled without a plan");
            expect (! view.getAnalyseButton().isEnabled(), "analyse disabled for an empty input");

            view.requestAnalysis();   // empty: no action
            expect (view.getStatus() == ui::AssistantView::Status::ready);

            view.setText (de ("Weniger Tiefen."));
            view.analyseNow();
            expect (view.getStatus() == ui::AssistantView::Status::proposalsReady);
            expect (view.getStatusText() == de ("Änderungsvorschläge bereit"));
            expect (view.getApplyButton().isEnabled());

            const auto plan = view.getPlan();
            view.setSelected (0, false);
            expect (view.applySelected());
            expect (view.getStatus() == ui::AssistantView::Status::applied);
            expectWithinAbsoluteError (value (h, plan.changes[0].paramId.toRawUTF8()), plan.changes[0].current, 0.01f, "unticked not applied");
            for (size_t i = 1; i < plan.changes.size(); ++i)
                expectWithinAbsoluteError (value (h, plan.changes[i].paramId.toRawUTF8()), plan.changes[i].proposed, 0.01f, "ticked applied");
            expect (! view.getApplyButton().isEnabled(), "cannot apply twice");
            expect (view.getUndoButton().isEnabled());

            expect (view.undoLast());
            for (auto& c : plan.changes)
                expectWithinAbsoluteError (value (h, c.paramId.toRawUTF8()), c.current, 0.01f, "undo restores");

            view.setText (de ("Die Vocal soll sich besser gegen den Beat durchsetzen."));
            view.analyseNow();
            expect (view.getStatusText().contains (de ("Beat-Sidechain nicht verbunden")), "sidechain state in the status");

            view.setText (de ("Bitte koche mir einen Kaffee."));
            view.analyseNow();
            expect (view.getStatus() == ui::AssistantView::Status::noMatch);
            expect (view.getStatusText() == de ("Keine passende Funktion gefunden"));

            const auto before = AssistantSession::readValues (h.proc.getState());
            view.resetPlan();
            expect (view.getStatus() == ui::AssistantView::Status::ready && view.getText().isEmpty());
            expect (AssistantSession::readValues (h.proc.getState()) == before, "reset never touches the plugin state");
        }

        beginTest ("Klangwoerter und Stilprofile: weniger Tiefen, klare Stimme, crisp, im Stil von Ski Aggu oder Tream");
        {
            const auto text = de ("weniger tiefen, klare stimme, crisp, im stil von ski aggu oder tream");
            const auto parsed = IntentParser::parse (text);
            expect (hasIntent (parsed, Goal::bass, -1), "weniger Tiefen");
            expect (hasIntent (parsed, Goal::presence, 1), "klare Stimme");
            expect (hasIntent (parsed, Goal::brightness, 1), "crisp");
            expect (parsed.styles.contains ("Ski Aggu") && parsed.styles.contains ("Tream"), "both styles recognised");
            expect (parsed.unrecognised.empty() && parsed.unknownStyles.isEmpty(), "everything understood");
            // The two styles disagree on reverb -> left out instead of guessed
            expect (std::find (parsed.styleConflicts.begin(), parsed.styleConflicts.end(), Goal::reverb) != parsed.styleConflicts.end());
            for (auto& i : parsed.intents)
            {
                expect (i.goal != Goal::reverb, "no reverb change from conflicting styles");
                if (i.goal == Goal::bass || i.goal == Goal::presence || i.goal == Goal::brightness)
                    expect (i.source.isEmpty(), "typed wishes win over style traits");
            }
            expect (hasIntent (parsed, Goal::compression, 1) && hasIntent (parsed, Goal::punch, 1), "style traits added");

            Harness h;
            const auto plan = AssistantPlanner::plan (parsed, buildContext (h.proc));
            expectValid (plan, "style request");
            expect (noteContains (plan, de ("kein Nachbau")), "style profiles are labelled honestly");
            if (const auto* mid = changeFor (plan, pid::midGain))
                expect (mid->proposed - mid->current >= 2.4f, "a style trait never weakens the typed wish for a clear voice");
            else
                expect (false, "presence change present");
            bool styleTagged = false;
            for (auto& c : plan.changes)
                styleTagged = styleTagged || c.reason.startsWith (de ("Stilprofil"));
            expect (styleTagged, "style-derived changes say where they come from");

            const auto single = IntentParser::parse (de ("Klingt wie Tream"));
            expect (single.styles.size() == 1 && hasIntent (single, Goal::reverb, 1), "one style: its traits");

            const auto unknown = IntentParser::parse (de ("im Stil von Max Mustermann"));
            expect (unknown.unknownStyles.size() == 1 && unknown.intents.empty(), "unknown artist is not guessed");
            const auto unknownPlan = AssistantPlanner::plan (unknown, buildContext (h.proc));
            expect (! unknownPlan.hasChanges() && noteContains (unknownPlan, de ("kenne ich nicht")));
        }

        beginTest ("Sofort anwenden: the analysis applies everything selected right away; undo still works");
        {
            Harness h;
            ui::settings::set (ui::settings::assistantAutoApply, true);
            ui::AssistantView view (h.proc);
            view.setBounds (0, 0, 1080, 664);
            expect (view.getAutoApplyButton().getToggleState());
            const auto before = AssistantSession::readValues (h.proc.getState());

            view.setText (de ("weniger tiefen, klare stimme, crisp, im stil von ski aggu oder tream"));
            view.analyseNow();
            expect (view.getStatus() == ui::AssistantView::Status::applied, "applied immediately");
            const auto& plan = view.getPlan();
            expect (plan.hasChanges());
            for (auto& c : plan.changes)
                expectWithinAbsoluteError (value (h, c.paramId.toRawUTF8()), c.selected ? c.proposed : c.current, 0.01f, "auto-applied " + c.paramId);

            expect (view.undoLast());
            expect (AssistantSession::readValues (h.proc.getState()) == before, "one undo restores everything");

            // Questions only: nothing is applied
            view.setText (de ("Mach alles extremer."));
            view.analyseNow();
            expect (view.getStatus() == ui::AssistantView::Status::question);
            expect (AssistantSession::readValues (h.proc.getState()) == before);
            ui::settings::set (ui::settings::assistantAutoApply, false);
        }

        beginTest ("Assistant tab in the editor; module tabs stay reachable; no parameter changes from switching");
        {
            Harness h;
            std::unique_ptr<juce::AudioProcessorEditor> e (h.proc.createEditor());
            auto& ed = dynamic_cast<VelvoxAudioProcessorEditor&> (*e);
            const auto before = AssistantSession::readValues (h.proc.getState());

            ed.getTopBar().getAssistantButton().triggerClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
            expect (ed.isAssistantOpen() && ed.getAssistantView().isVisible());
            expect (! ed.getBeginnerView().isVisible() && ed.getDetail() == nullptr);

            ed.getChainView().onSelect ((int) dsp::ModuleId::reverb);
            expect (! ed.isAssistantOpen() && ! ed.getAssistantView().isVisible(), "a module tab leaves the assistant");

            ed.setAssistantOpen (true);
            ed.getTopBar().onModeChange (true);
            expect (! ed.isAssistantOpen() && ed.isProMode() && ed.getDetail() != nullptr);
            expect (AssistantSession::readValues (h.proc.getState()) == before, "switching views changes no parameter");

            // Remembered with the project
            ed.setAssistantOpen (true);
            std::unique_ptr<juce::AudioProcessorEditor> again (h.proc.createEditor());
            expect (dynamic_cast<VelvoxAudioProcessorEditor&> (*again).isAssistantOpen());
        }
    }
};

static AssistantTests assistantTests;
