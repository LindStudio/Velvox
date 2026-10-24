// Design system of the HEADLINER VOCAL redesign (docs/design/HEADLINER_DESIGN_HANDOFF.md):
// tokens, module identity (colour + neon icon), controls, layout of the views.

#include <set>
#include "TestHelpers.h"
#include "UI/Theme.h"
#include "PluginEditor.h"
#include "UI/Segments.h"

using namespace velvox;
using namespace velvox::test;
using dsp::ModuleId;

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
    }
};

static DesignTests designTests;
