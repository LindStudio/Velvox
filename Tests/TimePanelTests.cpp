// The time-module panels of 0.6 (spec 5.11 Delay echo map, 5.13 Stutter cuts on the waveform, 5.14 Pulse
// "like Kickstart"): what they draw comes from the DSP feeds, every graph gesture edits the right parameters
// as one undo step.

#include "TestHelpers.h"
#include "PluginEditor.h"
#include "UI/TimePanels.h"
#include "UI/ChainView.h"
#include "UI/HelpContent.h"

using namespace velvox;
using namespace velvox::test;
using dsp::ModuleId;

namespace
{
    juce::MouseEvent mouseAt (juce::Component& c, juce::Point<float> pos, juce::Point<float> downPos, bool dragged,
                              juce::ModifierKeys mods = {}, int clicks = 1)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, mods,
                                 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, downPos, now, clicks, dragged);
    }

    juce::MouseWheelDetails wheel (float dy)
    {
        juce::MouseWheelDetails w;
        w.deltaX = 0.0f;
        w.deltaY = dy;
        w.isReversed = false;
        w.isSmooth = false;
        w.isInertial = false;
        return w;
    }

    // Press at `from`, move to `to` in one step, release
    void drag (juce::Component& c, juce::Point<float> from, juce::Point<float> to, juce::ModifierKeys mods = {})
    {
        c.mouseDown (mouseAt (c, from, from, false, mods));
        c.mouseDrag (mouseAt (c, to, from, true, mods));
        c.mouseUp (mouseAt (c, to, from, true, mods));
    }

    ui::LiveData liveFor (Harness& h, int instance = 0)
    {
        ui::LiveData live;
        live.settings = h.proc.readEffectiveSettings();
        if (instance > 0)
            live.settings.setInstance0 (live.settings.instance (instance));
        live.sampleRate = h.fs;
        return live;
    }

    const juce::ModifierKeys shift { juce::ModifierKeys::shiftModifier };
    const juce::ModifierKeys alt { juce::ModifierKeys::altModifier };

    template <typename T>
    T* findMain (ui::DetailPanel& panel, const juce::String& paramId)
    {
        for (auto* c : panel.getMainControls())
            if (auto* t = dynamic_cast<T*> (c); t != nullptr && t->getParamId() == paramId)
                return t;
        return nullptr;
    }

    template <typename T>
    T* findIn (juce::Component& parent, const juce::String& paramId)
    {
        for (auto* c : parent.getChildren())
        {
            if (auto* t = dynamic_cast<T*> (c); t != nullptr && t->getParamId() == paramId)
                return t;
            if (auto* deeper = findIn<T> (*c, paramId))
                return deeper;
        }
        return nullptr;
    }
}

class TimePanelTests final : public juce::UnitTest
{
public:
    TimePanelTests() : juce::UnitTest ("Headliner time panels", "Headliner") {}

    void runTest() override
    {
        delayMap();
        delayPanel();
        pulsePanel();
        pulseTiles();
        pulseCurve();
    }

private:
    // A view sized like the panel's visual, fed once
    struct Map
    {
        Harness& h;
        ui::EchoMapView view;
        Map (Harness& harness, int instance = 0, double bpm = 0.0) : h (harness), view (harness.proc, instance)
        {
            view.tempoSource = [bpm] { return bpm; };
            view.setBounds (0, 0, 900, 280);
            feed (instance);
        }
        void feed (int instance = 0) { view.update (liveFor (h, instance)); }
        juce::Point<float> barCentre (int tap) const
        {
            auto b = view.getBarBounds (tap, true);
            if (b.isEmpty())
                b = view.getBarBounds (tap, false);
            return b.getCentre();
        }
    };

