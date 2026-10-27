// UI interaction tests: chain editor (drag & drop, power, send/insert, "+" insert points,
// add / duplicate / remove / undo of instances), module browser, EQ mouse-wheel Q.
// Real mouse events are sent to the components.

#include "TestHelpers.h"
#include "UI/ChainView.h"
#include "UI/ModuleBrowser.h"
#include "UI/ModuleViews.h"
#include "UI/EqPanel.h"
#include "UI/HelpContent.h"
#include "PluginEditor.h"

using namespace velvox;
using namespace velvox::test;
using dsp::ModuleId;

namespace
{
    juce::MouseEvent mouse (juce::Component& c, juce::Point<float> pos, juce::Point<float> downPos, bool dragged,
                            juce::ModifierKeys mods = {})
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, mods,
                                 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, downPos, now, 1, dragged);
    }

    void drag (juce::Component& c, juce::Point<float> from, juce::Point<float> to)
    {
        c.mouseDown (mouse (c, from, from, false));
        c.mouseDrag (mouse (c, (from + to) * 0.5f, from, true));
        c.mouseDrag (mouse (c, to, from, true));
        c.mouseUp (mouse (c, to, from, true));
    }

    void click (juce::Component& c, juce::Point<float> at)
    {
        c.mouseMove (mouse (c, at, at, false));
        c.mouseDown (mouse (c, at, at, false));
        c.mouseUp (mouse (c, at, at, false));
    }

    juce::MouseWheelDetails wheel (float dy, float dx = 0.0f)
    {
        juce::MouseWheelDetails w;
        w.deltaX = dx;
        w.deltaY = dy;
        w.isReversed = false;
        w.isSmooth = false;
        w.isInertial = false;
        return w;
    }

    struct ViewFixture
    {
        Harness h;
        ui::ChainView view { h.proc };
        ViewFixture() { view.setBounds (0, 0, 886, ui::ChainView::preferredHeight); }
    };

    float value (Harness& h, const juce::String& id) { return h.proc.getState().getRawParameterValue (id)->load(); }
}

class UiTests final : public juce::UnitTest
{
public:
    UiTests() : juce::UnitTest ("Headliner UI interaction", "Headliner") {}

