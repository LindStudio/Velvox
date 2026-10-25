// Voice panels of 0.6 (spec 5.8, 5.9): the Doubler "like Waves Doubler" (voice mode Classic / 2 / 4, stage strip
// with draggable voices, per-voice value rows) and the Choir half-circle stage.

#include "TestHelpers.h"
#include "PluginEditor.h"
#include "UI/VoicesPanels.h"

using namespace velvox;
using namespace velvox::test;
using dsp::ModuleId;
using velvox::ui::doubleredit::Field;

namespace
{
    juce::Rectangle<int> panelSize() { return VelvoxAudioProcessorEditor::panelArea().withZeroOrigin(); }

    float real (Harness& h, const juce::String& id) { return h.proc.getState().getRawParameterValue (id)->load(); }

    void setReal (Harness& h, const juce::String& id, float v)
    {
        auto* p = h.proc.getState().getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (v));
    }

    juce::String inst (const char* base, int instance) { return instance == 0 ? juce::String (base) : dsp::instanceParamId (base, instance); }

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

    // Drag from `from` by (dx, dy) in ten steps
    template <typename View>
    void drag (View& view, juce::Point<float> from, float dx, float dy, juce::ModifierKeys mods = {})
    {
        view.mouseDown (mouseAt (view, from, from, false, mods));
        for (int i = 1; i <= 10; ++i)
            view.mouseDrag (mouseAt (view, from.translated (dx * (float) i / 10.0f, dy * (float) i / 10.0f), from, true, mods));
        view.mouseUp (mouseAt (view, from.translated (dx, dy), from, true, mods));
    }

    struct HostLog final : juce::AudioProcessorListener
    {
        explicit HostLog (juce::AudioProcessor& p) : proc (p) { proc.addListener (this); }
        ~HostLog() override { proc.removeListener (this); }
        void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override { ++values; }
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
        void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int) override { ++begins; }
        void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int) override { ++ends; }
        juce::AudioProcessor& proc;
        int values = 0, begins = 0, ends = 0;
    };

    template <typename View>
    struct Stage
    {
        explicit Stage (Harness& harness, int instance = 0) : h (harness), view (harness.proc, instance)
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
        Harness& h;
        View view;
        std::unique_ptr<ui::LiveData> live = std::make_unique<ui::LiveData>();
    };

    template <typename T>
    T* findChild (juce::Component& parent)
    {
        for (auto* c : parent.getChildren())
        {
            if (auto* t = dynamic_cast<T*> (c))
                return t;
            if (auto* t = findChild<T> (*c))
                return t;
        }
        return nullptr;
    }

    juce::String paramOf (juce::Component* c)
    {
        if (auto* k = dynamic_cast<ui::Knob*> (c)) return k->getParamId();
        if (auto* s = dynamic_cast<ui::Segments*> (c)) return s->getParamId();
        if (auto* t = dynamic_cast<ui::Toggle*> (c)) return t->getParamId();
        return {};
    }
}

class VoicesPanelTests final : public juce::UnitTest
{
public:
    VoicesPanelTests() : juce::UnitTest ("Headliner voices panels", "Headliner") {}