    void delayMap()
    {
        beginTest ("Delay map: bars are DelayModule::echoTaps (plain, spread, ping-pong)");
        {
            for (int variant = 0; variant < 3; ++variant)
            {
                Harness h;
                h.set (pid::delayTime, 300.0f);
                h.set (pid::delayFeedback, 50.0f);
                h.set (pid::delaySpread, variant == 1 ? 20.0f : 0.0f);
                h.set (pid::delayPingPong, variant == 2 ? 1.0f : 0.0f);
                Map m (h);

                auto ds = liveFor (h).settings.delay;
                ds.timeMs = 300.0f;
                std::array<dsp::DelayModule::EchoTap, 64> expected {};
                const int n = dsp::DelayModule::echoTaps (ds, h.fs, expected.data(), 64, ui::EchoMapView::floorDb);
                expect (n >= 5, "several echoes at 50 % feedback");
                expectEquals (m.view.getNumTaps(), n, "one bar pair per echo");
                for (int i = 0; i < std::min (n, m.view.getNumTaps()); ++i)
                {
                    expectWithinAbsoluteError (m.view.getTap (i).timeMs, expected[(size_t) i].timeMs, 0.01f);
                    expectWithinAbsoluteError (m.view.getTap (i).levelL, expected[(size_t) i].levelL, 1.0e-5f);
                    expectWithinAbsoluteError (m.view.getTap (i).levelR, expected[(size_t) i].levelR, 1.0e-5f);
                    if (expected[(size_t) i].timeMs > m.view.getWindowMs())
                        continue;
                    const auto l = m.view.getBarBounds (i, true), r = m.view.getBarBounds (i, false);
                    const float floorLin = juce::Decibels::decibelsToGain (ui::EchoMapView::floorDb);
                    expect (l.isEmpty() == (expected[(size_t) i].levelL <= floorLin), "left bar exists exactly when L sounds");
                    expect (r.isEmpty() == (expected[(size_t) i].levelR <= floorLin), "right bar exists exactly when R sounds");
                    const float cy = m.view.getPlotArea().getCentreY();
                    if (! l.isEmpty())
                    {
                        expectWithinAbsoluteError (l.getBottom(), cy, 1.0f, "L above the centre line");
                        expectWithinAbsoluteError (l.getHeight() / (m.view.getPlotArea().getHeight() * 0.5f),
                                                   ui::EchoMapView::heightForLevel (expected[(size_t) i].levelL), 0.02f, "height = level");
                        expectWithinAbsoluteError (l.getCentreX(), m.view.xForMs (expected[(size_t) i].timeMs), 0.6f, "x = time");
                    }
                    if (! r.isEmpty())
                        expectWithinAbsoluteError (r.getY(), cy, 1.0f, "R below the centre line");
                }
                if (variant == 2)
                {
                    expect (! m.view.getBarBounds (0, true).isEmpty() && m.view.getBarBounds (0, false).isEmpty(), "ping-pong: echo 1 left");
                    expect (m.view.getBarBounds (1, true).isEmpty() && ! m.view.getBarBounds (1, false).isEmpty(), "ping-pong: echo 2 right");
                }
            }
        }

        beginTest ("Delay map: drag the first echo = time (free ms, Shift/Ctrl fine)");
        {
            Harness h;
            h.set (pid::delayTime, 300.0f);
            Map m (h);
            const auto from = m.barCentre (0);
            drag (m.view, from, { m.view.xForMs (450.0f), from.y });
            expectWithinAbsoluteError (h.get (pid::delayTime), 450.0f, 2.0f);
            expectEquals (h.get (pid::delaySync), 0.0f, "free stays free");

            h.set (pid::delayTime, 300.0f);
            m.feed();
            const auto from2 = m.barCentre (0);
            drag (m.view, from2, { m.view.xForMs (450.0f), from2.y }, shift);
            expectWithinAbsoluteError (h.get (pid::delayTime), 315.0f, 1.0f, "Shift: a tenth of the move");

            // Any echo can be grabbed: echo 2 follows the pointer, so the time is half of its position
            h.set (pid::delayTime, 300.0f);
            h.set (pid::delayFeedback, 50.0f);
            m.feed();
            const auto from3 = m.barCentre (1);
            drag (m.view, from3, { m.view.xForMs (800.0f), from3.y });
            expectWithinAbsoluteError (h.get (pid::delayTime), 400.0f, 2.0f, "echo 2 at 800 ms = 400 ms time");
        }

        beginTest ("Delay map: synced drags snap to note values, Shift leaves the grid");
        {
            Harness h;
            h.set (pid::delaySync, 1.0f);
            h.set (pid::delayDivision, 9.0f);   // 1/4 = 500 ms at 120 BPM
            Map m (h, 0, 120.0);
            expectWithinAbsoluteError (m.view.getEffectiveTimeMs(), 500.0f, 0.01f, "synced time from the host tempo");
            const auto from = m.barCentre (0);
            drag (m.view, from, { m.view.xForMs (760.0f), from.y });
            expectEquals ((int) h.get (pid::delayDivision), 10, "760 ms snaps to 1/4D (750 ms)");
            expectEquals (h.get (pid::delaySync), 1.0f);

            h.set (pid::delayDivision, 9.0f);
            m.feed();
            const auto from2 = m.barCentre (0);
            drag (m.view, from2, { m.view.xForMs (760.0f), from2.y }, shift);
            expectEquals (h.get (pid::delaySync), 0.0f, "Shift: free time");
            expectWithinAbsoluteError (h.get (pid::delayTime), 760.0f, 3.0f);
        }

        beginTest ("Delay map: synced without host tempo drags milliseconds");
        {
            Harness h;
            h.set (pid::delaySync, 1.0f);
            h.set (pid::delayDivision, 9.0f);
            h.set (pid::delayTime, 300.0f);
            Map m (h, 0, 0.0);
            expectWithinAbsoluteError (m.view.getEffectiveTimeMs(), 300.0f, 0.01f, "no tempo: the delay runs on ms");
            const auto from = m.barCentre (0);
            drag (m.view, from, { m.view.xForMs (420.0f), from.y });
            expectWithinAbsoluteError (h.get (pid::delayTime), 420.0f, 2.0f);
            expectEquals ((int) h.get (pid::delayDivision), 9, "note value untouched");
        }

        beginTest ("Delay map: vertical drag = feedback (repeat ratio, feedbackForRatio)");
        {
            Harness h;
            h.set (pid::delayTime, 300.0f);
            h.set (pid::delayFeedback, 30.0f);
            Map m (h);
            auto ds = liveFor (h).settings.delay;
            ds.timeMs = 300.0f;
            const float ratio0 = m.view.getRepeatRatio();
            expect (ratio0 > 0.05f && ratio0 < 0.6f, "ratio from the bars " + juce::String (ratio0));
            const auto from = m.barCentre (0);
            drag (m.view, from, from.translated (0.0f, -60.0f));
            const float expected = 100.0f * dsp::DelayModule::feedbackForRatio (ds, h.fs, ratio0 + 60.0f / ui::EchoMapView::pixelsPerRatio);
            expectWithinAbsoluteError (h.get (pid::delayFeedback), expected, 0.5f, "up = more feedback");
            expectWithinAbsoluteError (h.get (pid::delayTime), 300.0f, 0.01f, "time untouched");

            h.set (pid::delayFeedback, 30.0f);
            m.feed();
            drag (m.view, m.barCentre (0), m.barCentre (0).translated (0.0f, 40.0f));
            expect (h.get (pid::delayFeedback) < 30.0f, "down = less feedback");
        }

        beginTest ("Delay map: one drag is one undo step");
        {
            Harness h;
            h.set (pid::delayTime, 300.0f);
            h.set (pid::delayFeedback, 30.0f);
            Map m (h);
            const int before = h.proc.getUndo().getNumSteps();
            const auto from = m.barCentre (0);
            m.view.mouseDown (mouseAt (m.view, from, from, false));
            for (int i = 1; i <= 10; ++i)
                m.view.mouseDrag (mouseAt (m.view, from.translated (8.0f * (float) i, -4.0f * (float) i), from, true));
            m.view.mouseUp (mouseAt (m.view, from.translated (80.0f, -40.0f), from, true));
            expectEquals (h.proc.getUndo().getNumSteps(), before + 1);
            expect (h.get (pid::delayTime) > 320.0f && h.get (pid::delayFeedback) > 30.5f, "both moved");
            h.proc.getUndo().undo();
            expectWithinAbsoluteError (h.get (pid::delayTime), 300.0f, 0.01f);
            expectWithinAbsoluteError (h.get (pid::delayFeedback), 30.0f, 0.01f);
        }

        beginTest ("Delay map: double-click / Alt+click reset time and feedback, wheel steps feedback");
        {
            Harness h;
            h.set (pid::delayTime, 600.0f);
            h.set (pid::delayFeedback, 60.0f);
            Map m (h);
            const int before = h.proc.getUndo().getNumSteps();
            const auto at = m.barCentre (0);
            m.view.mouseDoubleClick (mouseAt (m.view, at, at, false, {}, 2));
            expectWithinAbsoluteError (h.get (pid::delayTime), 375.0f, 0.01f);
            expectWithinAbsoluteError (h.get (pid::delayFeedback), 30.0f, 0.01f);
            expectEquals (h.proc.getUndo().getNumSteps(), before + 1, "reset = one step");

            h.set (pid::delayTime, 600.0f);
            m.feed();
            const auto at2 = m.barCentre (0);
            m.view.mouseDown (mouseAt (m.view, at2, at2, false, alt));
            m.view.mouseUp (mouseAt (m.view, at2, at2, false, alt));
            expectWithinAbsoluteError (h.get (pid::delayTime), 375.0f, 0.01f, "Alt+click");

            m.feed();
            m.view.mouseWheelMove (mouseAt (m.view, m.barCentre (0), m.barCentre (0), false), wheel (0.25f));
            expect (h.get (pid::delayFeedback) > 30.0f, "wheel up = more feedback");
        }

        beginTest ("Delay map: instance 2 edits its own parameters");
        {
            Harness h;
            const auto timeId = dsp::instanceParamId (pid::delayTime, 1);
            const auto fbId = dsp::instanceParamId (pid::delayFeedback, 1);
            expect (h.proc.getState().getParameter (timeId) != nullptr, "delayTime_2 exists");
            h.set (timeId.toRawUTF8(), 250.0f);
            h.set (pid::delayTime, 375.0f);
            Map m (h, 1);
            expectWithinAbsoluteError (m.view.getEffectiveTimeMs(), 250.0f, 0.01f, "shows instance 2");
            const auto from = m.barCentre (0);
            drag (m.view, from, { m.view.xForMs (350.0f), from.y - 30.0f });
            expectWithinAbsoluteError (h.get (timeId.toRawUTF8()), 350.0f, 2.0f);
            expect (h.get (fbId.toRawUTF8()) > 30.5f, "its own feedback");
            expectWithinAbsoluteError (h.get (pid::delayTime), 375.0f, 0.01f, "instance 1 untouched");
        }
    }

