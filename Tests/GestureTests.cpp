// Uniform control gestures (spec 2.6): every knob, slider and segment group behaves the same.
//   drag; Shift or Ctrl drag = 1/10; double-click and Alt+click = default; wheel = step (over the control);
//   click on the value = type a value (units accepted); right-click = reset / enter / copy / paste
//   (+ module items inside a panel); hover info after 600 ms.

#include "TestHelpers.h"
#include "UI/Widgets.h"
#include "UI/Segments.h"
#include "UI/ValueText.h"

using namespace velvox;
using namespace velvox::test;

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

    double dragBy (juce::Slider& s, float pixels, juce::ModifierKeys mods, bool horizontal = false)
    {
        const double before = s.getValue();
        const juce::Point<float> start (s.getWidth() * 0.5f, s.getHeight() * 0.4f);
        const auto to = [&] (float d) { return horizontal ? start.translated (d, 0.0f) : start.translated (0.0f, -d); };
        s.mouseDown (mouseAt (s, start, start, false, mods));
        for (int i = 1; i <= 10; ++i)
            s.mouseDrag (mouseAt (s, to (pixels * (float) i / 10.0f), start, true, mods));
        s.mouseUp (mouseAt (s, to (pixels), start, true, mods));
        return s.getValue() - before;
    }

    juce::RangedAudioParameter& param (Harness& h, const char* id) { return *h.proc.getState().getParameter (id); }

    juce::StringArray menuTexts (const juce::PopupMenu& m)
    {
        juce::StringArray out;
        for (juce::PopupMenu::MenuItemIterator it (m); it.next();)
            out.add (it.getItem().text);
        return out;
    }
}

class GestureTests final : public juce::UnitTest
{
public:
    GestureTests() : juce::UnitTest ("Headliner control gestures", "Headliner") {}

    void runTest() override
    {
        beginTest ("Typed values: units, k suffix, fractions, garbage");
        {
            Harness h;
            const auto parse = [&] (const char* id, const char* text) { return ui::parseValueText (text, param (h, id)); };
            const auto near = [&] (std::optional<float> v, float expected, const juce::String& what)
            {
                expect (v.has_value(), what + " parses");
                if (v.has_value())
                    expectWithinAbsoluteError (*v, expected, std::abs (expected) * 1.0e-3f + 1.0e-3f, what);
            };
            near (parse (pid::deessFreq, "7.5k"), 7500.0f, "7.5k");
            near (parse (pid::deessFreq, "7,5 kHz"), 7500.0f, "German decimal comma + kHz");
            near (parse (pid::deessFreq, "8000 Hz"), 8000.0f, "Hz");
            near (parse (pid::outputGain, "-3 dB"), -3.0f, "-3 dB");
            near (parse (pid::outputGain, "+2.5"), 2.5f, "plain number");
            near (parse (pid::outputGain, "-500 dB"), -24.0f, "clamped to the range");
            near (parse (pid::delayTime, "1.5 s"), 1500.0f, "seconds on a ms parameter");
            near (parse (pid::delayTime, "250ms"), 250.0f, "ms");
            near (parse (pid::reverbDecay, "800 ms"), 0.8f, "ms on a seconds parameter");
            near (parse (pid::compRatio, "4:1"), 4.0f, "ratio");
            near (parse (pid::reverbMix, "50%"), 50.0f, "percent");
            near (parse (pid::monoBelow, "off"), 20.0f, "Off = the bottom of the range");
            near (parse (pid::delaySync, "on"), 1.0f, "bool on");
            near (parse (pid::delaySync, "aus"), 0.0f, "bool aus");
            if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (&param (h, pid::delayDivision)))
                near (parse (pid::delayDivision, "1/8"), (float) c->choices.indexOf ("1/8"), "choice by name");
            expect (! parse (pid::outputGain, "abc").has_value(), "garbage is refused");
            expect (! parse (pid::outputGain, "   ").has_value(), "empty is refused");
            expect (! parse (pid::delayDivision, "1/7").has_value(), "unknown choice is refused");
        }

