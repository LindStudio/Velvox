// Reverb panel "Raumtiefe" (spec 5.12): the vocal as a dot in a room; its depth moves Mix, Pre-Delay and
// Size together along the curated path of dsp::reverbDepthPath (one undo step, one host gesture group).

#include "TestHelpers.h"
#include "PluginEditor.h"
#include "UI/ReverbPanel.h"
#include "DSP/ReverbDepth.h"
#include "DSP/ReverbModule.h"

using namespace velvox;
using namespace velvox::test;
using dsp::ModuleId;

namespace
{
    juce::Rectangle<int> panelSize() { return VelvoxAudioProcessorEditor::panelArea().withZeroOrigin(); }

    juce::String idFor (const char* base, int instance) { return dsp::instanceParamId (base, instance); }

    float real (Harness& h, const juce::String& id) { return h.proc.getState().getRawParameterValue (id)->load(); }

    void setReal (Harness& h, const juce::String& id, float v)
    {
        auto* p = h.proc.getState().getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (v));
    }

    // Host view of the edits: gesture begins / ends and value changes in the order they arrive
    struct HostLog final : juce::AudioProcessorListener
    {
        explicit HostLog (juce::AudioProcessor& p) : proc (p) { proc.addListener (this); }
        ~HostLog() override { proc.removeListener (this); }
        void audioProcessorParameterChanged (juce::AudioProcessor*, int index, float) override { events.add ("v" + juce::String (index)); }
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
        void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int index) override { events.add ("b" + juce::String (index)); }
        void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int index) override { events.add ("e" + juce::String (index)); }
        juce::AudioProcessor& proc;
        juce::StringArray events;
    };

    int indexOf (Harness& h, const juce::String& id)
    {
        return h.proc.getState().getParameter (id)->getParameterIndex();
    }
}

class ReverbPanelTests final : public juce::UnitTest
{
public:
    ReverbPanelTests() : juce::UnitTest ("Headliner reverb panel", "Headliner") {}

