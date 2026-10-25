// Design system of Headliner (0.5 identity, 0.6 shell): tokens, module identity (colour + neon icon),
// controls, layout of the views.

#include <set>
#include "TestHelpers.h"
#include "UI/Theme.h"
#include "PluginEditor.h"
#include "UI/Segments.h"
#include "UI/ChainView.h"
#include "UI/SimpleView.h"
#include "UI/HelpContent.h"
#include "UI/TutorialOverlay.h"

using namespace velvox;
using namespace velvox::test;
using dsp::ModuleId;

namespace
{
    // Every parameter-bound control of a panel: knobs, segment groups, switches
    struct BoundControls
    {
        juce::StringArray knobs, segments, toggles, choices;
        std::vector<juce::Colour> knobAccents;
    };

    void collect (juce::Component& c, BoundControls& out)
    {
        for (auto* child : c.getChildren())
        {
            if (auto* k = dynamic_cast<ui::Knob*> (child))
            {
                if (k->getParamId().isNotEmpty())   // unbound knobs (linked controls like Smooth's Speed) have none
                    out.knobs.add (k->getParamId());
                out.knobAccents.push_back (k->slider.findColour (juce::Slider::rotarySliderFillColourId));
            }
            else if (auto* s = dynamic_cast<ui::Segments*> (child))
                out.segments.add (s->getParamId());
            else if (auto* t = dynamic_cast<ui::Toggle*> (child))
                out.toggles.add (t->getParamId());
            else if (auto* ch = dynamic_cast<ui::Choice*> (child))
                out.choices.add (ch->getParamId());
            collect (*child, out);
        }
    }

    // Module panel size in the editor (Advanced mode)
    juce::Rectangle<int> panelSize() { return VelvoxAudioProcessorEditor::panelArea().withZeroOrigin(); }
}

class DesignTests final : public juce::UnitTest
{
public:
    DesignTests() : juce::UnitTest ("Headliner design", "Headliner") {}

    void runTest() override
    {
        beginTest ("Design: every module has its colour and a neon icon");
        {
            std::set<juce::uint32> seen;
            for (int i = 0; i < dsp::numModules; ++i)
            {
                const auto id = (ModuleId) i;
                const auto svg = ui::moduleIconSvg (id);
                expect (svg.isNotEmpty(), "icon for " + juce::String (dsp::moduleKey (id)));
                const auto utf8 = svg.toUTF8();
                expect (juce::Drawable::createFromImageData (utf8.getAddress(), utf8.sizeInBytes() - 1) != nullptr,
                        "icon parses: " + juce::String (dsp::moduleKey (id)));
                seen.insert (ui::moduleColour (id).getARGB());
            }
            expectEquals ((int) seen.size(), dsp::numModules, "every module has its own colour");
            expect (ui::moduleColour (ModuleId::ride) == juce::Colour (0xff00d1ff), "LEVEL RIDE #00D1FF");
            expect (ui::moduleColour (ModuleId::smooth) == juce::Colour (0xff22b8ff), "SMOOTH #22B8FF");
            expect (ui::moduleColour (ModuleId::smash) == juce::Colour (0xff3b82f6), "VOCAL SMASH #3B82F6");
            expect (ui::moduleColour (ModuleId::choir) == juce::Colour (0xff6366ff), "CHOIR #6366FF");
            expect (ui::moduleColour (ModuleId::pulse) == juce::Colour (0xff8b5cf6), "PULSE #8B5CF6");
            expect (ui::moduleColour (ModuleId::chop) == juce::Colour (0xffa855f7), "STUTTER #A855F7");
            expect (ui::moduleColour (ModuleId::delay) == juce::Colour (0xffd946ef), "DELAY #D946EF");
            expect (ui::moduleColour (ModuleId::reverb) == juce::Colour (0xfff472b6), "REVERB #F472B6");
            expect (ui::colours::windowBg == juce::Colour (0xff05060a), "window #05060A");
            expect (ui::monoFont (11.5f).getTypefaceName().containsIgnoreCase ("Roboto Mono"), "values in Roboto Mono");
        }

        beginTest ("Design: segments follow the parameter both ways");
        {
            Harness h;
            auto& state = h.proc.getState();
            const juce::StringArray labels { "1/4", "1/4D", "1/8", "1/8D", "1/8T", "1/16" };
            ui::Segments seg (state, pid::delayDivision, labels, ui::moduleColour (ModuleId::delay));
            seg.setBounds (0, 0, 360, 28);
            auto* choice = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (pid::delayDivision));
            expect (choice != nullptr);
            if (choice != nullptr)
            {
                for (int i = 0; i < labels.size(); ++i)
                    expectEquals (seg.getChoiceForSegment (i), choice->choices.indexOf (labels[i]), "label maps by name: " + labels[i]);

                const int dotted8 = choice->choices.indexOf ("1/8D");
                choice->setValueNotifyingHost (choice->convertTo0to1 ((float) dotted8));
                juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
                expectEquals (seg.getSelectedSegment(), 3, "host change shows in the segments");

                seg.clickSegment (0);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
                expectEquals (choice->getIndex(), choice->choices.indexOf ("1/4"), "click sets the parameter");

                // A value without a segment (1/64) selects none
                choice->setValueNotifyingHost (choice->convertTo0to1 (0.0f));
                juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
                expectEquals (seg.getSelectedSegment(), -1, "no segment for 1/64");
            }

            // Bool parameter: two segments (off / on)
            ui::Segments onOff (state, pid::delaySync, { "Free", "Sync" }, ui::moduleColour (ModuleId::delay));
            onOff.clickSegment (1);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
            expect (state.getRawParameterValue (pid::delaySync)->load() > 0.5f, "bool segment on");
        }

