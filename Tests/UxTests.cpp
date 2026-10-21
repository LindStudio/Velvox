// Help, tutorial, module organisation, preset state and control handling.
// Real editors and real mouse events; the preferences live in a scratch file
// (TestMain points the settings store at it).

#include "TestHelpers.h"
#include "PluginEditor.h"
#include "UI/HelpContent.h"
#include "UI/UserSettings.h"
#include "StateKeys.h"

using namespace velvox;
using namespace velvox::test;
using dsp::ModuleId;

namespace
{
    juce::MouseEvent mouseAt (juce::Component& c, juce::Point<float> pos, juce::Point<float> downPos, bool dragged,
                              juce::ModifierKeys mods = {})
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, mods,
                                 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, downPos, now, 1, dragged);
    }

    VelvoxAudioProcessorEditor& asVelvox (juce::AudioProcessorEditor& e)
    {
        return dynamic_cast<VelvoxAudioProcessorEditor&> (e);
    }

    // Moves the mouse in small steps (like a real drag) and returns the change of the value
    double dragKnob (ui::Knob& k, float pixelsUp, juce::ModifierKeys mods)
    {
        auto& s = k.slider;
        const double before = s.getValue();
        const juce::Point<float> start (s.getWidth() * 0.5f, s.getHeight() * 0.4f);
        s.mouseDown (mouseAt (s, start, start, false, mods));
        for (int i = 1; i <= 10; ++i)
            s.mouseDrag (mouseAt (s, start.translated (0.0f, -pixelsUp * (float) i / 10.0f), start, true, mods));
        s.mouseUp (mouseAt (s, start.translated (0.0f, -pixelsUp), start, true, mods));
        return s.getValue() - before;
    }
}

class UxTests final : public juce::UnitTest
{
public:
    UxTests() : juce::UnitTest ("Headliner UX: help, tutorial, organisation", "Headliner") {}

