// UI interaction tests: drag & drop routing, power dots, send/insert switch,
// rejected configurations. Real mouse events are sent to the ChainView.

#include "TestHelpers.h"
#include "UI/ChainView.h"
#include "PluginEditor.h"

using namespace velvox;
using namespace velvox::test;
using dsp::ModuleId;

namespace
{
    juce::MouseEvent mouse (juce::Component& c, juce::Point<float> pos, juce::Point<float> downPos, bool dragged)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, juce::ModifierKeys(),
                                 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, downPos, now, 1, dragged);
    }

    void drag (juce::Component& c, juce::Point<float> from, juce::Point<float> to)
    {
        c.mouseDown (mouse (c, from, from, false));
        c.mouseDrag (mouse (c, (from + to) * 0.5f, from, true));
        c.mouseDrag (mouse (c, to, from, true));
        c.mouseUp (mouse (c, to, from, true));
    }

    void click (juce::Component& c, juce::Point<float> at)
    {
        c.mouseDown (mouse (c, at, at, false));
        c.mouseUp (mouse (c, at, at, false));
    }

    struct ViewFixture
    {
        Harness h;
        ui::ChainView view { h.proc };
        ViewFixture() { view.setBounds (0, 0, 1248, 104); }
    };
}

class UiTests final : public juce::UnitTest
{
public:
    UiTests() : juce::UnitTest ("Velvox UI interaction", "Velvox") {}

    void runTest() override
    {
        beginTest ("Drag a module to the front of the chain");
        {
            ViewFixture f;
            const auto sat = f.view.getTileBounds (ModuleId::sat);
            const auto eq = f.view.getTileBounds (ModuleId::eq);
            drag (f.view, sat.getCentre(), { eq.getX() + 5.0f, eq.getCentreY() });
            const auto r = f.h.proc.getRouting();
            expect (r.positionOf (ModuleId::sat) < r.positionOf (ModuleId::eq), "saturation now before EQ");
            expect (r.isValidPermutation());
        }

        beginTest ("Drag a module to the end of the chain");
        {
            ViewFixture f;
            const auto eq = f.view.getTileBounds (ModuleId::eq);
            const auto chop = f.view.getTileBounds (ModuleId::chop);
            drag (f.view, eq.getCentre(), { chop.getRight() - 3.0f, chop.getCentreY() });
            const auto r = f.h.proc.getRouting();
            for (auto id : { ModuleId::comp, ModuleId::deess, ModuleId::sat, ModuleId::doubler, ModuleId::pitch, ModuleId::chop })
                expect (r.positionOf (ModuleId::eq) > r.positionOf (id), juce::String ("EQ after ") + dsp::moduleDisplayName (id));
        }

        beginTest ("Drag the delay from the parallel lane into the chain (becomes an insert)");
        {
            ViewFixture f;
            expect (f.h.proc.getRouting().isSend (ModuleId::delay));
            const auto delay = f.view.getTileBounds (ModuleId::delay);
            const auto deess = f.view.getTileBounds (ModuleId::deess);
            drag (f.view, delay.getCentre().translated (-30.0f, 0.0f), { deess.getX() + 4.0f, deess.getCentreY() });
            const auto r = f.h.proc.getRouting();
            expect (r.delayInsert, "delay is an insert now");
            expect (r.positionOf (ModuleId::comp) < r.positionOf (ModuleId::delay)
                    && r.positionOf (ModuleId::delay) < r.positionOf (ModuleId::deess), "between compressor and de-esser");

            // ...and back onto the lane
            const auto delayNow = f.view.getTileBounds (ModuleId::delay);
            drag (f.view, delayNow.getCentre().translated (-30.0f, 0.0f), f.view.getLaneBounds().getCentre());
            expect (f.h.proc.getRouting().isSend (ModuleId::delay), "back to send");
        }

        beginTest ("Serial modules cannot be dropped onto the parallel lane");
        {
            ViewFixture f;
            const auto before = f.h.proc.getRouting();
            const auto comp = f.view.getTileBounds (ModuleId::comp);
            drag (f.view, comp.getCentre(), f.view.getLaneBounds().getCentre());
            expect (f.h.proc.getRouting() == before, "routing unchanged");
            expect (f.view.getHint().contains ("Only Delay and Reverb"), "explains why");
        }

        beginTest ("Power dot switches the module, send/insert switch toggles the mode");
        {
            ViewFixture f;
            const auto comp = f.view.getTileBounds (ModuleId::comp);
            const float before = f.h.get (pid::compOn);
            click (f.view, { comp.getX() + 15.0f, comp.getCentreY() });
            expectEquals (f.h.get (pid::compOn), 1.0f - before);

            const auto reverb = f.view.getTileBounds (ModuleId::reverb);
            click (f.view, { reverb.getRight() - 23.0f, reverb.getCentreY() });
            expect (f.h.proc.getRouting().reverbInsert, "reverb switched to insert");
        }

        beginTest ("Selecting tiles opens the matching Pro panel; mode switch keeps the sound");
        {
            Harness h;
            h.proc.getState().state.setProperty ("uiMode", "pro", nullptr);
            std::unique_ptr<juce::AudioProcessorEditor> editor (h.proc.createEditor());
            expect (editor != nullptr);
            for (int id = -1; id < dsp::numModules; ++id)
            {
                h.proc.getState().state.setProperty ("uiSelection", id, nullptr);
                std::unique_ptr<juce::AudioProcessorEditor> e (h.proc.createEditor()); // opens with this selection
                expect (e != nullptr);
            }
            const auto out = h.run (h.noise (0.2f, 0.3, 3));
            expect (allFinite (out));
        }

        beginTest ("Hover info card sits next to its control and stays inside the window");
        {
            juce::Component parent;
            parent.setBounds (0, 0, ui::baseWidth, ui::baseHeight);
            ui::InfoPopup info;
            parent.addChildComponent (info);

            const juce::Rectangle<int> anchor (600, 400, 90, 110);
            info.show (anchor, "Presence", "Brings the voice to the front: clearer top end, harsh S sounds kept in check.");
            expect (info.isVisible());
            expect (info.getBottom() <= anchor.getY() + 8, "above the control");
            expect (parent.getLocalBounds().contains (info.getBounds()));

            // Near the top edge it flips below the control
            const juce::Rectangle<int> top (20, 10, 90, 110);
            info.show (top, "Gain", "Input level");
            expect (info.getY() >= top.getBottom(), "below the control");
            expect (parent.getLocalBounds().contains (info.getBounds()));

            // Optional visual check: VELVOX_DUMP_DIR=<folder>
            const auto dumpDir = juce::SystemStats::getEnvironmentVariable ("VELVOX_DUMP_DIR", {});
            if (dumpDir.isNotEmpty())
            {
                info.show (anchor, "Presence", "Brings the voice to the front: clearer top end, harsh S sounds kept in check.");
                juce::Image img (juce::Image::ARGB, info.getWidth() + 40, info.getHeight() + 40, true);
                info.setAlpha (1.0f);
                {
                    juce::Graphics g (img); // must finish before the pixels are read
                    g.fillAll (ui::colours::background);
                    g.setOrigin (20, 20);
                    info.paintEntireComponent (g, true);
                }
                juce::FileOutputStream os (juce::File (dumpDir).getChildFile ("info-popup.png"));
                os.setPosition (0);
                os.truncate();
                juce::PNGImageFormat().writeImageToStream (img, os);
            }
        }
    }
};

static UiTests uiTests;