        beginTest ("Design: knob is dial, value, name from top to bottom; double-click returns the default");
        {
            Harness h;
            auto& state = h.proc.getState();
            ui::Knob k (state, pid::compRatio, "Ratio", ui::moduleColour (ModuleId::comp));
            k.setDialSize (50);
            k.setBounds (0, 0, 90, 84);   // exactly dial 50 + gap 4 + value 16 + name 14
            expectEquals (k.getDialArea().getHeight(), 50, "dial 50 px");
            expect (k.getDialArea().getY() <= 4, "dial on top");
            expect (k.getValueArea().getY() >= k.getDialArea().getBottom(), "value below the dial");
            expect (k.getNameArea().getY() > k.getValueArea().getY(), "name below the value");
            expect (k.slider.findColour (juce::Slider::rotarySliderFillColourId) == ui::moduleColour (ModuleId::comp), "accent = module colour");
            expect (k.slider.isDoubleClickReturnEnabled());
            expectWithinAbsoluteError ((float) k.slider.getDoubleClickReturnValue(), 3.0f, 1.0e-4f, "double-click value = default 3:1");
        }

        // 0.6 shell (spec 2.1): the 1384 x 1040 canvas, the CHAIN tab and the A/B header of 0.5 are gone
        beginTest ("Design: 1000 x 600 canvas, header with SIMPEL | ADVANCED and the tabs AUTO MIX | VOCAL CHECK");
        {
            expectEquals (ui::baseWidth, 1000);
            expectEquals (ui::baseHeight, 600);

            Harness h;
            h.proc.getAutoMix().dismissPrompt();
            std::unique_ptr<juce::AudioProcessorEditor> e (h.proc.createEditor());
            auto& ed = dynamic_cast<VelvoxAudioProcessorEditor&> (*e);
            auto& top = ed.getTopBar();
            expectEquals (top.getHeight(), 44, "header 44");

            top.selectMode (ui::TopBar::Mode::simple);
            expect (ed.getSimpleView().isVisible() && ! ed.getChainView().isVisible(), "Simple mode");
            expect (ed.getDetail() == nullptr, "no module panel in Simple mode");

            top.selectTab (ui::TopBar::Tab::vocalCheck);
            expect (ed.getCheckView().isVisible(), "VOCAL CHECK shows the check view");
            expect (! ed.getSimpleView().isVisible());
            expectEquals (h.proc.getState().state.getProperty ("uiTab").toString(), juce::String ("check"), "tab stored with the project");

            top.selectTab (ui::TopBar::Tab::autoMix);
            top.selectMode (ui::TopBar::Mode::advanced);
            expect (ed.isAdvanced() && ed.getChainView().isVisible(), "Advanced: chain");
            expect (ed.getDetail() != nullptr, "module panel in Advanced");
            expect (ed.getDetail()->getY() > ed.getChainView().getBottom(), "panel below the chain");
            expectEquals (h.proc.getState().state.getProperty ("uiMode").toString(), juce::String ("pro"), "mode stored with the project");
        }

