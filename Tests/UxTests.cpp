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
    UxTests() : juce::UnitTest ("Velvox UX: help, tutorial, organisation", "Velvox") {}

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

            for (auto* key : { "start", "chain", "modules", "controls", "modes", "presets", "compare", "meters", "problems" })
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

            ed.getChainView().onSelect ((int) ModuleId::smash);
            expect (ed.isProMode());
            expect (ed.getDetail() != nullptr);
            expectEquals (ed.getChainView().getSelected(), (int) ModuleId::smash);

            // Switching views never changes a parameter
            const auto after = h.proc.readSettings();
            expect (after.smash.enabled == before.smash.enabled);
            expectEquals (after.comp.ratio, before.comp.ratio);
        }

        beginTest ("Hiding modules is view only: same routing, same sound, saved with the project");
        {
            Harness a, b;
            for (auto* hh : { &a, &b })
            {
                hh->set (pid::satOn, 1.0f);
                hh->set (pid::satDrive, 12.0f);
            }

            ui::ChainView view (b.proc);
            view.setBounds (0, 0, 1408, ui::ChainView::preferredHeight);
            const auto routingBefore = b.proc.getRouting();
            view.setHidden (ModuleId::sat, true);      // on and processing
            view.setHidden (ModuleId::pitch, true);    // off
            expect (view.isHidden (ModuleId::sat));
            expectEquals (view.getNumHidden(), 2);
            expect (view.getTileBounds (ModuleId::sat).isEmpty(), "hidden tile is not drawn");
            expect (b.proc.getRouting() == routingBefore, "hiding never changes the order");

            const auto input = a.noise (0.3f, 0.5, 9);
            const auto outA = a.run (input);
            const auto outB = b.run (input);
            float maxDiff = 0.0f;
            for (int ch = 0; ch < outA.getNumChannels(); ++ch)
                for (int i = 0; i < outA.getNumSamples(); ++i)
                    maxDiff = std::max (maxDiff, std::abs (outA.getSample (ch, i) - outB.getSample (ch, i)));
            expectEquals (maxDiff, 0.0f, "identical output with hidden modules");

            juce::MemoryBlock state;
            b.proc.getStateInformation (state);
            Harness c;
            c.proc.setStateInformation (state.getData(), (int) state.getSize());
            ui::ChainView restored (c.proc);
            expect (restored.isHidden (ModuleId::sat) && restored.isHidden (ModuleId::pitch), "hidden modules restored with the project");
            expect (! restored.isHidden (ModuleId::eq));

            view.showAllModules();
            expectEquals (view.getNumHidden(), 0);
        }

        beginTest ("Drag and drop with hidden modules lands where the marker shows");
        {
            Harness h;
            ui::ChainView view (h.proc);
            view.setBounds (0, 0, 1408, ui::ChainView::preferredHeight);
            view.setHidden (ModuleId::ride, true);   // between EQ and Comp by default

            const auto sat = view.getTileBounds (ModuleId::sat);
            const auto comp = view.getTileBounds (ModuleId::comp);
            const juce::Point<float> from = sat.getCentre(), to { comp.getX() + 4.0f, comp.getCentreY() };
            view.mouseDown (mouseAt (view, from, from, false));
            view.mouseDrag (mouseAt (view, (from + to) * 0.5f, from, true));
            view.mouseDrag (mouseAt (view, to, from, true));
            view.mouseUp (mouseAt (view, to, from, true));

            const auto r = h.proc.getRouting();
            expectEquals (r.positionOf (ModuleId::sat) + 1, r.positionOf (ModuleId::comp), "saturation directly before the compressor");
            expect (r.positionOf (ModuleId::eq) < r.positionOf (ModuleId::sat));
            expect (r.isValidPermutation());
        }

        beginTest ("Default order and personal layouts");
        {
            Harness h;
            ui::ChainView view (h.proc);
            view.setBounds (0, 0, 1408, ui::ChainView::preferredHeight);

            auto r = h.proc.getRouting();
            r.move (ModuleId::reverb, 0);
            r.reverbInsert = true;
            r.move (ModuleId::sat, 1);
            h.proc.setRouting (r);
            view.setHidden (ModuleId::chop, true);
            view.saveLayoutAsDefault();

            view.restoreDefaultOrder();
            expect (h.proc.getRouting().order == dsp::Routing::defaults().order, "factory order back");
            expect (h.proc.getRouting().reverbInsert, "send / insert mode kept");

            Harness other;
            ui::ChainView otherView (other.proc);
            expect (otherView.loadDefaultLayout());
            expect (other.proc.getRouting() == r, "personal layout applied");
            expect (otherView.isHidden (ModuleId::chop));
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
            r.move (ModuleId::sat, 0);
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