    void runTest() override
    {
        using namespace ui;
        namespace de = ui::doubleredit;

        //======================================================================
        beginTest ("Doubler edit: Classic voices follow Delay (D and 1.37 D, 0 ct, 0 dB, pans -100 / +100)");
        {
            Harness h;
            setReal (h, pid::doublerDelay, 25.0f);
            expectEquals (de::mode (h.proc, 0), 0);
            expectEquals (de::voiceCount (h.proc, 0), 2);
            const auto a = de::voice (h.proc, 0, 0), b = de::voice (h.proc, 0, 1);
            expectWithinAbsoluteError (a.delayMs, 25.0f, 0.01f);
            expectWithinAbsoluteError (b.delayMs, 34.25f, 0.01f);
            expectWithinAbsoluteError (a.panPercent, -100.0f, 0.01f);
            expectWithinAbsoluteError (b.panPercent, 100.0f, 0.01f);
            expectWithinAbsoluteError (a.gainDb + b.gainDb + a.detuneCents + b.detuneCents, 0.0f, 1.0e-4f);
            expect (a.on && b.on);
            setReal (h, pid::doublerVoices, 2.0f);
            expectEquals (de::voiceCount (h.proc, 0), 4);
            expectWithinAbsoluteError (de::voice (h.proc, 0, 2).delayMs, 24.0f, 0.01f, "4 voices: V3 parameters");
        }

        beginTest ("Doubler edit: a voice edit in Classic converts to 2 in one step (and undo returns to Classic)");
        {
            Harness h;
            setReal (h, pid::doublerDelay, 20.0f);
            setReal (h, pid::doublerDetune, 12.0f);
            setReal (h, pid::doublerV1Delay, 40.0f);
            setReal (h, pid::doublerV1Gain, -10.0f);
            setReal (h, pid::doublerV2Pan, 30.0f);
            auto& undo = h.proc.getUndo();
            undo.clear();
            HostLog log (h.proc);
            de::setValue (h.proc, 0, 1, Field::gain, -4.0f);
            expectEquals (de::mode (h.proc, 0), 1, "mode 2");
            expectWithinAbsoluteError (real (h, pid::doublerV1Delay), 20.0f, 0.01f, "V1 = Classic delay");
            expectWithinAbsoluteError (real (h, pid::doublerV2Delay), 27.4f, 0.01f, "V2 = 1.37 x");
            expectWithinAbsoluteError (real (h, pid::doublerV1Gain), 0.0f, 0.01f);
            expectWithinAbsoluteError (real (h, pid::doublerV2Gain), -4.0f, 0.01f, "the edit itself");
            expectWithinAbsoluteError (real (h, pid::doublerV1Pan), -100.0f, 0.01f);
            expectWithinAbsoluteError (real (h, pid::doublerV2Pan), 100.0f, 0.01f);
            expectWithinAbsoluteError (real (h, pid::doublerV1Detune) + real (h, pid::doublerV2Detune), 0.0f, 0.01f);
            expect (real (h, pid::doublerV1On) > 0.5f && real (h, pid::doublerV2On) > 0.5f);
            expectWithinAbsoluteError (real (h, pid::doublerModDepth), 12.0f, 0.01f, "Mod Depth = Classic Detune");
            expectEquals (undo.getNumSteps(), 1, "one step");
            expectEquals (log.begins, log.ends, "no gesture left open");

            // The voices the engine plays: the same as Classic apart from the edited gain
            auto s = h.proc.readEffectiveSettings().doubler;
            const auto r = dsp::DoublerModule::resolveVoices (s);
            expectWithinAbsoluteError (r.v[0].pan, -s.width, 1.0e-4f, "effective pan as Classic");
            expectWithinAbsoluteError (r.v[1].delayMs, 27.4f, 0.01f);

            expect (undo.undo());
            expectEquals (de::mode (h.proc, 0), 0, "undo: Classic");
            expectWithinAbsoluteError (real (h, pid::doublerV1Delay), 40.0f, 0.01f, "undo: old V1");
            expectWithinAbsoluteError (real (h, pid::doublerV1Gain), -10.0f, 0.01f);
            expectWithinAbsoluteError (real (h, pid::doublerV2Pan), 30.0f, 0.01f);
            expectWithinAbsoluteError (real (h, pid::doublerModDepth), 8.0f, 0.01f);
        }

        beginTest ("Doubler edit: in 2 / 4 voice mode an Edit is one step, keeps the mode, gestures around the values");
        {
            Harness h;
            setReal (h, pid::doublerVoices, 2.0f);
            auto& undo = h.proc.getUndo();
            undo.clear();
            HostLog log (h.proc);
            {
                de::Edit e (h.proc, 0, 3, { Field::pan, Field::gain }, "drag");
                expect (log.begins >= 2, "gestures begun");
                for (int i = 1; i <= 5; ++i)
                {
                    e.set (Field::pan, 50.0f - 10.0f * (float) i);
                    e.set (Field::gain, -3.0f + (float) i);
                }
                expectEquals (log.ends, 0, "open while dragging");
            }
            expectEquals (log.begins, log.ends);
            expectEquals (undo.getNumSteps(), 1);
            expectEquals (de::mode (h.proc, 0), 2, "still 4 voices");
            expectWithinAbsoluteError (real (h, pid::doublerV4Pan), 0.0f, 0.01f);
            expectWithinAbsoluteError (real (h, pid::doublerV4Gain), 2.0f, 0.01f);
        }

        beginTest ("Doubler edit: toggle, reset voice, reset position; value texts and typed values");
        {
            Harness h;
            setReal (h, pid::doublerVoices, 1.0f);
            setReal (h, pid::doublerV2Delay, 33.0f);
            setReal (h, pid::doublerV2Detune, -7.0f);
            setReal (h, pid::doublerV2Gain, -9.0f);
            setReal (h, pid::doublerV2Pan, 20.0f);
            auto& undo = h.proc.getUndo();
            undo.clear();
            de::toggleOn (h.proc, 0, 1);
            expect (real (h, pid::doublerV2On) < 0.5f, "off");
            expectEquals (undo.getNumSteps(), 1);
            de::resetPosition (h.proc, 0, 1);
            expectWithinAbsoluteError (real (h, pid::doublerV2Pan), 100.0f, 0.01f, "pan default");
            expectWithinAbsoluteError (real (h, pid::doublerV2Gain), 0.0f, 0.01f, "gain default");
            expectWithinAbsoluteError (real (h, pid::doublerV2Delay), 33.0f, 0.01f, "delay kept");
            expectEquals (undo.getNumSteps(), 2);
            de::resetVoice (h.proc, 0, 1);
            expectWithinAbsoluteError (real (h, pid::doublerV2Delay), 19.18f, 0.01f);
            expectWithinAbsoluteError (real (h, pid::doublerV2Detune), 0.0f, 0.01f);
            expect (real (h, pid::doublerV2On) > 0.5f, "on again");
            expectEquals (undo.getNumSteps(), 3);

            expectEquals (de::valueText (Field::pan, -50.0f), juce::String ("L50"));
            expectEquals (de::valueText (Field::pan, 0.2f), juce::String ("C"));
            expectEquals (de::valueText (Field::pan, 100.0f), juce::String ("R100"));
            expectEquals (de::valueText (Field::detune, 5.0f), juce::String ("+5.0 ct"));
            expectEquals (de::valueText (Field::delay, 14.0f), juce::String ("14.0 ms"));
            expectEquals (de::valueText (Field::gain, -3.0f), juce::String ("-3.0 dB"));
            const auto typed = [&] (Field f, const char* t) { return de::parseValue (h.proc, 0, 0, f, t).value_or (-999.0f); };
            expectWithinAbsoluteError (typed (Field::pan, "L40"), -40.0f, 1.0e-4f);
            expectWithinAbsoluteError (typed (Field::pan, "r 25"), 25.0f, 1.0e-4f);
            expectWithinAbsoluteError (typed (Field::pan, "C"), 0.0f, 1.0e-4f);
            expectWithinAbsoluteError (typed (Field::delay, "22 ms"), 22.0f, 1.0e-4f);
            expectWithinAbsoluteError (typed (Field::detune, "-12"), -12.0f, 1.0e-4f);
            expect (! de::parseValue (h.proc, 0, 0, Field::gain, "abc").has_value());
        }

        beginTest ("Doubler edit: instance 2 edits only its own parameters");
        {
            Harness h;
            auto r = h.proc.getRouting();
            r.insertDefault (ModuleId::doubler, 1);
            h.proc.setRouting (r);
            de::setValue (h.proc, 1, 0, Field::pan, -30.0f);
            expectWithinAbsoluteError (real (h, inst (pid::doublerV1Pan, 1)), -30.0f, 0.01f, "doublerV1Pan_2");
            expectEquals (de::mode (h.proc, 1), 1, "instance 2 left Classic");
            expectEquals (de::mode (h.proc, 0), 0, "instance 1 untouched");
            expectWithinAbsoluteError (real (h, pid::doublerV1Pan), -100.0f, 0.01f);
        }

        //======================================================================
        beginTest ("Doubler stage: voices at pan (x) and gain (y), lead in the centre; Classic shows two linked voices");
        {
            Harness h;
            Stage<DoublerStage> st (h);
            auto& v = st.view;
            expect (v.isClassicShown());
            expectEquals (v.numShownVoices(), 2);
            expect (v.voicePoint (0).x < v.leadPoint().x - 100.0f && v.voicePoint (1).x > v.leadPoint().x + 100.0f, "Classic voices at the sides");
            expectWithinAbsoluteError (v.leadPoint().x, v.getPlotArea().getCentreX(), 0.5f);

            setReal (h, pid::doublerVoices, 1.0f);
            setReal (h, pid::doublerV1Pan, -60.0f);
            setReal (h, pid::doublerV1Gain, -6.0f);
            st.update();
            expect (! v.isClassicShown());
            expectWithinAbsoluteError (v.voicePoint (0).x, v.xForPan (-60.0f), 0.01f);
            expectWithinAbsoluteError (v.voicePoint (0).y, v.yForGain (-6.0f), 0.01f);
            expect (v.yForGain (6.0f) < v.yForGain (-24.0f), "up = louder");
            expect (v.getPlotArea().contains (v.voicePoint (0)));
            expectEquals (v.voiceAt (v.voicePoint (0)), 0);
            expectEquals (v.voiceAt (v.voicePoint (1)), 1);
            expectEquals (v.voiceAt (v.leadPoint().translated (0.0f, -60.0f)), -1);
            expect (v.cardValue (0).contains ("L60") && v.cardValue (0).contains ("-6.0 dB"), v.cardValue (0));
        }

        beginTest ("Doubler stage: a drag moves pan and gain in one undo step; Shift is fine");
        {
            Harness h;
            setReal (h, pid::doublerVoices, 1.0f);
            setReal (h, pid::doublerV2Pan, 50.0f);
            setReal (h, pid::doublerV2Gain, -6.0f);
            Stage<DoublerStage> st (h);
            auto& v = st.view;
            auto& undo = h.proc.getUndo();
            undo.clear();
            HostLog log (h.proc);
            const float pxPerPan = (v.xForPan (100.0f) - v.xForPan (0.0f)) / 100.0f;
            const float pxPerDb = (v.yForGain (-24.0f) - v.yForGain (6.0f)) / 30.0f;
            drag (v, v.voicePoint (1), -20.0f * pxPerPan, -3.0f * pxPerDb);
            expectWithinAbsoluteError (real (h, pid::doublerV2Pan), 30.0f, 0.3f, "pan follows x");
            expectWithinAbsoluteError (real (h, pid::doublerV2Gain), -3.0f, 0.1f, "gain follows y");
            expectEquals (undo.getNumSteps(), 1, "one step");
            expectEquals (log.begins, log.ends);
            expect (! v.isDragging());

            st.update();
            drag (v, v.voicePoint (1), -20.0f * pxPerPan, 0.0f, juce::ModifierKeys::shiftModifier);
            expectWithinAbsoluteError (real (h, pid::doublerV2Pan), 28.0f, 0.3f, "Shift: 1/10");
            expectEquals (undo.getNumSteps(), 2);
        }

        beginTest ("Doubler stage: a click without movement records nothing");
        {
            Harness h;
            Stage<DoublerStage> st (h);
            auto& v = st.view;
            h.proc.getUndo().clear();
            HostLog log (h.proc);
            const auto p = v.voicePoint (0);
            v.mouseDown (mouseAt (v, p, p, false));
            v.mouseUp (mouseAt (v, p, p, false));
            expectEquals (h.proc.getUndo().getNumSteps(), 0);
            expectEquals (log.values, 0);
            expectEquals (de::mode (h.proc, 0), 0, "still Classic");
            expect (! v.isDragging());
        }

        beginTest ("Doubler stage: dragging a Classic voice converts to 2 voices (one step)");
        {
            Harness h;
            setReal (h, pid::doublerDelay, 18.0f);
            Stage<DoublerStage> st (h);
            auto& v = st.view;
            h.proc.getUndo().clear();
            const float pxPerDb = (v.yForGain (-24.0f) - v.yForGain (6.0f)) / 30.0f;
            drag (v, v.voicePoint (0), 0.0f, 6.0f * pxPerDb);
            expectEquals (de::mode (h.proc, 0), 1);
            expectWithinAbsoluteError (real (h, pid::doublerV1Gain), -6.0f, 0.1f);
            expectWithinAbsoluteError (real (h, pid::doublerV1Delay), 18.0f, 0.01f);
            expectWithinAbsoluteError (real (h, pid::doublerV2Delay), 18.0f * 1.37f, 0.01f);
            expectEquals (h.proc.getUndo().getNumSteps(), 1);
            st.update();
            expect (! v.isClassicShown());
        }

        beginTest ("Doubler stage: double-click / Alt+click reset pan and gain, wheel = detune, menu on/off");
        {
            Harness h;
            setReal (h, pid::doublerVoices, 1.0f);
            setReal (h, pid::doublerV1Pan, -20.0f);
            setReal (h, pid::doublerV1Gain, -12.0f);
            Stage<DoublerStage> st (h);
            auto& v = st.view;
            auto& undo = h.proc.getUndo();
            undo.clear();
            auto p = v.voicePoint (0);
            v.mouseDown (mouseAt (v, p, p, false));
            v.mouseUp (mouseAt (v, p, p, false));
            v.mouseDown (mouseAt (v, p, p, false, {}, 2));
            v.mouseDoubleClick (mouseAt (v, p, p, false, {}, 2));
            v.mouseUp (mouseAt (v, p, p, false, {}, 2));
            expectWithinAbsoluteError (real (h, pid::doublerV1Pan), -100.0f, 0.01f, "double-click: pan default");
            expectWithinAbsoluteError (real (h, pid::doublerV1Gain), 0.0f, 0.01f, "gain default");
            expectEquals (undo.getNumSteps(), 1);

            setReal (h, pid::doublerV1Pan, -20.0f);
            st.update();
            p = v.voicePoint (0);
            v.mouseDown (mouseAt (v, p, p, false, juce::ModifierKeys::altModifier));
            v.mouseUp (mouseAt (v, p, p, false, juce::ModifierKeys::altModifier));
            expectWithinAbsoluteError (real (h, pid::doublerV1Pan), -100.0f, 0.01f, "Alt+click");

            st.update();
            undo.clear();
            p = v.voicePoint (0);
            v.mouseWheelMove (mouseAt (v, p, p, false), wheel (0.2f));
            expectWithinAbsoluteError (real (h, pid::doublerV1Detune), DoublerStage::wheelCents, 0.01f, "wheel up = +1 ct");
            v.mouseWheelMove (mouseAt (v, p, p, false, juce::ModifierKeys::shiftModifier), wheel (-0.2f));
            expectWithinAbsoluteError (real (h, pid::doublerV1Detune), 0.9f, 0.01f, "Shift: 0.1 ct");
            expectEquals (undo.getNumSteps(), 2);

            const auto menu = v.buildMenu (0);
            juce::StringArray items;
            for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                items.add (it.getItem().text);
            expect (items.contains ("Stimme aus"), items.joinIntoString ("|"));
            expect (items.contains (juce::String::fromUTF8 ("Stimme zurücksetzen")), items.joinIntoString ("|"));
        }

        beginTest ("Doubler stage: rows for 2 / 4 voices; V3 / V4 keep their values while hidden");
        {
            Harness h;
            Stage<DoublerStage> st (h);
            auto& v = st.view;
            expect (v.getRow (0).isVisible() && v.getRow (1).isVisible());
            expect (! v.getRow (2).isVisible() && ! v.getRow (3).isVisible(), "Classic: two rows");
            setReal (h, pid::doublerVoices, 2.0f);
            st.update();
            expectEquals (v.numShownVoices(), 4);
            for (int i = 0; i < 4; ++i)
            {
                expect (v.getRow (i).isVisible(), "row " + juce::String (i + 1));
                expect (v.getRow (i).getBottom() <= v.getHeight(), "inside the view");
                expect (v.getRow (i).getY() >= (int) v.getPlotArea().getBottom(), "under the strip");
            }
            de::setValue (h.proc, 0, 2, Field::delay, 44.0f);
            setReal (h, pid::doublerVoices, 1.0f);
            st.update();
            expect (! v.getRow (2).isVisible());
            expectWithinAbsoluteError (real (h, pid::doublerV3Delay), 44.0f, 0.01f, "V3 kept");

            // Every voice field names its parameter (panel reach)
            for (int i = 0; i < 4; ++i)
                for (auto f : { Field::on, Field::delay, Field::detune, Field::gain, Field::pan })
                {
                    auto* c = v.getField (i, f);
                    expect (c != nullptr && c->getProperties()["paramId"].toString() == de::paramId (0, i, f), de::paramId (0, i, f));
                }
        }

        beginTest ("Doubler stage: a value field drags (one step), resets on double-click, shows the value");
        {
            Harness h;
            setReal (h, pid::doublerVoices, 1.0f);
            Stage<DoublerStage> st (h);
            auto& v = st.view;
            auto& undo = h.proc.getUndo();
            undo.clear();
            auto* field = v.getField (1, Field::delay);
            expect (field != nullptr && field->getWidth() >= 44 && field->getHeight() >= 16, "field size");
            if (field != nullptr)
            {
                const auto c = field->getLocalBounds().getCentre().toFloat();
                field->mouseDown (mouseAt (*field, c, c, false));
                for (int i = 1; i <= 11; ++i)
                    field->mouseDrag (mouseAt (*field, c.translated (0.0f, -2.0f * (float) i), c, true));
                field->mouseUp (mouseAt (*field, c.translated (0.0f, -22.0f), c, true));
                auto* dp = h.proc.getState().getParameter (pid::doublerV2Delay);
                const float want = dp->convertFrom0to1 (dp->convertTo0to1 (19.18f) + 22.0f / 220.0f);   // 220 px = the whole (skewed) range
                expectWithinAbsoluteError (real (h, pid::doublerV2Delay), want, 0.05f, "220 px = full range");
                expectEquals (undo.getNumSteps(), 1);
                field->mouseDown (mouseAt (*field, c, c, false, {}, 2));
                field->mouseDoubleClick (mouseAt (*field, c, c, false, {}, 2));
                field->mouseUp (mouseAt (*field, c, c, false, {}, 2));
                expectWithinAbsoluteError (real (h, pid::doublerV2Delay), 19.18f, 0.01f, "double-click = default");
                expectEquals (undo.getNumSteps(), 2);
            }
            auto* onSwitch = v.getField (0, Field::on);
            if (onSwitch != nullptr)
            {
                const auto c = onSwitch->getLocalBounds().getCentre().toFloat();
                onSwitch->mouseDown (mouseAt (*onSwitch, c, c, false));
                onSwitch->mouseUp (mouseAt (*onSwitch, c, c, false));
                expect (real (h, pid::doublerV1On) < 0.5f, "click on the switch = off");
                expectEquals (undo.getNumSteps(), 3);
            }
        }

        beginTest ("Doubler stage: reads the engine's live voice feed (effective pan with Width)");
        {
            Harness h;
            setReal (h, pid::doublerOn, 1.0f);
            setReal (h, pid::doublerVoices, 1.0f);
            setReal (h, pid::doublerWidth, 50.0f);
            auto r = h.proc.getRouting();
            if (! r.contains (ModuleId::doubler))
                r.insertDefault (ModuleId::doubler, 0);
            h.proc.setRouting (r);
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            for (int b = 0; b < 40; ++b)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                        buffer.setSample (ch, i, 0.2f * std::sin (0.05f * (float) (b * 512 + i)));
                h.proc.processBlock (buffer, midi);
            }
            Stage<DoublerStage> st (h);
            expectWithinAbsoluteError (st.view.getLivePan (0), -0.5f, 0.05f, "voice 1 heard at -50 %");
            expectWithinAbsoluteError (st.view.getLivePan (1), 0.5f, 0.05f);
        }

