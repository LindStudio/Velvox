// Design system of the HEADLINER VOCAL redesign (docs/design/HEADLINER_DESIGN_HANDOFF.md):
// tokens, module identity (colour + neon icon), controls, layout of the views.

#include <set>
#include "TestHelpers.h"
#include "UI/Theme.h"
#include "PluginEditor.h"

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
    }
};

static DesignTests designTests;
