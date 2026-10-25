// The 0.6 EQ panel (spec 5.1, Pro-Q style): band operations (one undo step each), the graph with its
// gestures, the selected-band row and the Mehr drawer.

#include "TestHelpers.h"
#include "UI/EqPanel.h"
#include "Parameters/EqBands.h"
#include "PluginEditor.h"

using namespace velvox;
using namespace velvox::test;
using dsp::ModuleId;
using dsp::EqBandType;

namespace
{
    float value (Harness& h, const juce::String& id) { return h.proc.getState().getRawParameterValue (id)->load(); }

    void setValue (Harness& h, const juce::String& id, float v)
    {
        auto* p = h.proc.getState().getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (v));
    }

    dsp::EqBand band (Harness& h, int instance, int b) { return ui::eqedit::parameterBand (h.proc, instance, b); }

    int steps (Harness& h) { return h.proc.getUndo().getNumSteps(); }

    // The response of the effective settings of EQ `instance` at a few frequencies
    std::vector<double> response (Harness& h, int instance)
    {
        const auto eq = h.proc.readEffectiveSettings().instance (instance).eq;
        std::vector<double> out;
        for (double f : { 60.0, 150.0, 400.0, 1000.0, 3000.0, 8000.0, 12000.0, 16000.0 })
            out.push_back (dsp::EqualizerModule::responseDb (eq, f, 48000.0));
        return out;
    }

    void withEq (Harness& h, int instance)
    {
        auto r = h.proc.getRouting();
        if (! r.contains (ModuleId::eq, instance))
            r.insertAt (r.count, ModuleId::eq, instance, true);
        h.proc.setRouting (r);
        h.proc.getUndo().clear();
    }

    juce::MouseEvent mouse (juce::Component& c, juce::Point<float> pos, juce::Point<float> downPos, bool dragged,
                            juce::ModifierKeys mods = {})
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, mods,
                                 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, downPos, now, 1, dragged);
    }

    void drag (juce::Component& c, juce::Point<float> from, juce::Point<float> to, juce::ModifierKeys mods = {})
    {
        c.mouseDown (mouse (c, from, from, false, mods));
        c.mouseDrag (mouse (c, (from + to) * 0.5f, from, true, mods));
        c.mouseDrag (mouse (c, to, from, true, mods));
        c.mouseUp (mouse (c, to, from, true, mods));
    }

    void doubleClick (juce::Component& c, juce::Point<float> at)
    {
        for (int i = 0; i < 2; ++i)
        {
            c.mouseDown (mouse (c, at, at, false));
            c.mouseUp (mouse (c, at, at, false));
        }
        c.mouseDoubleClick (mouse (c, at, at, false));
    }

    juce::MouseWheelDetails wheel (float dy, float dx = 0.0f)
    {
        juce::MouseWheelDetails w;
        w.deltaX = dx;
        w.deltaY = dy;
        w.isReversed = false;
        w.isSmooth = false;
        w.isInertial = false;
        return w;
    }

    juce::String paramOf (juce::Component* c)
    {
        if (auto* k = dynamic_cast<ui::Knob*> (c)) return k->getParamId();
        if (auto* s = dynamic_cast<ui::Segments*> (c)) return s->getParamId();
        if (auto* t = dynamic_cast<ui::Toggle*> (c)) return t->getParamId();
        if (auto* ch = dynamic_cast<ui::Choice*> (c)) return ch->getParamId();
        return {};
    }

    void collectParams (juce::Component& c, juce::StringArray& out)
    {
        for (auto* child : c.getChildren())
        {
            if (const auto id = paramOf (child); id.isNotEmpty())
                out.addIfNotAlreadyThere (id);
            collectParams (*child, out);
        }
    }

    juce::StringArray mainRowParams (ui::DetailPanel& panel)
    {
        juce::StringArray ids;
        for (auto* c : panel.getMainControls())
            if (c->isVisible())
            {
                if (const auto id = paramOf (c); id.isNotEmpty())
                    ids.add (id);
                collectParams (*c, ids);
            }
        return ids;
    }

    std::unique_ptr<ui::EqPanel> eqPanel (Harness& h, int instance)
    {
        auto p = ui::createDetailPanel (h.proc, dsp::selectionCode (ModuleId::eq, instance));
        p->setBounds (VelvoxAudioProcessorEditor::panelArea().withZeroOrigin());
        auto* eq = dynamic_cast<ui::EqPanel*> (p.get());
        if (eq != nullptr)
            p.release();
        return std::unique_ptr<ui::EqPanel> (eq);
    }

    juce::Array<int> menuIds (const juce::PopupMenu& menu)
    {
        juce::Array<int> ids;
        for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
            if (it.getItem().itemID != 0)
                ids.add (it.getItem().itemID);
        return ids;
    }
}