        beginTest ("Knob: the value text accepts units; garbage keeps the value");
        {
            Harness h;
            ui::Knob k (h.proc.getState(), pid::deessFreq, "Frequency");
            k.setBounds (0, 0, 90, 90);
            expectWithinAbsoluteError (k.slider.getValueFromText ("9k"), 9000.0, 0.5);
            k.slider.setValue (5000.0, juce::sendNotificationSync);
            expectWithinAbsoluteError (k.slider.getValueFromText ("xyz"), 5000.0, 0.5, "invalid text = no change");
        }

        beginTest ("Fine drag: Shift and Ctrl are 10 times finer");
        {
            Harness h;
            ui::Knob k (h.proc.getState(), pid::compThreshold, "Threshold");
            k.setBounds (0, 0, 100, 120);
            expectEquals (ui::FineSlider::fineFactor, 10);
            k.slider.setValue (-50.0, juce::sendNotificationSync);
            const double normal = dragBy (k.slider, 40.0f, {});
            for (auto mods : { juce::ModifierKeys (juce::ModifierKeys::shiftModifier), juce::ModifierKeys (juce::ModifierKeys::ctrlModifier) })
            {
                k.slider.setValue (-50.0, juce::sendNotificationSync);
                const double fine = dragBy (k.slider, 40.0f, mods);
                expectWithinAbsoluteError (fine * 10.0, normal, normal * 0.15, "1/10 speed");
            }
        }

        beginTest ("Double-click and Alt+click return to the default (one undo step each)");
        {
            Harness h;
            ui::Knob k (h.proc.getState(), pid::compRatio, "Ratio");
            k.setBounds (0, 0, 100, 120);
            k.slider.setValue (9.0, juce::sendNotificationSync);
            const juce::Point<float> c (50.0f, 30.0f);
            k.slider.mouseDoubleClick (mouseAt (k.slider, c, c, false, {}, 2));
            expectWithinAbsoluteError (h.get (pid::compRatio), 3.0f, 1.0e-3f, "double-click");

            k.slider.setValue (9.0, juce::sendNotificationSync);
            const int steps = h.proc.getUndo().getNumSteps();
            const juce::ModifierKeys alt (juce::ModifierKeys::altModifier | juce::ModifierKeys::leftButtonModifier);
            k.slider.mouseDown (mouseAt (k.slider, c, c, false, alt));
            k.slider.mouseUp (mouseAt (k.slider, c, c, false, alt));
            expectWithinAbsoluteError (h.get (pid::compRatio), 3.0f, 1.0e-3f, "Alt+click");
            expectEquals (h.proc.getUndo().getNumSteps(), steps + 1, "an undo step");
        }

        beginTest ("Wheel steps the value over the control");
        {
            Harness h;
            ui::Knob k (h.proc.getState(), pid::compThreshold, "Threshold");
            k.setBounds (0, 0, 100, 120);
            const double v0 = k.slider.getValue();
            const juce::Point<float> c (50.0f, 30.0f);
            k.slider.mouseWheelMove (mouseAt (k.slider, c, c, false), wheel (0.12f));
            expectGreaterThan (k.slider.getValue(), v0, "wheel up raises");
            juce::Thread::sleep (5);   // juce::Slider drops a second wheel event with the same time stamp
            k.slider.mouseWheelMove (mouseAt (k.slider, c, c, false), wheel (-0.24f));
            expectLessThan (k.slider.getValue(), v0, "wheel down lowers");
        }

        beginTest ("Right-click menu: reset, enter, copy, paste; module items inside a panel");
        {
            Harness h;
            auto& thr = param (h, pid::compThreshold);
            const auto texts = menuTexts (ui::gestures::buildMenu (thr, nullptr, [] {}));
            for (auto* t : { "Standardwert", "Wert eingeben", "Wert kopieren", "Wert einfügen" })
                expect (texts.joinIntoString ("|").contains (juce::String::fromUTF8 (t)), juce::String::fromUTF8 (t) + " in " + texts.joinIntoString ("|"));

            struct Panel final : ui::gestures::PanelMenuHost
            {
                void addPanelItems (juce::PopupMenu& m) override { m.addItem (juce::String::fromUTF8 ("Modul zurücksetzen"), [] {}); }
            } panel;
            const auto withPanel = menuTexts (ui::gestures::buildMenu (thr, &panel, [] {}));
            expect (withPanel.joinIntoString ("|").contains (juce::String::fromUTF8 ("Modul zurücksetzen")), "panel items appended");

            // Copy -40 dB from the compressor, paste into the de-esser threshold
            ui::gestures::setReal (thr, -40.0f);
            expect (ui::gestures::copyValue (thr));
            auto& deess = param (h, pid::deessThreshold);
            expect (ui::gestures::pasteValue (deess));
            expectWithinAbsoluteError (h.get (pid::deessThreshold), -40.0f, 0.05f, "pasted");
            ui::gestures::resetToDefault (deess);
            expectWithinAbsoluteError (h.get (pid::deessThreshold), -30.0f, 0.05f, "default of the de-esser threshold is -30");
            ui::gestures::resetToDefault (thr);
            expectWithinAbsoluteError (h.get (pid::compThreshold), -18.0f, 0.05f, "reset");
        }

