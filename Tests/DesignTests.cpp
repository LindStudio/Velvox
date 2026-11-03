// Design system of VOCALYPSE (0.5 identity, 0.6 shell): tokens, module identity (colour + neon icon),
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
#include "UI/DetailPanels.h"
#include "UI/ModuleBrowser.h"
#include "UI/PresetBrowser.h"
#include "UI/TopBar.h"
#include "UI/ValueText.h"
#include "Text/I18n.h"
#include "Text/ParamText.h"

using namespace vocalypse;
using namespace vocalypse::test;
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
    juce::Rectangle<int> panelSize() { return VocalypseAudioProcessorEditor::panelArea().withZeroOrigin(); }
}

class DesignTests final : public juce::UnitTest
{
public:
    DesignTests() : juce::UnitTest ("VOCALYPSE design", "VOCALYPSE") {}

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
            expect (ui::moduleColour (ModuleId::reverb) == juce::Colour (0xfff472b6), "REVERB #F472B6");   // colours by group since the review of 2026-10-28
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

        // Review 2026-10-28 (item 12): legends where a picture mixes several things, always visible
        beginTest ("Review: SIMPEL spectrum, EQ, De-Esser, Smooth and Output show a legend that stays");
        {
            Harness h;
            auto r = h.proc.getRouting();
            for (auto id : { ModuleId::eq, ModuleId::deess, ModuleId::smooth })
                if (! r.contains (id))
                    r.insertDefault (id, 0);
            h.proc.setRouting (r);
            const auto findLegends = [] (juce::Component& root)
            {
                std::vector<const ui::Legend*> found;
                std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
                {
                    if (auto* l = dynamic_cast<ui::Legend*> (&c); l != nullptr && l->isVisible())
                        found.push_back (l);
                    for (auto* child : c.getChildren())
                        walk (*child);
                };
                walk (root);
                return found;
            };
            const auto check = [&] (juce::Component& view, const juce::String& where)
            {
                const auto legends = findLegends (view);
                expect (! legends.empty(), where + ": a legend");
                for (auto* l : legends)
                {
                    expect (l->getItems().size() >= 2, where + ": at least two entries");
                    expect (l->getWidth() >= l->getIdealWidth() && l->getHeight() >= ui::Legend::height - 2, where + ": room for its text");
                    expect (view.getLocalBounds().contains (l->getBoundsInParent()) || l->getParentComponent() != &view, where + ": inside");
                    for (auto& item : l->getItems())
                        expect (item.text.isNotEmpty(), where);
                }
            };
            for (auto lang : { i18n::Language::german, i18n::Language::english })
            {
                i18n::ScopedLanguage scoped (lang);
                for (int id : { (int) ModuleId::eq, (int) ModuleId::deess, (int) ModuleId::smooth, -1 })
                {
                    auto panel = ui::createDetailPanel (h.proc, id);
                    panel->setBounds (panelSize());
                    panel->refresh();
                    check (*panel, id < 0 ? juce::String ("output") : juce::String (dsp::moduleKey ((ModuleId) id)));
                }
                std::unique_ptr<juce::AudioProcessorEditor> e (h.proc.createEditor());
                auto& ed = *e;
                ui::SimpleView* simple = nullptr;
                std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
                {
                    if (auto* s = dynamic_cast<ui::SimpleView*> (&c)) simple = s;
                    for (auto* child : c.getChildren()) walk (*child);
                };
                walk (ed);
                expect (simple != nullptr);
                if (simple != nullptr)
                {
                    check (*simple, "simple");
                    const auto l = findLegends (*simple);
                    if (! l.empty())
                    {
                        expect (l[0]->getItems()[0].style == ui::LegendItem::Style::dashed, "before = dashed");
                        expect (l[0]->getBounds().getRight() <= simple->getSpectrumArea().getRight()
                                && l[0]->getY() >= simple->getSpectrumArea().getY(), "top right in the spectrum");
                    }
                }
            }
        }

