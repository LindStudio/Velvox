// Headliner 0.6 shell (spec 2.1 - 2.4, 8 Q8): view state, header, keyboard undo, Simple mode, chain row.

#include "TestHelpers.h"
#include "SyntheticVocal.h"
#include "PluginEditor.h"

using namespace velvox;
using namespace velvox::test;
using dsp::ModuleId;

namespace
{
    VelvoxAudioProcessorEditor& asVelvox (juce::AudioProcessorEditor& e) { return dynamic_cast<VelvoxAudioProcessorEditor&> (e); }

    std::unique_ptr<juce::AudioProcessorEditor> editorWith (Harness& h, const juce::String& uiMode, const juce::String& uiTab = {})
    {
        h.proc.getAutoMix().dismissPrompt();
        h.proc.getState().state.setProperty ("uiMode", uiMode, nullptr);
        if (uiTab.isNotEmpty())
            h.proc.getState().state.setProperty ("uiTab", uiTab, nullptr);
        return std::unique_ptr<juce::AudioProcessorEditor> (h.proc.createEditor());
    }

    void gesture (Harness& h, const char* id, float value)
    {
        auto* p = h.proc.getState().getParameter (id);
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }

    juce::StringArray buttonTexts (juce::Component& c)
    {
        juce::StringArray out;
        for (auto* child : c.getChildren())
            if (auto* b = dynamic_cast<juce::Button*> (child))
                out.add (b->getButtonText());
        return out;
    }

    void runMix (Harness& h)
    {
        VocalSpec s;
        s.seconds = 14.0;
        s.channels = 2;
        s.sampleRate = h.fs;
        auto& am = h.proc.getAutoMix();
        am.applyResult (am.runSynchronously (makeVocal (s)));
    }
}

class ShellTests final : public juce::UnitTest
{
public:
    ShellTests() : juce::UnitTest ("Headliner shell", "Headliner") {}

