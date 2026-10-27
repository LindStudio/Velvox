// The common module panel frame (spec 2.4, style A "Grafik zuerst"): header, large visual, one row of
// 3 - 5 main controls, everything else in the "Mehr" drawer. No parameter may become unreachable.

#include "TestHelpers.h"
#include "PluginEditor.h"
#include "UI/UserSettings.h"
#include "Text/I18n.h"
#include "UI/EqPanel.h"
#include "UI/VoicesPanels.h"

using namespace velvox;
using namespace velvox::test;
using dsp::ModuleId;

namespace
{
    juce::String paramOf (juce::Component* c)
    {
        if (auto* k = dynamic_cast<ui::Knob*> (c)) return k->getParamId();
        if (auto* s = dynamic_cast<ui::Segments*> (c)) return s->getParamId();
        if (auto* t = dynamic_cast<ui::Toggle*> (c)) return t->getParamId();
        if (auto* ch = dynamic_cast<ui::Choice*> (c)) return ch->getParamId();
        // Graph controls of their own (e.g. the Pulse shape tiles) name the parameter they edit
        if (const auto& id = c->getProperties()["paramId"]; id.isString()) return id.toString();
        return {};
    }

    void collectParams (juce::Component& c, juce::StringArray& out)
    {
        for (auto* child : c.getChildren())
        {
            if (const auto id = paramOf (child); id.isNotEmpty())
                out.addIfNotAlreadyThere (id);
            // Graph handles of the visual (e.g. Smooth's weighting points and limit edges; their own panel
            // tests check that every listed parameter really is edited by the graph)
            if (auto* view = dynamic_cast<ui::ModuleView*> (child))
                for (const auto& id : view->getEditedParameters())
                    out.addIfNotAlreadyThere (id);
            if (auto* ch = dynamic_cast<ui::Choice*> (child))
                juce::ignoreUnused (ch);
            collectParams (*child, out);
        }
    }

    juce::Rectangle<int> panelSize() { return VelvoxAudioProcessorEditor::panelArea().withZeroOrigin(); }

    void allModulesInChain (Harness& h)
    {
        auto r = h.proc.getRouting();
        for (int id = 0; id < dsp::numModules; ++id)
            if (! r.contains ((ModuleId) id))
                r.insertDefault ((ModuleId) id, 0);
        h.proc.setRouting (r);
    }

    // Every parameter a panel must reach (instance 1), by module: all of the module's parameters
    juce::StringArray moduleParams (ModuleId id)
    {
        juce::StringArray ids;
        for (const auto& spec : getParameterSpecs())
            if (moduleOfParameter (spec) == id && dsp::splitInstanceParamId (spec.id).second == 0)
                ids.add (spec.id);
        return ids;
    }
}

class PanelFrameTests final : public juce::UnitTest
{
public:
    PanelFrameTests() : juce::UnitTest ("Headliner panel frame", "Headliner") {}