    void runTest() override
    {
        beginTest ("Help covers every module with all seven questions, plus the general topics");
        {
            const auto& headings = ui::help::moduleSectionHeadings();
            for (int id = -1; id < dsp::numModules; ++id)
            {
                const auto key = ui::help::keyForModule (id);
                const auto* page = ui::help::find (key);
                expect (page != nullptr, "help page for " + key);
                if (page == nullptr)
                    continue;

                int last = -1;
                for (const auto& h : headings)
                {
                    const int at = page->body.indexOf ("## " + h + "\n");
                    expect (at > last, key + ": section \"" + h + "\" present and in order");
                    last = at;
                }
                expect (ui::help::shortDescription (id).isNotEmpty());
            }

            for (auto* key : { "start", "chain", "modules", "controls", "modes", "automix", "presets", "compare", "meters", "problems" })
                expect (ui::help::find (key) != nullptr, juce::String ("general page ") + key);

            // Keys are unique
            juce::StringArray keys;
            for (auto& p : ui::help::pages())
            {
                expect (! keys.contains (p.key), "duplicate help key " + p.key);
                keys.add (p.key);
            }
        }

        beginTest ("First start shows the welcome card; skipping is remembered");
        {
            ui::settings::set (ui::settings::tutorialDone, false);
            Harness h;
            h.proc.getAutoMix().dismissPrompt();   // the analysis popup has priority on a new instance
            {
                std::unique_ptr<juce::AudioProcessorEditor> e (h.proc.createEditor());
                auto& tour = asVelvox (*e).getTutorial();
                expect (tour.isActive(), "welcome card on first start");
                expectEquals (tour.getStep(), -1);

                // Every step highlights a real area of the interface
                tour.goTo (0);
                for (int s = 0; s < (int) ui::TutorialOverlay::steps().size(); ++s)
                {
                    tour.goTo (s);
                    expectEquals (tour.getStep(), s);
                }

                tour.finish (ui::TutorialOverlay::Outcome::skipped);
                expect (! tour.isActive());
            }
            expect (ui::settings::getBool (ui::settings::tutorialDone), "skip is saved");

            std::unique_ptr<juce::AudioProcessorEditor> again (h.proc.createEditor());
            expect (! asVelvox (*again).getTutorial().isActive(), "not shown again after it was skipped");

            // ... and it can always be started again from the help
            asVelvox (*again).showHelp ("start");
            expect (asVelvox (*again).getHelp().isOpen());
            asVelvox (*again).startTutorial (false);
            expect (asVelvox (*again).getTutorial().isActive());
            expectEquals (asVelvox (*again).getTutorial().getStep(), 0);
            expect (! asVelvox (*again).getHelp().isOpen(), "help closes when the tour starts");
        }

        beginTest ("Tutorial targets lie inside the window and are not empty");
        {
            Harness h;
            std::unique_ptr<juce::AudioProcessorEditor> e (h.proc.createEditor());
            auto& ed = asVelvox (*e);
            const juce::Rectangle<int> canvas (0, 0, ui::baseWidth, ui::baseHeight);
            for (auto* key : { "chain", "presets", "global", "help", "chainAndModules", "body", "compare", "modes" })
            {
                // tutorialTarget is private; the overlay resolves it through targetFor
                const auto r = ed.getTutorial().targetFor (key);
                expect (! r.isEmpty() && canvas.contains (r), juce::String ("target ") + key);
            }
        }

        beginTest ("Remind me later: not saved, not shown again in this session");
        {
            ui::settings::set (ui::settings::tutorialDone, false);
            Harness h;
            h.proc.getAutoMix().dismissPrompt();
            {
                std::unique_ptr<juce::AudioProcessorEditor> e (h.proc.createEditor());
                auto& tour = asVelvox (*e).getTutorial();
                expect (tour.isActive());
                tour.finish (ui::TutorialOverlay::Outcome::later);
            }
            expect (! ui::settings::getBool (ui::settings::tutorialDone), "later is not 'done'");
            std::unique_ptr<juce::AudioProcessorEditor> e2 (h.proc.createEditor());
            expect (! asVelvox (*e2).getTutorial().isActive(), "postponed for the rest of the session");
            ui::settings::set (ui::settings::tutorialDone, true);
        }

        beginTest ("Help buttons open the right page");
        {
            Harness h;
            h.proc.getState().state.setProperty ("uiMode", "pro", nullptr);
            std::unique_ptr<juce::AudioProcessorEditor> e (h.proc.createEditor());
            auto& ed = asVelvox (*e);

            // Every module in the chain (a fresh chain has only a few)
            for (int id = 0; id < dsp::numModules; ++id)
                if (! h.proc.getRouting().contains ((ModuleId) id))
                {
                    auto r = h.proc.getRouting();
                    r.insertDefault ((ModuleId) id, 0);
                    h.proc.setRouting (r);
                }
            for (int id = -1; id < dsp::numModules; ++id)
            {
                ed.selectModule (id);
                expect (ed.getDetail() != nullptr);
                ed.getDetail()->getHelpButton().triggerClick();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
                expect (ed.getHelp().isOpen());
                expectEquals (ed.getHelp().getCurrentKey(), ui::help::keyForModule (id));
                ed.getHelp().close();
            }

            // The top bar button opens the page of the open module in Pro, the start page in Beginner
            ed.selectModule ((int) ModuleId::ride);
            ed.getTopBar().getHelpButton().triggerClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
            expectEquals (ed.getHelp().getCurrentKey(), juce::String ("ride"));
            ed.getHelp().close();

            ed.getTopBar().onModeChange (false);
            ed.getTopBar().getHelpButton().triggerClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
            expectEquals (ed.getHelp().getCurrentKey(), juce::String ("start"));
        }

        beginTest ("Auto Mix view: clicking a module card opens it with all its controls");
        {
            Harness h;
            h.proc.getState().state.setProperty ("uiMode", "beginner", nullptr);
            std::unique_ptr<juce::AudioProcessorEditor> e (h.proc.createEditor());
            auto& ed = asVelvox (*e);
            const auto before = h.proc.readSettings();
            expect (! ed.isProMode());

            ed.getChainView().onSelect ((int) ModuleId::deess);
            expect (ed.isProMode());
            expect (ed.getDetail() != nullptr);
            expectEquals (ed.getChainView().getSelected(), (int) ModuleId::deess);

            // Switching views never changes a parameter
            const auto after = h.proc.readSettings();
            expect (after.smash.enabled == before.smash.enabled);
            expectEquals (after.comp.ratio, before.comp.ratio);
        }

        beginTest ("Chain menu: usual order and personal default chains (with repeated instances)");
        {
            Harness h;
            ui::ChainView view (h.proc);
            view.setBounds (0, 0, 1408, ui::ChainView::preferredHeight);

            auto r = dsp::Routing::fromString ("v2,deess,eq#2,comp,eq,reverb:insert,delay:send");
            h.proc.setRouting (r);
            view.saveLayoutAsDefault();

            view.restoreDefaultChain();
            expectEquals (h.proc.getRouting().toString(), juce::String ("v2,eq#2,eq,comp,deess,delay:send,reverb:insert"),
                          "sorted into the usual order, instances of a module together, modes kept");
            expect (view.undo(), "sorting can be undone");
            expect (h.proc.getRouting() == r);

            Harness other;
            ui::ChainView otherView (other.proc);
            expect (otherView.loadDefaultLayout());
            expect (other.proc.getRouting() == r, "personal default chain applied: " + other.proc.getRouting().toString());
        }

        beginTest ("Preset field knows when a preset was edited");
        {
            Harness h;
            auto& pm = h.proc.getPresetManager();
            int idx = -1;
            for (int i = 0; i < pm.getEntries().size(); ++i)
                if (pm.getEntries()[i].name == "Hard Dance Lead")
                    idx = i;
            expect (idx >= 0);

            expect (! pm.isCurrentPresetModified(), "nothing loaded = nothing edited");
            pm.loadPreset (idx);
            expect (! pm.isCurrentPresetModified(), "freshly loaded");

            h.set (pid::compRatio, h.get (pid::compRatio) + 1.0f);
            expect (pm.isCurrentPresetModified(), "a value changed");

            pm.loadPreset (idx);
            expect (! pm.isCurrentPresetModified(), "reloaded");

            auto r = h.proc.getRouting();
            r.moveTo (ModuleId::deess, 0, 0);
            h.proc.setRouting (r);
            expect (pm.isCurrentPresetModified(), "module order changed");

            pm.loadPreset (idx);
            h.set (pid::inputGain, 6.0f);   // not part of a preset
            h.set (pid::bypass, 1.0f);
            expect (! pm.isCurrentPresetModified(), "input gain and bypass do not count as edits");
        }

        beginTest ("Knobs: Shift drags finely, double-click returns to the default");
        {
            Harness h;
            ui::Knob knob (h.proc.getState(), pid::compThreshold, "Threshold");
            knob.setBounds (0, 0, 100, 120);

            const double normal = dragKnob (knob, 40.0f, {});
            knob.slider.setValue (-30.0, juce::sendNotificationSync);
            const double fine = dragKnob (knob, 40.0f, juce::ModifierKeys::shiftModifier);
            expectGreaterThan (normal, 0.0);
            expectGreaterThan (fine, 0.0);
            expectWithinAbsoluteError (fine * ui::FineSlider::fineFactor, normal, normal * 0.15, "Shift is 8 times finer");

            // Gesture reached the parameter (automation-safe)
            expectWithinAbsoluteError ((double) h.get (pid::compThreshold), knob.slider.getValue(), 1.0e-3);

            expect (knob.slider.getDoubleClickReturnValue() == (double) findParameterSpec (pid::compThreshold)->defaultValue,
                    "double-click returns to the real default");
        }
    }
};

static UxTests uxTests;
