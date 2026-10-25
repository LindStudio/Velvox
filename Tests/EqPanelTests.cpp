// The 0.6 EQ panel (spec 5.1, Pro-Q style): band operations (one undo step each), the graph with its
// gestures, the selected-band row and the Mehr drawer.

#include "TestHelpers.h"
#include "UI/EqPanel.h"
#include "Parameters/EqBands.h"

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

        beginTest ("Macro bands: shown, and editing them never doubles the macro share");
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
            expect (shown[(size_t) air].macroOnly, "it has no parameters yet");
            expectWithinAbsoluteError (shown[(size_t) air].gainOffset, 5.0f, 0.01f, "macro share +5 dB");

            const auto before = response (h, 0);
            expect (ui::eqedit::materialise (h.proc, 0, air));
            const auto after = response (h, 0);
            for (size_t i = 0; i < before.size(); ++i)
                expectWithinAbsoluteError (after[i], before[i], 0.01, "same sound after materialising");
            const auto p = band (h, 0, air);
            expect (p.on && p.role == (int) dsp::EqRole::air, "now a parameter band with its role");
            expectWithinAbsoluteError (p.gain, 0.0f, 1.0e-3f, "base gain 0 dB");
            expect (! ui::eqedit::shownBands (h.proc, 0)[(size_t) air].macroOnly);
            expect (! ui::eqedit::materialise (h.proc, 0, air), "second time: nothing to do");

            // Switching it off through the panel works on the materialised band in one step
            h.proc.getUndo().clear();
            Harness m;
            withEq (m, 0);
            m.proc.getState().getParameter (pid::macroPresence)->setValueNotifyingHost (1.0f);
            ui::eqedit::setType (m.proc, 0, air, EqBandType::bell);
            expectEquals (band (m, 0, air).type, (int) EqBandType::bell, "edit on a macro-only band");
            expectEquals (band (m, 0, air).role, (int) dsp::EqRole::air, "keeps its role");
            expectEquals (steps (m), 1, "materialise + edit = one step");
        }
    }
};

static EqPanelTests eqPanelTests;
