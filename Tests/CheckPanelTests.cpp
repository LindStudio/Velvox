// VOCAL CHECK view of 0.6 (spec 5.15, "Check 1 · score + tiles"): score ring, genre, "Alles anwenden",
// one tile per finding with "Ignorieren" / "Anwenden", the "x ignoriert" row, the compact summary in the
// Check module panel and the shell integration (both modes, undo).

#include "TestHelpers.h"
#include "SyntheticVocal.h"
#include "PluginEditor.h"
#include "UI/VocalCheckView.h"
#include "UI/ModuleBrowser.h"
#include "UI/TopBar.h"
#include "UI/SimpleView.h"
#include "UI/UserSettings.h"
#include "StateKeys.h"
#include "Text/I18n.h"

using namespace velvox;
using namespace velvox::test;
using namespace velvox::dsp;

namespace
{
    // A take with clear problems: rumble, a harsh resonance, plosive-like low end
    automix::CapturedAudio problemTake()
    {
        VocalSpec s;
        s.seconds = 14.0;
        s.sampleRate = 48000.0;
        s.channels = 2;
        s.rumbleDb = -26.0f;
        s.resonanceHz = 3150.0f;
        s.resonanceDb = 16.0f;
        return makeVocal (s);
    }

    // Blocks through the processor, pumping the analyzer like its timer (no refresh forced)
    void feedBlocks (Harness& h, const automix::CapturedAudio& v)
    {
        juce::AudioBuffer<float> block (2, 512);
        juce::MidiBuffer midi;
        for (int start = 0; start + 512 <= v.numSamples(); start += 512)
        {
            for (int ch = 0; ch < 2; ++ch)
                block.copyFrom (ch, 0, v.vocal[(size_t) std::min (ch, (int) v.vocal.size() - 1)].data() + start, 512);
            h.proc.processBlock (block, midi);
            if ((start / 512) % 8 == 0)
                h.proc.pumpAnalyzer();
        }
    }

    void feed (Harness& h, const automix::CapturedAudio& v)
    {
        juce::AudioBuffer<float> block (2, 512);
        juce::MidiBuffer midi;
        for (int start = 0; start + 512 <= v.numSamples(); start += 512)
        {
            for (int ch = 0; ch < 2; ++ch)
                block.copyFrom (ch, 0, v.vocal[(size_t) std::min (ch, (int) v.vocal.size() - 1)].data() + start, 512);
            h.proc.processBlock (block, midi);
            if ((start / 512) % 8 == 0)
                h.proc.pumpAnalyzer();
        }
        h.proc.pumpAnalyzer();
        h.proc.refreshAnalysis();
    }

    // Check on in capture mode, EQ off, a finished capture of the problem take
    void analysedTake (Harness& h)
    {
        h.neutral();
        h.set (pid::checkOn, 1.0f);
        h.set (pid::checkMode, 1.0f);
        h.set (pid::eqOn, 0.0f);
        h.proc.setRouting (Routing::fromString ("v2,comp,check"));
        h.proc.pumpAnalyzer();
        h.proc.getAnalyzer().startCapture();
        feed (h, problemTake());
        h.proc.getAnalyzer().stopCapture();
        h.proc.refreshAnalysis();
    }

    void click (juce::Button& b)
    {
        if (b.onClick)
            b.onClick();
    }

    void runLoop (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }
}

class CheckPanelTests final : public juce::UnitTest
{
public:
    CheckPanelTests() : juce::UnitTest ("Headliner Vocal Check view", "Headliner") {}

    void runTest() override
    {
        view();
        shell();
    }