        beginTest ("Design: a chain tile shows its name, then its icon, then its on/off switch - and no knobs");
        {
            Harness h;
            ui::ChainView view (h.proc);
            view.setBounds (0, 0, 886, ui::ChainView::preferredHeight);
            for (const auto& s : h.proc.getRouting())
            {
                const auto tile = view.getTileBounds (s.id, s.instance);
                const auto name = view.getNameBounds (s.id, s.instance), icon = view.getIconBounds (s.id, s.instance),
                           power = view.getPowerBounds (s.id, s.instance);
                const auto key = juce::String (dsp::moduleKey (s.id));
                expect (tile.contains (name) && tile.contains (icon) && tile.contains (power), key + ": inside the tile");
                expect (name.getBottom() <= icon.getY() + 1.0f && icon.getBottom() <= power.getY() + 1.0f, key + ": name, icon, on/off from top to bottom");
            }
            int knobs = 0;
            for (auto* c : view.getChildren())
                knobs += dynamic_cast<ui::Knob*> (c) != nullptr ? 1 : 0;
            expectEquals (knobs, 0, "no knobs in the chain");
            expectEquals (ui::ChainView::preferredHeight, 88, "slim row");
        }

        beginTest ("Design: tile menu moves, bypassed tile stays switchable");
        {
            Harness h;
            ui::ChainView view (h.proc);
            view.setBounds (0, 0, 886, ui::ChainView::preferredHeight);
            view.refresh();
            const auto r0 = h.proc.getRouting();
            const int eqPos = r0.indexOf (ModuleId::eq, 0);
            expect (view.performTileAction ((int) ModuleId::eq, ui::ChainView::TileAction::moveRight));
            expectEquals (h.proc.getRouting().indexOf (ModuleId::eq, 0), eqPos + 1, "moves EQ one step later");
            expect (view.undo());
            expectEquals (h.proc.getRouting().indexOf (ModuleId::eq, 0), eqPos, "undo restores the order");
            expect (! view.performTileAction ((int) ModuleId::eq, ui::ChainView::TileAction::moveLeft), "first tile cannot move left");

            juce::StringArray items;
            const auto delayMenu = view.buildTileMenu ((int) ModuleId::delay);
            for (juce::PopupMenu::MenuItemIterator it (delayMenu); it.next();)
                items.add (it.getItem().text);
            const auto all = items.joinIntoString ("|");
            for (auto* t : { "Einschalten", "Duplizieren", "Entfernen", "Nach links", "Nach rechts", "Zurücksetzen", "Als Send", "Als Insert",
                             "Rückgängig", "Wiederholen" })
                expect (all.contains (juce::String::fromUTF8 (t)), juce::String::fromUTF8 (t) + " in the menu: " + all);
            juce::StringArray compItems;
            const auto compMenu = view.buildTileMenu ((int) ModuleId::comp);
            for (juce::PopupMenu::MenuItemIterator it (compMenu); it.next();)
                compItems.add (it.getItem().text);
            expect (! compItems.joinIntoString ("|").contains ("Als Send"), "no send option for the compressor");

            // Bypass: the power switch turns it back on
            h.set (pid::compOn, 0.0f);
            view.refresh();
            const auto power = view.getPowerBounds (ModuleId::comp, 0).getCentre();
            view.mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), power, {}, 1.0f, 0, 0, 0, 0, &view, &view,
                                              juce::Time::getCurrentTime(), power, juce::Time::getCurrentTime(), 1, false));
            expect (h.proc.getState().getRawParameterValue (pid::compOn)->load() > 0.5f, "power turns it back on");

            // Reset: every value of the instance back to its default
            h.set (pid::compRatio, 9.0f);
            expect (view.performTileAction ((int) ModuleId::comp, ui::ChainView::TileAction::reset));
            expectWithinAbsoluteError (h.get (pid::compRatio), 3.0f, 1.0e-3f, "reset");
            expect (h.get (pid::compOn) > 0.5f, "reset keeps it on");
        }

        beginTest ("Design: 14 tiles scroll instead of shrinking below 84 px");
        {
            Harness h;
            ui::ChainView view (h.proc);
            view.setBounds (0, 0, 886, ui::ChainView::preferredHeight);
            for (auto id : { ModuleId::ride, ModuleId::smash, ModuleId::smooth, ModuleId::sat, ModuleId::doubler, ModuleId::choir,
                             ModuleId::chop, ModuleId::pulse, ModuleId::check, ModuleId::pitch, ModuleId::eq, ModuleId::comp })
                view.insertModule (id);
            for (const auto& s : h.proc.getRouting())
                expect (view.getTileBounds (s.id, s.instance).getWidth() >= 84.0f - 0.01f, "tile width >= 84");
            expect (view.getMaxScroll() > 0.0f, "the chain scrolls");
        }

        beginTest ("Design: every panel binds only existing parameters, in its module colour");
        {
            Harness h;
            auto r = h.proc.getRouting();
            for (int id = 0; id < dsp::numModules; ++id)
                if (! r.contains ((ModuleId) id))
                    r.insertDefault ((ModuleId) id, 0);
            h.proc.setRouting (r);

            std::map<int, BoundControls> found;
            for (int id = -1; id < dsp::numModules; ++id)
            {
                auto panel = ui::createDetailPanel (h.proc, id);
                panel->setBounds (panelSize());
                auto& c = found[id];
                collect (*panel, c);
                juce::StringArray all (c.knobs);
                all.addArray (c.segments);
                all.addArray (c.toggles);
                for (auto& pid : all)
                    expect (h.proc.getState().getParameter (pid) != nullptr, "exists: " + pid);
                if (id >= 0)
                    for (auto& accent : c.knobAccents)
                        expect (accent == ui::moduleColour ((ModuleId) id), "knob accent = module colour (" + juce::String (dsp::moduleKey ((ModuleId) id)) + ")");
            }

            const auto& ride = found[(int) ModuleId::ride];
            for (auto* pid : { "rideTarget", "rideMaxBoost", "rideMaxCut", "rideSilence", "rideResponse" })
                expect (ride.knobs.contains (pid), juce::String ("LEVEL RIDE has ") + pid);
            expect (found[(int) ModuleId::delay].segments.contains ("delayDivision"), "DELAY time as segments");
            expect (found[(int) ModuleId::delay].toggles.contains ("delaySync") && found[(int) ModuleId::delay].toggles.contains ("delayPingPong"), "DELAY switches");
            expect (found[(int) ModuleId::reverb].choices.contains ("reverbType"), "REVERB room list (dropdown, 0.6)");
            expect (found[(int) ModuleId::pulse].segments.contains ("pulseRate"), "PULSE rate as segments");
            expect (found[(int) ModuleId::choir].segments.contains ("choirVoices"), "CHOIR voices as segments");
            expect (found[(int) ModuleId::smooth].segments.contains ("smoothMode") && found[(int) ModuleId::smooth].toggles.contains ("smoothDelta"), "SMOOTH character + delta");
            expect (found[(int) ModuleId::chop].segments.contains ("chopRate"), "STUTTER rate as segments");
            expect (found[(int) ModuleId::smash].knobs.contains ("smashAmount"), "VOCAL SMASH big knob");

            // EQ (0.6): the bands live in the graph and the selected-band row ("Headliner EQ panel"); with no band only Mix
            // (Mehr); the hidden 0.5 fixed bands are never shown
            expect (found[(int) ModuleId::eq].knobs.contains ("eqMix"), "EQ Mix");
            for (auto* legacy : { "hpfSlope", "lowType", "highType", "hpfOn", "lpfOn", "hpfFreq", "midGain" })
                expect (! found[(int) ModuleId::eq].segments.contains (legacy) && ! found[(int) ModuleId::eq].toggles.contains (legacy)
                        && ! found[(int) ModuleId::eq].knobs.contains (legacy), juce::String ("EQ hides ") + legacy);
            expect (found[(int) ModuleId::comp].segments.contains ("compDetector"), "COMPRESSOR detector as segments");
            expect (found[(int) ModuleId::deess].segments.contains ("deessMode") && found[(int) ModuleId::deess].toggles.contains ("deessListen"), "DE-ESSER mode + listen");
            expect (found[(int) ModuleId::sat].segments.contains ("satCharacter"), "SATURATION character as segments");
            expect (found[(int) ModuleId::pitch].toggles.contains ("pitchPreserveFormants"), "PITCH keep formants switch");
            expect (found[-1].segments.contains ("levelMatchMode") && found[-1].segments.contains ("levelMatchWindow"), "OUTPUT level match as segments");
            expect (found[-1].knobs.contains ("inputGain"), "OUTPUT input gain");
            for (auto* pid : { "compThreshold", "compRatio", "compAttack", "compRelease", "compKnee", "compMakeup", "compMix", "compScHpf" })
                expect (found[(int) ModuleId::comp].knobs.contains (pid), juce::String ("COMPRESSOR keeps ") + pid);
            for (auto* pid : { "doublerWidth", "doublerDelay", "doublerDetune", "doublerMonoSafe", "doublerTone", "doublerMix" })
                expect (found[(int) ModuleId::doubler].knobs.contains (pid), juce::String ("DOUBLER keeps ") + pid);
            for (int id = 0; id < dsp::numModules; ++id)
                expect (found[id].segments.size() + found[id].knobs.size() + found[id].toggles.size() > 0 || id == (int) ModuleId::check,
                        juce::String ("controls in ") + dsp::moduleKey ((ModuleId) id));

            // Nothing the old panels offered gets lost
            for (auto* pid : { "delayDuckAmount", "delayDuckThreshold", "delayDuckAttack", "delayDuckRelease", "delayToReverb", "delayDrive", "delayDiffusion" })
                expect (found[(int) ModuleId::delay].knobs.contains (pid), juce::String ("DELAY keeps ") + pid);
            for (auto* pid : { "reverbEarly", "reverbDiffusion", "reverbBass", "reverbHighCut", "reverbDuckAmount" })
                expect (found[(int) ModuleId::reverb].knobs.contains (pid), juce::String ("REVERB keeps ") + pid);
        }

        // 0.6 (spec 2.2 / 2.3): the Vocal Control bar is gone; its six macros are the Simple-mode faders
        beginTest ("Design: Simple mode faders PRESENCE SPACE WIDTH PUNCH WARMTH VOCAL LEVEL, in the macro colours");
        {
            Harness h;
            ui::SimpleView view (h.proc, nullptr);
            view.setBounds (VelvoxAudioProcessorEditor::bodyArea().withZeroOrigin());
            juce::StringArray ids;
            for (auto& s : view.getMacroSliders())
                ids.add (s->getParamId());
            expect (ids == juce::StringArray { "macroPresence", "macroSpace", "macroWidth", "macroAggression", "macroWarmth", "outputGain" },
                    "order: " + ids.joinIntoString (","));
            const juce::uint32 expected[] { 0xff00d1ff, 0xff3b82f6, 0xff6366ff, 0xff8b5cf6, 0xffd946ef, 0xfff472b6 };
            for (size_t i = 0; i < view.getMacroSliders().size() && i < 6; ++i)
                expect (view.getMacroSliders()[i]->getAccent() == juce::Colour (expected[i]), "macro colour " + juce::String ((int) i));
            expectEquals (view.getMacroSliders()[3]->getLabel(), juce::String ("Punch"));
            expectEquals (view.getMacroSliders()[5]->getLabel(), juce::String ("Vocal Level"));

            // Spectrum left with the analyse button under it, faders right, meters at the bottom
            expect (view.getSpectrumArea().getRight() < view.getMacroArea().getX(), "spectrum left of the faders");
            expect (view.getAnalyseButton().getY() > view.getSpectrumArea().getY() + view.getSpectrumArea().getHeight() / 2, "button under the spectrum");
            expect (view.getMeterArea().getY() >= view.getMacroArea().getBottom(), "meters at the bottom");
            expectEquals (view.getAnalyseButton().getButtonText(), juce::String ("Vocal analysieren"));
            expect (! view.getBeforeButton().isVisible(), "Vorher / Nachher only after an analysis");
        }

        beginTest ("Design: auto-mix details keep Density and the fair compare; VOCAL CHECK uses the whole body");
        {
            Harness h;
            h.proc.getAutoMix().dismissPrompt();
            std::unique_ptr<juce::AudioProcessorEditor> e (h.proc.createEditor());
            auto& ed = dynamic_cast<VelvoxAudioProcessorEditor&> (*e);
            ed.setMode (VelvoxAudioProcessorEditor::Mode::simple);
            ed.getSimpleView().getDetailsButton().triggerClick();
            for (int i = 0; i < 100 && ! ed.isMixDetailsOpen(); ++i)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
            expect (ed.isMixDetailsOpen(), "Details opens the report");
            auto& view = ed.getAutoMixView();

            BoundControls c;
            collect (view, c);
            expect (c.knobs == juce::StringArray { "macroDensity" }, "only Density here: " + c.knobs.joinIntoString (","));
            expect (c.segments.contains ("levelMatchMode"), "fair compare as segments");

            ed.getTopBar().selectTab (ui::TopBar::Tab::vocalCheck);
            expect (ed.getCheckView().isVisible() && ed.getCheckView().getWidth() >= 980, "VOCAL CHECK uses the whole width");
        }

        beginTest ("Design: help and tutorial describe the new interface");
        {
            const juce::StringArray outdated { "lower lane", "Analysieren starts", "Auto Mix and Module switch", "Module button",
                                               "Hide from chain view", "six fine-tuning controls", "right rail", "\"Audio analysieren\"",
                                               "Ins / Send switch", "Modules button", "VOCAL CONTROL", "A/B", "CHAIN tab", "Copy A to B" };
            for (auto& step : ui::TutorialOverlay::steps())
                for (auto& phrase : outdated)
                    expect (! step.text.contains (phrase), "tutorial '" + step.title + "' mentions: " + phrase);
            for (auto& page : ui::help::pages())
                for (auto& phrase : outdated)
                    expect (! page.body.contains (phrase), "help '" + page.key + "' mentions: " + phrase);

            const auto* start = ui::help::find ("start");
            expect (start != nullptr && start->body.contains ("SIMPEL") && start->body.contains ("ADVANCED")
                    && start->body.contains ("VOCAL CHECK"), "start page explains the modes and the tabs");
            bool modesStep = false;
            for (auto& step : ui::TutorialOverlay::steps())
                modesStep = modesStep || (step.target == "modes" && step.text.contains ("SIMPEL") && step.text.contains ("ADVANCED"));
            expect (modesStep, "tutorial step for SIMPEL / ADVANCED");
            const auto* controls = ui::help::find ("controls");
            expect (controls != nullptr && controls->body.contains ("Alt") && controls->body.contains ("Shift"), "gestures explained in the help");
        }

        // ---- Findings of the 0.5 review that still apply
        beginTest ("Review: every delay division and pulse rate stays reachable (overflow segment)");
        {
            Harness h;
            auto& state = h.proc.getState();
            ui::Segments seg (state, pid::delayDivision, { "1/4", "1/4D", "1/8", "1/8D", "1/8T", "1/16" }, ui::moduleColour (ModuleId::delay), true);
            seg.setBounds (0, 0, 420, 28);
            auto* choice = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (pid::delayDivision));
            expect (choice != nullptr);
            if (choice != nullptr)
            {
                choice->setValueNotifyingHost (choice->convertTo0to1 ((float) choice->choices.indexOf ("1/64")));
                juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
                expectEquals (seg.getSelectedSegment(), seg.getOverflowSegment(), "a value without its own segment lights the overflow segment");
                expectEquals (seg.getOverflowText(), juce::String ("1/64"), "and shows its name");
                seg.chooseFromOverflow (choice->choices.indexOf ("1/2D"));
                juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
                expectEquals (choice->getIndex(), choice->choices.indexOf ("1/2D"), "every choice can be picked");
                expectEquals (seg.getOverflowText(), juce::String ("1/2D"));
                seg.clickSegment (0);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
                expect (seg.getOverflowText() != juce::String ("1/2D"), "overflow resets when a visible segment is chosen");
            }

            // The panels use it for the many-step parameters
            auto r = h.proc.getRouting();
            if (! r.contains (ModuleId::pulse)) r.insertDefault (ModuleId::pulse, 0);
            h.proc.setRouting (r);
            for (auto id : { ModuleId::delay, ModuleId::pulse })
            {
                auto panel = ui::createDetailPanel (h.proc, (int) id);
                panel->setBounds (panelSize());
                int overflowGroups = 0;
                std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
                {
                    for (auto* child : c.getChildren())
                    {
                        if (auto* s = dynamic_cast<ui::Segments*> (child))
                            if (s->getOverflowSegment() >= 0)
                                ++overflowGroups;
                        walk (*child);
                    }
                };
                walk (*panel);
                expect (overflowGroups >= 1, juce::String ("overflow segment in ") + dsp::moduleKey (id));
            }
        }

        beginTest ("Review: window size is stored as a scale, never taller than the screen, min scale keeps 9 px text >= 8 px");
        {
            Harness h;
            h.proc.getState().state.setProperty ("uiWindowWidth", 1440, nullptr);   // saved by the 1440 x 900 layout of 0.4
            std::unique_ptr<juce::AudioProcessorEditor> e (h.proc.createEditor());
            if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
                expect (e->getHeight() <= display->userArea.getHeight(), "fits the work area: " + juce::String (e->getHeight()));
            expect (h.proc.getState().state.hasProperty ("uiScale"), "scale stored");
            if (auto* c = e->getConstrainer())
            {
                const int minW = c->getMinimumWidth();
                expect (9.0 * minW / ui::baseWidth >= 8.0 - 1.0e-6, "9 px text >= 8 px at the minimum size: " + juce::String (minW));
                expect (minW <= 900, "but the window can be small: " + juce::String (minW));
            }

            // A stored scale is kept
            Harness h2;
            h2.proc.getState().state.setProperty ("uiScale", 1.2, nullptr);
            std::unique_ptr<juce::AudioProcessorEditor> e2 (h2.proc.createEditor());
            if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
                if (display->userArea.getWidth() > 1300 && display->userArea.getHeight() > 800)
                    expectEquals (e2->getWidth(), 1200, "stored scale 1.2");
        }

        beginTest ("Design: an old project shows the same values in the new panels");
        {
            Harness h;
            const std::vector<std::pair<const char*, float>> values {
                { pid::rideTarget, -22.0f }, { pid::rideMaxBoost, 5.0f }, { pid::compRatio, 4.5f }, { pid::compThreshold, -24.0f },
                { pid::deessFreq, 7200.0f }, { pid::delayDivision, 7.0f }, { pid::delayFeedback, 42.0f }, { pid::delayMix, 23.0f },
                { pid::reverbType, 3.0f }, { pid::reverbDecay, 2.4f }, { pid::reverbMix, 16.0f }, { pid::pulseRate, 4.0f },
                { pid::choirVoices, 2.0f }, { pid::smoothMode, 1.0f }, { pid::smashAmount, 64.0f }, { pid::chopRate, 2.0f },
                { pid::satDrive, 9.0f }, { pid::hpfFreq, 140.0f }, { pid::outputGain, -2.5f }, { pid::macroAggression, 30.0f } };
            for (auto& [id, v] : values)
                h.set (id, v);
            juce::MemoryBlock state;
            h.proc.getStateInformation (state);

            Harness h2;
            h2.proc.setStateInformation (state.getData(), (int) state.getSize());
            h2.proc.getState().state.setProperty ("uiMode", "pro", nullptr);
            {
                std::unique_ptr<juce::AudioProcessorEditor> e (h2.proc.createEditor());
                auto& ed = dynamic_cast<VelvoxAudioProcessorEditor&> (*e);
                for (int id = -1; id < dsp::numModules; ++id)
                    if (id < 0 || h2.proc.getRouting().contains ((ModuleId) id))
                        ed.selectModule (id);
                ed.setMode (VelvoxAudioProcessorEditor::Mode::simple);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            }
            for (auto& [id, v] : values)
                expectWithinAbsoluteError (h2.get (id), v, std::abs (v) * 0.01f + 0.01f, juce::String ("unchanged: ") + id);
        }
    }
};

static DesignTests designTests;