class EqPanelTests final : public juce::UnitTest
{
public:
    EqPanelTests() : juce::UnitTest ("Headliner EQ panel", "Headliner") {}

    void runTest() override
    {
        beginTest ("Band operations: add, delete, type, slope, on, reset - one undo step each");
        {
            Harness h;
            withEq (h, 0);
            for (int b = 0; b < dsp::maxEqBands; ++b)
                expect (band (h, 0, b).isFree(), "fresh instance has no bands");

            const int b = ui::eqedit::addBand (h.proc, 0, 2500.0f, 4.0f);
            expectEquals (b, 0, "first free slot");
            auto v = band (h, 0, b);
            expect (v.on && v.type == (int) EqBandType::bell && v.role == (int) dsp::EqRole::none);
            expectWithinAbsoluteError (v.freq, 2500.0f, 0.5f);
            expectWithinAbsoluteError (v.gain, 4.0f, 0.01f);
            expectWithinAbsoluteError (v.q, 1.0f, 1.0e-3f);
            expectEquals (steps (h), 1, "add = one step");
            expect (h.proc.getUndo().undo());
            expect (band (h, 0, b).isFree(), "undo removes the band");
            expect (h.proc.getUndo().redo());
            expect (! band (h, 0, b).isFree(), "redo brings it back");

            h.proc.getUndo().clear();
            ui::eqedit::setType (h.proc, 0, b, EqBandType::lowCut);
            expectEquals (band (h, 0, b).type, (int) EqBandType::lowCut);
            ui::eqedit::setSlope (h.proc, 0, b, 5);
            expectEquals (band (h, 0, b).slope, 5, "48 dB/oct");
            ui::eqedit::toggleOn (h.proc, 0, b);
            expect (! band (h, 0, b).on, "switched off");
            ui::eqedit::setType (h.proc, 0, b, EqBandType::bell);
            setValue (h, ui::eqedit::paramId (0, b, pid::eqband::q), 4.0f);
            ui::eqedit::resetBand (h.proc, 0, b);
            expectWithinAbsoluteError (band (h, 0, b).gain, 0.0f, 1.0e-3f, "Alt+click: gain 0 dB");
            expectWithinAbsoluteError (band (h, 0, b).q, 1.0f, 1.0e-3f, "and Q 1");
            expectWithinAbsoluteError (band (h, 0, b).freq, 2500.0f, 0.5f, "frequency kept");
            expectEquals (steps (h), 5, "type, slope, on, type, reset = five steps (the plain Q write has no gesture)");

            ui::eqedit::deleteBand (h.proc, 0, b);
            expect (band (h, 0, b).isFree(), "delete = all 7 parameters at their defaults");
            for (auto* f : { pid::eqband::on, pid::eqband::type, pid::eqband::freq, pid::eqband::gain, pid::eqband::q,
                             pid::eqband::slope, pid::eqband::role })
            {
                auto* p = h.proc.getState().getParameter (ui::eqedit::paramId (0, b, f));
                expectWithinAbsoluteError (p->getValue(), p->getDefaultValue(), 1.0e-6f, juce::String ("default ") + f);
            }
            expectEquals (steps (h), 6, "delete = one step");
        }

        beginTest ("Band operations: a full EQ adds nothing; role bands can be deleted");
        {
            Harness h;
            withEq (h, 0);
            for (int i = 0; i < dsp::maxEqBands; ++i)
                expectEquals (ui::eqedit::addBand (h.proc, 0, 100.0f * (float) (i + 1), 0.0f), i);
            const int before = steps (h);
            expectEquals (ui::eqedit::addBand (h.proc, 0, 5000.0f, 3.0f), -1, "17th band refused");
            expectEquals (steps (h), before, "no step for nothing");

            Harness r;
            withEq (r, 0);
            const int air = findOrCreateEqBand (r.proc.getState(), dsp::EqRole::air, EqBandType::highShelf, 10000.0f, 2.0f, 0.7071f);
            r.proc.getUndo().clear();
            ui::eqedit::deleteBand (r.proc, 0, air);
            expect (band (r, 0, air).isFree(), "role band deleted (the macro recreates it when moved)");
            expectEquals (steps (r), 1);
        }

        beginTest ("Band operations: EQ 2 edits its own IDs only");
        {
            Harness h;
            withEq (h, 1);
            const int b = ui::eqedit::addBand (h.proc, 1, 800.0f, -3.0f);
            expectEquals (b, 0);
            expect (value (h, "eqBand1On_2") > 0.5f, "EQ 2 band 1 on");
            expectWithinAbsoluteError (value (h, "eqBand1Gain_2"), -3.0f, 0.01f);
            expect (band (h, 0, 0).isFree(), "EQ 1 untouched");
            ui::eqedit::setType (h.proc, 1, b, EqBandType::notch);
            expectEquals ((int) value (h, "eqBand1Type_2"), (int) EqBandType::notch);
            expectEquals ((int) value (h, "eqBand1Type"), 0);
        }

        beginTest ("Macro bands: a Simple move creates a real parameter band; editing it is a plain edit (0.6.1)");
        {
            Harness h;
            withEq (h, 0);
            h.proc.getState().getParameter (pid::macroPresence)->setValueNotifyingHost (1.0f);   // 100 %
            const auto shown = ui::eqedit::shownBands (h.proc, 0);
            int air = -1;
            for (int b = 0; b < dsp::maxEqBands; ++b)
                if (shown[(size_t) b].used && shown[(size_t) b].band.role == (int) dsp::EqRole::air)
                    air = b;
            expect (air >= 0, "the Presence band is shown");
            if (air < 0)
                return;
            expect (! shown[(size_t) air].macroOnly, "no hidden share: the band is a parameter band");
            expectWithinAbsoluteError (shown[(size_t) air].gainOffset, 0.0f, 1.0e-4f);
            const auto p = band (h, 0, air);
            expect (p.on && p.role == (int) dsp::EqRole::air, "a parameter band with its role");
            expectWithinAbsoluteError (p.gain, 5.0f, 1.0e-3f, "the +5 dB are in its Gain parameter");
            expect (! ui::eqedit::materialise (h.proc, 0, air), "nothing to materialise");

            h.proc.getUndo().clear();
            ui::eqedit::setType (h.proc, 0, air, EqBandType::bell);
            expectEquals (band (h, 0, air).type, (int) EqBandType::bell);
            expectEquals (band (h, 0, air).role, (int) dsp::EqRole::air, "keeps its role");
            expectEquals (steps (h), 1, "one step");
        }

        beginTest ("Graph: double-click on empty space adds a Bell there and selects it");
        {
            Harness h;
            withEq (h, 0);
            ui::EqGraph g (h.proc, nullptr, 0);
            g.setBounds (0, 0, 900, 320);
            int selectedByCallback = -2;
            g.onSelect = [&] (int b) { selectedByCallback = b; };
            const juce::Point<float> at (g.xForFreq (2000.0f), g.yForGain (6.0f));
            expectEquals (g.bandAt (at), -1, "empty space");
            doubleClick (g, at);
            const auto b = band (h, 0, 0);
            expect (b.on && b.type == (int) EqBandType::bell, "a Bell");
            expectWithinAbsoluteError (b.freq / 2000.0f, 1.0f, 0.02f, "at the clicked frequency");
            expectWithinAbsoluteError (b.gain, 6.0f, 0.3f, "and gain");
            expectEquals (g.getSelected(), 0);
            expectEquals (selectedByCallback, 0, "the panel hears about it");
            expectEquals (steps (h), 1, "one undo step (the clicks before it record nothing)");
            g.update (*std::make_unique<ui::LiveData>());
            expect (g.pointFor (0).getDistanceFrom (at) < 2.0f, "the point sits where it was added");
            expectEquals (g.bandAt (at), 0);

            // Gains beyond the shown +-18 dB keep the point inside the plot
            setValue (h, ui::eqedit::paramId (0, 0, pid::eqband::gain), 27.0f);
            g.update (*std::make_unique<ui::LiveData>());
            expect (g.plotArea().expanded (0.5f).contains (g.pointFor (0)), "clamped to the plot");

            // A full EQ: nothing happens
            for (int i = 1; i < dsp::maxEqBands; ++i)
                ui::eqedit::addBand (h.proc, 0, 100.0f + 50.0f * (float) i, 0.0f);
            g.update (*std::make_unique<ui::LiveData>());
            const int before = steps (h);
            const juce::Point<float> empty (g.xForFreq (15000.0f), g.yForGain (-14.0f));
            doubleClick (g, empty);
            expectEquals (steps (h), before, "17th band refused, no step");
        }

        beginTest ("Graph: drag a point (Shift / Ctrl fine), one undo step with host gestures");
        {
            Harness h;
            withEq (h, 0);
            ui::eqedit::addBand (h.proc, 0, 1000.0f, 0.0f);
            h.proc.getUndo().clear();
            ui::EqGraph g (h.proc, nullptr, 0);
            g.setBounds (0, 0, 900, 320);
            g.update (*std::make_unique<ui::LiveData>());

            const auto from = g.pointFor (0);
            const juce::Point<float> to (g.xForFreq (4000.0f), g.yForGain (-6.0f));
            drag (g, from, to);
            expectWithinAbsoluteError (band (h, 0, 0).freq / 4000.0f, 1.0f, 0.02f, "frequency follows");
            expectWithinAbsoluteError (band (h, 0, 0).gain, -6.0f, 0.3f, "gain follows");
            expectEquals (steps (h), 1, "the whole drag = one step");
            expect (h.proc.getUndo().undo());
            expectWithinAbsoluteError (band (h, 0, 0).freq, 1000.0f, 1.0f, "undone");
            expectWithinAbsoluteError (band (h, 0, 0).gain, 0.0f, 0.01f);

            // Fine: the same mouse distance moves a tenth
            g.update (*std::make_unique<ui::LiveData>());
            const auto p = g.pointFor (0);
            drag (g, p, p + juce::Point<float> (100.0f, -50.0f), juce::ModifierKeys::shiftModifier);
            const float fineOct = std::log2 (band (h, 0, 0).freq / 1000.0f);
            const float normalOct = std::log2 (g.freqForX (p.x + 100.0f) / 1000.0f);
            expectWithinAbsoluteError (fineOct, normalOct * 0.1f, 0.01f, "Shift: 1/10 in frequency");
            expectWithinAbsoluteError (band (h, 0, 0).gain, (g.gainForY (p.y - 50.0f) - 0.0f) * 0.1f, 0.05f, "and in gain");

            // Ctrl, too
            setValue (h, ui::eqedit::paramId (0, 0, pid::eqband::gain), 0.0f);
            g.update (*std::make_unique<ui::LiveData>());
            const auto c = g.pointFor (0);
            drag (g, c, c + juce::Point<float> (0.0f, -50.0f), juce::ModifierKeys::ctrlModifier);
            expectWithinAbsoluteError (band (h, 0, 0).gain, g.gainForY (c.y - 50.0f) * 0.1f, 0.05f, "Ctrl: fine");

            // A click on a point without moving records nothing
            const int before = steps (h);
            g.update (*std::make_unique<ui::LiveData>());
            drag (g, g.pointFor (0), g.pointFor (0));
            expectEquals (steps (h), before);
        }

        beginTest ("Graph: cut points move in frequency only; wheel = Q (cuts: slope); Alt+click resets; double-click toggles");
        {
            Harness h;
            withEq (h, 0);
            ui::eqedit::addBand (h.proc, 0, 2000.0f, 5.0f);
            const int cut = ui::eqedit::addBand (h.proc, 0, 100.0f, 0.0f);
            ui::eqedit::setType (h.proc, 0, cut, EqBandType::lowCut);
            ui::EqGraph g (h.proc, nullptr, 0);
            g.setBounds (0, 0, 900, 320);
            g.update (*std::make_unique<ui::LiveData>());

            const auto cp = g.pointFor (cut);
            expectWithinAbsoluteError (cp.y, g.yForGain (0.0f), 0.5f, "a cut sits on the 0 dB line");
            drag (g, cp, { g.xForFreq (150.0f), cp.y - 60.0f });
            expectWithinAbsoluteError (band (h, 0, cut).freq / 150.0f, 1.0f, 0.02f);
            expectWithinAbsoluteError (band (h, 0, cut).gain, 0.0f, 1.0e-3f, "no gain on a cut");

            g.update (*std::make_unique<ui::LiveData>());
            const auto bp = g.pointFor (0);
            const float q0 = band (h, 0, 0).q;
            g.mouseWheelMove (mouse (g, bp, bp, false), wheel (0.11f));
            const float q1 = band (h, 0, 0).q;
            g.mouseWheelMove (mouse (g, bp, bp, false), wheel (-0.33f));
            const float q2 = band (h, 0, 0).q;
            expect (q1 > q0 * 1.15f && q2 < q1, "wheel up narrows, down widens");
            const juce::Point<float> empty (g.xForFreq (12000.0f), g.yForGain (-12.0f));
            g.mouseWheelMove (mouse (g, empty, empty, false), wheel (0.11f));
            g.mouseWheelMove (mouse (g, bp, bp, false), wheel (0.0f, 0.3f));
            expectEquals (band (h, 0, 0).q, q2, "elsewhere / horizontal: nothing");

            g.update (*std::make_unique<ui::LiveData>());
            const int slope0 = band (h, 0, cut).slope;
            const auto cp2 = g.pointFor (cut);
            g.mouseWheelMove (mouse (g, cp2, cp2, false), wheel (0.11f));
            expectEquals (band (h, 0, cut).slope, slope0 + 1, "wheel on a cut: one slope step steeper");
            g.mouseWheelMove (mouse (g, cp2, cp2, false), wheel (-0.11f));
            expectEquals (band (h, 0, cut).slope, slope0);

            // Alt+click: gain and Q back to the defaults
            g.update (*std::make_unique<ui::LiveData>());
            const auto alt = g.pointFor (0);
            h.proc.getUndo().clear();
            g.mouseDown (mouse (g, alt, alt, false, juce::ModifierKeys::altModifier));
            g.mouseUp (mouse (g, alt, alt, false, juce::ModifierKeys::altModifier));
            expectWithinAbsoluteError (band (h, 0, 0).gain, 0.0f, 1.0e-3f, "Alt+click: 0 dB");
            expectWithinAbsoluteError (band (h, 0, 0).q, 1.0f, 1.0e-3f, "Q 1");
            expectEquals (steps (h), 1);

            // Double-click on a point: on / off
            g.update (*std::make_unique<ui::LiveData>());
            doubleClick (g, g.pointFor (0));
            expect (! band (h, 0, 0).on, "switched off");
            g.update (*std::make_unique<ui::LiveData>());
            doubleClick (g, g.pointFor (0));
            expect (band (h, 0, 0).on, "and on again");
            expectEquals (steps (h), 3);
        }

        beginTest ("Graph: right-click menu - 8 types, slopes for cuts, on/off, delete; Delete key");
        {
            Harness h;
            withEq (h, 0);
            ui::eqedit::addBand (h.proc, 0, 500.0f, -4.0f);
            ui::eqedit::addBand (h.proc, 0, 5000.0f, 2.0f);
            ui::EqGraph g (h.proc, nullptr, 0);
            g.setBounds (0, 0, 900, 320);
            g.update (*std::make_unique<ui::LiveData>());

            auto ids = menuIds (g.buildPointMenu (0));
            for (int t = 0; t < dsp::numEqBandTypes; ++t)
                expect (ids.contains (ui::EqGraph::menuTypeBase + t), "type " + juce::String (t));
            expect (! ids.contains (ui::EqGraph::menuSlopeBase), "no slopes for a Bell");
            expect (ids.contains (ui::EqGraph::menuToggle) && ids.contains (ui::EqGraph::menuDelete));

            h.proc.getUndo().clear();
            g.performMenu (0, ui::EqGraph::menuTypeBase + (int) EqBandType::highCut);
            expectEquals (band (h, 0, 0).type, (int) EqBandType::highCut);
            ids = menuIds (g.buildPointMenu (0));
            for (int s = 0; s < dsp::numEqSlopes; ++s)
                expect (ids.contains (ui::EqGraph::menuSlopeBase + s), "slope " + juce::String (dsp::eqSlopeDbPerOct (s)));
            g.performMenu (0, ui::EqGraph::menuSlopeBase + 1);
            expectEquals (band (h, 0, 0).slope, 1, "12 dB/oct");
            g.performMenu (0, ui::EqGraph::menuToggle);
            expect (! band (h, 0, 0).on);
            g.setSelected (0);
            g.performMenu (0, ui::EqGraph::menuDelete);
            expect (band (h, 0, 0).isFree(), "deleted");
            expectEquals (g.getSelected(), 1, "the selection moves to the remaining band");
            expectEquals (steps (h), 4, "every menu action = one step");

            expect (g.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)));
            expect (band (h, 0, 1).isFree(), "Delete key removes the selected band");
            expectEquals (g.getSelected(), -1, "nothing left to select");
        }

        beginTest ("Graph: hover card after 600 ms; macro bands are shown, tagged and editable");
        {
            Harness h;
            withEq (h, 0);
            ui::eqedit::addBand (h.proc, 0, 1000.0f, 3.0f);
            h.proc.getState().getParameter (pid::macroPresence)->setValueNotifyingHost (0.6f);   // 60 %
            ui::EqGraph g (h.proc, nullptr, 0);
            g.setBounds (0, 0, 900, 320);
            auto live = std::make_unique<ui::LiveData>();
            g.update (*live);

            const auto p = g.pointFor (0);
            g.mouseMove (mouse (g, p, p, false));
            g.update (*live);
            expect (! g.isHoverCardShown(), "not at once");
            juce::Thread::sleep (650);
            g.update (*live);
            expect (g.isHoverCardShown(), "after 600 ms");
            g.mouseExit (mouse (g, { 1.0f, 1.0f }, { 1.0f, 1.0f }, false));
            expect (! g.isHoverCardShown());

            int air = -1;
            for (int b = 0; b < dsp::maxEqBands; ++b)
                if (g.getShown()[(size_t) b].used && g.getShown()[(size_t) b].band.role == (int) dsp::EqRole::air)
                    air = b;
            expectEquals (air, 1, "the Presence band is shown next to the user band");
            if (air < 0)
                return;
            expectEquals (ui::eqedit::roleTag (g.getShown()[(size_t) air].band.role), juce::String ("AIR"), "role tag");
            expectEquals (ui::eqedit::roleTag (g.getShown()[0].band.role), juce::String(), "user bands carry no tag");
            const auto ap = g.pointFor (air);
            expectWithinAbsoluteError (ap.y, g.yForGain (3.0f), 0.5f, "drawn at the processed gain (+3 dB at 60 %)");
            h.proc.getUndo().clear();
            drag (g, ap, { ap.x, g.yForGain (8.0f) });
            expectWithinAbsoluteError (h.proc.readEffectiveSettings().eq.bands[(size_t) air].gain, 8.0f, 0.3f,
                                       "the processed band follows the mouse");
            expectWithinAbsoluteError (band (h, 0, air).gain, 8.0f, 0.3f, "0.6.1: the parameter IS the processed gain");
            expectEquals (steps (h), 1, "one step");
            expect (h.proc.getUndo().undo());
            expectWithinAbsoluteError (band (h, 0, air).gain, 3.0f, 1.0e-3f, "undo: back to what PRESENCE set");
        }

        beginTest ("Graph: EQ 2 edits its own bands only");
        {
            Harness h;
            withEq (h, 1);
            ui::EqGraph g (h.proc, nullptr, 1);
            g.setBounds (0, 0, 900, 320);
            doubleClick (g, { g.xForFreq (300.0f), g.yForGain (-5.0f) });
            expect (value (h, "eqBand1On_2") > 0.5f);
            expectWithinAbsoluteError (value (h, "eqBand1Gain_2"), -5.0f, 0.3f);
            expect (band (h, 0, 0).isFree(), "EQ 1 untouched");
            g.update (*std::make_unique<ui::LiveData>());
            const auto p = g.pointFor (0);
            g.mouseWheelMove (mouse (g, p, p, false), wheel (0.22f));
            expect (value (h, "eqBand1Q_2") > 1.3f, "wheel on EQ 2");
            expectWithinAbsoluteError (value (h, "eqBand1Q"), 1.0f, 1.0e-4f, "EQ 1 Q untouched");
        }

        beginTest ("Panel: the main row shows the selected band (On, Type, Freq, Gain, Q - Slope for cuts)");
        {
            Harness h;
            withEq (h, 0);
            auto panel = eqPanel (h, 0);
            expect (panel != nullptr, "the EQ panel");
            if (panel == nullptr)
                return;
            expect (panel->getGraph() != nullptr, "its visual is the graph");
            expectEquals (panel->getSelectedBand(), -1, "no bands, nothing selected");
            expect (mainRowParams (*panel).isEmpty(), "empty row without a band");
            expectEquals ((int) panel->getMainControls().size(), 5);

            ui::eqedit::addBand (h.proc, 0, 300.0f, -2.0f);
            ui::eqedit::addBand (h.proc, 0, 3000.0f, 2.0f);
            ui::eqedit::addBand (h.proc, 0, 8000.0f, 1.0f);
            panel->refresh();
            panel->selectBand (2);
            expectEquals (panel->getGraph()->getSelected(), 2, "graph follows the panel");
            auto ids = mainRowParams (*panel);
            for (auto* f : { pid::eqband::on, pid::eqband::type, pid::eqband::freq, pid::eqband::gain, pid::eqband::q })
                expect (ids.contains (pid::eqBandId (3, f)), juce::String ("band 3 ") + f);
            expect (! ids.contains (pid::eqBandId (3, pid::eqband::slope)), "no slope for a Bell");
            for (auto& id : ids)
                expect (id.startsWith ("eqBand3"), "only band 3: " + id);

            panel->getGraph()->setSelected (0);
            expectEquals (panel->getSelectedBand(), 0, "panel follows the graph");
            expect (mainRowParams (*panel).contains (pid::eqBandId (1, pid::eqband::freq)));

            ui::eqedit::setType (h.proc, 0, 0, EqBandType::lowCut);
            panel->refresh();
            ids = mainRowParams (*panel);
            expect (ids.contains (pid::eqBandId (1, pid::eqband::slope)), "a cut shows its slope");
            expect (! ids.contains (pid::eqBandId (1, pid::eqband::q)), "instead of Q");

            // Main knobs are large, values editable through them
            for (auto* c : panel->getMainControls())
                for (auto* child : c->getChildren())
                    if (auto* k = dynamic_cast<ui::Knob*> (child))
                        expect (k->getDialSize() >= 50, "main knob large");

            // Delete the selected band through the graph: the row follows
            panel->getGraph()->performMenu (0, ui::EqGraph::menuDelete);
            expectEquals (panel->getSelectedBand(), 1, "next band");
            expect (mainRowParams (*panel).contains (pid::eqBandId (2, pid::eqband::gain)));
        }

        beginTest ("Panel: no 0.5 fixed band shown; every band parameter reachable; Mix in Mehr");
        {
            Harness h;
            withEq (h, 0);
            auto panel = eqPanel (h, 0);
            if (panel == nullptr)
                return;
            juce::StringArray all;
            collectParams (*panel, all);
            for (auto* legacy : { pid::hpfOn, pid::hpfFreq, pid::hpfSlope, pid::lowType, pid::lowFreq, pid::lowGain, pid::lowQ,
                                  pid::midFreq, pid::midGain, pid::midQ, pid::highType, pid::highFreq, pid::highGain, pid::highQ,
                                  pid::lpfOn, pid::lpfFreq, pid::lpfSlope })
                expect (! all.contains (legacy), juce::String ("hidden: ") + legacy);

            bool mixInMehr = false;
            for (auto* c : panel->getMoreControls())
                if (auto* k = dynamic_cast<ui::Knob*> (c))
                    mixInMehr = mixInMehr || k->getParamId() == pid::eqMix;
            expect (mixInMehr, "Mix in Mehr");

            for (int b = 0; b < dsp::maxEqBands; ++b)
            {
                for (auto type : { EqBandType::bell, EqBandType::highCut })
                {
                    setValue (h, ui::eqedit::paramId (0, b, pid::eqband::type), (float) type);
                    panel->selectBand (b);
                    panel->refresh();
                    juce::StringArray ids;
                    collectParams (*panel, ids);
                    for (auto* f : { pid::eqband::on, pid::eqband::type, pid::eqband::freq })
                        expect (ids.contains (pid::eqBandId (b + 1, f)), "reaches " + pid::eqBandId (b + 1, f));
                    if (type == EqBandType::bell)
                        expect (ids.contains (pid::eqBandId (b + 1, pid::eqband::gain)) && ids.contains (pid::eqBandId (b + 1, pid::eqband::q)),
                                "reaches gain / Q of band " + juce::String (b + 1));
                    else
                        expect (ids.contains (pid::eqBandId (b + 1, pid::eqband::slope)), "reaches the slope of band " + juce::String (b + 1));
                }
            }
        }

        beginTest ("Panel: Alte EQ-Kurve - umstellen only while eqLegacyResponse is set, one undo step; EQ 3 binds _3");
        {
            Harness h;
            withEq (h, 0);
            auto panel = eqPanel (h, 0);
            if (panel == nullptr)
                return;
            panel->setMoreOpen (true);
            panel->refresh();
            expect (! panel->getLegacyResponseButton().isVisible(), "hidden for a 0.6 state");
            setValue (h, pid::eqLegacyResponse, 1.0f);
            panel->refresh();
            expect (panel->getLegacyResponseButton().isVisible(), "shown for a migrated state");
            expect (panel->getMoreDrawer().getLocalBounds().contains (panel->getLegacyResponseButton().getBounds()), "inside Mehr");
            h.proc.getUndo().clear();
            panel->getLegacyResponseButton().onClick();
            expect (value (h, pid::eqLegacyResponse) < 0.5f, "cleared");
            expectEquals (steps (h), 1, "one step");
            panel->refresh();
            expect (! panel->getLegacyResponseButton().isVisible(), "gone again");

            Harness h3;
            withEq (h3, 2);
            ui::eqedit::addBand (h3.proc, 2, 500.0f, 3.0f);
            auto p3 = eqPanel (h3, 2);
            if (p3 == nullptr)
                return;
            expectEquals (p3->getSelectedBand(), 0, "the first band is selected on opening");
            for (auto& id : mainRowParams (*p3))
                expect (id.endsWith ("_3"), "EQ 3 controls: " + id);
        }

        beginTest ("Panel: a macro band selected in the panel shows its processed values");
        {
            Harness h;
            withEq (h, 0);
            h.proc.getState().getParameter (pid::macroPresence)->setValueNotifyingHost (1.0f);
            auto panel = eqPanel (h, 0);
            if (panel == nullptr)
                return;
            panel->refresh();
            const auto before = response (h, 0);
            panel->selectBand (0);
            expectEquals (band (h, 0, 0).role, (int) dsp::EqRole::air, "the panel binds the macro band's own parameters");
            expectWithinAbsoluteError (band (h, 0, 0).freq, 10000.0f, 1.0f, "at the macro's frequency");
            const auto after = response (h, 0);
            for (size_t i = 0; i < before.size(); ++i)
                expectWithinAbsoluteError (after[i], before[i], 0.01, "sound unchanged");
            expectEquals (steps (h), 0, "selecting is not an edit");
        }
    }
};

static EqPanelTests eqPanelTests;