        // Review 2026-10-28 (items 2 / 15): IN / OUT meters with a peak hold, OUT tells the difference to the input
        beginTest ("Review: SIMPEL meters hold their peak about 1.5 s; OUT shows the loudness difference to IN");
        {
            ui::CompactMeters m;
            m.setSize (800, 30);
            m.setLevels (-6.0f, -10.0f, -20.0f, -25.0f, false, 0.0);
            for (int i = 1; i <= 60; ++i)   // one second of quiet frames
                m.setLevels (-40.0f, -40.0f, -20.0f, -25.0f, false, i / 60.0);
            expectWithinAbsoluteError (m.getPeakHold (0), -6.0f, 0.01f, "IN holds its peak");
            expectWithinAbsoluteError (m.getPeakHold (1), -10.0f, 0.01f, "OUT too");
            expect (m.getInputDb() < -15.0f, "while the bar itself falls");
            for (int i = 61; i <= 240; ++i)
                m.setLevels (-40.0f, -40.0f, -20.0f, -25.0f, false, i / 60.0);
            expect (m.getPeakHold (0) < -30.0f, "then the hold falls back");
            expectEquals (m.getDifferenceText(), juce::String::fromUTF8 ("\xe2\x88\x92" "5 LU"), "OUT 5 LU under IN");
            m.setLevels (-40.0f, -40.0f, -20.0f, -20.3f, false, 5.0);
            expectEquals (m.getDifferenceText(), juce::String::fromUTF8 ("\xc2\xb1" "0"), "the same loudness");
            m.setLevels (-40.0f, -40.0f, -100.0f, -100.0f, false, 6.0);
            expect (m.getDifferenceText().isEmpty(), "nothing without signal");
        }

        // Review 2026-10-28 (item 11): the Output read-outs say what they are; "Wie Bypass" is not squeezed
        beginTest ("Review: Output panel names its read-outs in words, every Level Match segment fits its text");
        {
            Harness h;
            for (auto lang : { i18n::Language::german, i18n::Language::english })
            {
                i18n::ScopedLanguage scoped (lang);
                auto panel = ui::createDetailPanel (h.proc, -1);
                panel->setBounds (0, 0, 1400, 640);
                panel->refresh();
                juce::StringArray texts;
                std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
                {
                    if (auto* l = dynamic_cast<juce::Label*> (&c); l != nullptr && l->isVisible())
                        texts.add (l->getText());
                    if (auto* s = dynamic_cast<ui::Segments*> (&c); s != nullptr && s->getParamId() == pid::levelMatchMode)
                        for (auto* child : s->getChildren())
                            if (auto* b = dynamic_cast<juce::TextButton*> (child); b != nullptr && b->isVisible())
                            {
                                const int need = juce::GlyphArrangement::getStringWidthInt (
                                                     b->getLookAndFeel().getTextButtonFont (*b, b->getHeight()), b->getButtonText()) + 16;
                                expect (b->getWidth() >= need, b->getButtonText() + ": " + juce::String (b->getWidth()) + " < " + juce::String (need));
                            }
                    for (auto* child : c.getChildren())
                        walk (*child);
                };
                walk (*panel);
                const bool de = lang == i18n::Language::german;
                for (auto* want : de ? std::initializer_list<const char*> { "Eingang", "Nach den Modulen", "Lautheits-Ausgleich" }
                                     : std::initializer_list<const char*> { "Input", "After the modules", "Loudness match" })
                    expect (texts.contains (juce::String::fromUTF8 (want)), juce::String ("read-out caption: ") + want);
                for (auto& t : texts)
                    expect (! t.startsWith ("IN ") && ! t.startsWith ("FX ") && ! t.startsWith ("MATCH "), "no abbreviation: " + t);
            }
        }