    void view()
    {
        using ui::VocalCheckView;
        const auto make = [] (Harness& h)
        {
            auto v = std::make_unique<VocalCheckView> (h.proc);
            v->setAnimationsEnabled (false);
            v->setBounds (0, 0, 984, 540);
            v->refreshNow();
            return v;
        };

        beginTest ("View: one tile per visible finding, in order; Anwenden only where a fix applies");
        {
            Harness h;
            analysedTake (h);
            auto view = make (h);
            auto& vc = h.proc.getVocalCheck();
            const auto visible = vc.visibleFindings();
            expectGreaterThan ((int) visible.size(), 1, "the take has findings");
            expectEquals (view->getNumTiles(), (int) visible.size());
            for (int i = 0; i < (int) visible.size(); ++i)
            {
                auto* tile = view->getTileAt (i);
                expect (tile != nullptr && tile->getId() == visible[(size_t) i]->id, "order");
                if (tile == nullptr)
                    continue;
                expect (tile->getIgnoreButton().isVisible(), "Ignorieren on every tile");
                expect (tile->getApplyButton().isVisible() == vc.canApply (tile->getId()), "Anwenden iff applicable: " + tile->getId());
                expectEquals (tile->getProblemText(), i18n::show (visible[(size_t) i]->problem));
                expectEquals (tile->getFixText(), i18n::show (visible[(size_t) i]->fix));
                if (vc.canApply (tile->getId()))
                    expect (tile->getApplyButton().getTooltip().isNotEmpty(), "preview as tooltip");
            }
            expect (view->getScoreText() == juce::String (vc.score()), view->getScoreText());
        }

        beginTest ("View: Anwenden is one undo step; the tile then shows it as applied");
        {
            Harness h;
            analysedTake (h);
            auto view = make (h);
            auto& undo = h.proc.getUndo();
            undo.clear();
            auto* tile = view->getTile ("rumble");
            expect (tile != nullptr && tile->getApplyButton().isVisible());
            if (tile != nullptr)
                click (tile->getApplyButton());
            runLoop (30);
            view->refreshNow();
            expect (h.proc.getRouting().contains (ModuleId::eq), "EQ inserted");
            expectEquals (undo.getNumSteps(), 1);
            tile = view->getTile ("rumble");
            expect (tile != nullptr && tile->isApplied() && ! tile->getApplyButton().isVisible(), "applied state");
            expect (undo.undo());
            view->refreshNow();
            tile = view->getTile ("rumble");
            expect (tile != nullptr && ! tile->isApplied() && tile->getApplyButton().isVisible(), "applicable again after undo");
        }

        beginTest ("View: Ignorieren collapses into 'x ignoriert'; restore and restore all bring the tiles back");
        {
            Harness h;
            analysedTake (h);
            auto view = make (h);
            const int n = view->getNumTiles();
            const auto first = view->getTileAt (0)->getId();
            const auto second = view->getTileAt (1)->getId();
            click (view->getTileAt (0)->getIgnoreButton());
            runLoop (30);
            view->refreshNow();
            expectEquals (view->getNumTiles(), n - 1);
            expect (view->getTile (first) == nullptr, "no tile for the ignored finding");
            expect (view->getIgnoredToggle().isVisible());
            expectEquals (view->getIgnoredToggle().getButtonText().upToFirstOccurrenceOf (" ", false, false) + " ignoriert",
                          juce::String ("1 ignoriert"));
            expect (! view->isIgnoredExpanded() && view->getNumIgnoredRows() == 0, "collapsed");
            click (view->getIgnoredToggle());
            view->refreshNow();
            expect (view->isIgnoredExpanded());
            expectEquals (view->getNumIgnoredRows(), 1);
            auto* row = view->getIgnoredRow (first);
            expect (row != nullptr && row->getRestoreButton().isVisible() && ! row->getApplyButton().isVisible());
            if (row != nullptr)
                click (row->getRestoreButton());
            runLoop (30);
            view->refreshNow();
            expectEquals (view->getNumTiles(), n, "restored");
            expect (! view->getIgnoredToggle().isVisible(), "no ignored row without ignored findings");

            click (view->getTile (first)->getIgnoreButton());
            runLoop (30);
            view->refreshNow();
            click (view->getTile (second)->getIgnoreButton());
            runLoop (30);
            view->refreshNow();
            expectEquals (view->getNumTiles(), n - 2);
            expect (view->getIgnoredToggle().getButtonText().startsWith ("2 ignoriert"), view->getIgnoredToggle().getButtonText());
            click (view->getIgnoredToggle());
            view->refreshNow();
            expect (view->getRestoreAllButton().isVisible());
            click (view->getRestoreAllButton());
            runLoop (30);
            view->refreshNow();
            expectEquals (view->getNumTiles(), n, "all restored");
        }

        beginTest ("View: Alles anwenden shows the count, applies all as one step and is disabled at 0");
        {
            Harness h;
            analysedTake (h);
            auto view = make (h);
            auto& vc = h.proc.getVocalCheck();
            int applicable = 0;
            for (auto* f : vc.visibleFindings())
                applicable += vc.canApply (f->id) ? 1 : 0;
            expectGreaterThan (applicable, 1);
            auto& all = view->getApplyAllButton();
            expect (all.isEnabled());
            expectEquals (all.getButtonText(), "Alles anwenden (" + juce::String (applicable) + ")");
            auto& undo = h.proc.getUndo();
            undo.clear();
            click (all);
            runLoop (30);
            view->refreshNow();
            expectEquals (undo.getNumSteps(), 1, "one step");
            expect (! all.isEnabled(), "nothing left to apply");
            expectEquals (all.getButtonText(), juce::String ("Alles anwenden"));
        }

        beginTest ("View: genre buttons set the genre the targets come from");
        {
            Harness h;
            analysedTake (h);
            auto view = make (h);
            auto& vc = h.proc.getVocalCheck();
            expect (view->getGenreButton (check::Genre::party).getToggleState(), "default Party");
            click (view->getGenreButton (check::Genre::acoustic));
            view->refreshNow();
            expect (vc.getGenre() == check::Genre::acoustic);
            expect (view->getGenreButton (check::Genre::acoustic).getToggleState()
                    && ! view->getGenreButton (check::Genre::party).getToggleState());
            expectEquals (view->getGenreButton (check::Genre::pop).getButtonText(), juce::String ("Pop"));
            expectEquals (view->getGenreButton (check::Genre::acoustic).getButtonText(), juce::String ("Akustik"));
        }

        beginTest ("View in English: buttons, genres, status, tiles and the preview");
        {
            Harness h;
            analysedTake (h);   // the analysis is made in German; the view is built after the switch
            i18n::ScopedLanguage en (i18n::Language::english);
            auto view = make (h);
            auto& vc = h.proc.getVocalCheck();
            int applicable = 0;
            for (auto* f : vc.visibleFindings())
                applicable += vc.canApply (f->id) ? 1 : 0;
            expectEquals (view->getApplyAllButton().getButtonText(), "Apply all (" + juce::String (applicable) + ")");
            expectEquals (view->getGenreButton (check::Genre::party).getButtonText(), juce::String ("Party"));
            expectEquals (view->getGenreButton (check::Genre::acoustic).getButtonText(), juce::String ("Acoustic"));
            expect (view->getStatusText().contains ("s of voice"), view->getStatusText());
            expect (! view->getStatusText().contains (","), "decimal point: " + view->getStatusText());
            expectGreaterThan (view->getNumTiles(), 0);
            for (int i = 0; i < view->getNumTiles(); ++i)
            {
                auto* t = view->getTileAt (i);
                expectEquals (t->getIgnoreButton().getButtonText(), juce::String ("Ignore"));
                expectEquals (t->getApplyButton().getButtonText(), juce::String ("Apply"));
                for (const auto& s : { t->getProblemText(), t->getFixText(), t->getTooltip() })
                {
                    expect (! i18n::isPacked (s), s);
                    for (const char* german : { "\xc3\xa4", "\xc3\xb6", "\xc3\xbc", "\xc3\x9f", " bei ", " um ", " und ", "Ziel" })
                        expect (! s.contains (juce::String::fromUTF8 (german)), "English tile text: " + s);
                }
            }
            if (auto* rumble = view->getTile ("rumble"))
                expect (rumble->getProblemText().startsWith ("Rumble below 90 Hz"), rumble->getProblemText());
            const auto preview = ui::checkPreviewText (h.proc, vc.preview ("rumble"));
            expect (preview.contains ("Insert EQ"), preview);
            click (view->getTileAt (0)->getIgnoreButton());
            runLoop (30);
            view->refreshNow();
            expect (view->getIgnoredToggle().getButtonText().startsWith ("1 ignored"), view->getIgnoredToggle().getButtonText());

            h.set (pid::checkOn, 0.0f);
            view->refreshNow();
            expectEquals (view->getEnableButton().getButtonText(), juce::String ("Turn on"));
            expectEquals (view->getStatusText(), juce::String ("Off"));

            ui::CheckSummary summary (h.proc);
            expectEquals (summary.getCountText(), juce::String ("Off"));
            expect (summary.getOpenButton().getButtonText().startsWith ("Open VOCAL CHECK"));
        }

        beginTest ("View in English: the empty state before any voice");
        {
            Harness h;
            h.neutral();
            h.set (pid::checkOn, 1.0f);
            h.proc.pumpAnalyzer();
            h.proc.refreshAnalysis();
            i18n::ScopedLanguage en (i18n::Language::english);
            auto view = make (h);
            expectEquals (view->getStatusText(), juce::String ("Sing or play back to analyze"));
            expectEquals (view->getApplyAllButton().getButtonText(), juce::String ("Apply all"));
        }

        beginTest ("View: without enough voice the score is a dash with a short status; switched off it offers Einschalten");
        {
            Harness h;
            h.neutral();
            h.set (pid::checkOn, 1.0f);
            h.proc.pumpAnalyzer();
            h.proc.refreshAnalysis();
            auto view = make (h);
            expectEquals (view->getScoreText(), juce::String::fromUTF8 ("\xe2\x80\x94"));
            expectEquals (view->getStatusText(), juce::String ("Singe/spiele ab, um zu analysieren"));
            expectEquals (view->getNumTiles(), 0);
            expect (! view->getApplyAllButton().isEnabled());

            h.set (pid::checkOn, 0.0f);
            view->refreshNow();
            expect (view->getEnableButton().isVisible(), "Einschalten");
            expectEquals (view->getScoreText(), juce::String::fromUTF8 ("\xe2\x80\x94"));
            click (view->getEnableButton());
            view->refreshNow();
            expectEquals (h.get (pid::checkOn), 1.0f, "switched on");
            expect (! view->getEnableButton().isVisible());
        }

        beginTest ("View: tile layout - texts left, buttons right, nothing overlaps, everything inside the list");
        {
            Harness h;
            analysedTake (h);
            for (int width : { 984, 700 })
            {
                auto view = make (h);
                view->setSize (width, 540);
                view->refreshNow();
                for (int i = 0; i < view->getNumTiles(); ++i)
                {
                    auto* t = view->getTileAt (i);
                    const auto text = t->getTextArea();
                    expect (! text.intersects (t->getIgnoreButton().getBounds()), "text vs Ignorieren");
                    if (t->getApplyButton().isVisible())
                    {
                        expect (! text.intersects (t->getApplyButton().getBounds()), "text vs Anwenden");
                        expect (! t->getApplyButton().getBounds().intersects (t->getIgnoreButton().getBounds()), "buttons");
                    }
                    expect (t->getLocalBounds().contains (t->getApplyButton().getBounds()) && t->getLocalBounds().contains (t->getIgnoreButton().getBounds()));
                    expectGreaterOrEqual (text.getWidth(), 200, "room for the texts at width " + juce::String (width));
                    if (i > 0)
                        expect (! view->getTileAt (i - 1)->getBounds().intersects (t->getBounds()), "tiles do not overlap");
                }
            }
        }

        // Review 2026-10-28 (item 6): one "Neu messen", the score with a word, "Alles anwenden" beside it,
        // the voice's spectrum against the genre's target in the free space below
        beginTest ("View: Neu messen instead of mode / Start / Reset; score word; Apply all beside the score; voice vs target");
        {
            expectEquals (ui::VocalCheckView::scoreWord (30), juce::String ("schwach"));
            expectEquals (ui::VocalCheckView::scoreWord (60), juce::String ("okay"));
            expectEquals (ui::VocalCheckView::scoreWord (80), juce::String ("gut"));
            expectEquals (ui::VocalCheckView::scoreWord (95), juce::String ("sehr gut"));
            expect (ui::VocalCheckView::scoreWord (-1).isEmpty());

            Harness h;
            analysedTake (h);
            auto view = make (h);
            view->setSize (984, 540);
            view->refreshNow();
            const int n = (int) h.proc.getVocalCheck().visibleFindings().size();
            expect (view->getSummaryText().contains (juce::String (n)) && view->getSummaryText().contains ("verbessern"), view->getSummaryText());
            expect (view->getScoreWordText() == ui::VocalCheckView::scoreWord (h.proc.getVocalCheck().score()));

            juce::StringArray texts;
            for (auto* c : view->getChildren())
                if (auto* b = dynamic_cast<juce::Button*> (c); b != nullptr && b->isVisible())
                    texts.add (b->getButtonText());
            for (auto* gone : { "Start", "Stop", "Reset", "Capture" })
                expect (! texts.contains (gone), juce::String ("no ") + gone);
            auto& remeasure = view->getRemeasureButton();
            expect (remeasure.isVisible() && remeasure.getButtonText() == "Neu messen");
            expect (view->getApplyAllButton().getX() < view->getWidth() / 2, "Alles anwenden next to the score (left half)");

            auto& spec = view->getTargetView();
            expect (spec.isVisible() && spec.hasVoice(), "the voice's spectrum");
            expect (spec.getY() > view->getHeight() / 2 && spec.getHeight() >= 100, "in the space below");
            const ui::Legend* legend = nullptr;
            for (auto* c : spec.getChildren())
                if (auto* l = dynamic_cast<ui::Legend*> (c))
                    legend = l;
            expect (legend != nullptr && legend->isVisible() && legend->getItems().size() == 2, "legend: voice / target");

            click (remeasure);
            expect (h.proc.getAnalyzer().getMode() == dsp::VocalAnalyzer::Mode::continuous, "measures continuously again");
            expect (h.proc.getState().getRawParameterValue (pid::checkMode)->load() < 0.5f);
            view->refreshNow();
            expectEquals (view->getStatusText(), juce::String ("Singe/spiele ab, um zu analysieren"), "measures again from nothing");
        }

        beginTest ("Preview text: the parameters a fix writes and the modules it inserts");
        {
            Harness h;
            analysedTake (h);
            const auto fix = h.proc.getVocalCheck().preview ("rumble");
            const auto text = ui::checkPreviewText (h.proc, fix);
            logMessage ("  " + text.replace ("\n", " | "));
            expect (text.contains ("EQ"), text);
            expect (text.contains (juce::String::fromUTF8 ("einfügen")), "the EQ is inserted: " + text);
            expectGreaterThan (juce::StringArray::fromLines (text).size(), 1);
        }

        beginTest ("Colours: score and severity");
        {
            expect (ui::checkScoreColour (95) == ui::colours::ok);
            expect (ui::checkScoreColour (40) == ui::colours::warning);
            expect (ui::checkScoreColour (-1) == ui::colours::textFaint);
            expect (ui::checkSeverityColour (check::Severity::high) == ui::colours::warning);
            expect (ui::checkSeverityColour (check::Severity::low) == ui::moduleColour (ModuleId::check));
        }

        // Review 2026-10-28 (item 3): VOCAL CHECK is no module of the chain any more; it measures after the chain
        // Final review 2026-10-28: on by default, so a closed instance must not analyse every 0.5 s - it only collects
        beginTest ("On by default, but without an open editor it only collects; the analysis runs once an editor is open");
        {
            Harness h;
            h.neutral();
            h.set (pid::checkOn, 1.0f);
            VocalSpec s;
            s.seconds = 8.0;
            s.channels = 2;
            feedBlocks (h, makeVocal (s));
            juce::Thread::sleep (600);   // longer than the refresh interval
            h.proc.pumpAnalyzer();
            expect (! h.proc.getLatestAnalysis().enoughMaterial && h.proc.getLatestAnalysis().voicedSeconds <= 0.0f,
                    "no analysis while closed");
            std::unique_ptr<juce::AudioProcessorEditor> e (h.proc.createEditorIfNeeded());   // as a host opens it
            h.proc.pumpAnalyzer();
            expect (h.proc.getLatestAnalysis().voicedSeconds > 4.0f, "open: analysed from what was collected");
        }

        beginTest ("No chain module: not in the routing, the browser or a preset; the diagnosis still measures after the chain");
        {
            expect (! Routing::defaults().contains (ModuleId::check));
            const auto parsed = Routing::fromString ("v2,eq,check,comp");
            expectEquals (parsed.toString(), juce::String ("v2,eq,comp"), "a stored check slot is dropped");
            Routing r;
            expect (! r.insertDefault (ModuleId::check, 0) && ! r.insertAt (0, ModuleId::check, 0, true));
            {
                Harness h;
                ui::ModuleBrowser browser (h.proc);
                browser.setBounds (0, 0, 900, 600);
                const auto shown = browser.visibleModules();
                expect (std::find (shown.begin(), shown.end(), ModuleId::check) == shown.end(), "not in the module browser");
                expectEquals (h.get (pid::checkOn), 1.0f, "the diagnosis runs by default");
            }
            Harness h;
            analysedTake (h);
            expect (! h.proc.getRouting().contains (ModuleId::check));
            expect (! h.proc.getVocalCheck().visibleFindings().empty(), "findings without a check slot");
            expect (h.proc.getVocalCheck().visibleFindings().front()->handling != Finding::Handling::handledLater,
                    "the chain is part of the measurement (VOCAL CHECK measures after it)");
        }
    }