    void delayPanel()
    {
        beginTest ("Delay panel: note value in the main row while synced, ms knob while free; echo map visual");
        {
            Harness h;
            auto panel = ui::createDetailPanel (h.proc, (int) ModuleId::delay);
            panel->setBounds (VelvoxAudioProcessorEditor::panelArea().withZeroOrigin());
            expect (dynamic_cast<ui::EchoMapView*> (panel->getChildren().getFirst()) != nullptr
                        || findIn<ui::Knob> (*panel, pid::delayTime) != nullptr, "panel built");
            bool hasMap = false;
            for (auto* c : panel->getChildren())
                hasMap = hasMap || dynamic_cast<ui::EchoMapView*> (c) != nullptr;
            expect (hasMap, "the echo map is the visual");

            h.set (pid::delaySync, 0.0f);
            panel->refresh();
            expectEquals ((int) panel->getMainControls().size(), 5);
            expect (findMain<ui::Knob> (*panel, pid::delayTime) != nullptr, "free: Time (ms) in the main row");
            expect (findMain<ui::Segments> (*panel, pid::delayDivision) == nullptr, "free: note value in Mehr");
            expect (findIn<ui::Segments> (panel->getMoreDrawer(), pid::delayDivision) != nullptr, "note value reachable in Mehr");
            for (auto* id : { pid::delaySync, pid::delayPingPong })
                expect (findMain<ui::Toggle> (*panel, id) != nullptr, juce::String ("main: ") + id);
            for (auto* id : { pid::delayFeedback, pid::delayMix })
                expect (findMain<ui::Knob> (*panel, id) != nullptr, juce::String ("main: ") + id);

            h.set (pid::delaySync, 1.0f);
            panel->refresh();
            expectEquals ((int) panel->getMainControls().size(), 5);
            expect (findMain<ui::Segments> (*panel, pid::delayDivision) != nullptr, "synced: note value in the main row");
            auto* time = findIn<ui::Knob> (panel->getMoreDrawer(), pid::delayTime);
            expect (time != nullptr && time->getDialSize() <= 34, "synced: small ms knob in Mehr");
            auto* div = findMain<ui::Segments> (*panel, pid::delayDivision);
            expect (div != nullptr && panel->getMainRowArea().contains (div->getBounds().getCentre()), "laid out in the main row");

            h.set (pid::delaySync, 0.0f);
            panel->refresh();
            auto* knob = findMain<ui::Knob> (*panel, pid::delayTime);
            expect (knob != nullptr && knob->getDialSize() >= 50, "back: large ms knob");
        }
    }