        // Review 2026-10-28 (item 15): names under controls at least 11 px and readable on the panel
        beginTest ("Review: names under knobs are 11 px or more, faint texts reach 4.5:1 contrast on the panel");
        {
            Harness h;
            ui::Knob k (h.proc.getState(), pid::compRatio, "Ratio", ui::moduleColour (ModuleId::comp));
            expect (k.getNameLabel().getFont().getHeight() >= 11.0f, "knob name " + juce::String (k.getNameLabel().getFont().getHeight()));
            const auto lum = [] (juce::Colour c)
            {
                const auto ch = [] (juce::uint8 v)
                {
                    const double x = v / 255.0;
                    return x <= 0.03928 ? x / 12.92 : std::pow ((x + 0.055) / 1.055, 2.4);
                };
                return 0.2126 * ch (c.getRed()) + 0.7152 * ch (c.getGreen()) + 0.0722 * ch (c.getBlue());
            };
            const auto contrast = [&] (juce::Colour a, juce::Colour b)
            {
                const double la = lum (a), lb = lum (b);
                return (std::max (la, lb) + 0.05) / (std::min (la, lb) + 0.05);
            };
            for (auto c : { ui::colours::knobName, ui::colours::textFaint })
                for (auto bg : { ui::colours::panel, ui::colours::raisedHi, ui::colours::control })
                    expect (contrast (c, bg) >= 4.5, c.toDisplayString (false) + " on " + bg.toDisplayString (false) + ": "
                                                     + juce::String (contrast (c, bg), 2));
        }