    static VelvoxAudioProcessorEditor& asVelvox (juce::AudioProcessorEditor& e) { return dynamic_cast<VelvoxAudioProcessorEditor&> (e); }

    void shell()
    {
        beginTest ("Shell: VOCAL CHECK shows the check view in Simple and Advanced (no module panel)");
        {
            Harness h;
            analysedTake (h);
            for (auto mode : { "beginner", "pro" })
            {
                h.proc.getState().state.setProperty (statekeys::uiMode, mode, nullptr);
                h.proc.getState().state.setProperty (statekeys::uiTab, "check", nullptr);
                std::unique_ptr<juce::AudioProcessorEditor> e (h.proc.createEditor());
                auto& ed = asVelvox (*e);
                expect (ed.getTab() == VelvoxAudioProcessorEditor::Tab::vocalCheck);
                expect (ed.getCheckView().isVisible(), juce::String ("check view visible in ") + mode);
                expect (ed.getDetail() == nullptr, "no module panel on the tab");
                expect (! ed.getSimpleView().isVisible() && ! ed.getChainView().isVisible());
                expect (ed.getCheckView().getBounds() == VelvoxAudioProcessorEditor::bodyArea(), ed.getCheckView().getBounds().toString());
                ed.setTab (VelvoxAudioProcessorEditor::Tab::autoMix);
                expect (! ed.getCheckView().isVisible(), "hidden on AUTO MIX");
            }
        }

        // Review 2026-10-28 (item 3): Memphy says the tips in SIMPEL, one at a time; the header chip opens the tab
        beginTest ("Shell: Memphy says one tip at a time with Ja, mach / Zeig mir; the header chip shows score and tips");
        {
            Harness h;
            analysedTake (h);
            h.proc.getAutoMix().dismissPrompt();
            h.proc.getState().state.setProperty (statekeys::uiMode, "beginner", nullptr);
            h.proc.getState().state.setProperty (statekeys::uiTab, "automix", nullptr);
            std::unique_ptr<juce::AudioProcessorEditor> e (h.proc.createEditor());
            auto& ed = asVelvox (*e);
            auto& simple = ed.getSimpleView();
            auto& vc = h.proc.getVocalCheck();

            auto& chip = ed.getTopBar().getCheckChip();
            ed.getTopBar().refresh();
            expect (chip.isVisible(), "the chip once there is a score");
            const auto n = (int) vc.visibleFindings().size();
            expect (chip.getButtonText().contains (juce::String (vc.score())) && chip.getButtonText().contains (juce::String (n)),
                    chip.getButtonText());

            for (int i = 0; i < 60 * 3; ++i)
                simple.tick (1.0 / 60.0);
            const auto first = simple.getTipId();
            expect (first.isNotEmpty(), "a tip");
            const auto* f = vc.finding (first);
            expect (f != nullptr && vc.canApply (first), "an applicable finding");
            if (f != nullptr)
                expect (simple.getBubbleText().contains (f->problem), "he says the problem: " + simple.getBubbleText());
            auto& yes = simple.getTipYesButton();
            auto& show = simple.getTipShowButton();
            expect (yes.isVisible() && show.isVisible() && ! simple.getAnalyseButton().isVisible(), "his two answers instead");
            expect (yes.getButtonText() == "Ja, mach" && show.getButtonText() == "Zeig mir");
            click (yes);
            expect (vc.wasApplied (first), "Ja, mach applies the fix");
            expect (simple.getTipId().isEmpty() && simple.getAnalyseButton().isVisible(), "then the usual bubble");
            for (int i = 0; i < 60 * 4; ++i)
                simple.tick (1.0 / 60.0);
            const auto second = simple.getTipId();
            expect (second != first, "the next tip, never the same again");
            if (second.isNotEmpty())
            {
                click (simple.getTipShowButton());
                runLoop (30);
                expect (ed.getTab() == VelvoxAudioProcessorEditor::Tab::vocalCheck, "Zeig mir opens the tab");
            }
            ed.setTab (VelvoxAudioProcessorEditor::Tab::autoMix);
            click (chip);
            runLoop (30);
            expect (ed.getTab() == VelvoxAudioProcessorEditor::Tab::vocalCheck, "the chip opens the tab");
        }

        beginTest ("Shell: Ctrl+Z after a tile's Anwenden undoes the whole fix");
        {
            Harness h;
            analysedTake (h);
            h.proc.getState().state.setProperty (statekeys::uiTab, "check", nullptr);
            std::unique_ptr<juce::AudioProcessorEditor> e (h.proc.createEditor());
            auto& ed = asVelvox (*e);
            auto& view = ed.getCheckView();
            view.setAnimationsEnabled (false);
            view.refreshNow();
            const auto routingBefore = h.proc.getState().state.getProperty (statekeys::routing).toString();
            auto* tile = view.getTile ("rumble");
            expect (tile != nullptr);
            if (tile != nullptr)
                click (tile->getApplyButton());
            runLoop (50);
            expect (h.proc.getRouting().contains (ModuleId::eq));
            expect (ed.keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)));
            expectEquals (h.proc.getState().state.getProperty (statekeys::routing).toString(), routingBefore, "routing restored");
            expectEquals (h.get (pid::eqOn), 0.0f, "EQ switch restored");
            // The editor keeps running with the rebuilt list
            runLoop (120);
            view.refreshNow();
            expect (view.getTile ("rumble") != nullptr);
        }
    }
};

static CheckPanelTests checkPanelTests;
