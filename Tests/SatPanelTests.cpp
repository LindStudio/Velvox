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

    juce::MouseEvent mouseAt (juce::Component& c, juce::Point<float> pos, juce::Point<float> downPos, bool dragged,
                              juce::ModifierKeys mods = {}, int clicks = 1)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, mods,
                                 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, downPos, now, clicks, dragged);
    }

    // Press, drag by `delta` in steps, release - on component c (local coordinates)
    void dragBy (juce::Component& c, juce::Point<float> delta, juce::ModifierKeys mods = {})
    {
        const auto start = c.getLocalBounds().getCentre().toFloat();
        c.mouseDown (mouseAt (c, start, start, false, mods));
        for (int i = 1; i <= 5; ++i)
            c.mouseDrag (mouseAt (c, start + delta * ((float) i / 5.0f), start, true, mods));
        c.mouseUp (mouseAt (c, start + delta, start, true, mods));
    }

    void clickOn (juce::Component& c)
    {
        const auto p = c.getLocalBounds().getCentre().toFloat();
        c.mouseDown (mouseAt (c, p, p, false));
        c.mouseUp (mouseAt (c, p, p, false));
    }

    std::unique_ptr<ui::LiveData> liveFor (Harness& h)
    {
        auto live = std::make_unique<ui::LiveData>();
        live->settings = h.proc.readEffectiveSettings();
        return live;
    }

    // Visible all the way up to `root`
    bool shownIn (juce::Component& c, juce::Component& root)
    {
        for (auto* p = &c; p != nullptr && p != &root; p = p->getParentComponent())
            if (! p->isVisible())
                return false;
        return true;
    }

    template <typename T>
    std::vector<T*> findAll (juce::Component& root, bool visibleOnly)
    {
        std::vector<T*> out;
        std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
        {
            for (auto* child : c.getChildren())
            {
                if (auto* t = dynamic_cast<T*> (child); t != nullptr && (! visibleOnly || shownIn (*child, root)))
                    out.push_back (t);
                walk (*child);
            }
        };
        walk (root);
        return out;
    }

    juce::StringArray shownSegments (juce::Component& panel)
    {
        juce::StringArray ids;
        for (auto* s : findAll<ui::Segments> (panel, true))
            ids.add (s->getParamId());
        return ids;
    }

    // The big Drive: the visible knob with the largest dial
    ui::Knob* bigKnob (juce::Component& panel)
    {
        ui::Knob* best = nullptr;
        for (auto* k : findAll<ui::Knob> (panel, true))
            if (best == nullptr || k->getDialSize() > best->getDialSize())
                best = k;
        return best;
    }

    std::unique_ptr<ui::DetailPanel> satPanel (Harness& h, int instance = 0)
    {
        auto r = h.proc.getRouting();
        for (int k = 0; k <= instance; ++k)
            if (! r.contains (ModuleId::sat, k))
                r.insertDefault (ModuleId::sat, k);
        h.proc.setRouting (r);
        auto panel = ui::createDetailPanel (h.proc, dsp::selectionCode (ModuleId::sat, instance));
        panel->setBounds (VelvoxAudioProcessorEditor::panelArea().withZeroOrigin());
        panel->refresh();
        return panel;
    }
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
        testView();
        testPanel();
    }