    void pulsePanel()
    {
        beginTest ("Pulse panel: big Amount in the visual, shape tiles, rate buttons in the main row");
        {
            Harness h;
            auto panel = ui::createDetailPanel (h.proc, (int) ModuleId::pulse);
            panel->setBounds (VelvoxAudioProcessorEditor::panelArea().withZeroOrigin());
            const auto visual = panel->getVisualArea();

            auto* amount = findIn<ui::Knob> (*panel, pid::pulseAmount);
            expect (amount != nullptr && amount->getDialSize() >= 100, "one big Amount control");
            expect (amount != nullptr && visual.contains (amount->getBounds()), "Amount sits in the visual");

            ui::PulseShapeTiles* tiles = nullptr;
            for (auto* c : panel->getChildren())
                if (auto* t = dynamic_cast<ui::PulseShapeTiles*> (c))
                    tiles = t;
            expect (tiles != nullptr, "shape tiles");
            if (tiles != nullptr)
            {
                expect (visual.contains (tiles->getBounds()), "tiles in the visual");
                expectEquals (tiles->getNumTiles(), 5, "Custom + 4 shapes");
                expectEquals (tiles->getProperties()["paramId"].toString(), juce::String (pid::pulseShape), "bound to pulseShape");
                for (int i = 0; i < tiles->getNumTiles(); ++i)
                    expect (tiles->getTileBounds (i).getWidth() >= 70 && tiles->getTileBounds (i).getHeight() >= 40, "tiles large enough");
            }

            auto* rate = findMain<ui::Segments> (*panel, pid::pulseRate);
            expect (rate != nullptr, "rate buttons in the main row");
            if (rate != nullptr)
            {
                const auto& names = dsp::PulseModule::rateNames();
                juce::StringArray shown;
                for (int i = 0; i < rate->getNumSegments(); ++i)
                    shown.add (names[rate->getChoiceForSegment (i)]);
                expectEquals (shown.joinIntoString (" "), juce::String ("1/1 1/2 1/4 1/8 1/16"));
                expect (rate->getOverflowSegment() >= 0, "triplets / dotted behind the overflow");
            }
            expectEquals ((int) panel->getMainControls().size(), 3);
            for (auto* id : { pid::pulseMix, pid::pulseSmooth })
                expect (findMain<ui::Knob> (*panel, id) != nullptr, juce::String ("main: ") + id);
            for (auto* id : { pid::pulseDepth, pid::pulseAttack, pid::pulseRelease, pid::pulseTension, pid::pulseBpm })
                expect (findIn<ui::Knob> (panel->getMoreDrawer(), id) != nullptr, juce::String ("Mehr: ") + id);
            expect (findIn<ui::Segments> (panel->getMoreDrawer(), pid::pulseInterp) != nullptr, "Mehr: interpolation");
        }
    }

