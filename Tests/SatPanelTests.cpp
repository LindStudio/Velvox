// Saturation panel 0.6 (spec 5.6, "Sat 2" + Low / Mid / High): Link behaviour, character tiles,
// the frequency strip with its crossover handles and band areas, and the panel itself.

#include "TestHelpers.h"
#include "PluginEditor.h"
#include "UI/SatPanel.h"
#include "UI/Segments.h"

using namespace velvox;
using namespace velvox::test;
using dsp::ModuleId;

namespace
{
    juce::RangedAudioParameter& param (Harness& h, const juce::String& id)
    {
        auto* p = h.proc.getState().getParameter (id);
        jassert (p != nullptr);
        return *p;
    }

    // A user edit as every control makes it: begin, set, end
    void userEdit (Harness& h, const juce::String& id, std::initializer_list<float> values)
    {
        auto& p = param (h, id);
        p.beginChangeGesture();
        for (float v : values)
            p.setValueNotifyingHost (p.convertTo0to1 (v));
        p.endChangeGesture();
    }

    void setPlain (Harness& h, const juce::String& id, float v)
    {
        auto& p = param (h, id);
        p.setValueNotifyingHost (p.convertTo0to1 (v));
    }

    float value (Harness& h, const juce::String& id) { return h.proc.getState().getRawParameterValue (id)->load(); }

    juce::String id (const char* base, int instance) { return dsp::instanceParamId (base, instance); }
}

class SatPanelTests final : public juce::UnitTest
{
public:
    SatPanelTests() : juce::UnitTest ("Headliner saturation panel", "Headliner") {}

    void runTest() override
    {
        testLink();
        testTilePainter();
        testChainAccessor();
    }

private:
    void testLink()
    {
        beginTest ("Link on: a drive gesture on one band moves the others by the same amount, one undo step");
        {
            Harness h;
            ui::SatLink link (h.proc.getState(), 0);
            setPlain (h, pid::satLink, 1.0f);
            setPlain (h, pid::satLowDrive, 6.0f);
            setPlain (h, pid::satMidDrive, 8.0f);
            setPlain (h, pid::satHighDrive, 3.0f);
            h.proc.getUndo().clear();

            userEdit (h, pid::satLowDrive, { 7.0f, 9.0f, 10.0f });
            expectWithinAbsoluteError (value (h, pid::satLowDrive), 10.0f, 0.01f, "the dragged band");
            expectWithinAbsoluteError (value (h, pid::satMidDrive), 12.0f, 0.01f, "mid +4");
            expectWithinAbsoluteError (value (h, pid::satHighDrive), 7.0f, 0.01f, "high +4");
            expectEquals (h.proc.getUndo().getNumSteps(), 1, "one undo step");
            h.proc.getUndo().undo();
            expectWithinAbsoluteError (value (h, pid::satLowDrive), 6.0f, 0.01f, "undo low");
            expectWithinAbsoluteError (value (h, pid::satMidDrive), 8.0f, 0.01f, "undo mid");
            expectWithinAbsoluteError (value (h, pid::satHighDrive), 3.0f, 0.01f, "undo high");
        }

        beginTest ("Link: dragging back returns the others (relative to the gesture start), clamped at the range end");
        {
            Harness h;
            ui::SatLink link (h.proc.getState(), 0);
            setPlain (h, pid::satLink, 1.0f);
            setPlain (h, pid::satLowDrive, 6.0f);
            setPlain (h, pid::satMidDrive, 6.0f);
            setPlain (h, pid::satHighDrive, 22.0f);
            userEdit (h, pid::satLowDrive, { 12.0f });
            expectWithinAbsoluteError (value (h, pid::satHighDrive), 24.0f, 0.01f, "high stops at the end");
            expectWithinAbsoluteError (value (h, pid::satMidDrive), 8.0f, 0.01f, "mid moves by the clamped amount");
            userEdit (h, pid::satMidDrive, { 4.0f, 8.0f });
            expectWithinAbsoluteError (value (h, pid::satLowDrive), 12.0f, 0.01f, "back where it started");
        }

        beginTest ("Link off, automation, other parameters: nothing else moves");
        {
            Harness h;
            ui::SatLink link (h.proc.getState(), 0);
            setPlain (h, pid::satLowDrive, 6.0f);
            setPlain (h, pid::satMidDrive, 6.0f);
            setPlain (h, pid::satHighDrive, 6.0f);
            setPlain (h, pid::satLink, 0.0f);
            userEdit (h, pid::satLowDrive, { 12.0f });
            expectWithinAbsoluteError (value (h, pid::satMidDrive), 6.0f, 0.01f, "link off");
            setPlain (h, pid::satLink, 1.0f);
            setPlain (h, pid::satHighDrive, 18.0f);   // automation: no gesture
            expectWithinAbsoluteError (value (h, pid::satMidDrive), 6.0f, 0.01f, "automation never links");
            userEdit (h, pid::satDrive, { 18.0f });
            expectWithinAbsoluteError (value (h, pid::satMidDrive), 6.0f, 0.01f, "the single-band drive is not linked");
        }

        beginTest ("Link: the second instance links its own bands only");
        {
            Harness h;
            ui::SatLink link (h.proc.getState(), 1);
            for (int k : { 0, 1 })
            {
                setPlain (h, id (pid::satLink, k), 1.0f);
                for (auto* b : { pid::satLowDrive, pid::satMidDrive, pid::satHighDrive })
                    setPlain (h, id (b, k), 6.0f);
            }
            userEdit (h, id (pid::satLowDrive, 1), { 9.0f });
            expectWithinAbsoluteError (value (h, id (pid::satMidDrive, 1)), 9.0f, 0.01f, "instance 2 linked");
            userEdit (h, pid::satLowDrive, { 9.0f });
            expectWithinAbsoluteError (value (h, pid::satMidDrive), 6.0f, 0.01f, "instance 1 has no link object here");
        }
    }

    void testTilePainter()
    {
        beginTest ("Segments: a tile painter paints every segment, the gestures stay");
        {
            Harness h;
            ui::Segments s (h.proc.getState(), pid::satCharacter, { "Warm", "Tape", "Tube", "Hard" }, ui::colours::teal);
            std::vector<int> painted;
            s.setTilePainter ([&] (juce::Graphics&, int segment, juce::Rectangle<float> r, bool selected, bool)
            {
                expect (r.getWidth() > 10.0f);
                painted.push_back (segment * 10 + (selected ? 1 : 0));
            });
            s.setBounds (0, 0, 280, 64);
            setPlain (h, pid::satCharacter, 2.0f);
            s.createComponentSnapshot (s.getLocalBounds());
            std::sort (painted.begin(), painted.end());
            expect (painted == std::vector<int> ({ 0, 10, 21, 30 }), "four tiles, the third selected");
            s.clickSegment (3);
            expectEquals ((int) value (h, pid::satCharacter), 3);
        }
    }

    void testChainAccessor()
    {
        beginTest ("VocalChain::getSaturation gives each instance's module (meters for the panel)");
        {
            Harness h;
            const auto& chain = h.proc.getChain();
            expect (&chain.getSaturation (0) != &chain.getSaturation (1));
            expect (&chain.getSaturation (99) == &chain.getSaturation (dsp::maxInstances (ModuleId::sat) - 1), "clamped");
        }
    }
};

static SatPanelTests satPanelTests;