        beginTest ("Segments: double-click / Alt+click reset, wheel steps, right-click never selects");
        {
            Harness h;
            auto& state = h.proc.getState();
            auto* choice = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (pid::reverbType));
            ui::Segments seg (state, pid::reverbType, choice->choices, ui::colours::teal);
            seg.setBounds (0, 0, 400, 28);
            const int def = choice->getIndex();
            seg.clickSegment (3);
            expectEquals (choice->getIndex(), seg.getChoiceForSegment (3));

            auto* button = seg.getChildComponent (1);
            const auto c = button->getLocalBounds().getCentre().toFloat();
            button->mouseDoubleClick (mouseAt (*button, c, c, false, {}, 2));
            expectEquals (choice->getIndex(), def, "double-click resets");

            seg.clickSegment (3);
            const juce::ModifierKeys alt (juce::ModifierKeys::altModifier | juce::ModifierKeys::leftButtonModifier);
            button->mouseDown (mouseAt (*button, c, c, false, alt));
            button->mouseUp (mouseAt (*button, c, c, false, alt));
            expectEquals (choice->getIndex(), def, "Alt+click resets");

            seg.clickSegment (2);
            juce::Thread::sleep (5);
            button->mouseWheelMove (mouseAt (*button, c, c, false), wheel (-0.12f));
            expectEquals (choice->getIndex(), seg.getChoiceForSegment (3), "wheel down = next segment");
            button->mouseWheelMove (mouseAt (*button, c, c, false), wheel (0.12f));
            expectEquals (choice->getIndex(), seg.getChoiceForSegment (2), "wheel up = previous segment");

            const juce::ModifierKeys right (juce::ModifierKeys::rightButtonModifier);
            auto* other = seg.getChildComponent (5);
            const auto oc = other->getLocalBounds().getCentre().toFloat();
            other->mouseDown (mouseAt (*other, oc, oc, false, right));
            other->mouseUp (mouseAt (*other, oc, oc, false, right));
            juce::PopupMenu::dismissAllActiveMenus();
            expectEquals (choice->getIndex(), seg.getChoiceForSegment (2), "right-click opens the menu, does not select");
        }

        beginTest ("Horizontal slider: same gestures, value text, along the axis");
        {
            Harness h;
            ui::HSlider s (h.proc.getState(), pid::macroPresence, "Presence", ui::colours::teal);
            s.setBounds (0, 0, 300, 40);
            expect (s.slider.getSliderStyle() == juce::Slider::LinearHorizontal);
            expect (s.slider.getTextBoxPosition() == juce::Slider::TextBoxRight, "value text next to the slider");
            const double right = dragBy (s.slider, 30.0f, {}, true);
            expectGreaterThan (right, 0.0, "drag to the right raises");
            s.slider.setValue (50.0, juce::sendNotificationSync);
            const double fine = dragBy (s.slider, 30.0f, juce::ModifierKeys::shiftModifier, true);
            expectWithinAbsoluteError (fine * 10.0, right, right * 0.2, "Shift = 1/10");
            const juce::Point<float> c (100.0f, 20.0f);
            s.slider.mouseDoubleClick (mouseAt (s.slider, c, c, false, {}, 2));
            expectWithinAbsoluteError ((float) s.slider.getValue(), findParameterSpec (pid::macroPresence)->defaultValue, 1.0e-3f, "double-click default");
            expectEquals (s.getParamId(), juce::String (pid::macroPresence));
        }

        beginTest ("Hover info appears after 600 ms");
        {
            expectEquals (ui::motion::hoverInfoDelayMs, 600);
        }
    }
};

static GestureTests gestureTests;
