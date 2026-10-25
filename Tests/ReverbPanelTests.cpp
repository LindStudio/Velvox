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
    }
};

static ReverbPanelTests reverbPanelTests;