    void runTest() override
    {
        beginTest ("Every panel: 3 - 5 main controls, the rest in Mehr, every parameter of the module reachable");
        {
            Harness h;
            allModulesInChain (h);
            for (int id = -1; id < dsp::numModules; ++id)
            {
                auto panel = ui::createDetailPanel (h.proc, id);
                panel->setBounds (panelSize());
                const auto name = juce::String (id < 0 ? "output" : dsp::moduleKey ((ModuleId) id));
                const auto& main = panel->getMainControls();
                // 0.7: the single-parameter clean-up modules have one big knob in the visual and no main row
                const bool single = id == (int) ModuleId::denoise || id == (int) ModuleId::breath;
                if (single)
                    expect (main.empty(), name + ": no main row");
                else
                    expect (main.size() >= 3 && main.size() <= 5, name + ": " + juce::String ((int) main.size()) + " main controls");

                // Main knobs are large, Mehr knobs small - never a grid of equal knobs
                for (auto* c : main)
                    if (auto* k = dynamic_cast<ui::Knob*> (c))
                        expect (k->getDialSize() >= 50, name + ": main knob large");
                for (auto* c : panel->getMoreControls())
                    if (auto* k = dynamic_cast<ui::Knob*> (c))
                        expect (k->getDialSize() <= 34, name + ": Mehr knob small");

                juce::StringArray reached;
                collectParams (*panel, reached);
                if (id >= 0)
                    for (auto& pid : moduleParams ((ModuleId) id))
                    {
                        const bool isOnSwitch = pid == ui::ChainView::onParamFor ((ModuleId) id);
                        // The six Pulse curve points are the handles of the curve editor in the visual
                        const bool curveHandle = pid.startsWith ("pulseX") || pid.startsWith ("pulseY");
                        // EQ: the 16 bands are reached one at a time through the graph and the selected-band row,
                        // eqLegacyResponse through its Mehr button while it is set (both: "Headliner EQ panel");
                        // the hidden 0.5 fixed bands are not shown at all (they stay registered)
                        const bool eqBand = pid.startsWith ("eqBand") || pid == "eqLegacyResponse";
                        const auto* spec = findParameterSpec (pid);
                        if (id == (int) ModuleId::eq && ! eqBand && spec != nullptr && spec->hidden)
                        {
                            expect (! reached.contains (pid), name + ": hidden 0.5 band not shown: " + pid);
                            continue;
                        }
                        expect (isOnSwitch || curveHandle || eqBand || reached.contains (pid), name + ": reaches " + pid);
                    }
                expect (panel->getMoreButton().isVisible() == ! panel->getMoreControls().empty(), name + ": Mehr only when there is more");
            }
        }

        // In both interface languages (labels differ in length)
        for (auto language : { i18n::Language::german, i18n::Language::english })
        {
            const bool english = language == i18n::Language::english;
            beginTest (juce::String ("Layout: header, visual, main row; nothing outside, nothing tiny - Mehr closed and open")
                       + (english ? " (English)" : ""));
            const i18n::ScopedLanguage scoped (language);
            Harness h;
            allModulesInChain (h);
            for (int id = -1; id < dsp::numModules; ++id)
            {
                auto panel = ui::createDetailPanel (h.proc, id);
                panel->setBounds (panelSize());
                const auto name = juce::String (id < 0 ? "output" : dsp::moduleKey ((ModuleId) id));
                for (bool open : { false, true })
                {
                    panel->setMoreOpen (open);
                    const auto visual = panel->getVisualArea();
                    expect (visual.getHeight() >= 150, name + ": large visual " + visual.toString());
                    expect (visual.getY() >= panel->getHeaderArea().getBottom(), name + ": visual under the header");
                    if (! panel->getMainControls().empty())   // 0.7 single-control panels have no main row
                        expect (panel->getMainRowArea().getY() >= visual.getBottom(), name + ": main row under the visual");
                    for (auto* c : panel->getMainControls())
                        expect (panel->getMainRowArea().contains (c->getBounds().withTrimmedBottom (std::max (0, c->getBottom() - panel->getMainRowArea().getBottom()))),
                                name + ": main control in the main row " + c->getBounds().toString());

                    std::function<void (juce::Component&, juce::Rectangle<int>)> check = [&] (juce::Component& parent, juce::Rectangle<int> bounds)
                    {
                        for (auto* child : parent.getChildren())
                        {
                            if (! child->isVisible() || dynamic_cast<juce::Viewport*> (child) != nullptr)
                                continue;
                            expect (child->getHeight() >= 14 && child->getWidth() >= 14, name + ": too small " + child->getBounds().toString());
                            expect (bounds.contains (child->getBounds()), name + (open ? " (Mehr open)" : "") + ": outside " + child->getBounds().toString());
                        }
                    };
                    check (*panel, panel->getLocalBounds());
                    expect (panel->getMoreDrawer().isVisible() == open, name + ": drawer state");
                    if (open)
                        check (panel->getMoreDrawer(), panel->getMoreDrawer().getLocalBounds());
                }
            }
        }

        beginTest ("Delay / reverb as a send: SEND in the header and on the Mix knob");
        {
            Harness h;
            for (auto id : { ModuleId::delay, ModuleId::reverb })
            {
                auto r = h.proc.getRouting();
                if (! r.contains (id))
                    r.insertDefault (id, 0);
                r.setInsert (id, 0, true);
                h.proc.setRouting (r);
                auto panel = ui::createDetailPanel (h.proc, (int) id);
                panel->setBounds (panelSize());
                panel->refresh();
                expect (panel->getHeaderBadge() != "SEND", "insert: no SEND badge");
                const auto mixParam = juce::String (id == ModuleId::delay ? pid::delayMix : pid::reverbMix);
                ui::Knob* mix = nullptr;
                for (auto* c : panel->getMainControls())
                    if (auto* k = dynamic_cast<ui::Knob*> (c); k != nullptr && k->getParamId() == mixParam)
                        mix = k;
                expect (mix != nullptr, "Mix is a main control");
                if (mix == nullptr)
                    continue;
                expectEquals (mix->getLabel(), juce::String ("MIX"));

                r = h.proc.getRouting();
                r.setInsert (id, 0, false);
                h.proc.setRouting (r);
                panel->refresh();
                expect (panel->isSend());
                expectEquals (panel->getHeaderBadge(), juce::String ("SEND"));
                expectEquals (mix->getLabel(), juce::String ("SEND"), "the knob reads SEND");
                expect (mix->getDescription().contains ("Send"), "and explains it");
            }
        }

        beginTest ("Old mix law: \"Alte Mix-Kurve – umstellen\" in Mehr and in the panel menu");
        {
            Harness h;
            allModulesInChain (h);
            auto r = h.proc.getRouting();
            r.setInsert (ModuleId::delay, 0, true);
            h.proc.setRouting (r);
            const auto action = juce::String::fromUTF8 ("Alte Mix-Kurve – umstellen");
            for (auto id : { ModuleId::delay, ModuleId::doubler, ModuleId::smash, ModuleId::comp })
            {
                if (id != ModuleId::comp)
                    h.proc.setLegacyMixLaw (id, 0, true);
                auto panel = ui::createDetailPanel (h.proc, (int) id);
                panel->setBounds (panelSize());
                panel->refresh();
                juce::PopupMenu menu;
                panel->addPanelItems (menu);
                bool inMenu = false;
                for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                    inMenu = inMenu || it.getItem().text == action;
                const auto name = juce::String (dsp::moduleKey (id));
                if (id == ModuleId::comp)
                {
                    expect (! inMenu && ! panel->showsLegacyMixLawAction(), "compressor has no mix law");
                    continue;
                }
                expect (inMenu && panel->showsLegacyMixLawAction(), name + ": offered while on the old law");
                panel->setMoreOpen (true);
                expect (panel->getLegacyMixLawButton() != nullptr && panel->getLegacyMixLawButton()->isVisible(), name + ": button in Mehr");
                if (auto* b = panel->getLegacyMixLawButton())
                    b->onClick();
                expect (! h.proc.usesLegacyMixLaw (id, 0), name + ": switched to the true law");
                panel->refresh();
                expect (! panel->showsLegacyMixLawAction(), name + ": gone afterwards");
            }

            // A delay on the old law that runs as a send: nothing to switch
            h.proc.setLegacyMixLaw (ModuleId::delay, 0, true);
            r = h.proc.getRouting();
            r.setInsert (ModuleId::delay, 0, false);
            h.proc.setRouting (r);
            auto panel = ui::createDetailPanel (h.proc, (int) ModuleId::delay);
            panel->setBounds (panelSize());
            panel->refresh();
            expect (! panel->showsLegacyMixLawAction(), "not for a send");
        }

        beginTest ("Panel menu: reset module and save a module preset; controls inside get the panel items");
        {
            Harness h;
            auto panel = ui::createDetailPanel (h.proc, (int) ModuleId::comp);
            panel->setBounds (panelSize());
            h.set (pid::compRatio, 9.0f);
            panel->resetModule();
            expectWithinAbsoluteError (h.get (pid::compRatio), 3.0f, 1.0e-3f, "reset");
            expect (h.get (pid::compOn) > 0.5f, "stays on");
            expectEquals (h.proc.getUndo().getUndoName(), juce::String::fromUTF8 ("Compressor zurücksetzen"), "one undo step");

            h.set (pid::compRatio, 6.0f);
            panel->saveModulePreset ("Shell Test Preset");
            expect (ui::settings::getString ("userModulePresets").contains ("Shell Test Preset"), "stored in the user settings");

            for (auto* c : panel->getMainControls())
                if (auto* k = dynamic_cast<ui::Knob*> (c))
                    expect (k->slider.findParentComponentOfClass<ui::gestures::PanelMenuHost>() == panel.get(), "knob menu gets the panel items");
        }

        beginTest ("Module panels in English: knob and switch names, no German left");
        {
            const i18n::ScopedLanguage english (i18n::Language::english);
            Harness h;
            allModulesInChain (h);

            // Knob names / switch texts / labels of a panel (Mehr included), by parameter where there is one
            std::function<void (juce::Component&, std::map<juce::String, juce::String>&, juce::StringArray&)> collect;
            collect = [&] (juce::Component& parent, std::map<juce::String, juce::String>& byParam, juce::StringArray& texts)
            {
                for (auto* c : parent.getChildren())
                {
                    if (auto* k = dynamic_cast<ui::Knob*> (c))
                    {
                        byParam[k->getParamId()] = k->getLabel();
                        texts.add (k->getLabel());
                        texts.add (k->getDescription());
                    }
                    else if (auto* t = dynamic_cast<ui::Toggle*> (c))
                    {
                        byParam[t->getParamId()] = t->getButtonText();
                        texts.add (t->getButtonText());
                    }
                    else if (auto* l = dynamic_cast<juce::Label*> (c))
                        texts.add (l->getText());
                    collect (*c, byParam, texts);
                }
            };

            const auto isGerman = [] (const juce::String& s)
            {
                if (s.containsAnyOf (juce::String::fromUTF8 ("äöüÄÖÜß")))
                    return true;
                for (auto* word : { "AUSGLEICH", "ZIEL", "TIEFE", "STIMME", "RAUM", "MODUS", "ERKENNUNG", "FREQUENZ",
                                    "HALBT", "FEIN", "FORMANTEN", "MUSTER", "DICHTE", "BEWEGUNG", "GRENZE", "IGNORIEREN", "KEIN BAND",
                                    "ZIEHEN", "LATENZ", "ANTEIL" })
                    if (s.toUpperCase().contains (word))
                        return true;
                return false;
            };

            std::map<juce::String, juce::String> all;
            for (int id = 0; id < dsp::numModules; ++id)
            {
                if (id == (int) ModuleId::check)
                    continue;   // the Vocal Check panel is not part of this group
                auto panel = ui::createDetailPanel (h.proc, id);
                panel->setBounds (panelSize());
                panel->refresh();
                std::map<juce::String, juce::String> byParam;
                juce::StringArray texts;
                collect (*panel, byParam, texts);
                for (const auto& t : texts)
                    expect (! isGerman (t), juce::String (dsp::moduleKey ((ModuleId) id)) + ": German text in English: " + t);
                all.insert (byParam.begin(), byParam.end());
            }

            const auto label = [&] (const char* id) { return all.count (id) > 0 ? all[id] : juce::String ("<missing " + juce::String (id) + ">"); };
            expectEquals (label (pid::rideAmount), juce::String ("LEVELING"));
            expectEquals (label (pid::rideTarget), juce::String ("TARGET"));
            expectEquals (label (pid::rideResponse), juce::String ("SPEED"));
            expectEquals (label (pid::rideSilence), juce::String ("IGNORE BELOW"));
            expectEquals (label (pid::deessFreq), juce::String ("FREQUENCY"));
            expectEquals (label (pid::smoothAmount), juce::String ("DEPTH"));
            expectEquals (label (pid::smoothSensitivity), juce::String ("SHARPNESS"));
            expectEquals (label (pid::delayTime), juce::String ("TIME"));
            expectEquals (label (pid::reverbSize), juce::String ("SIZE"));
            expectEquals (label (pid::satMix), juce::String ("MIX"));
            expectEquals (label (pid::doublerModDepth), juce::String ("MOD DEPTH"));
            expectEquals (ui::eqedit::roleTag ((int) dsp::EqRole::air), juce::String ("AIR"));
            expectEquals (ui::eqedit::roleTag ((int) dsp::EqRole::mud), juce::String ("MUD"));
            expectEquals (ui::doubleredit::valueText (ui::doubleredit::Field::on, 0.0f), juce::String ("Off"));
        }

        beginTest ("De-Esser Range reads as a reduction");
        {
            Harness h;
            auto r = h.proc.getRouting();
            if (! r.contains (ModuleId::deess))
                r.insertDefault (ModuleId::deess, 0);
            h.proc.setRouting (r);
            h.set (pid::deessRange, 9.0f);
            auto panel = ui::createDetailPanel (h.proc, (int) ModuleId::deess);
            panel->setBounds (panelSize());
            ui::Knob* range = nullptr;
            for (auto* c : panel->getMainControls())
                if (auto* k = dynamic_cast<ui::Knob*> (c); k != nullptr && k->getParamId() == pid::deessRange)
                    range = k;
            expect (range != nullptr, "Range is a main control");
            if (range != nullptr)
            {
                expectEquals (range->slider.getTextFromValue (9.0), juce::String::fromUTF8 ("−9.0 dB"));
                expectWithinAbsoluteError ((float) range->slider.getValueFromText ("-6"), 6.0f, 1.0e-4f, "typed negative");
                expectWithinAbsoluteError ((float) range->slider.getValueFromText ("6"), 6.0f, 1.0e-4f, "typed positive");
            }
        }
    }
};

static PanelFrameTests panelFrameTests;