    void runTest() override
    {
        beginTest ("Drag a module to the front and to the end of the chain");
        {
            ViewFixture f;
            const auto deess = f.view.getTileBounds (ModuleId::deess);
            const auto eq = f.view.getTileBounds (ModuleId::eq);
            drag (f.view, deess.getCentre(), { eq.getX() + 5.0f, eq.getCentreY() });
            auto r = f.h.proc.getRouting();
            expect (r.positionOf (ModuleId::deess) < r.positionOf (ModuleId::eq), "de-esser now before EQ");
            expect (r.isValid());

            const auto eqNow = f.view.getTileBounds (ModuleId::eq);
            const auto comp = f.view.getTileBounds (ModuleId::comp);
            drag (f.view, eqNow.getCentre(), { comp.getRight() - 3.0f, comp.getCentreY() });
            r = f.h.proc.getRouting();
            expect (r.positionOf (ModuleId::eq) > r.positionOf (ModuleId::comp), "EQ after the compressor");
            expect (f.view.undo(), "a drag can be undone");
            expect (f.h.proc.getRouting().positionOf (ModuleId::eq) < f.h.proc.getRouting().positionOf (ModuleId::comp));
        }

        beginTest ("Drag the delay from the parallel lane into the chain (becomes an insert) and back");
        {
            ViewFixture f;
            f.h.proc.setRouting (dsp::Routing::legacyDefaults());   // delay / reverb as sends (a 0.5 project)
            f.view.refresh();
            expect (f.h.proc.getRouting().isSend (ModuleId::delay));
            const auto delay = f.view.getTileBounds (ModuleId::delay);
            const auto deess = f.view.getTileBounds (ModuleId::deess);
            drag (f.view, delay.getCentre().translated (-30.0f, 0.0f), { deess.getX() + 4.0f, deess.getCentreY() });
            const auto r = f.h.proc.getRouting();
            expect (r.isInsert (ModuleId::delay), "delay is an insert now");
            expect (r.positionOf (ModuleId::comp) < r.positionOf (ModuleId::delay)
                    && r.positionOf (ModuleId::delay) < r.positionOf (ModuleId::deess), "between compressor and de-esser");

            const auto delayNow = f.view.getTileBounds (ModuleId::delay);
            drag (f.view, { delayNow.getX() + delayNow.getWidth() * 0.45f, delayNow.getCentreY() }, f.view.getLaneBounds().getCentre());
            expect (f.h.proc.getRouting().isSend (ModuleId::delay), "back to send");
        }

        beginTest ("Serial modules cannot be dropped onto the parallel lane");
        {
            ViewFixture f;
            const auto before = f.h.proc.getRouting();
            const auto comp = f.view.getTileBounds (ModuleId::comp);
            drag (f.view, comp.getCentre(), f.view.getLaneBounds().getCentre());
            expect (f.h.proc.getRouting() == before, "routing unchanged");
            expect (f.view.getHint().contains ("nur Delay und Reverb"), "explains why");
        }

        beginTest ("Power switch bypasses the instance, \"Als Insert\" / \"Als Send\" switch the mode");
        {
            ViewFixture f;
            const float before = f.h.get (pid::compOn);
            click (f.view, f.view.getPowerBounds (ModuleId::comp).getCentre());
            expectEquals (f.h.get (pid::compOn), 1.0f - before);

            // A fresh chain holds the reverb as an insert (0.6): the menu makes it a send
            const int reverb = dsp::selectionCode (ModuleId::reverb, 0);
            const bool wasSend = f.h.proc.getRouting().isSend (ModuleId::reverb);
            expect (f.view.performTileAction (reverb, wasSend ? ui::ChainView::TileAction::asInsert : ui::ChainView::TileAction::asSend));
            expect (f.h.proc.getRouting().isSend (ModuleId::reverb) != wasSend, "reverb switched");
            expect (! f.view.performTileAction ((int) ModuleId::comp, ui::ChainView::TileAction::asSend), "only delay / reverb can be sends");
        }

        beginTest ("Add, duplicate, remove, undo and redo instances");
        {
            ViewFixture f;
            const int eq2 = f.view.insertModule (ModuleId::eq, 2);
            expectEquals (eq2, dsp::selectionCode (ModuleId::eq, 1), "second EQ instance");
            auto r = f.h.proc.getRouting();
            expectEquals (r.indexOf (ModuleId::eq, 1), 2, "inserted at serial position 2 (after the compressor)");
            expect (value (f.h, "eqOn_2") > 0.5f, "the new instance is on");
            expect (value (f.h, "hpfOn_2") < 0.5f, "and neutral (no low cut)");
            expectEquals (ui::ChainView::cardName (r, ModuleId::eq, 0), juce::String ("EQ 1"));
            expectEquals (ui::ChainView::cardName (r, ModuleId::eq, 1), juce::String ("EQ 2"));

            // Independent settings
            f.h.proc.getState().getParameter ("midGain_2")->setValueNotifyingHost (f.h.proc.getState().getParameter ("midGain_2")->convertTo0to1 (6.0f));
            expectWithinAbsoluteError (f.h.get (pid::midGain), 0.0f, 0.01f, "EQ 1 untouched");

            // Duplicate copies the settings into a new instance right after the original
            const int eq3 = f.view.duplicateInstance (ModuleId::eq, 1);
            expectEquals (eq3, dsp::selectionCode (ModuleId::eq, 2));
            r = f.h.proc.getRouting();
            expectEquals (r.indexOf (ModuleId::eq, 2), r.indexOf (ModuleId::eq, 1) + 1);
            expectWithinAbsoluteError (value (f.h, "midGain_3"), 6.0f, 0.01f, "settings copied");

            // Remove through the tile menu action
            expect (f.view.performTileAction (dsp::selectionCode (ModuleId::eq, 1), ui::ChainView::TileAction::remove));
            expect (! f.h.proc.getRouting().contains (ModuleId::eq, 1), "EQ 2 removed");
            expect (value (f.h, "eqOn_2") < 0.5f, "removed instance is switched off");

            expect (f.view.undo(), "undo the removal");
            expect (f.h.proc.getRouting().contains (ModuleId::eq, 1));
            expectWithinAbsoluteError (value (f.h, "midGain_2"), 6.0f, 0.01f, "with its settings");
            expect (f.view.redo());
            expect (! f.h.proc.getRouting().contains (ModuleId::eq, 1));

            // Bank full
            f.view.insertModule (ModuleId::eq);
            f.view.insertModule (ModuleId::eq);
            expectEquals (f.h.proc.getRouting().countOf (ModuleId::eq), 4);
            expectEquals (f.view.insertModule (ModuleId::eq), -1, "no fifth EQ");
            expect (f.view.getHint().contains ("Alle 4"));
        }

        beginTest ("\"+\" between two cards and the add card open the browser for that position");
        {
            ViewFixture f;
            int index = -9;
            bool atEnd = false;
            f.view.onAddModule = [&] (int i, bool end) { index = i; atEnd = end; };
            const auto eq = f.view.getTileBounds (ModuleId::eq);
            const auto comp = f.view.getTileBounds (ModuleId::comp);
            const juce::Point<float> between ((eq.getRight() + comp.getX()) * 0.5f, eq.getCentreY());
            click (f.view, between);
            expectEquals (index, 1, "between EQ (0) and compressor (1)");
            expect (! atEnd);

            click (f.view, f.view.getAddCardBounds().getCentre());
            expectEquals (index, 5, "behind the five serial modules (delay and reverb are inserts in a fresh chain)");
            expect (atEnd);
        }

        beginTest ("0.7: the module browser and the help offer exactly the 11 remaining modules");
        {
            Harness h;
            ui::ModuleBrowser browser (h.proc);
            browser.setBounds (0, 0, ui::baseWidth, ui::baseHeight);
            browser.open (1, false);
            juce::StringArray keys;
            for (auto id : browser.visibleModules())
                keys.add (dsp::moduleKey (id));
            keys.sort (false);
            juce::StringArray expected { "eq", "comp", "deess", "sat", "doubler", "delay", "reverb", "smash", "smooth", "check", "ride" };
            expected.sort (false);
            expectEquals (keys.joinIntoString (","), expected.joinIntoString (","));
            for (int i = 0; i < dsp::numModules; ++i)
                expect (ui::help::shortDescription (i).isNotEmpty(), dsp::moduleKey ((ModuleId) i));
            for (auto lang : { i18n::Language::german, i18n::Language::english })
                for (const auto& page : ui::help::pages (lang))
                    for (auto* gone : { "Pitch", "Stutter", "Pulse", "Choir" })
                        expect (page.title != gone && page.key != juce::String (gone).toLowerCase() && page.key != "chop",
                                "help page " + page.title);
        }

        beginTest ("Module row: readable cards, horizontal scrolling, visual order = DSP order");
        {
            ViewFixture f;
            for (auto id : { ModuleId::ride, ModuleId::smash, ModuleId::smooth, ModuleId::sat, ModuleId::doubler,
                             ModuleId::check, ModuleId::sat, ModuleId::smooth, ModuleId::comp })
                f.view.insertModule (id);
            f.view.insertModule (ModuleId::eq);
            const auto row = f.view.getRowViewport();
            const auto r0 = f.h.proc.getRouting();

            for (const auto& s : r0)
                if (s.insert)
                    expect (f.view.getTileBounds (s.id, s.instance).getWidth() >= ui::ChainView::minTileW - 0.01f,
                            juce::String ("card width ") + dsp::moduleDisplayName (s.id));
            expect (f.view.getMaxScroll() > 0.0f, "13 modules do not fit: the row scrolls");

            f.view.scrollTo (0.0f, false);
            expect (! f.view.isTileFullyVisible (ModuleId::eq, 1), "last module starts out of view");
            f.view.scrollToShow (ModuleId::eq, 1, false);
            expect (f.view.isTileFullyVisible (ModuleId::eq, 1), "scrolled to the selected card");

            const auto checkOrder = [&]
            {
                const auto r = f.h.proc.getRouting();
                std::vector<std::pair<float, int>> onScreen;
                std::vector<int> serial;
                for (const auto& s : r)
                    if (s.insert)
                    {
                        onScreen.push_back ({ f.view.getTileBounds (s.id, s.instance).getX(), dsp::selectionCode (s.id, s.instance) });
                        serial.push_back (dsp::selectionCode (s.id, s.instance));
                    }
                std::sort (onScreen.begin(), onScreen.end(), [] (auto& a, auto& b) { return a.first < b.first; });
                bool same = onScreen.size() == serial.size();
                for (size_t i = 0; same && i < serial.size(); ++i)
                    same = onScreen[i].second == serial[i];
                return same;
            };
            expect (checkOrder(), "initial order");

            // Scrolled to the end: drag the last visible card in front of the first visible one
            f.view.scrollTo (f.view.getMaxScroll(), false);
            std::vector<dsp::ChainSlot> visible;
            for (const auto& s : f.h.proc.getRouting())
                if (s.insert && f.view.isTileFullyVisible (s.id, s.instance))
                    visible.push_back (s);
            expect (visible.size() >= 3, "several cards visible after scrolling");
            if (visible.size() >= 3)
            {
                const auto first = f.view.getTileBounds (visible.front().id, visible.front().instance);
                const auto last = f.view.getTileBounds (visible.back().id, visible.back().instance);
                drag (f.view, last.getCentre(), { first.getX() + 6.0f, first.getCentreY() });
                const auto r = f.h.proc.getRouting();
                expect (r.indexOf (visible.back().id, visible.back().instance) == r.indexOf (visible.front().id, visible.front().instance) - 1,
                        "dropped right before the first visible card");
                expect (r.isValid());
                expect (checkOrder(), "visual order follows the new DSP order");
            }

            // Clicks outside the visible part of the row never hit a hidden card
            f.view.scrollTo (0.0f, false);
            const auto offscreen = f.view.getTileBounds (ModuleId::eq, 1);
            if (! row.contains (offscreen.getCentre()))
            {
                const float before = value (f.h, "eqOn_2");
                click (f.view, f.view.getPowerBounds (ModuleId::eq, 1).getCentre());
                expectEquals (value (f.h, "eqOn_2"), before, "no click through on a scrolled-away card");
            }
        }

        beginTest ("Module browser: cards grouped by category, search, Esc, Enter, highlight, full modules greyed with a reason");
        {
            Harness h;
            ui::ModuleBrowser browser (h.proc);
            browser.setBounds (0, 0, ui::baseWidth, ui::baseHeight);
            browser.open (1, false);
            using C = ui::ModuleBrowser::Category;
            expectEquals ((int) browser.visibleModules().size(), dsp::numModules, "every module");
            const std::vector<std::pair<C, std::vector<ModuleId>>> expected {
                { C::cleanUp, { ModuleId::eq, ModuleId::deess, ModuleId::smooth } },
                { C::dynamics, { ModuleId::comp, ModuleId::smash, ModuleId::ride } },
                { C::colour, { ModuleId::sat } },
                { C::widthVoices, { ModuleId::doubler } },
                { C::space, { ModuleId::delay, ModuleId::reverb } },
                { C::analysis, { ModuleId::check } } };
            std::vector<ModuleId> order;
            for (auto& [cat, ids] : expected)
            {
                const auto group = browser.groupBoundsFor (cat);
                expect (! group.isEmpty(), ui::ModuleBrowser::categoryName (cat));
                for (auto id : ids)
                {
                    expect (ui::ModuleBrowser::categoryOf (id) == cat);
                    expect (group.contains (browser.cardBoundsFor (id)), juce::String (dsp::moduleKey (id)) + " in its group");
                    order.push_back (id);
                }
            }
            expect (browser.visibleModules() == order, "grouped display order");
            for (auto id : order)
                expect (browser.getLocalBounds().contains (browser.cardBoundsFor (id)), "card inside the window");

            browser.setSearchText ("harsh");
            const auto harsh = browser.visibleModules();
            expect (std::find (harsh.begin(), harsh.end(), ModuleId::smooth) != harsh.end()
                    && std::find (harsh.begin(), harsh.end(), ModuleId::deess) != harsh.end(), "search finds by purpose");
            browser.setSearchText ("zzzz");
            expect (browser.visibleModules().empty());
            browser.setSearchText ({});

            // Hover shows the description
            const auto reverbCard = browser.cardBoundsFor (ModuleId::reverb).getCentre().toFloat();
            browser.mouseMove (mouse (browser, reverbCard, reverbCard, false));
            expect (browser.getInfoText().startsWith ("Reverb"), "hover description: " + browser.getInfoText());

            ModuleId chosen = ModuleId::eq;
            int at = -1;
            browser.onChoose = [&] (ModuleId id, int index, bool) { chosen = id; at = index; };
            click (browser, reverbCard);
            expect (chosen == ModuleId::reverb && at == 1, "clicking a card chooses it for the position");
            expect (! browser.isOpen(), "the browser closes");

            // Keys: highlight + Enter, Esc
            browser.open (0, true);
            expect (browser.getHighlighted() == ModuleId::eq, "first card highlighted");
            browser.moveHighlight (1);
            expect (browser.getHighlighted() == ModuleId::deess, "arrow moves the highlight");
            expect (browser.keyPressed (juce::KeyPress (juce::KeyPress::returnKey)));
            expect (chosen == ModuleId::deess && ! browser.isOpen(), "Enter adds the highlighted module");
            browser.open (0, true);
            browser.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
            expect (! browser.isOpen(), "Esc closes");

            // Full: greyed with a reason, not choosable, skipped by the highlight
            browser.open (0, true);
            auto r = h.proc.getRouting();
            if (! r.contains (ModuleId::check))
                r.insertAt (r.size(), ModuleId::check, 0);
            h.proc.setRouting (r);
            expect (browser.reasonUnavailable (ModuleId::check).isNotEmpty(), "reason for check");
            expect (browser.reasonUnavailable (ModuleId::reverb).isEmpty());
            chosen = ModuleId::eq;
            click (browser, browser.cardBoundsFor (ModuleId::check).getCentre().toFloat());
            expect (chosen == ModuleId::eq && browser.isOpen(), "check is already in the chain (one instance)");
            browser.setSearchText ("diagnose");
            expect (! browser.chooseHighlighted(), "Enter does not add a full module");
        }

        beginTest ("Editor: browser inserts at the chosen position and opens the new instance");
        {
            Harness h;
            h.proc.getState().state.setProperty ("uiMode", "pro", nullptr);
            std::unique_ptr<juce::AudioProcessorEditor> e (h.proc.createEditor());
            auto& ed = dynamic_cast<VelvoxAudioProcessorEditor&> (*e);
            ed.getChainView().onAddModule (1, false);
            expect (ed.getBrowser().isOpen());
            ed.getBrowser().onChoose (ModuleId::comp, 1, false);
            const auto r = h.proc.getRouting();
            expect (r.contains (ModuleId::comp, 1), "second compressor");
            expectEquals (r.indexOf (ModuleId::comp, 1), 1);
            expectEquals (ed.getChainView().getSelected(), dsp::selectionCode (ModuleId::comp, 1), "its controls are open");
            expect (ed.getDetail() != nullptr);

            // Reverb added at the end is an insert (0.6, spec Q2) on the true dry/wet law
            ed.getBrowser().onChoose (ModuleId::reverb, 99, true);
            expect (h.proc.getRouting().isInsert (ModuleId::reverb, 1), "reverb 2 is an insert");
            expect (! h.proc.usesLegacyMixLaw (ModuleId::reverb, 1));
        }

        beginTest ("Selecting tiles opens the matching Pro panel (every module and repeated instances)");
        {
            Harness h;
            h.proc.setRouting (dsp::Routing::fromString ("v2,eq,eq#2,ride,comp,comp#3,smash,deess,smooth,sat,sat#2,doubler,choir,pitch,"
                                                         "chop,pulse,check,delay:send,reverb:send,reverb#2:insert,delay#2:send"));
            h.proc.getState().state.setProperty ("uiMode", "pro", nullptr);
            for (const auto& s : h.proc.getRouting())
            {
                const int code = dsp::selectionCode (s.id, s.instance);
                h.proc.getState().state.setProperty ("uiSelection", code, nullptr);
                std::unique_ptr<juce::AudioProcessorEditor> e (h.proc.createEditor());
                auto& ed = dynamic_cast<VelvoxAudioProcessorEditor&> (*e);
                expect (ed.getDetail() != nullptr && ed.getChainView().getSelected() == code,
                        "panel for " + ui::ChainView::cardName (h.proc.getRouting(), s.id, s.instance));
                if (ed.getDetail() != nullptr)
                    expectEquals (ed.getDetail()->getInstance(), (int) s.instance);
            }
            h.proc.getState().state.setProperty ("uiSelection", -1, nullptr);
            std::unique_ptr<juce::AudioProcessorEditor> e (h.proc.createEditor());
            const auto out = h.run (h.noise (0.2f, 0.3, 3));
            expect (allFinite (out));
        }

        beginTest ("EQ: mouse wheel over a band node changes its Q, elsewhere it scrolls");
        {
            for (float down : { 0.11f, 0.11f * 5.0f })
            {
                const float q = 2.0f;
                const float up1 = ui::eqWheelQ (q, down, false, 0.3f, 8.0f);
                const float dn1 = ui::eqWheelQ (q, -down, false, 0.3f, 8.0f);
                expect (up1 > q && dn1 < q, "up = narrower (higher Q), down = wider");
                expectWithinAbsoluteError (std::log (up1 / q), -std::log (dn1 / q), 1.0e-4f, "symmetric steps");
            }
            expectWithinAbsoluteError (ui::eqWheelQ (1.0f, 0.11f, false, 0.3f, 8.0f), 1.247f, 0.02f, "one notch ~ x1.25");
            expect (ui::eqWheelQ (1.0f, 0.11f, true, 0.3f, 8.0f) < 1.06f, "Shift: fine steps");
            expectEquals (ui::eqWheelQ (7.9f, 2.0f, false, 0.3f, 8.0f), 8.0f, "clamped at the top");
            expectEquals (ui::eqWheelQ (0.31f, -2.0f, false, 0.3f, 8.0f), 0.3f, "clamped at the bottom");
            expectEquals (ui::eqWheelQ (1.0f, std::numeric_limits<float>::quiet_NaN(), false, 0.3f, 8.0f), 1.0f, "invalid delta ignored");

            // Real view (the 0.6 EQ graph), second EQ instance: the Q of that instance's band follows the wheel
            Harness h;
            auto r = h.proc.getRouting();
            r.insertAt (1, ModuleId::eq, 1);
            h.proc.setRouting (r);
            const int band = ui::eqedit::addBand (h.proc, 1, 1500.0f, 4.0f);
            auto view = ui::createModuleView (h.proc, dsp::selectionCode (ModuleId::eq, 1), nullptr);
            auto* graph = dynamic_cast<ui::EqGraph*> (view.get());
            expect (graph != nullptr, "the EQ visual is the EqGraph");
            if (graph == nullptr)
                return;
            view->setBounds (0, 0, 900, 200);
            const auto node = graph->pointFor (band);
            const auto qId = ui::eqedit::paramId (1, band, pid::eqband::q);
            const float before = value (h, qId);
            view->mouseWheelMove (mouse (*view, node, node, false), wheel (0.11f));
            const float narrower = value (h, qId);
            view->mouseWheelMove (mouse (*view, node, node, false), wheel (-0.22f));
            const float wider = value (h, qId);
            logMessage ("band Q: " + juce::String (before, 3) + " -> " + juce::String (narrower, 3) + " -> " + juce::String (wider, 3));
            expect (narrower > before * 1.15f, "wheel up narrows the band");
            expect (wider < before, "wheel down widens it");
            expectWithinAbsoluteError (value (h, ui::eqedit::paramId (0, band, pid::eqband::q)), 1.0f, 1.0e-4f, "EQ 1 untouched");

            const float atRest = value (h, qId);
            const auto plot = graph->plotArea();
            const juce::Point<float> empty (plot.getX() + 5.0f, plot.getY() + 4.0f);
            view->mouseWheelMove (mouse (*view, empty, empty, false), wheel (0.11f));
            expectEquals (value (h, qId), atRest, "away from the nodes nothing changes");
            view->mouseWheelMove (mouse (*view, node, node, false), wheel (0.0f, 0.3f));
            expectEquals (value (h, qId), atRest, "horizontal scrolling is not hijacked");
        }

        beginTest ("Hover info card sits next to its control and stays inside the window");
        {
            juce::Component parent;
            parent.setBounds (0, 0, ui::baseWidth, ui::baseHeight);
            ui::InfoPopup info;
            parent.addChildComponent (info);

            const juce::Rectangle<int> anchor (600, 400, 90, 110);
            info.show (anchor, "Presence", "25 %", "Brings the voice to the front: clearer top end, harsh S sounds kept in check.");
            expect (info.isVisible());
            expect (info.getBottom() <= anchor.getY() + 8, "above the control");
            expect (parent.getLocalBounds().contains (info.getBounds()));

            // Near the top edge it flips below the control
            const juce::Rectangle<int> top (20, 10, 90, 110);
            info.show (top, "Gain", "0.0 dB", "Input level");
            expect (info.getY() >= top.getBottom(), "below the control");
            expect (parent.getLocalBounds().contains (info.getBounds()));
        }
    }
};

static UiTests uiTests;