        // 0.6 shell (spec 2.1): the 1384 x 1040 canvas, the CHAIN tab and the A/B header of 0.5 are gone
        beginTest ("Design: 1000 x 600 canvas, header with SIMPEL | ADVANCED and the tabs AUTO MIX | VOCAL CHECK");
        {
            expectEquals (ui::baseWidth, 1000);
            expectEquals (ui::baseHeight, 600);

            Harness h;
            h.proc.getAutoMix().dismissPrompt();
            std::unique_ptr<juce::AudioProcessorEditor> e (h.proc.createEditor());
            auto& ed = dynamic_cast<VocalypseAudioProcessorEditor&> (*e);
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

        // Review 2026-10-28 (item 5): too many for normal tiles -> compact tiles that share the width (every
        // module stays visible, no scrolling); only far beyond that the row scrolls
        beginTest ("Design: many tiles become compact instead of scrolling; normal size while they fit");
        {
            Harness h;
            ui::ChainView view (h.proc);
            view.setBounds (0, 0, 886, ui::ChainView::preferredHeight);
            for (const auto& s : h.proc.getRouting())
                expect (! view.isCompact() && view.getTileBounds (s.id, s.instance).getWidth() >= 84.0f - 0.01f, "few modules: normal tiles");
            for (auto id : { ModuleId::ride, ModuleId::smash, ModuleId::smooth, ModuleId::sat, ModuleId::doubler, ModuleId::delay,
                             ModuleId::reverb, ModuleId::denoise, ModuleId::eq, ModuleId::comp, ModuleId::sat, ModuleId::smooth })
                view.insertModule (id);
            expectGreaterThan ((int) h.proc.getRouting().size(), 14);
            expect (view.isCompact(), "compact tiles");
            expectEquals (view.getMaxScroll(), 0.0f, "no scrolling: every module visible");
            for (const auto& s : h.proc.getRouting())
            {
                const auto b = view.getTileBounds (s.id, s.instance);
                expect (b.getWidth() < ui::ChainView::compactLookW && b.getWidth() >= ui::ChainView::compactTileW - 0.01f, "compact width " + juce::String (b.getWidth()));
                expect (view.getLocalBounds().toFloat().contains (b.getCentre()), "inside the row");
                expect (! view.getPowerBounds (s.id, s.instance).isEmpty(), "power reachable (a dot)");
            }
        }

        beginTest ("Design: module colours by group - Aufräumen teal, Dynamik blue, Klang violet, Raum pink");
        {
            using G = ui::ModuleGroup;
            const std::pair<G, std::vector<ModuleId>> groups[] {
                { G::cleanUp, { ModuleId::denoise, ModuleId::breath, ModuleId::eq } },
                { G::dynamics, { ModuleId::ride, ModuleId::comp, ModuleId::smash, ModuleId::deess } },
                { G::tone, { ModuleId::smooth, ModuleId::sat, ModuleId::doubler } },
                { G::space, { ModuleId::delay, ModuleId::reverb } } };
            for (auto& [group, ids] : groups)
                for (auto id : ids)
                {
                    expect (ui::moduleGroup (id) == group, dsp::moduleKey (id));
                    const float dh = std::abs (ui::moduleColour (id).getHue() - ui::groupColour (group).getHue());
                    expect (std::min (dh, 1.0f - dh) < 0.06f, juce::String (dsp::moduleKey (id)) + " in its group's hue");
                }
            expectEquals (ui::groupName (G::cleanUp), juce::String::fromUTF8 ("Aufräumen"));
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
            expect (found[(int) ModuleId::smooth].segments.contains ("smoothMode") && found[(int) ModuleId::smooth].toggles.contains ("smoothDelta"), "SMOOTH character + delta");
            expect (found[(int) ModuleId::smash].knobs.contains ("smashAmount"), "VOCAL SMASH big knob");

            // EQ (0.6): the bands live in the graph and the selected-band row ("VOCALYPSE EQ panel"); with no band only Mix
            // (Mehr); the hidden 0.5 fixed bands are never shown
            expect (found[(int) ModuleId::eq].knobs.contains ("eqMix"), "EQ Mix");
            for (auto* legacy : { "hpfSlope", "lowType", "highType", "hpfOn", "lpfOn", "hpfFreq", "midGain" })
                expect (! found[(int) ModuleId::eq].segments.contains (legacy) && ! found[(int) ModuleId::eq].toggles.contains (legacy)
                        && ! found[(int) ModuleId::eq].knobs.contains (legacy), juce::String ("EQ hides ") + legacy);
            expect (found[(int) ModuleId::comp].segments.contains ("compDetector"), "COMPRESSOR detector as segments");
            expect (found[(int) ModuleId::deess].segments.contains ("deessMode") && found[(int) ModuleId::deess].toggles.contains ("deessListen"), "DE-ESSER mode + listen");
            expect (found[(int) ModuleId::sat].segments.contains ("satCharacter"), "SATURATION character as segments");
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
        beginTest ("Design: Simple mode faders grouped Klang (Presence, Warmth) / Raum (Space, Width) / Dynamik & Pegel (Punch, Vocal Level)");
        {
            Harness h;
            ui::SimpleView view (h.proc, nullptr);
            view.setBounds (VocalypseAudioProcessorEditor::bodyArea().withZeroOrigin());
            juce::StringArray ids;
            for (auto& s : view.getMacroSliders())
                ids.add (s->getParamId());
            expect (ids == juce::StringArray { "macroPresence", "macroWarmth", "macroSpace", "macroWidth", "macroAggression", "outputGain" },
                    "order: " + ids.joinIntoString (","));
            const juce::uint32 expected[] { 0xff00d1ff, 0xffd946ef, 0xff3b82f6, 0xff6366ff, 0xff8b5cf6, 0xfff472b6 };
            for (size_t i = 0; i < view.getMacroSliders().size() && i < 6; ++i)
                expect (view.getMacroSliders()[i]->getAccent() == juce::Colour (expected[i]), "macro colour " + juce::String ((int) i));
            expectEquals (view.getMacroSliders()[4]->getLabel(), juce::String ("Punch"));
            expectEquals (view.getMacroSliders()[5]->getLabel(), juce::String ("Vocal Level"));

            // Review 2026-10-28 (item 1): three groups with a heading, the middle of each fader = unchanged,
            // a grey line under each fader with what the auto mix did there (empty without a mix)
            expect (view.getMacroGroupTitles() == juce::StringArray { "Klang", "Raum", "Dynamik & Pegel" }, view.getMacroGroupTitles().joinIntoString (","));
            for (int i = 0; i < 5; ++i)
            {
                auto& s = *view.getMacroSliders()[(size_t) i];
                expectEquals (s.slider.getMinimum(), -100.0, s.getLabel() + " bipolar");
                expect (s.isBipolar(), s.getLabel());
                expect (view.getMacroNote (i).isEmpty(), "no note without a mix");
            }
            for (int i = 1; i < 6; ++i)
                expect (view.getMacroSliders()[(size_t) i]->getY() > view.getMacroSliders()[(size_t) i - 1]->getY(), "top to bottom");

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
            auto& ed = dynamic_cast<VocalypseAudioProcessorEditor&> (*e);
            ed.setMode (VocalypseAudioProcessorEditor::Mode::simple);
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
            for (auto language : { i18n::Language::german, i18n::Language::english })
            {
                i18n::ScopedLanguage scoped (language);
                for (auto& step : ui::TutorialOverlay::steps())
                    for (auto& phrase : outdated)
                        expect (! step.text.contains (phrase), "tutorial '" + step.title + "' mentions: " + phrase);
                for (auto& page : ui::help::pages())
                    for (auto& phrase : outdated)
                        expect (! page.body.contains (phrase), "help '" + page.key + "' mentions: " + phrase);
            }

            const auto* start = ui::help::find ("start");
            expect (start != nullptr && start->body.contains ("SIMPEL") && start->body.contains ("ADVANCED")
                    && start->body.contains ("VOCAL CHECK"), "start page explains the modes and the tabs");
            bool modesStep = false;
            for (auto& step : ui::TutorialOverlay::steps())
                modesStep = modesStep || (step.target == "modes" && step.text.contains ("SIMPEL") && step.text.contains ("ADVANCED"));
            expect (modesStep, "tutorial step for SIMPEL / ADVANCED");
            const auto* controls = ui::help::find ("controls");
            expect (controls != nullptr && controls->body.contains ("Alt") && controls->body.contains ("Shift"), "gestures explained in the help");

            // The tour starts with Memphy, then the main action; the settings (gear) are mentioned
            expectEquals (ui::TutorialOverlay::steps().front().target, juce::String ("mascot"), "tour starts with Memphy");
            expectEquals (ui::TutorialOverlay::steps()[1].target, juce::String ("analyse"), "... then the analysis");
            expect (ui::TutorialOverlay::steps().back().text.contains ("Zahnrad"), "tour mentions the settings");
            expect (start != nullptr && start->body.contains (juce::String::fromUTF8 ("„Vocal analysieren“")), "German help quotes the German button");
        }

        beginTest ("Design: help and tutorial in English");
        {
            i18n::ScopedLanguage en (i18n::Language::english);

            const auto* start = ui::help::find ("start");
            expect (start != nullptr && start->title == "Welcome to VOCALYPSE");
            expect (start != nullptr && start->body.contains ("SIMPLE") && start->body.contains ("ADVANCED")
                    && start->body.contains ("VOCAL CHECK") && start->body.contains ("\"Analyze vocal\""),
                    "English start page explains the modes and the tabs with the English UI words");
            const auto* controls = ui::help::find ("controls");
            expect (controls != nullptr && controls->body.contains ("Alt") && controls->body.contains ("Shift"));
            expectEquals (ui::help::moduleSectionHeadings()[0], juce::String ("What it does"));
            expectEquals (ui::help::shortDescription (-1), juce::String ("Stereo image, mono safety and fair loudness comparison"));

            bool modesStep = false;
            for (auto& step : ui::TutorialOverlay::steps())
                modesStep = modesStep || (step.target == "modes" && step.text.contains ("SIMPLE") && step.text.contains ("ADVANCED"));
            expect (modesStep, "English tutorial step for SIMPLE / ADVANCED");
            expectEquals (ui::TutorialOverlay::steps().front().title, juce::String ("Meet Memphy"));
            expectEquals (ui::TutorialOverlay::steps()[1].title, juce::String ("Analyze vocal"));
            expect (ui::TutorialOverlay::steps().back().text.contains ("gear"), "English tour mentions the settings");

            // Nothing German left in the English help and tour
            juce::StringArray german;
            for (auto* w : { "SIMPEL", "Vorher", "Nachher", "analysieren", "Mehr", "Duplizieren", "Zurücksetzen", "Als Send",
                             "Anwenden", "Eigene", "Favoriten", "Speichern", "Modul-", " die ", " und ", " der ", "Strg",
                             "ä", "ö", "ü", "ß", "„", "–" })
                german.add (juce::String::fromUTF8 (w));
            const auto germanIn = [&] (const juce::String& text)
            {
                for (auto& w : german)
                    if (text.contains (w))
                        return w;
                return juce::String();
            };
            for (auto& page : ui::help::pages())
                for (auto* text : { &page.title, &page.group, &page.body })
                    expect (germanIn (*text).isEmpty(), "help '" + page.key + "' still German: " + germanIn (*text));
            for (int id = -1; id < dsp::numModules; ++id)
                expect (germanIn (ui::help::shortDescription (id)).isEmpty(), "short description " + juce::String (id));
            for (auto& step : ui::TutorialOverlay::steps())
                expect (germanIn (step.title + " " + step.text).isEmpty(), "tour step '" + step.title + "': " + germanIn (step.text));

            // Both languages have the same pages in the same order, and the same tour targets
            const auto& de = ui::help::pages (i18n::Language::german);
            const auto& enPages = ui::help::pages (i18n::Language::english);
            expectEquals ((int) de.size(), (int) enPages.size());
            for (size_t i = 0; i < std::min (de.size(), enPages.size()); ++i)
                expectEquals (de[i].key, enPages[i].key);
            const auto enSteps = ui::TutorialOverlay::steps();
            {
                i18n::ScopedLanguage german_ (i18n::Language::german);
                const auto& deSteps = ui::TutorialOverlay::steps();
                expectEquals ((int) deSteps.size(), (int) enSteps.size());
                for (size_t i = 0; i < std::min (deSteps.size(), enSteps.size()); ++i)
                    expectEquals (deSteps[i].target, enSteps[i].target);
            }
        }

        // ---- Findings of the 0.5 review that still apply
        beginTest ("Review: every delay division stays reachable (overflow segment)");
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
            for (auto id : { ModuleId::delay })
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
            // 0.6.1: a Simple slider writes its modules - set first, so the module values below are the saved ones
            const std::vector<std::pair<const char*, float>> values { { pid::macroAggression, 30.0f },
                { pid::rideTarget, -22.0f }, { pid::rideMaxBoost, 5.0f }, { pid::compRatio, 4.5f }, { pid::compThreshold, -24.0f },
                { pid::deessFreq, 7200.0f }, { pid::delayDivision, 7.0f }, { pid::delayFeedback, 42.0f }, { pid::delayMix, 23.0f },
                { pid::reverbType, 3.0f }, { pid::reverbDecay, 2.4f }, { pid::reverbMix, 16.0f },
                { pid::smoothMode, 1.0f }, { pid::smashAmount, 64.0f },
                { pid::satDrive, 9.0f }, { pid::hpfFreq, 140.0f }, { pid::outputGain, -2.5f } };
            for (auto& [id, v] : values)
                h.set (id, v);
            juce::MemoryBlock state;
            h.proc.getStateInformation (state);

            Harness h2;
            h2.proc.setStateInformation (state.getData(), (int) state.getSize());
            h2.proc.getState().state.setProperty ("uiMode", "pro", nullptr);
            {
                std::unique_ptr<juce::AudioProcessorEditor> e (h2.proc.createEditor());
                auto& ed = dynamic_cast<VocalypseAudioProcessorEditor&> (*e);
                for (int id = -1; id < dsp::numModules; ++id)
                    if (id < 0 || h2.proc.getRouting().contains ((ModuleId) id))
                        ed.selectModule (id);
                ed.setMode (VocalypseAudioProcessorEditor::Mode::simple);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            }
            for (auto& [id, v] : values)
                expectWithinAbsoluteError (h2.get (id), v, std::abs (v) * 0.01f + 0.01f, juce::String ("unchanged: ") + id);
        }

        beginTest ("Language (1.0): parameter texts in German for the interface, the host keeps English");
        {
            Harness h;
            auto* inGain = h.proc.getState().getParameter (pid::inputGain);
            expect (i18n::isGerman(), "tests run in German");
            expectEquals (inGain->getName (32), juce::String ("Input Gain"), "the host sees the English name in a German interface");
            expectEquals (i18n::paramName (pid::inputGain, "Input Gain"), juce::String ("Eingangspegel"));
            expectEquals (i18n::choiceName (pid::levelMatchMode, 1, "Bypass match"), juce::String ("Wie Bypass"));
            expectEquals (i18n::choiceName (pid::checkMode, 0, "Continuous"), juce::String ("Laufend"));
            expect (i18n::paramDescription (pid::compThreshold, "x") != juce::String ("x"), "descriptions are translated");
            expectEquals (i18n::paramName ("compOn_2", "Compressor 2: On"), juce::String ("Compressor 2: An"), "extra instances keep their name");
            {
                i18n::ScopedLanguage en (i18n::Language::english);
                expectEquals (i18n::paramName (pid::inputGain, "Input Gain"), juce::String ("Input Gain"));
                expectEquals (i18n::choiceName (pid::levelMatchMode, 1, "Bypass match"), juce::String ("Bypass match"));
                expectEquals (inGain->getName (32), juce::String ("Input Gain"));
                // Typed choices: both languages
                auto& mode = *h.proc.getState().getParameter (pid::checkMode);
                expect (ui::parseValueText ("Laufend", mode) == std::optional<float> (0.0f), "German choice name typed in English");
                expect (ui::parseValueText ("capture", mode) == std::optional<float> (1.0f));
            }
            auto& mode = *h.proc.getState().getParameter (pid::checkMode);
            expect (ui::parseValueText ("Continuous", mode) == std::optional<float> (0.0f), "English choice name typed in German");
        }

        beginTest ("Language (1.0): header, chain menus, value menu, panels and lists in English");
        {
            i18n::ScopedLanguage en (i18n::Language::english);
            Harness h;
            const auto noGerman = [this] (const juce::String& text, const juce::String& where)
            {
                bool clean = ! text.containsAnyOf (juce::String::fromUTF8 ("äöüÄÖÜß„“"));
                for (auto* w : { "Einschalten", "Entfernen", "Kette", "zurücksetzen", "Wert ", "Modul", "Eigene", "Alle", "Mehr", " und " })
                    clean = clean && ! text.contains (juce::String::fromUTF8 (w));
                expect (clean, where + ": " + text);
            };

            // Header
            {
                ui::DialogOverlay dialog;
                ui::TopBar top (h.proc, dialog);
                expectEquals (top.getModeButton (ui::TopBar::Mode::simple).getButtonText(), juce::String ("SIMPLE"));
                top.setMode (ui::TopBar::Mode::advanced);
                expectEquals (top.getTabButton (ui::TopBar::Tab::autoMix).getButtonText(), juce::String ("MODULES"));
                noGerman (top.getPrevButton().getTooltip() + top.getNextButton().getTooltip() + top.getHelpButton().getTooltip()
                              + top.getModeButton (ui::TopBar::Mode::simple).getTooltip(), "header tooltips");
            }

            // Chain tile menu and chain menu
            {
                ui::ChainView view (h.proc);
                view.setBounds (0, 0, 886, ui::ChainView::preferredHeight);
                view.refresh();
                juce::StringArray items;
                const auto menu = view.buildTileMenu ((int) ModuleId::delay);
                for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                    items.add (it.getItem().text);
                const auto all = items.joinIntoString ("|");
                for (auto* t : { "Duplicate", "Remove", "Move left", "Move right", "Reset", "As send", "As insert", "Undo", "Redo", "Help: Delay" })
                    expect (all.contains (t), juce::String (t) + " in the menu: " + all);
                noGerman (all, "tile menu");

                h.set (pid::compRatio, 9.0f);
                expect (view.performTileAction ((int) ModuleId::comp, ui::ChainView::TileAction::reset));
                expectEquals (h.proc.getUndo().getUndoName(), juce::String ("Reset Compressor"));
            }

            // Value menu (right-click on any control)
            {
                auto& thr = *h.proc.getState().getParameter (pid::compThreshold);
                juce::StringArray texts;
                const auto m = ui::gestures::buildMenu (thr, nullptr, [] {});
                for (juce::PopupMenu::MenuItemIterator it (m); it.next();)
                    texts.add (it.getItem().text);
                for (auto* t : { "Default value", "Enter value", "Copy value", "Paste value" })
                    expect (texts.joinIntoString ("|").contains (t), juce::String (t) + " in " + texts.joinIntoString ("|"));
                noGerman (texts.joinIntoString ("|"), "value menu");
            }

            // Output panel: segment labels by position, read-outs with units, More
            {
                auto panel = ui::createDetailPanel (h.proc, -1);
                panel->setBounds (0, 0, 984, 440);
                panel->refresh();
                juce::StringArray labels;
                std::function<void (juce::Component&)> collectLabels = [&] (juce::Component& c)
                {
                    for (auto* child : c.getChildren())
                    {
                        if (auto* s = dynamic_cast<ui::Segments*> (child); s != nullptr && s->getParamId() == pid::levelMatchMode)
                            for (auto* b : s->getChildren())
                                if (auto* tb = dynamic_cast<juce::TextButton*> (b))
                                    labels.add (tb->getButtonText());
                        if (auto* l = dynamic_cast<juce::Label*> (child))
                            labels.add (l->getText());
                        collectLabels (*child);
                    }
                };
                collectLabels (*panel);
                const auto all = labels.joinIntoString ("|");
                expect (all.contains ("Off|Like bypass|Auto-Gain"), "Level Match segments: " + all);
                expect (all.contains (" LUFS") && all.contains (" dB") && all.contains ("CORRELATION"), "read-outs with units: " + all);
            }

            // Preset list and module browser
            {
                ui::DialogOverlay dialog;
                ui::PresetBrowser presets (h.proc.getPresetManager(), dialog);
                const auto cats = presets.getCategories();
                expect (cats.contains ("All") && cats.contains ("User") && cats.contains ("Favorites"), cats.joinIntoString (","));
                expectEquals (ui::PresetBrowser::categoryLabel ("Creative"), juce::String ("Creative"));
                {
                    i18n::ScopedLanguage de (i18n::Language::german);
                    expectEquals (ui::PresetBrowser::categoryLabel ("Creative"), juce::String ("Kreativ"));
                }
                expectEquals (ui::ModuleBrowser::categoryName (ui::ModuleBrowser::Category::colour), juce::String ("Color"));

                ui::ModuleBrowser browser (h.proc);
                browser.setBounds (0, 0, 1000, 600);
                browser.open (0, true);
                browser.setSearchText ("hall");   // a German word finds Reverb in English too
                expect (browser.cardBoundsFor (ModuleId::reverb).getWidth() > 0, "German search word in English");
                browser.setSearchText ("reverb");
                expect (browser.cardBoundsFor (ModuleId::reverb).getWidth() > 0, "English search word");
            }
        }

        beginTest ("Fix (1.0): VOCAL CHECK on does not mark the preset as changed");
        {
            Harness h;
            auto& pm = h.proc.getPresetManager();
            expect (pm.loadPreset (0));
            expect (! pm.isCurrentPresetModified(), "fresh preset");
            h.set (pid::checkOn, h.get (pid::checkOn) > 0.5f ? 0.0f : 1.0f);
            expect (! pm.isCurrentPresetModified(), "analysis only: not a change of the sound");
            h.set (pid::compRatio, 9.0f);
            expect (pm.isCurrentPresetModified(), "a real change still counts");
        }
    }
};

static DesignTests designTests;