        //======================================================================
        beginTest ("Doubler panel: Voices, Mix, Detune (Classic) / Mod Depth (2, 4), Dry; the rest in Mehr");
        {
            Harness h;
            auto panel = createDetailPanel (h.proc, (int) ModuleId::doubler);
            panel->setBounds (panelSize());
            expect (findChild<DoublerStage> (*panel) != nullptr, "the stage is the visual");
            const auto mainIds = [&]
            {
                juce::StringArray ids;
                for (auto* c : panel->getMainControls())
                    ids.add (paramOf (c));
                return ids.joinIntoString (",");
            };
            expectEquals (mainIds(), juce::String ("doublerVoices,doublerMix,doublerDetune,doublerDry"));
            juce::StringArray more;
            Knob* delayKnob = nullptr;
            for (auto* c : panel->getMoreControls())
            {
                more.add (paramOf (c));
                if (auto* k = dynamic_cast<Knob*> (c); k != nullptr && k->getParamId() == pid::doublerDelay)
                    delayKnob = k;
            }
            for (auto* id : { pid::doublerWidth, pid::doublerMonoSafe, pid::doublerTone, pid::doublerLowCut, pid::doublerModRate,
                              pid::doublerDelay, pid::doublerModDepth })
                expect (more.contains (id), juce::String ("Mehr holds ") + id);
            expect (delayKnob != nullptr && delayKnob->isEnabled(), "Delay usable in Classic");
            expectEquals (h.proc.getState().getParameter (pid::doublerModRate)->getCurrentValueAsText(), juce::String ("0.31 Hz"),
                          "slow rates keep their decimals");

            setReal (h, pid::doublerVoices, 1.0f);
            panel->refresh();
            expectEquals (mainIds(), juce::String ("doublerVoices,doublerMix,doublerModDepth,doublerDry"));
            juce::StringArray more2;
            for (auto* c : panel->getMoreControls())
                more2.add (paramOf (c));
            expect (more2.contains (pid::doublerDetune), "Classic Detune waits in Mehr");
            expect (delayKnob != nullptr && ! delayKnob->isEnabled(), "Delay only acts in Classic");
            for (auto* c : panel->getMoreControls())
                if (auto* k = dynamic_cast<Knob*> (c))
                    expect (k->getDialSize() <= 34, "Mehr knob small");
            for (auto* c : panel->getMainControls())
                if (auto* k = dynamic_cast<Knob*> (c))
                    expect (k->getDialSize() >= 50, "main knob large");
        }

