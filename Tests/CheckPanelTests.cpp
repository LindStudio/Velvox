// VOCAL CHECK view of 0.6 (spec 5.15, "Check 1 · score + tiles"): score ring, genre, "Alles anwenden",
// one tile per finding with "Ignorieren" / "Anwenden", the "x ignoriert" row, the compact summary in the
// Check module panel and the shell integration (both modes, undo).

#include "TestHelpers.h"
#include "SyntheticVocal.h"
#include "PluginEditor.h"
#include "UI/VocalCheckView.h"
#include "UI/UserSettings.h"
#include "StateKeys.h"

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
                expectEquals (tile->getProblemText(), visible[(size_t) i]->problem);
                expectEquals (tile->getFixText(), visible[(size_t) i]->fix);
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

        beginTest ("Summary (Check module panel): score, count and the link to the VOCAL CHECK tab");
        {
            Harness h;
            analysedTake (h);
            ui::CheckSummary summary (h.proc);
            summary.setBounds (0, 0, 280, 120);
            summary.refresh();
            int opened = 0;
            summary.onOpenTab = [&opened] { ++opened; };
            expectEquals (summary.getScoreText(), juce::String (h.proc.getVocalCheck().score()));
            expect (summary.getCountText().contains (juce::String ((int) h.proc.getVocalCheck().visibleFindings().size())), summary.getCountText());
            click (summary.getOpenButton());
            expectEquals (opened, 1);
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

        beginTest ("Shell: the Check module panel links to the VOCAL CHECK tab");
        {
            Harness h;
            analysedTake (h);
            h.proc.getState().state.setProperty (statekeys::uiMode, "pro", nullptr);
            h.proc.getState().state.setProperty (statekeys::uiTab, "automix", nullptr);
            std::unique_ptr<juce::AudioProcessorEditor> e (h.proc.createEditor());
            auto& ed = asVelvox (*e);
            ed.selectModule ((int) ModuleId::check);
            auto* panel = ed.getDetail();
            expect (panel != nullptr && panel->getModuleId() == (int) ModuleId::check);
            ui::CheckSummary* summary = nullptr;
            if (panel != nullptr)
                for (auto* c : panel->getChildren())
                    if (auto* s = dynamic_cast<ui::CheckSummary*> (c))
                        summary = s;
            expect (summary != nullptr, "summary in the panel");
            if (summary != nullptr)
                click (summary->getOpenButton());
            runLoop (50);
            expect (ed.getTab() == VelvoxAudioProcessorEditor::Tab::vocalCheck, "link opens the tab");
            expect (ed.getCheckView().isVisible());
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