    void runTest() override
    {
        beginTest ("Stored view states of older versions map onto SIMPEL / ADVANCED and the tabs");
        {
            {
                Harness h;
                auto e = editorWith (h, "beginner");
                expect (! asVelvox (*e).isAdvanced() && asVelvox (*e).getTab() == VelvoxAudioProcessorEditor::Tab::autoMix, "beginner -> Simple");
                expect (asVelvox (*e).getSimpleView().isVisible());
            }
            {
                Harness h;
                auto e = editorWith (h, "pro");
                expect (asVelvox (*e).isAdvanced() && asVelvox (*e).isModulePanelShown(), "pro -> Advanced");
            }
            {
                Harness h;
                auto e = editorWith (h, "check");
                auto& ed = asVelvox (*e);
                expect (ed.isAdvanced() && ed.getTab() == VelvoxAudioProcessorEditor::Tab::vocalCheck, "check (0.5) -> Advanced + VOCAL CHECK");
                expect (ed.getCheckView().isVisible() && ed.getCheckView().getBounds() == VelvoxAudioProcessorEditor::bodyArea(), "check view over the body");
                expectEquals (h.proc.getState().state.getProperty ("uiMode").toString(), juce::String ("pro"), "stored again in the 0.6 form");
                expectEquals (h.proc.getState().state.getProperty ("uiTab").toString(), juce::String ("check"));
            }
            {
                Harness h;
                auto e = editorWith (h, "beginner", "check");
                expect (! asVelvox (*e).isAdvanced() && asVelvox (*e).getTab() == VelvoxAudioProcessorEditor::Tab::vocalCheck, "Simple + VOCAL CHECK");
                expect (asVelvox (*e).getCheckView().isVisible(), "the check view in Simple mode too");
            }
        }

        beginTest ("Header: logo, presets, SIMPEL | ADVANCED, AUTO MIX | VOCAL CHECK, Bypass, ? - no A/B");
        {
            Harness h;
            auto e = editorWith (h, "beginner");
            auto& top = asVelvox (*e).getTopBar();
            const auto texts = buttonTexts (top);
            for (auto* t : { "SIMPEL", "ADVANCED", "AUTO MIX", "VOCAL CHECK", "Bypass", "?" })
                expect (texts.contains (t), juce::String (t) + " in " + texts.joinIntoString ("|"));
            for (auto* t : { "A", "B", "Copy A to B", "A = B", "CHAIN" })
                expect (! texts.contains (t), juce::String ("no ") + t);
            // Left to right
            expect (top.getPresetArea().getRight() < top.getModeArea().getX(), "presets before the mode switch");
            expect (top.getModeArea().getRight() < top.getTabArea().getX(), "mode switch before the tabs");
            expect (top.getTabArea().getRight() < top.getBypassArea().getX(), "tabs before bypass");
            expect (top.getBypassArea().getRight() < top.getHelpButton().getX(), "bypass before ?");
            for (auto& r : { top.getPresetArea(), top.getModeArea(), top.getTabArea(), top.getBypassArea() })
                expect (top.getLocalBounds().contains (r), "inside the header");
        }

        beginTest ("Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z undo and redo in the editor");
        {
            Harness h;
            auto e = editorWith (h, "pro");
            auto& ed = asVelvox (*e);
            gesture (h, pid::compRatio, 7.0f);
            expect (ed.keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 'z')));
            expectWithinAbsoluteError (h.get (pid::compRatio), 3.0f, 1.0e-3f, "Ctrl+Z");
            expect (ed.keyPressed (juce::KeyPress ('y', juce::ModifierKeys::commandModifier, 'y')));
            expectWithinAbsoluteError (h.get (pid::compRatio), 7.0f, 1.0e-3f, "Ctrl+Y");
            ed.keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 'z'));
            expect (ed.keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 'Z')));
            expectWithinAbsoluteError (h.get (pid::compRatio), 7.0f, 1.0e-3f, "Ctrl+Shift+Z");
            expect (! ed.keyPressed (juce::KeyPress ('z', {}, 'z')), "plain z is not undo");
        }

        beginTest ("Simple mode: Vocal analysieren starts the analysis; Vorher / Nachher appears after it");
        {
            Harness h;
            auto e = editorWith (h, "beginner");
            auto& ed = asVelvox (*e);
            auto& view = ed.getSimpleView();
            expect (! view.getBeforeButton().isVisible() && ! view.getAfterButton().isVisible(), "hidden before an analysis");
            view.getAnalyseButton().onClick();
            expect (ed.getAnalysisOverlay().isOpen(), "progress card");
            h.proc.getAutoMix().cancel();
            ed.getAnalysisOverlay().setVisible (false);

            h.set (pid::compRatio, 2.0f);
            runMix (h);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            expect (view.getBeforeButton().isVisible() && view.getAfterButton().isVisible(), "visible after the analysis");
            expect (view.getBeforeButton().getX() > view.getAnalyseButton().getRight(), "next to Vocal analysieren");
            view.getBeforeButton().onClick();
            expect (h.proc.getAutoMix().isShowingPrevious());
            expectWithinAbsoluteError (h.get (pid::compRatio), 2.0f, 0.01f, "Vorher = before");
            expect (view.getBeforeButton().getToggleState() && ! view.getAfterButton().getToggleState());
            view.getAfterButton().onClick();
            expect (! h.proc.getAutoMix().isShowingPrevious(), "Nachher = the mix");
        }

        beginTest ("Advanced: chain on top with IN/OUT meters, the module panel takes the rest");
        {
            Harness h;
            auto e = editorWith (h, "pro");
            auto& ed = asVelvox (*e);
            const auto body = VelvoxAudioProcessorEditor::bodyArea();
            expect (ed.getChainView().getBounds().getY() == body.getY(), "chain at the top");
            expect (ed.getMeters().isVisible() && ed.getMeters().getBounds().getBottom() < ed.getDetail()->getY(), "IN / OUT meters next to the chain");
            expect (ed.getDetail()->getBounds() == VelvoxAudioProcessorEditor::panelArea());
            expectEquals (ed.getDetail()->getWidth(), body.getWidth(), "full width");
            expectEquals (ed.getDetail()->getBottom(), body.getBottom(), "down to the bottom");
        }

        beginTest ("Removing or undoing the module whose panel is open leaves a valid selection");
        {
            Harness h;
            auto e = editorWith (h, "pro");
            auto& ed = asVelvox (*e);
            auto& chain = ed.getChainView();
            const int code = chain.insertModule (ModuleId::smooth, 1);
            expectEquals (chain.getSelected(), code, "new module selected");
            expect (ed.getDetail() != nullptr);
            expect (chain.removeInstance (ModuleId::smooth, 0));
            expect (chain.getSelected() != code, "selection moved on");
            expect (ed.getDetail() != nullptr, "a panel is still shown");

            chain.insertModule (ModuleId::smooth, 1);
            expect (ed.keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 'z')), "undo the insert");
            chain.refresh();
            expect (! h.proc.getRouting().contains (ModuleId::smooth));
            ed.selectModule (chain.getSelected());
            expect (ed.getDetail() != nullptr, "panel after the undo");
        }

        beginTest ("Chain edits keep the mix law: duplicates copy it, browser inserts use the true law");
        {
            Harness h;
            ui::ChainView view (h.proc);
            view.setBounds (0, 0, 886, ui::ChainView::preferredHeight);
            const int code = view.insertModule (ModuleId::smash, 1);
            expect (code >= 0 && ! h.proc.usesLegacyMixLaw (ModuleId::smash, 0), "added = true law");
            h.proc.setLegacyMixLaw (ModuleId::smash, 0, true);
            const int copy = view.duplicateInstance (ModuleId::smash, 0);
            expect (copy >= 0 && h.proc.usesLegacyMixLaw (ModuleId::smash, dsp::selectionInstance (copy)), "the copy keeps the legacy law");
        }

        beginTest ("A delay or reverb running as a send says SEND on its tile");
        {
            Harness h;
            ui::ChainView view (h.proc);
            view.setBounds (0, 0, 886, ui::ChainView::preferredHeight);
            auto r = h.proc.getRouting();
            r.setInsert (ModuleId::delay, 0, false);
            h.proc.setRouting (r);
            view.refresh();
            const auto lane = view.getLaneBounds();
            expect (lane.contains (view.getTileBounds (ModuleId::delay)), "sends sit behind the divider");
        }

        beginTest ("The tab row names the view: Advanced shows MODULE | VOCAL CHECK, Simple AUTO MIX | VOCAL CHECK");
        {
            Harness h;
            auto e = editorWith (h, "pro");
            auto& ed = asVelvox (*e);
            auto& top = ed.getTopBar();
            using T = ui::TopBar;
            expectEquals (top.getTabButton (T::Tab::autoMix).getButtonText(), juce::String ("MODULE"), "Advanced: the chain is MODULE");
            expect (top.getTabButton (T::Tab::autoMix).getToggleState() && ! top.getTabButton (T::Tab::vocalCheck).getToggleState());
            expect (! buttonTexts (top).contains ("AUTO MIX"), "no AUTO MIX tab while the chain is shown");
            top.selectTab (T::Tab::vocalCheck);
            expect (ed.getCheckView().isVisible() && ! top.getTabButton (T::Tab::autoMix).getToggleState());
            top.selectTab (T::Tab::autoMix);
            top.selectMode (T::Mode::simple);
            expectEquals (top.getTabButton (T::Tab::autoMix).getButtonText(), juce::String ("AUTO MIX"), "Simple: AUTO MIX");
            expect (ed.getSimpleView().isVisible() && top.getTabButton (T::Tab::autoMix).getToggleState());
            top.selectMode (T::Mode::advanced);
            expectEquals (top.getTabButton (T::Tab::autoMix).getButtonText(), juce::String ("MODULE"));
        }

        beginTest ("Simple spectrum: the frequency labels are drawn above the fill, on a backing");
        {
            dsp::SampleTap inTap, outTap;
            ui::SpectrumSource source (inTap, outTap);
            juce::Random random (3);
            std::vector<float> block (512);
            for (int k = 0; k < 300; ++k)
            {
                for (auto& s : block)
                    s = random.nextFloat() * 1.6f - 0.8f;   // loud white noise: the fill covers the floor
                const float* p[] { block.data() };
                inTap.push (p, 1, (int) block.size());
                outTap.push (p, 1, (int) block.size());
                source.update (48000.0);
            }
            ui::SpectrumView view (&source);
            view.setInputDashed (true);
            view.setRange (-84.0f, -6.0f);   // as in the Simple view
            view.setBounds (0, 0, 640, 200);
            juce::Image image (juce::Image::ARGB, 640, 200, true);
            {
                juce::Graphics g (image);
                g.fillAll (ui::colours::panelInset);
                view.paintEntireComponent (g, false);
            }
            // Contrast inside the label box of "1k": brightest (text) against darkest (backing / fill)
            const auto r = view.getLocalBounds().toFloat();
            const int x0 = (int) ui::SpectrumView::xForFrequency (1000.0f, r) + 4, y0 = (int) r.getBottom() - 18;
            float lo = 1.0f, hi = 0.0f;
            for (int y = y0; y < y0 + 14; ++y)
                for (int x = x0; x < x0 + 18; ++x)
                {
                    const float b = image.getPixelAt (x, y).getPerceivedBrightness();
                    lo = std::min (lo, b);
                    hi = std::max (hi, b);
                }
            logMessage ("label contrast " + juce::String (hi - lo, 3));
            expectGreaterThan (hi - lo, 0.3f, "the label stands out from the spectrum fill");
        }
    }
};

static ShellTests shellTests;