private:
    void testPanel()
    {
        beginTest ("Panel: main row = character tiles, Multiband, Link, Mix (0.6.1); big Drive in the visual; Mehr: Output, crossovers");
        {
            Harness h;
            auto panel = satPanel (h);
            const auto& main = panel->getMainControls();
            expectEquals ((int) main.size(), 4);
            if (main.size() == 4)
            {
                auto* mix = dynamic_cast<ui::Knob*> (main[3]);
                expect (mix != nullptr && mix->getParamId() == juce::String (pid::satMix), "Mix in the main row");
                expect (mix != nullptr && mix->getLabel() == "MIX", "labelled MIX");
                expect (mix != nullptr && panel->getMainRowArea().contains (mix->getBounds().getCentre()) && mix->getWidth() >= 60,
                        "placed in the row " + (mix != nullptr ? mix->getBounds().toString() : juce::String()));
                for (int i = 0; i < 3 && mix != nullptr; ++i)
                    expect (! main[(size_t) i]->getBounds().intersects (mix->getBounds()), "no overlap with main control " + juce::String (i));
                auto tiles = findAll<ui::Segments> (*main[0], true);
                expect (tiles.size() == 1 && tiles[0]->hasTilePainter() && tiles[0]->getNumSegments() == 4, "four character tiles");
                auto* mb = dynamic_cast<ui::Toggle*> (main[1]);
                auto* link = dynamic_cast<ui::Toggle*> (main[2]);
                expect (mb != nullptr && mb->getParamId() == juce::String (pid::satMultiband), "Multiband switch");
                expect (link != nullptr && link->getParamId() == juce::String (pid::satLink), "Link switch");
                expect (main[0]->getHeight() >= 60, "tiles are tall " + main[0]->getBounds().toString());
            }
            expect (shownSegments (*panel) == juce::StringArray (pid::satCharacter), "single band: satCharacter");
            auto* drive = bigKnob (*panel);
            expect (drive != nullptr && drive->getParamId() == juce::String (pid::satDrive) && drive->getDialSize() >= 100, "big Drive = satDrive");
            expect (drive != nullptr && panel->getVisualArea().contains (drive->getBounds()), "Drive in the visual");

            juce::StringArray more;
            for (auto* c : panel->getMoreControls())
                if (auto* k = dynamic_cast<ui::Knob*> (c))
                    more.add (k->getParamId());
            for (auto* id : { pid::satOutput, pid::satXoverLow, pid::satXoverHigh })
                expect (more.contains (id), juce::String ("Mehr has ") + id);
            expect (! more.contains (pid::satMix), "Mix is no longer (only) in Mehr: one control per parameter");
        }

        beginTest ("Panel: multiband shows the selected band's tiles and Drive; the strip selects; automation follows");
        {
            Harness h;
            auto panel = satPanel (h);
            setPlain (h, pid::satMultiband, 1.0f);   // automation
            panel->refresh();
            auto views = findAll<ui::SatView> (*panel, true);
            expect (views.size() == 1, "the visual");
            if (views.empty())
                return;
            auto& view = *views[0];
            auto live = liveFor (h);
            view.update (*live);
            expect (shownSegments (*panel) == juce::StringArray (pid::satMidChar), "Mid selected: its tiles");
            expect (bigKnob (*panel)->getParamId() == juce::String (pid::satMidDrive), "Mid Drive");
            expectEquals (bigKnob (*panel)->getLabel(), juce::String ("MID DRIVE"));

            clickOn (view.getBandArea (0));
            expect (shownSegments (*panel) == juce::StringArray (pid::satLowChar), "Low tiles after a click on the low band");
            expect (bigKnob (*panel)->getParamId() == juce::String (pid::satLowDrive), "Low Drive");

            // Tiles set the selected band's character
            auto tiles = findAll<ui::Segments> (*panel, true);
            if (! tiles.empty())
                tiles[0]->clickSegment (2);
            expectEquals ((int) value (h, pid::satLowChar), 2, "Low = Tube");
            expectEquals ((int) value (h, pid::satCharacter), 0, "single band untouched");

            // Link through the panel's Drive: a user gesture moves all bands
            setPlain (h, pid::satLink, 1.0f);
            userEdit (h, pid::satLowDrive, { 10.0f });
            expectWithinAbsoluteError (value (h, pid::satHighDrive), 10.0f, 0.01f, "linked");

            setPlain (h, pid::satMultiband, 0.0f);
            panel->refresh();
            view.update (*live);
            expect (shownSegments (*panel) == juce::StringArray (pid::satCharacter), "back to single band");
            expect (bigKnob (*panel)->getParamId() == juce::String (pid::satDrive));
            expectEquals (bigKnob (*panel)->getLabel(), juce::String ("DRIVE"));
        }

        beginTest ("Panel: the second instance binds its own parameters and meters");
        {
            Harness h;
            auto panel = satPanel (h, 1);
            setPlain (h, id (pid::satMultiband, 1), 1.0f);
            panel->refresh();
            for (auto* s : findAll<ui::Segments> (*panel, false))
                expect (s->getParamId().endsWith ("_2"), "segments " + s->getParamId());
            for (auto* k : findAll<ui::Knob> (*panel, false))
                expect (k->getParamId().endsWith ("_2"), "knob " + k->getParamId());
            for (auto* t : findAll<ui::Toggle> (*panel, false))
                expect (t->getParamId().endsWith ("_2"), "switch " + t->getParamId());
            expect (shownSegments (*panel) == juce::StringArray (id (pid::satMidChar, 1)), "instance 2 multiband: mid tiles");
        }
    }

    void testView()
    {
        beginTest ("SatView: log axis, multiband strip with three bands and two crossovers");
        {
            Harness h;
            setPlain (h, pid::satMultiband, 1.0f);
            ui::SatView view (h.proc, 0);
            view.setBounds (0, 0, 720, 270);
            auto live = liveFor (h);
            view.update (*live);
            expect (view.isMultiband());
            for (float x : { 40.0f, 200.0f, 500.0f })
                expectWithinAbsoluteError (view.xForFrequency (view.frequencyForX (x)), x, 0.01f, "round trip");
            expectWithinAbsoluteError (view.getBandBounds (0).getRight(), view.xForFrequency (250.0f), 0.6f, "low band ends at 250 Hz");
            expectWithinAbsoluteError (view.getBandBounds (2).getX(), view.xForFrequency (4000.0f), 0.6f, "high band from 4 kHz");
            expectWithinAbsoluteError (view.getBandBounds (0).getX(), view.getStripArea().getX(), 0.6f, "strip start");
            expectWithinAbsoluteError (view.getBandBounds (2).getRight(), view.getStripArea().getRight(), 0.6f, "strip end");
            for (int k : { 0, 1 })
            {
                expect (view.getCrossoverHandle (k).isVisible(), "crossover handle visible");
                expect (view.getStripArea().contains (view.getCrossoverHandle (k).getBounds().toFloat().getCentre()), "handle on the strip");
            }
            for (int b = 0; b < 3; ++b)
                expect (view.getBandSwitch (b).isVisible() && view.getBandSwitch (b).getParamId() == juce::String (b == 0 ? "satLowOn" : b == 1 ? "satMidOn" : "satHighOn"),
                        "band switch");
            expectWithinAbsoluteError (view.getCrossoverHandle (0).getBounds().toFloat().getCentreX(), view.xForFrequency (250.0f), 1.0f, "handle at 250 Hz");

            // Automation moves the handles
            setPlain (h, pid::satXoverLow, 500.0f);
            view.update (*live);
            expectWithinAbsoluteError (view.getCrossoverHandle (0).getBounds().toFloat().getCentreX(), view.xForFrequency (500.0f), 1.0f, "handle follows");

            // Single band: one band over the whole strip, no handles, no band switches
            setPlain (h, pid::satMultiband, 0.0f);
            view.update (*live);
            expect (! view.isMultiband());
            expectEquals (view.getSelectedBand(), -1);
            expectWithinAbsoluteError (view.getBandBounds (0).getWidth(), view.getStripArea().getWidth(), 0.6f, "full width");
            expect (! view.getCrossoverHandle (0).isVisible() && ! view.getBandSwitch (0).isVisible(), "no handles / switches");
            expect (! view.getBandArea (1).isVisible(), "only one band");
        }

        beginTest ("SatView crossover handles: drag (one undo step), fine, order guard, reset, wheel");
        {
            Harness h;
            setPlain (h, pid::satMultiband, 1.0f);
            ui::SatView view (h.proc, 0);
            view.setBounds (0, 0, 720, 270);
            auto live = liveFor (h);
            view.update (*live);
            h.proc.getUndo().clear();

            auto& low = view.getCrossoverHandle (0);
            const float pxPerOctave = view.xForFrequency (2000.0f) - view.xForFrequency (1000.0f);
            dragBy (low, { pxPerOctave, 0.0f });
            expectWithinAbsoluteError (value (h, pid::satXoverLow), 500.0f, 10.0f, "one octave up");
            expectEquals (h.proc.getUndo().getNumSteps(), 1, "one undo step");

            dragBy (low, { pxPerOctave, 0.0f }, juce::ModifierKeys::shiftModifier);
            expectWithinAbsoluteError (value (h, pid::satXoverLow), 500.0f * std::pow (2.0f, 0.1f), 10.0f, "Shift: fine (1/10)");

            dragBy (low, { 600.0f, 0.0f });
            expectLessOrEqual (value (h, pid::satXoverLow), value (h, pid::satXoverHigh) / 1.5f + 1.0f, "stops at high / 1.5");
            auto& high = view.getCrossoverHandle (1);
            view.update (*live);
            dragBy (high, { -600.0f, 0.0f });
            expectGreaterOrEqual (value (h, pid::satXoverHigh), value (h, pid::satXoverLow) * 1.5f - 1.0f, "high stays above low x 1.5");

            const auto c = low.getLocalBounds().getCentre().toFloat();
            low.mouseDoubleClick (mouseAt (low, c, c, false, {}, 2));
            expectWithinAbsoluteError (value (h, pid::satXoverLow), 250.0f, 0.5f, "double-click: default");
            setPlain (h, pid::satXoverLow, 400.0f);
            low.mouseDown (mouseAt (low, c, c, false, juce::ModifierKeys::altModifier));
            low.mouseUp (mouseAt (low, c, c, false, juce::ModifierKeys::altModifier));
            expectWithinAbsoluteError (value (h, pid::satXoverLow), 250.0f, 0.5f, "Alt+click: default");

            juce::MouseWheelDetails w {};
            w.deltaY = 0.25f;
            low.mouseWheelMove (mouseAt (low, c, c, false), w);
            expectGreaterThan (value (h, pid::satXoverLow), 251.0f, "wheel up: higher");
        }

        beginTest ("SatView band areas: click selects, vertical drag = that band's drive, double-click resets");
        {
            Harness h;
            setPlain (h, pid::satMultiband, 1.0f);
            setPlain (h, pid::satLink, 0.0f);
            ui::SatView view (h.proc, 0);
            view.setBounds (0, 0, 720, 270);
            auto live = liveFor (h);
            view.update (*live);
            int selected = -9;
            view.onSelectBand = [&] (int b) { selected = b; };
            expectEquals (view.getSelectedBand(), 1, "Mid selected at first");

            clickOn (view.getBandArea (2));
            expectEquals (view.getSelectedBand(), 2);
            expectEquals (selected, 2, "callback");
            expectWithinAbsoluteError (value (h, pid::satHighDrive), 6.0f, 0.01f, "a click changes nothing");

            dragBy (view.getBandArea (0), { 0.0f, -55.0f });
            expectWithinAbsoluteError (value (h, pid::satLowDrive), 12.0f, 0.1f, "55 px up = +6 dB");
            expectEquals (view.getSelectedBand(), 0, "dragging selects the band");
            expectWithinAbsoluteError (value (h, pid::satMidDrive), 6.0f, 0.01f, "the others stay (link off)");

            auto& area = view.getBandArea (0);
            const auto c = area.getLocalBounds().getCentre().toFloat();
            area.mouseDoubleClick (mouseAt (area, c, c, false, {}, 2));
            expectWithinAbsoluteError (value (h, pid::satLowDrive), 6.0f, 0.01f, "double-click: default drive");
        }
    }

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