    void runTest() override
    {
        using namespace ui;

        beginTest ("setDepth writes the curated path for every room type; depth() reads it back");
        {
            Harness h;
            for (int type = 0; type < dsp::ReverbModule::numTypes; ++type)
            {
                setReal (h, pid::reverbType, (float) type);
                for (float d : { 0.0f, 0.3f, 0.7f, 1.0f })
                {
                    reverbedit::setDepth (h.proc, 0, d);
                    const auto want = dsp::reverbDepthPath (type, d);
                    const auto tag = "type " + juce::String (type) + " depth " + juce::String (d);
                    expectWithinAbsoluteError (real (h, pid::reverbMix), want.mixPercent, 0.05f, tag + " mix");
                    expectWithinAbsoluteError (real (h, pid::reverbPreDelay), want.preDelayMs, 0.05f, tag + " pre-delay");
                    expectWithinAbsoluteError (real (h, pid::reverbSize), want.sizePercent, 0.05f, tag + " size");
                    expectWithinAbsoluteError (reverbedit::depth (h.proc, 0), d, 0.02f, tag + " read back");
                }
            }
        }

        beginTest ("A depth change is one undo step and one host gesture group");
        {
            Harness h;
            auto& undo = h.proc.getUndo();
            undo.clear();
            const float mix0 = real (h, pid::reverbMix), pre0 = real (h, pid::reverbPreDelay), size0 = real (h, pid::reverbSize);
            HostLog log (h.proc);
            reverbedit::setDepth (h.proc, 0, 0.8f);
            expectEquals (undo.getNumSteps(), 1, "one step");

            const int iMix = indexOf (h, pid::reverbMix), iPre = indexOf (h, pid::reverbPreDelay), iSize = indexOf (h, pid::reverbSize);
            const auto& ev = log.events;
            const int firstValue = [&] { for (int i = 0; i < ev.size(); ++i) if (ev[i].startsWith ("v")) return i; return -1; }();
            const int lastValue = [&] { for (int i = ev.size(); --i >= 0;) if (ev[i].startsWith ("v")) return i; return -1; }();
            expect (firstValue > 0 && lastValue >= firstValue, "values were sent: " + ev.joinIntoString (" "));
            for (int idx : { iMix, iPre, iSize })
            {
                const int b = ev.indexOf ("b" + juce::String (idx)), e = ev.indexOf ("e" + juce::String (idx));
                expect (b >= 0 && b < firstValue, "gesture begins before the first value: " + ev.joinIntoString (" "));
                expect (e > lastValue, "gesture ends after the last value: " + ev.joinIntoString (" "));
            }

            expect (undo.undo());
            expectWithinAbsoluteError (real (h, pid::reverbMix), mix0, 0.01f, "undo restores mix");
            expectWithinAbsoluteError (real (h, pid::reverbPreDelay), pre0, 0.01f, "undo restores pre-delay");
            expectWithinAbsoluteError (real (h, pid::reverbSize), size0, 0.01f, "undo restores size");
            expect (undo.redo());
            expectWithinAbsoluteError (real (h, pid::reverbMix), dsp::reverbDepthPath ((int) real (h, pid::reverbType), 0.8f).mixPercent, 0.05f, "redo");
        }

        beginTest ("resetDepth: Mix, Pre-Delay and Size back to their defaults, one step");
        {
            Harness h;
            reverbedit::setDepth (h.proc, 0, 1.0f);
            h.proc.getUndo().clear();
            reverbedit::resetDepth (h.proc, 0);
            expectWithinAbsoluteError (real (h, pid::reverbMix), 18.0f, 0.01f);
            expectWithinAbsoluteError (real (h, pid::reverbPreDelay), 20.0f, 0.01f);
            expectWithinAbsoluteError (real (h, pid::reverbSize), 60.0f, 0.01f);
            expectEquals (h.proc.getUndo().getNumSteps(), 1);
        }

        beginTest ("Instance 2 edits only its own parameters");
        {
            Harness h;
            auto r = h.proc.getRouting();
            r.insertDefault (ModuleId::reverb, 1);
            h.proc.setRouting (r);
            const float mix1 = real (h, pid::reverbMix);
            reverbedit::setDepth (h.proc, 1, 1.0f);
            const auto want = dsp::reverbDepthPath ((int) real (h, idFor (pid::reverbType, 1)), 1.0f);
            expectWithinAbsoluteError (real (h, idFor (pid::reverbMix, 1)), want.mixPercent, 0.05f, "reverbMix_2");
            expectWithinAbsoluteError (real (h, idFor (pid::reverbSize, 1)), want.sizePercent, 0.05f, "reverbSize_2");
            expectWithinAbsoluteError (real (h, pid::reverbMix), mix1, 1.0e-4f, "instance 1 untouched");
        }

        beginTest ("Panel: Room dropdown (8 types), Size, Decay, Mix; the rest in Mehr");
        {
            Harness h;
            auto panel = createDetailPanel (h.proc, (int) ModuleId::reverb);
            panel->setBounds (panelSize());
            const auto& main = panel->getMainControls();
            expectEquals ((int) main.size(), 4);
            if (main.size() == 4)
            {
                auto* type = dynamic_cast<Choice*> (main[0]);
                expect (type != nullptr && type->getParamId() == pid::reverbType, "room type is a dropdown");
                if (type != nullptr)
                {
                    expectEquals (type->box.getNumItems(), 8);
                    expectEquals (type->box.getItemText (3), juce::String ("Large Hall"));
                }
                const char* knobs[] { pid::reverbSize, pid::reverbDecay, pid::reverbMix };
                for (int i = 0; i < 3; ++i)
                {
                    auto* k = dynamic_cast<Knob*> (main[(size_t) i + 1]);
                    expect (k != nullptr && k->getParamId() == knobs[i], juce::String ("main knob ") + knobs[i]);
                }
            }
            juce::StringArray more;
            for (auto* c : panel->getMoreControls())
            {
                if (auto* k = dynamic_cast<Knob*> (c)) more.add (k->getParamId());
                if (auto* s = dynamic_cast<Segments*> (c)) more.add (s->getParamId());
            }
            for (auto* id : { pid::reverbPreDelay, pid::reverbDamping, pid::reverbWidth, pid::reverbLowCut, pid::reverbHighCut, pid::reverbMod,
                              pid::reverbEarly, pid::reverbDiffusion, pid::reverbBass, pid::reverbDuckAmount, pid::reverbDuckThreshold,
                              pid::reverbDuckAttack, pid::reverbDuckRelease, pid::duckSource })
                expect (more.contains (id), juce::String ("Mehr holds ") + id);
        }

        //======================================================================
        // The room view
        beginTest ("The panel's visual is the room; manual edits move the dot (back = higher, smaller)");
        {
            Harness h;
            auto panel = createDetailPanel (h.proc, (int) ModuleId::reverb);
            panel->setBounds (panelSize());
            ReverbRoom* inPanel = nullptr;
            for (auto* c : panel->getChildren())
                if (auto* r = dynamic_cast<ReverbRoom*> (c))
                    inPanel = r;
            expect (inPanel != nullptr, "ReverbRoom is the visual");

            Room room (h);
            setReal (h, pid::reverbType, 2.0f);
            reverbedit::setDepth (h.proc, 0, 0.2f);
            room.update();
            const auto front = room.view.getDotPosition();
            const float frontR = room.view.getDotRadius();
            expectWithinAbsoluteError (room.view.getShownDepth(), reverbedit::depth (h.proc, 0), 1.0e-4f, "shown = parameters");
            setReal (h, pid::reverbMix, 45.0f);   // a hand-set Mix: the vocal moves back
            room.update();
            expect (room.view.getShownDepth() > 0.6f, "depth follows the mix: " + juce::String (room.view.getShownDepth()));
            expect (room.view.getDotPosition().y < front.y - 10.0f, "dot moved back (up)");
            expect (room.view.getDotRadius() < frontR, "and got smaller");
            expect (room.view.getReadout().contains ("Mix 45 %"), room.view.getReadout());
        }

        beginTest ("Room shape follows type and size");
        {
            Harness h;
            Room room (h);
            const auto backArea = [&] (int type, float size)
            {
                setReal (h, pid::reverbType, (float) type);
                setReal (h, pid::reverbSize, size);
                room.update();
                const auto b = room.view.getGeometry().back;
                return b.getWidth() * b.getHeight();
            };
            expect (backArea (0, 60.0f) > 1.5f * backArea (3, 60.0f), "Room shallower than Large Hall");
            expect (backArea (4, 60.0f) > backArea (2, 60.0f), "Ambience shallower than Hall");
            expect (backArea (2, 0.0f) > backArea (2, 100.0f), "Size 100 deeper than Size 0");
            setReal (h, pid::reverbSize, 0.0f);
            room.update();
            const float w0 = room.view.getGeometry().front.getWidth();
            setReal (h, pid::reverbSize, 100.0f);
            room.update();
            expect (room.view.getGeometry().front.getWidth() > w0, "Size 100 wider");
            for (int type = 0; type < 8; ++type)
            {
                setReal (h, pid::reverbType, (float) type);
                room.update();
                const auto& g = room.view.getGeometry();
                expect (room.view.getLocalBounds().toFloat().contains (g.front) && g.front.contains (g.back), "type " + juce::String (type) + " inside");
            }
        }

        beginTest ("Drag: up = back, 260 px = whole path, Shift fine; one undo step, gestures around it");
        {
            Harness h;
            Room room (h);
            reverbedit::setDepth (h.proc, 0, 0.2f);
            room.update();
            auto& undo = h.proc.getUndo();
            undo.clear();
            HostLog log (h.proc);
            room.drag (130.0f);
            expectWithinAbsoluteError (reverbedit::depth (h.proc, 0), 0.7f, 0.02f, "half the path");
            const auto want = dsp::reverbDepthPath ((int) real (h, pid::reverbType), 0.7f);
            expectWithinAbsoluteError (real (h, pid::reverbMix), want.mixPercent, 0.6f, "mix on the path");
            expectWithinAbsoluteError (real (h, pid::reverbPreDelay), want.preDelayMs, 1.0f, "pre-delay on the path");
            expectEquals (undo.getNumSteps(), 1, "one step");
            const int iMix = indexOf (h, pid::reverbMix);
            expectEquals (log.events.indexOf ("b" + juce::String (iMix)), 0, "gesture opened at mouse-down");
            expect (log.events[log.events.size() - 1].startsWith ("e"), "closed at mouse-up");
            expect (! room.view.isDragging());

            room.drag (-130.0f, juce::ModifierKeys::shiftModifier);
            expectWithinAbsoluteError (reverbedit::depth (h.proc, 0), 0.65f, 0.02f, "Shift: 1/10");
            expectEquals (undo.getNumSteps(), 2);
            expect (undo.undo() && undo.undo());
            expectWithinAbsoluteError (reverbedit::depth (h.proc, 0), 0.2f, 0.02f, "undone");
        }

        beginTest ("Drag past the end clamps; turning back moves at once (no dead zone)");
        {
            Harness h;
            Room room (h);
            reverbedit::setDepth (h.proc, 0, 0.9f);
            room.update();
            const auto start = room.view.getDotPosition();
            room.view.mouseDown (mouseAt (room.view, start, start, false));
            room.view.mouseDrag (mouseAt (room.view, start.translated (0.0f, -260.0f), start, true));
            expectWithinAbsoluteError (reverbedit::depth (h.proc, 0), 1.0f, 0.01f, "clamped at the back");
            room.view.mouseDrag (mouseAt (room.view, start.translated (0.0f, -234.0f), start, true));
            room.view.mouseUp (mouseAt (room.view, start.translated (0.0f, -234.0f), start, true));
            expectWithinAbsoluteError (reverbedit::depth (h.proc, 0), 0.9f, 0.02f, "26 px back = 0.1");
        }

        beginTest ("A drag anywhere in the room moves the dot; settings off the path stay inside and snap on the first move");
        {
            Harness h;
            Room room (h);
            setReal (h, pid::reverbMix, 100.0f);
            setReal (h, pid::reverbPreDelay, 250.0f);
            setReal (h, pid::reverbSize, 0.0f);
            room.update();
            const auto& g = room.view.getGeometry();
            expect (g.front.contains (room.view.getDotPosition()), "dot inside the room");
            const float d0 = room.view.getShownDepth();
            const juce::Point<float> corner (g.front.getX() + 8.0f, g.front.getBottom() - 8.0f);
            room.view.mouseDown (mouseAt (room.view, corner, corner, false));
            room.view.mouseDrag (mouseAt (room.view, corner.translated (0.0f, 13.0f), corner, true));
            room.view.mouseUp (mouseAt (room.view, corner.translated (0.0f, 13.0f), corner, true));
            const auto want = reverbedit::pointFor (h.proc, 0, juce::jmax (0.0f, d0 - 0.05f));
            expectWithinAbsoluteError (real (h, pid::reverbPreDelay), want.preDelayMs, 1.0f, "on the path now");
            expectWithinAbsoluteError (real (h, pid::reverbMix), want.mixPercent, 0.6f);
        }

        beginTest ("Double-click and Alt+click reset Mix, Pre-Delay and Size; wheel steps the depth (one step each)");
        {
            Harness h;
            Room room (h);
            auto& undo = h.proc.getUndo();
            reverbedit::setDepth (h.proc, 0, 1.0f);
            room.update();
            undo.clear();
            const auto p = room.view.getDotPosition();
            room.view.mouseDown (mouseAt (room.view, p, p, false));
            room.view.mouseUp (mouseAt (room.view, p, p, false));
            room.view.mouseDown (mouseAt (room.view, p, p, false, {}, 2));
            room.view.mouseDoubleClick (mouseAt (room.view, p, p, false, {}, 2));
            room.view.mouseDrag (mouseAt (room.view, p.translated (0.0f, -40.0f), p, true, {}, 2));
            room.view.mouseUp (mouseAt (room.view, p.translated (0.0f, -40.0f), p, true, {}, 2));
            expectWithinAbsoluteError (real (h, pid::reverbMix), 18.0f, 0.01f, "double-click: default mix (no drag after it)");
            expectWithinAbsoluteError (real (h, pid::reverbPreDelay), 20.0f, 0.01f);
            expectWithinAbsoluteError (real (h, pid::reverbSize), 60.0f, 0.01f);
            expectEquals (undo.getNumSteps(), 1, "double-click = one step");

            reverbedit::setDepth (h.proc, 0, 1.0f);
            room.update();
            undo.clear();
            room.view.mouseDown (mouseAt (room.view, p, p, false, juce::ModifierKeys::altModifier));
            room.view.mouseUp (mouseAt (room.view, p, p, false, juce::ModifierKeys::altModifier));
            expectWithinAbsoluteError (real (h, pid::reverbMix), 18.0f, 0.01f, "Alt+click: default mix");
            expectEquals (undo.getNumSteps(), 1, "Alt+click = one step");

            reverbedit::setDepth (h.proc, 0, 0.5f);
            room.update();
            undo.clear();
            room.view.mouseWheelMove (mouseAt (room.view, p, p, false), wheel (0.11f));
            expectWithinAbsoluteError (reverbedit::depth (h.proc, 0), 0.5f + ReverbRoom::wheelStep, 0.006f, "wheel up = a step back");
            room.view.mouseWheelMove (mouseAt (room.view, p, p, false), wheel (-0.22f));
            expectWithinAbsoluteError (reverbedit::depth (h.proc, 0), 0.5f - ReverbRoom::wheelStep, 0.006f, "two notches forward");
            expectEquals (undo.getNumSteps(), 2, "one step per wheel event");
        }

        beginTest ("Right-click menu: Vorne / Mitte / Hinten / Standardwerte and the panel items");
        {
            Harness h;
            auto panel = createDetailPanel (h.proc, (int) ModuleId::reverb);
            panel->setBounds (panelSize());
            ReverbRoom* room = nullptr;
            for (auto* c : panel->getChildren())
                if (auto* r = dynamic_cast<ReverbRoom*> (c))
                    room = r;
            if (room == nullptr)
            {
                expect (false, "no room");
                return;
            }
            auto menu = room->buildMenu();
            juce::StringArray texts;
            std::function<void()> back;
            for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            {
                texts.add (it.getItem().text);
                if (it.getItem().text == "Hinten")
                    back = it.getItem().action;
            }
            for (auto* t : { "Vorne", "Mitte", "Hinten", "Standardwerte" })
                expect (texts.contains (t), juce::String ("menu has ") + t);
            expect (texts.contains (juce::String::fromUTF8 ("Modul zurücksetzen")), "panel items: " + texts.joinIntoString (", "));
            h.proc.getUndo().clear();
            expect (back != nullptr);
            if (back != nullptr)
                back();
            expectWithinAbsoluteError (reverbedit::depth (h.proc, 0), 0.9f, 0.02f, "Hinten = depth 0.9");
            expectEquals (h.proc.getUndo().getNumSteps(), 1);
        }

        beginTest ("Decay hint = ReverbModule::predictedRt60 of the heard settings; hover text; instance 2");
        {
            Harness h;
            setReal (h, pid::reverbDamping, 3000.0f);
            setReal (h, pid::reverbDecay, 2.5f);
            Room room (h);
            room.update();
            const auto heard = room.live->settings.instance (0).reverb;
            for (int b = 0; b < ReverbRoom::numDecayBands; ++b)
                expectWithinAbsoluteError (room.view.getDecayBands()[(size_t) b],
                                           dsp::ReverbModule::predictedRt60 (heard, ReverbRoom::decayBandHz (b), 48000.0), 1.0e-4f);
            expect (room.view.getDecayBands()[5] < room.view.getDecayBands()[2], "dark damping: 4 kHz decays faster than 500 Hz");
            expectEquals (room.view.getReadout(), juce::String::fromUTF8 ("Mix 18 % · Pre 20 ms · Size 60 %"));

            auto r = h.proc.getRouting();
            r.insertDefault (ModuleId::reverb, 1);
            h.proc.setRouting (r);
            Room second (h, 1);
            reverbedit::setDepth (h.proc, 0, 0.0f);
            setReal (h, idFor (pid::reverbMix, 1), 50.0f);
            second.update();
            expect (second.view.getShownDepth() > 0.8f, "instance 2 shows its own mix");
            second.drag (-26.0f);
            expect (real (h, idFor (pid::reverbMix, 1)) < 50.0f && reverbedit::depth (h.proc, 0) < 0.01f, "and drags its own parameters");
        }
    }