    void pulseTiles()
    {
        beginTest ("Pulse tiles: click = one undo step, double-click / Alt+click = Custom, wheel = previous / next");
        {
            Harness h;
            ui::PulseShapeTiles tiles (h.proc);
            tiles.setBounds (0, 0, 640, 64);
            const auto refresh = [&] { tiles.refresh (h.proc.readEffectiveSettings().pulse); };
            refresh();
            expectEquals (tiles.getSelectedTile(), 0, "old states: Custom");

            const int before = h.proc.getUndo().getNumSteps();
            const auto at = tiles.getTileBounds (2).getCentre().toFloat();
            tiles.mouseDown (mouseAt (tiles, at, at, false));
            tiles.mouseUp (mouseAt (tiles, at, at, false));
            expectEquals ((int) h.get (pid::pulseShape), 2, "click chooses");
            expectEquals (h.proc.getUndo().getNumSteps(), before + 1, "one undo step");
            refresh();
            expectEquals (tiles.getSelectedTile(), 2);

            tiles.mouseDoubleClick (mouseAt (tiles, at, at, false, {}, 2));
            expectEquals ((int) h.get (pid::pulseShape), 0, "double-click: Custom");

            h.set (pid::pulseShape, 3.0f);
            const auto at4 = tiles.getTileBounds (4).getCentre().toFloat();
            tiles.mouseDown (mouseAt (tiles, at4, at4, false, alt));
            expectEquals ((int) h.get (pid::pulseShape), 0, "Alt+click: Custom");

            h.set (pid::pulseShape, 2.0f);
            refresh();
            tiles.mouseWheelMove (mouseAt (tiles, at, at, false), wheel (-0.25f));
            expectEquals ((int) h.get (pid::pulseShape), 3, "wheel down = next");
            tiles.mouseWheelMove (mouseAt (tiles, at, at, false), wheel (0.25f));
            expectEquals ((int) h.get (pid::pulseShape), 2, "wheel up = previous");
        }

        beginTest ("Pulse tiles: switching shapes never touches the custom points");
        {
            Harness h;
            h.set (pid::pulseX[2], 0.33f);
            h.set (pid::pulseY[2], 0.4f);
            ui::PulseShapeTiles tiles (h.proc);
            tiles.setBounds (0, 0, 640, 64);
            tiles.chooseTile (1);
            tiles.chooseTile (4);
            tiles.chooseTile (0);
            expectEquals ((int) h.get (pid::pulseShape), 0);
            expectWithinAbsoluteError (h.get (pid::pulseX[2]), 0.33f, 1.0e-4f);
            expectWithinAbsoluteError (h.get (pid::pulseY[2]), 0.4f, 1.0e-4f);
        }
    }

