// Preset list (spec 2.8): ‹ name ▾ ›, categories (factory + Eigene + Favoriten), search, favourites kept
// in the user settings, hover description, double-click / Enter loads, save / save as / rename / delete.

#include "TestHelpers.h"
#include "PluginEditor.h"
#include "UI/PresetBrowser.h"
#include "UI/UserSettings.h"
#include "Text/I18n.h"

using namespace velvox;
using namespace velvox::test;

namespace
{
    juce::MouseEvent mouseAt (juce::Component& c, juce::Point<int> pos, int clicks = 1)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos.toFloat(), {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &c, &c, now, pos.toFloat(), now, clicks, false);
    }

    int indexOf (PresetManager& pm, const juce::String& name, bool factory)
    {
        for (int i = 0; i < pm.getEntries().size(); ++i)
            if (pm.getEntries()[i].name == name && pm.getEntries()[i].isFactory == factory)
                return i;
        return -1;
    }

    struct Fixture
    {
        Harness h;
        juce::TemporaryFile dirFile;
        juce::File dir { dirFile.getFile() };
        std::unique_ptr<PresetManager> pm;
        ui::DialogOverlay dialog;
        std::unique_ptr<ui::PresetBrowser> browser;

        Fixture()
        {
            dir.createDirectory();
            pm = std::make_unique<PresetManager> (h.proc.getState(), dir);
            pm->setUndoHistory (&h.proc.getUndo());
            browser = std::make_unique<ui::PresetBrowser> (*pm, dialog);
            browser->setBounds (0, 0, ui::baseWidth, ui::baseHeight);
            browser->open();
        }
        ~Fixture() { dir.deleteRecursively(); }
    };
}

class PresetBrowserTests final : public juce::UnitTest
{
public:
    PresetBrowserTests() : juce::UnitTest ("Headliner preset list", "Headliner") {}

