// Design system of the HEADLINER VOCAL redesign (docs/design/HEADLINER_DESIGN_HANDOFF.md):
// tokens, module identity (colour + neon icon), controls, layout of the views.

#include <set>
#include "TestHelpers.h"
#include "UI/Theme.h"
#include "PluginEditor.h"
#include "UI/Segments.h"
#include "UI/ChainView.h"

using namespace velvox;
using namespace velvox::test;
using dsp::ModuleId;

namespace
{
    // Every parameter-bound control of a panel: knobs, segment groups, switches
    struct BoundControls
    {
        juce::StringArray knobs, segments, toggles;
        std::vector<juce::Colour> knobAccents;
    };

    void collect (juce::Component& c, BoundControls& out)
    {
        for (auto* child : c.getChildren())
        {
            if (auto* k = dynamic_cast<ui::Knob*> (child))
            {
                out.knobs.add (k->getParamId());
                out.knobAccents.push_back (k->slider.findColour (juce::Slider::rotarySliderFillColourId));
            }
            else if (auto* s = dynamic_cast<ui::Segments*> (child))
                out.segments.add (s->getParamId());
            else if (auto* t = dynamic_cast<ui::Toggle*> (child))
                out.toggles.add (t->getParamId());
            collect (*child, out);
        }
    }
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

        beginTest ("Design: 1384 x 1040 canvas, header tabs switch the three views");
        {
            expectEquals (ui::baseWidth, 1384);
            expectEquals (ui::baseHeight, 1040);

            Harness h;
            h.proc.getAutoMix().dismissPrompt();
            std::unique_ptr<juce::AudioProcessorEditor> e (h.proc.createEditor());
            auto& ed = dynamic_cast<VelvoxAudioProcessorEditor&> (*e);
            auto& top = ed.getTopBar();
            expectEquals (top.getHeight(), 56, "header 56");

            top.selectView (ui::TopBar::View::autoMix);
            expect (ed.getAutoMixView().isVisible(), "AUTO MIX view");
            expect (! ed.isProMode());

            top.selectView (ui::TopBar::View::vocalCheck);
            expect (ed.getDetail() != nullptr && ed.getDetail()->isVisible(), "VOCAL CHECK view shows the check panel");
            expect (! ed.getAutoMixView().isVisible());
            expectEquals (h.proc.getState().state.getProperty ("uiMode").toString(), juce::String ("check"), "view stored with the project");

            top.selectView (ui::TopBar::View::chain);
            expect (ed.isProMode() && ed.getChainView().isVisible(), "CHAIN view");
            expect (ed.getDetail() != nullptr, "module panel in the chain view");
            expect (top.getView() == ui::TopBar::View::chain);

            // The AUTO MIX view owns the analyse button
            top.selectView (ui::TopBar::View::autoMix);
            expect (ed.getAutoMixView().isVisible() && ed.getAutoMixView().getAnalyseButton().isVisible(), "analyse in the AUTO MIX view");
        }

        beginTest ("Design: a column carries its two knobs on the instance's parameters");
        {
            Harness h;
            ui::ChainView view (h.proc);
            view.setBounds (0, 0, 1360, ui::ChainView::preferredHeight);
            expectEquals (ui::ChainView::preferredHeight, 330, "chain height 330");

            const auto ids = [&] (ModuleId id, int instance)
            {
                juce::StringArray s;
                for (int k = 0; k < 2; ++k)
                    if (auto* knob = view.getColumnKnob (id, instance, k))
                        s.add (knob->getParamId());
                return s;
            };
            expect (ids (ModuleId::eq, 0) == juce::StringArray { "hpfFreq", "highGain" }, "EQ: low cut, highs");
            view.insertModule (ModuleId::eq, 1);
            view.refresh();
            expect (ids (ModuleId::eq, 1) == juce::StringArray { "hpfFreq_2", "highGain_2" }, "EQ 2 on its own parameters");
            expect (ids (ModuleId::reverb, 0) == juce::StringArray { "reverbDecay", "reverbMix" }, "REVERB: decay, mix");

            const auto pairFor = ui::ChainView::columnParams (ModuleId::ride);
            expect (juce::String (pairFor.first) == "rideTarget" && juce::String (pairFor.second) == "rideResponse", "LEVEL RIDE: target, speed");
            expect (ui::ChainView::columnParams (ModuleId::check).first == nullptr, "VOCAL CHECK has no knobs");
            for (int i = 0; i < dsp::numModules; ++i)
            {
                const auto p = ui::ChainView::columnParams ((ModuleId) i);
                for (auto* id : { p.first, p.second })
                    if (id != nullptr)
                        expect (h.proc.getState().getParameter (id) != nullptr, juce::String ("column knob exists: ") + id);
            }

            // Knob sits inside its column, under the icon
            const auto col = view.getTileBounds (ModuleId::eq, 0);
            if (auto* k = view.getColumnKnob (ModuleId::eq, 0, 0))
            {
                expect (col.contains (k->getBounds().toFloat()), "knob inside the column");
                expect (k->getY() > col.getY() + 100.0f, "below name and icon");
            }
        }