        //======================================================================
        beginTest ("Choir stage: voices on the half circle (sides and order of the heard pans), Width 0 collapses");
        {
            Harness h;
            setReal (h, pid::choirVoices, 3.0f);   // 8
            Stage<ChoirStage> st (h);
            auto& v = st.view;
            expectEquals (v.numShownVoices(), 8);
            const auto s = h.proc.readEffectiveSettings().choir;
            const auto lead = v.leadPoint();
            float outerL = 0.0f, outerR = 0.0f;
            for (int i = 0; i < 8; ++i)
            {
                const float pan = dsp::ChoirModule::voicePosition (i, s).pan;
                (pan < 0.0f ? outerL : outerR) = std::max (pan < 0.0f ? outerL : outerR, std::abs (pan));
            }
            for (int i = 0; i < 8; ++i)
            {
                const float pi = dsp::ChoirModule::voicePosition (i, s).pan;
                const auto p = v.voicePoint (i);
                const float a = v.seatAngle (i);
                expect ((a < 0.0f) == (pi < 0.0f) && (p.x < lead.x) == (pi < 0.0f), "on its side, voice " + juce::String (i + 1));
                expect (p.y <= lead.y + 0.01f, "on or above the lead's line");
                expect (v.getLocalBounds().toFloat().contains (p), "inside");
                expect (v.voiceAt (p) >= 0, "hit voice " + juce::String (i + 1));
                if (std::abs (pi) == (pi < 0.0f ? outerL : outerR))
                    expectWithinAbsoluteError (std::sin (std::abs (a)), std::abs (pi), 1.0e-4f, "the outermost voice at its heard pan");
                for (int j = 0; j < 8; ++j)
                {
                    const float pj = dsp::ChoirModule::voicePosition (j, s).pan;
                    if ((pj < 0.0f) == (pi < 0.0f) && std::abs (pj) > std::abs (pi) + 1.0e-4f)
                        expect (std::abs (v.seatAngle (j)) > std::abs (a) + 0.05f, "order of the heard pans, seats apart");
                }
            }
            expect (v.voicePoint (0).x < lead.x && v.voicePoint (1).x > lead.x, "alternating sides");
            expect (v.cardValue (0).startsWith ("L"), v.cardValue (0));

            setReal (h, pid::choirWidth, 0.0f);
            st.update();
            for (int i = 0; i < 8; ++i)
            {
                const auto p = v.voicePoint (i);
                expect (std::isfinite (p.x) && std::isfinite (p.y));
                expectWithinAbsoluteError (p.x, lead.x, 0.5f, "Width 0: behind the lead");
                expect (p.y < lead.y - 20.0f, "at the top of the arc");
            }
            setReal (h, pid::choirVoices, 0.0f);
            st.update();
            expectEquals (v.numShownVoices(), 2);
        }