    void pulseCurve()
    {
        beginTest ("Pulse curve: the drawn gain is PulseModule::gainAt (shape, amount, depth)");
        {
            Harness h;
            ui::PulseCurveView view (h.proc);
            view.setBounds (0, 0, 720, 220);
            for (int shape = 0; shape < 5; ++shape)
                for (float amount : { 0.0f, 25.0f, 50.0f, 100.0f })
                {
                    h.set (pid::pulseShape, (float) shape);
                    h.set (pid::pulseAmount, amount);
                    const auto live = liveFor (h);
                    view.update (live);
                    float worst = 0.0f;
                    for (int i = 0; i <= 50; ++i)
                    {
                        const float ph = (float) i / 50.0f * 0.999f;
                        worst = std::max (worst, std::abs (view.drawnGainAt (ph) - dsp::PulseModule::gainAt (live.settings.pulse, ph)));
                    }
                    expectLessThan (worst, 0.01f, "shape " + juce::String (shape) + " amount " + juce::String (amount));
                    expect (view.yForGain (1.0f) < view.yForGain (0.0f), "gain 1 at the top");
                }
        }

        beginTest ("Pulse curve: Custom shows the 6 points as handles, a curated shape hides them");
        {
            Harness h;
            ui::PulseCurveView view (h.proc);
            view.setBounds (0, 0, 720, 220);
            h.set (pid::pulseShape, 0.0f);
            view.update (liveFor (h));
            expect (view.showsPoints(), "Custom: points");
            const auto plot = view.getPlotArea();
            for (int i = 0; i < 6; ++i)
            {
                const auto pt = view.pointPosition (i);
                expectWithinAbsoluteError (pt.x, plot.getX() + plot.getWidth() * h.get (pid::pulseX[i]), 0.5f);
                expectWithinAbsoluteError (pt.y, plot.getBottom() - plot.getHeight() * h.get (pid::pulseY[i]), 0.5f);
            }

            h.set (pid::pulseShape, 2.0f);
            view.update (liveFor (h));
            expect (! view.showsPoints(), "curated: no points");
            const int before = h.proc.getUndo().getNumSteps();
            const float x2 = h.get (pid::pulseX[2]);
            drag (view, view.pointPosition (2), view.pointPosition (2).translated (40.0f, -30.0f));
            expectEquals (h.proc.getUndo().getNumSteps(), before, "nothing to drag");
            expectWithinAbsoluteError (h.get (pid::pulseX[2]), x2, 1.0e-6f);
        }

        beginTest ("Pulse curve: dragging a point is one undo step, Shift = fine, double-click = default point");
        {
            Harness h;
            ui::PulseCurveView view (h.proc);
            view.setBounds (0, 0, 720, 220);
            view.update (liveFor (h));
            const auto plot = view.getPlotArea();
            const float x0 = h.get (pid::pulseX[2]), y0 = h.get (pid::pulseY[2]);
            const int before = h.proc.getUndo().getNumSteps();
            const auto from = view.pointPosition (2);
            drag (view, from, from.translated (0.1f * plot.getWidth(), -0.1f * plot.getHeight()));
            expectWithinAbsoluteError (h.get (pid::pulseX[2]), x0 + 0.1f, 0.005f);
            expectWithinAbsoluteError (h.get (pid::pulseY[2]), y0 + 0.1f, 0.005f);
            expectEquals (h.proc.getUndo().getNumSteps(), before + 1, "one step for x and y");

            view.update (liveFor (h));
            const auto from2 = view.pointPosition (2);
            drag (view, from2, from2.translated (0.1f * plot.getWidth(), 0.0f), shift);
            expectWithinAbsoluteError (h.get (pid::pulseX[2]), x0 + 0.11f, 0.005f, "Shift: a tenth");

            view.update (liveFor (h));
            const auto at = view.pointPosition (2);
            view.mouseDoubleClick (mouseAt (view, at, at, false, {}, 2));
            expectWithinAbsoluteError (h.get (pid::pulseX[2]), 0.28f, 1.0e-4f, "default x");
            expectWithinAbsoluteError (h.get (pid::pulseY[2]), 0.7f, 1.0e-4f, "default y");
        }
    }
};

static TimePanelTests timePanelTests;