    void runTest() override
    {
        ui::settings::set (ui::settings::presetFavourites, juce::String());

        beginTest ("Header: ‹ name ▾ › opens the preset list");
        {
            Harness h;
            h.proc.getAutoMix().dismissPrompt();
            std::unique_ptr<juce::AudioProcessorEditor> e (h.proc.createEditor());
            auto& ed = dynamic_cast<VelvoxAudioProcessorEditor&> (*e);
            auto& top = ed.getTopBar();
            expect (top.getPrevButton().getX() < top.getPresetButton().getX() && top.getPresetButton().getRight() < top.getNextButton().getRight(),
                    "‹ name ›");
            top.getPresetButton().onClick();
            expect (ed.getPresetBrowser().isOpen(), "the list opens");
        }

        beginTest ("Categories: factory categories, Eigene, Favoriten; search filters");
        {
            Fixture f;
            const auto cats = f.browser->getCategories();
            expectEquals (cats[0], ui::PresetBrowser::allCategory());
            expect (cats.contains ("Eigene") && cats.contains ("Favoriten"), cats.joinIntoString (","));
            expectEquals ((int) f.browser->visibleEntries().size(), f.pm->getEntries().size(), "Alle");
            for (auto& c : cats)
                if (c != "Alle" && c != "Eigene" && c != "Favoriten")
                {
                    f.browser->setCategory (c);
                    expect (! f.browser->visibleEntries().empty(), "factory category " + c);
                    for (auto i : f.browser->visibleEntries())
                        expectEquals (f.pm->getEntries()[i].category, c);
                }
            f.browser->setCategory ("Eigene");
            expect (f.browser->visibleEntries().empty(), "no user presets yet");
            f.browser->setCategory ("Alle");
            f.browser->setSearchText ("hard dance");
            expect (! f.browser->visibleEntries().empty() && (int) f.browser->visibleEntries().size() < f.pm->getEntries().size(), "search narrows");
            f.browser->setSearchText ("zzzzqq");
            expect (f.browser->visibleEntries().empty());
        }

        beginTest ("Favourites: star toggles, kept in the user settings, Favoriten lists them");
        {
            Fixture f;
            const int i = f.browser->visibleEntries()[2];
            f.browser->mouseDown (mouseAt (*f.browser, f.browser->starBounds (i).getCentre()));
            expect (f.browser->isFavourite (i), "star click");
            expect (ui::settings::getString (ui::settings::presetFavourites).contains (f.pm->getEntries()[i].name), "persisted");
            f.browser->setCategory ("Favoriten");
            expect (f.browser->visibleEntries() == std::vector<int> { i });
            f.browser->toggleFavourite (i);
            expect (f.browser->visibleEntries().empty(), "un-starred");
        }

        beginTest ("Hover shows the description; one click loads and keeps the list open (one undo step), Enter loads and closes");
        {
            Fixture f;
            const int hd = indexOf (*f.pm, "Hard Dance Lead", true);
            expect (hd >= 0);
            const auto row = f.browser->rowBounds (hd);
            f.browser->mouseMove (mouseAt (*f.browser, row.getCentre()));
            expect (f.browser->getInfoText().isNotEmpty() && f.browser->getInfoText() == i18n::show (f.pm->getEntries()[hd].description), "description");
            {
                i18n::ScopedLanguage en (i18n::Language::english);
                expect (f.browser->getInfoText().startsWith ("Level-ridden"), "the English description: " + f.browser->getInfoText());
            }
            f.browser->mouseDown (mouseAt (*f.browser, row.getCentre()));
            expectEquals (f.pm->getCurrentPresetName(), juce::String ("Hard Dance Lead"), "one click loads");
            expect (f.browser->isOpen(), "and the list stays open");
            expectEquals (f.h.proc.getUndo().getNumSteps(), 1, "one undo step");
            f.browser->mouseDown (mouseAt (*f.browser, row.getCentre()));
            f.browser->mouseDoubleClick (mouseAt (*f.browser, row.getCentre(), 2));
            expect (f.browser->isOpen(), "a double-click keeps it open too");
            expectEquals (f.h.proc.getUndo().getNumSteps(), 1, "the loaded preset is not loaded again");
            f.browser->mouseDown (mouseAt (*f.browser, { 2, 2 }));
            expect (! f.browser->isOpen(), "a click outside closes");

            f.browser->open();
            f.browser->moveSelection (1);
            const int next = f.browser->getSelectedEntry();
            expect (f.browser->keyPressed (juce::KeyPress (juce::KeyPress::returnKey)));
            expectEquals (f.pm->getCurrentIndex(), next, "Enter loads the selection");
            f.browser->open();
            f.browser->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
            expect (! f.browser->isOpen(), "Esc closes");
        }

        beginTest ("Save as, save, rename, delete user presets (factory presets cannot be renamed)");
        {
            Fixture f;
            f.h.set (pid::compRatio, 7.0f);
            expect (f.browser->saveAs ("My Lead"));
            const int mine = indexOf (*f.pm, "My Lead", false);
            expect (mine >= 0 && f.pm->getCurrentPresetName() == "My Lead");
            f.browser->setCategory ("Eigene");
            expect (f.browser->visibleEntries() == std::vector<int> { mine });

            f.h.set (pid::compRatio, 5.0f);
            expect (f.browser->saveCurrent(), "overwrites the loaded user preset");
            f.h.set (pid::compRatio, 2.0f);
            f.pm->loadPreset (indexOf (*f.pm, "My Lead", false));
            expectWithinAbsoluteError (f.h.get (pid::compRatio), 5.0f, 0.01f);

            f.browser->toggleFavourite (indexOf (*f.pm, "My Lead", false));
            f.browser->selectEntry (indexOf (*f.pm, "My Lead", false));
            expect (f.browser->getRenameButton().isEnabled() && f.browser->getDeleteButton().isEnabled());
            expect (f.browser->renameSelected ("Party Lead"));
            const int renamed = indexOf (*f.pm, "Party Lead", false);
            expect (renamed >= 0 && indexOf (*f.pm, "My Lead", false) < 0, "renamed");
            expectEquals (f.pm->getCurrentPresetName(), juce::String ("Party Lead"), "the loaded name follows");
            expect (f.browser->isFavourite (renamed), "the star follows");

            f.browser->selectEntry (indexOf (*f.pm, "Hard Dance Lead", true));
            expect (! f.browser->getRenameButton().isEnabled() && ! f.browser->renameSelected ("X"), "factory preset");
            expect (! f.browser->deleteSelected());

            f.browser->selectEntry (renamed);
            expect (f.browser->deleteSelected());
            expect (indexOf (*f.pm, "Party Lead", false) < 0, "deleted");
        }
        ui::settings::set (ui::settings::presetFavourites, juce::String());
    }
};

static PresetBrowserTests presetBrowserTests;
