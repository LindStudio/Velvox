// Interface language (1.0): German / English, the computer's language by default,
// chosen on the first-start welcome card and in the settings sheet.
#include "TestHelpers.h"
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/Text/I18n.h"
#include "../Source/UI/UserSettings.h"

using namespace velvox;

namespace
{
    // Runs the message loop briefly (the language switch rebuilds the view asynchronously)
    void pumpMessages (int ms = 30)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    }

    struct SettingsRestore
    {
        ~SettingsRestore()
        {
            ui::settings::set (ui::settings::language, "de");
            ui::settings::set (ui::settings::tutorialDone, true);
            ui::settings::applyLanguage();
        }
    };
}

class I18nTests final : public juce::UnitTest
{
public:
    I18nTests() : juce::UnitTest ("Interface language", "Velvox") {}

    void runTest() override
    {
        using i18n::Language;

        beginTest ("tr picks the current language");
        {
            i18n::ScopedLanguage de (Language::german);
            expectEquals (i18n::tr ("Schließen", "Close"), juce::String::fromUTF8 ("Schließen"));
            i18n::ScopedLanguage en (Language::english);
            expectEquals (i18n::tr ("Schließen", "Close"), juce::String ("Close"));
        }

        beginTest ("Packed text keeps both languages, also concatenated with plain text");
        {
            const auto s = i18n::both ("Präsenz ", "Presence ") + "+2 dB" + i18n::both (" bei ", " at ") + "3 kHz";
            expect (i18n::isPacked (s));
            expectEquals (i18n::show (s, Language::german), juce::String::fromUTF8 ("Präsenz +2 dB bei 3 kHz"));
            expectEquals (i18n::show (s, Language::english), juce::String ("Presence +2 dB at 3 kHz"));
            expectEquals (i18n::show ("Low Cut 92 Hz", Language::english), juce::String ("Low Cut 92 Hz"));   // plain stays
            expect (! i18n::isPacked (i18n::both ("EQ", "EQ")));                                               // equal = plain
        }

        beginTest ("Packed text survives the project state (XML)");
        {
            juce::ValueTree t ("Report");
            t.setProperty ("title", i18n::both ("Höhen sanfter", "Softer highs"), nullptr);
            const auto restored = juce::ValueTree::fromXml (t.toXmlString());
            expectEquals (i18n::show (restored["title"].toString(), Language::english), juce::String ("Softer highs"));
            expectEquals (i18n::show (restored["title"].toString(), Language::german), juce::String::fromUTF8 ("Höhen sanfter"));
        }

        beginTest ("format and numbers");
        {
            expectEquals (i18n::format ("{0} von {1}", { "3", "14" }), juce::String ("3 von 14"));
            expectEquals (i18n::number (3.26, 1, Language::german), juce::String ("3,3"));
            expectEquals (i18n::number (3.26, 1, Language::english), juce::String ("3.3"));
            expectEquals (i18n::number (12.6, 0, Language::english), juce::String ("13"));
            expectEquals (i18n::number (-0.2, 0, Language::german), juce::String ("0"));
            expectEquals (i18n::show (i18n::numberBoth (1.5, 1), Language::german), juce::String ("1,5"));
        }

        beginTest ("Preference: system follows the computer, de / en are fixed");
        {
            expect (i18n::resolve (i18n::Preference::german) == Language::german);
            expect (i18n::resolve (i18n::Preference::english) == Language::english);
            expect (i18n::resolve (i18n::Preference::system) == i18n::systemLanguage());
            expect (i18n::preferenceFromString ("de") == i18n::Preference::german);
            expect (i18n::preferenceFromString ("en") == i18n::Preference::english);
            expect (i18n::preferenceFromString ("") == i18n::Preference::system);
            expect (i18n::preferenceFromString ("system") == i18n::Preference::system);
        }

        beginTest ("Settings: never chosen = system; a choice is stored and applied");
        {
            SettingsRestore restore;
            ui::settings::set (ui::settings::language, "en");
            expect (ui::settings::hasLanguageChoice());
            ui::settings::applyLanguage();
            expect (i18n::current() == Language::english);
            ui::settings::setLanguagePreference (i18n::Preference::german);
            expect (i18n::current() == Language::german);
            expectEquals (ui::settings::getString (ui::settings::language), juce::String ("de"));
        }

        beginTest ("First start: the welcome card asks for the language; picking one switches the interface");
        {
            SettingsRestore restore;
            ui::settings::getFile().deleteFile();          // a fresh computer: nothing chosen yet
            ui::settings::set (ui::settings::tutorialDone, false);
            expect (! ui::settings::hasLanguageChoice());

            auto procPtr = std::make_unique<VelvoxAudioProcessor>(); auto& proc = *procPtr;
            std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
            auto& ed = dynamic_cast<VelvoxAudioProcessorEditor&> (*editor);
            expect (ed.getTutorial().isActive(), "welcome card shown on the very first start");
            expectEquals (ed.getTutorial().getStep(), -1);

            ed.getTutorial().getLanguageButton (Language::english).triggerClick(); pumpMessages();
            pumpMessages();
            expect (i18n::current() == Language::english);
            expectEquals (ui::settings::getString (ui::settings::language), juce::String ("en"));
            expect (ed.getTutorial().isActive(), "the welcome card is shown again, now in English");
            expectEquals (ed.getTutorial().getAnalyseButton().getButtonText(), juce::String ("Analyze vocal"));
            expect (ed.getTutorial().getLanguageButton (Language::english).getToggleState());

            ed.getTutorial().getLanguageButton (Language::german).triggerClick(); pumpMessages();
            pumpMessages();
            expectEquals (ed.getTutorial().getAnalyseButton().getButtonText(), juce::String ("Vocal analysieren"));

            ed.getTutorial().getPluginButton().triggerClick(); pumpMessages();
            expect (! ed.getTutorial().isActive());
            expect (ui::settings::hasLanguageChoice());
        }

        beginTest ("Leaving the welcome card without a choice keeps the computer's language");
        {
            SettingsRestore restore;
            ui::settings::getFile().deleteFile();
            auto procPtr = std::make_unique<VelvoxAudioProcessor>(); auto& proc = *procPtr;
            std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
            auto& ed = dynamic_cast<VelvoxAudioProcessorEditor&> (*editor);
            expect (i18n::current() == i18n::systemLanguage());
            ed.getTutorial().getPluginButton().triggerClick(); pumpMessages();
            expectEquals (ui::settings::getString (ui::settings::language), juce::String ("system"));
        }

        beginTest ("Settings sheet: language switch rebuilds the interface and keeps the view");
        {
            SettingsRestore restore;
            ui::settings::set (ui::settings::language, "de");
            ui::settings::set (ui::settings::tutorialDone, true);

            auto procPtr = std::make_unique<VelvoxAudioProcessor>(); auto& proc = *procPtr;
            proc.getAutoMix().dismissPrompt();
            std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
            auto& ed = dynamic_cast<VelvoxAudioProcessorEditor&> (*editor);
            ed.setMode (VelvoxAudioProcessorEditor::Mode::advanced);
            const auto sizeBefore = ed.getBounds();

            ed.getTopBar().getSettingsButton().triggerClick(); pumpMessages();
            expect (ed.getSettings().isOpen());
            expect (ed.getSettings().getLanguageButton (i18n::Preference::german).getToggleState());

            ed.getSettings().getLanguageButton (i18n::Preference::english).triggerClick(); pumpMessages();
            pumpMessages();
            expect (i18n::current() == Language::english);
            expect (ed.getSettings().isOpen(), "the settings sheet stays open after the rebuild");
            expect (ed.getSettings().getLanguageButton (i18n::Preference::english).getToggleState());
            expect (ed.isAdvanced(), "the view mode survives the rebuild");
            expect (ed.getBounds() == sizeBefore, "the window keeps its size");

            ed.getSettings().getLanguageButton (i18n::Preference::system).triggerClick(); pumpMessages();
            pumpMessages();
            expectEquals (ui::settings::getString (ui::settings::language), juce::String ("system"));
            expect (i18n::current() == i18n::systemLanguage());
        }

        beginTest ("Settings sheet: start view, analysis popup and info cards are stored");
        {
            SettingsRestore restore;
            auto procPtr = std::make_unique<VelvoxAudioProcessor>(); auto& proc = *procPtr;
            std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
            auto& ed = dynamic_cast<VelvoxAudioProcessorEditor&> (*editor);
            ed.showSettings();
            auto& s = ed.getSettings();

            s.getModeButton (true).triggerClick(); pumpMessages();
            expectEquals (ui::settings::getString (ui::settings::defaultMode), juce::String ("advanced"));
            s.getOfferAnalysisToggle().triggerClick(); pumpMessages();
            expect (! ui::settings::getBool (ui::settings::offerAnalysis, true));
            s.getInfoCardsToggle().triggerClick(); pumpMessages();
            expect (! ui::settings::getBool (ui::settings::infoCards, true));

            // A new instance opens in the stored view and without the analysis popup
            auto proc2Ptr = std::make_unique<VelvoxAudioProcessor>(); auto& proc2 = *proc2Ptr;
            std::unique_ptr<juce::AudioProcessorEditor> editor2 (proc2.createEditor());
            auto& ed2 = dynamic_cast<VelvoxAudioProcessorEditor&> (*editor2);
            expect (ed2.isAdvanced());
            expect (! ed2.getAnalysisOverlay().isVisible());

            ui::settings::set (ui::settings::defaultMode, "simple");
            ui::settings::set (ui::settings::offerAnalysis, true);
            ui::settings::set (ui::settings::infoCards, true);
        }
    }
};

static I18nTests i18nTests;