        beginTest ("Design: column footer arrows reorder, bypassed column dims but stays switchable");
        {
            Harness h;
            ui::ChainView view (h.proc);
            view.setBounds (0, 0, 1360, ui::ChainView::preferredHeight);
            view.refresh();
            const auto r0 = h.proc.getRouting();
            const int eqPos = r0.indexOf (ModuleId::eq, 0);
            view.mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), view.getMoveRightBounds (ModuleId::eq, 0).getCentre(),
                                              {}, 1.0f, 0, 0, 0, 0, &view, &view, juce::Time::getCurrentTime(),
                                              view.getMoveRightBounds (ModuleId::eq, 0).getCentre(), juce::Time::getCurrentTime(), 1, false));
            expectEquals (h.proc.getRouting().indexOf (ModuleId::eq, 0), eqPos + 1, "right arrow moves EQ one step later");
            expect (view.undo());
            expectEquals (h.proc.getRouting().indexOf (ModuleId::eq, 0), eqPos, "undo restores the order");
            expect (! view.getMoveLeftBounds (ModuleId::eq, 0).isEmpty(), "left arrow exists");

            // Bypass: column content at 30 %, power button still works
            h.set (pid::compOn, 0.0f);
            view.refresh();
            if (auto* k = view.getColumnKnob (ModuleId::comp, 0, 0))
                expectWithinAbsoluteError (k->getAlpha(), 0.3f, 0.01f, "bypassed column dimmed");
            const auto power = view.getPowerBounds (ModuleId::comp, 0).getCentre();
            view.mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), power, {}, 1.0f, 0, 0, 0, 0, &view, &view,
                                              juce::Time::getCurrentTime(), power, juce::Time::getCurrentTime(), 1, false));
            expect (h.proc.getState().getRawParameterValue (pid::compOn)->load() > 0.5f, "power turns it back on");
            view.refresh();
            if (auto* k = view.getColumnKnob (ModuleId::comp, 0, 0))
                expectWithinAbsoluteError (k->getAlpha(), 1.0f, 0.01f, "full opacity again");
        }

        beginTest ("Design: 14 columns scroll instead of shrinking below 104 px");
        {
            Harness h;
            ui::ChainView view (h.proc);
            view.setBounds (0, 0, 1360, ui::ChainView::preferredHeight);
            for (auto id : { ModuleId::ride, ModuleId::smash, ModuleId::smooth, ModuleId::sat, ModuleId::doubler, ModuleId::choir,
                             ModuleId::chop, ModuleId::pulse, ModuleId::check, ModuleId::pitch, ModuleId::eq, ModuleId::comp })
                view.insertModule (id);
            for (const auto& s : h.proc.getRouting())
                expect (view.getTileBounds (s.id, s.instance).getWidth() >= 104.0f - 0.01f, "column width >= 104");
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
                panel->setBounds (0, 0, 1180, 460);
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
            expect (found[(int) ModuleId::reverb].segments.contains ("reverbType"), "REVERB room list");
            expect (found[(int) ModuleId::pulse].segments.contains ("pulseRate"), "PULSE rate as segments");
            expect (found[(int) ModuleId::choir].segments.contains ("choirVoices"), "CHOIR voices as segments");
            expect (found[(int) ModuleId::smooth].segments.contains ("smoothMode") && found[(int) ModuleId::smooth].toggles.contains ("smoothDelta"), "SMOOTH character + delta");
            expect (found[(int) ModuleId::chop].segments.contains ("chopRate"), "STUTTER rate as segments");
            expect (found[(int) ModuleId::smash].knobs.contains ("smashAmount"), "VOCAL SMASH big knob");

            // Nothing the old panels offered gets lost
            for (auto* pid : { "delayDuckAmount", "delayDuckThreshold", "delayDuckAttack", "delayDuckRelease", "delayToReverb", "delayDrive", "delayDiffusion" })
                expect (found[(int) ModuleId::delay].knobs.contains (pid), juce::String ("DELAY keeps ") + pid);
            for (auto* pid : { "reverbEarly", "reverbDiffusion", "reverbBass", "reverbHighCut", "reverbDuckAmount" })
                expect (found[(int) ModuleId::reverb].knobs.contains (pid), juce::String ("REVERB keeps ") + pid);
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
                juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            }
            for (auto& [id, v] : values)
                expectWithinAbsoluteError (h2.get (id), v, std::abs (v) * 0.01f + 0.01f, juce::String ("unchanged: ") + id);
        }
    }
};

static DesignTests designTests;