        beginTest ("Choir stage: horizontal drag = Width (one step), double-click = default, wheel steps");
        {
            Harness h;
            setReal (h, pid::choirWidth, 50.0f);
            Stage<ChoirStage> st (h);
            auto& v = st.view;
            auto& undo = h.proc.getUndo();
            undo.clear();
            // Every action a second apart (quick repeats on one parameter coalesce, see UndoTests)
            undo.setClockForTests ([t = juce::uint32 (0)]() mutable { return t += 1000; });
            HostLog log (h.proc);
            const auto from = v.leadPoint().translated (0.0f, -40.0f);
            drag (v, from, 44.0f, 0.0f);
            expectWithinAbsoluteError (real (h, pid::choirWidth), 70.0f, 0.5f, "44 px = 20 %");
            expectEquals (undo.getNumSteps(), 1);
            expectEquals (log.begins, log.ends);
            v.mouseDown (mouseAt (v, from, from, false, {}, 2));
            v.mouseDoubleClick (mouseAt (v, from, from, false, {}, 2));
            v.mouseUp (mouseAt (v, from, from, false, {}, 2));
            expectWithinAbsoluteError (real (h, pid::choirWidth), 80.0f, 0.01f, "default");
            v.mouseWheelMove (mouseAt (v, from, from, false), wheel (-0.2f));
            expect (real (h, pid::choirWidth) < 80.0f, "wheel down = narrower");
            expectEquals (undo.getNumSteps(), 3);
        }

        beginTest ("Choir panel: Voices, Detune, Width, Mix; Timing, Humanize, Density in Mehr");
        {
            Harness h;
            auto panel = createDetailPanel (h.proc, (int) ModuleId::choir);
            panel->setBounds (panelSize());
            expect (findChild<ChoirStage> (*panel) != nullptr, "the half circle is the visual");
            juce::StringArray mainIds, more;
            for (auto* c : panel->getMainControls())
                mainIds.add (paramOf (c));
            for (auto* c : panel->getMoreControls())
                more.add (paramOf (c));
            expectEquals (mainIds.joinIntoString (","), juce::String ("choirVoices,choirDetune,choirWidth,choirMix"));
            for (auto* id : { pid::choirTiming, pid::choirHumanize, pid::choirDensity })
                expect (more.contains (id), juce::String ("Mehr holds ") + id);
        }
    }
};

static VoicesPanelTests voicesPanelTests;