    // A free-standing room (as in the panel's visual area) fed with the processor's settings
    struct Room
    {
        explicit Room (Harness& harness, int instance = 0) : h (harness), view (harness.proc, instance)
        {
            view.setBounds (0, 0, 900, 280);
            update();
        }
        void update()
        {
            live->settings = h.proc.readEffectiveSettings();
            live->sampleRate = 48000.0;
            live->time += 1.0 / 30.0;
            view.update (*live);
        }
        // Vertical drag from the dot; positive = up (back)
        void drag (float pixels, juce::ModifierKeys mods = {})
        {
            const auto start = view.getDotPosition();
            view.mouseDown (mouseAt (view, start, start, false, mods));
            for (int i = 1; i <= 10; ++i)
                view.mouseDrag (mouseAt (view, start.translated (0.0f, -pixels * (float) i / 10.0f), start, true, mods));
            view.mouseUp (mouseAt (view, start.translated (0.0f, -pixels), start, true, mods));
            update();
        }
        Harness& h;
        ui::ReverbRoom view;
        std::unique_ptr<ui::LiveData> live = std::make_unique<ui::LiveData>();
    };

    static juce::MouseEvent mouseAt (juce::Component& c, juce::Point<float> pos, juce::Point<float> downPos, bool dragged,
                                     juce::ModifierKeys mods = {}, int clicks = 1)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, mods,
                                 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, downPos, now, clicks, dragged);
    }

    static juce::MouseWheelDetails wheel (float dy)
    {
        juce::MouseWheelDetails w;
        w.deltaX = 0.0f;
        w.deltaY = dy;
        w.isReversed = false;
        w.isSmooth = false;
        w.isInertial = false;
        return w;
    }
};

static ReverbPanelTests reverbPanelTests;
