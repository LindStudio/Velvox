// Pitch panel "zwei große Regler" (spec 5.10): a big semitone control and a big Formant control in the visual,
// the detected input note and the note that comes out between them; Fine, Mix, Formanten erhalten in the main row.

#include "TestHelpers.h"
#include "PluginEditor.h"
#include "UI/PitchPanel.h"
#include "DSP/PitchModule.h"

using namespace velvox;
using namespace velvox::test;
using dsp::ModuleId;

namespace
{
    juce::Rectangle<int> panelSize() { return VelvoxAudioProcessorEditor::panelArea().withZeroOrigin(); }

    void runTone (dsp::PitchModule& m, float hz, double seconds, float level = 0.4f)
    {
        juce::AudioBuffer<float> b (1, 480);
        double phase = 0.0;
        for (int block = 0; block < (int) (seconds * 100.0); ++block)
        {
            for (int i = 0; i < 480; ++i)
            {
                b.setSample (0, i, level * (float) std::sin (phase));
                phase += juce::MathConstants<double>::twoPi * hz / 48000.0;
            }
            m.process (b.getArrayOfWritePointers(), 1, 480);
        }
    }

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
}

class PitchPanelTests final : public juce::UnitTest
{
public:
    PitchPanelTests() : juce::UnitTest ("Headliner pitch panel", "Headliner") {}

    void runTest() override
    {
        using namespace ui;

        beginTest ("PitchModule feed: the detected input pitch (voiced tone), 0 for silence");
        {
            dsp::PitchModule m;
            m.prepare (48000.0, 480);
            dsp::PitchSettings s;
            s.enabled = true;
            s.semitones = 3.0f;
            m.setSettings (s);
            m.activate();
            expectEquals (m.getDetectedHz(), 0.0f, "nothing before audio");
            runTone (m, 220.0f, 0.5);
            expectWithinAbsoluteError (m.getDetectedHz(), 220.0f, 1.0f, "220 Hz tone");
            runTone (m, 220.0f, 0.5, 0.0f);
            expectEquals (m.getDetectedHz(), 0.0f, "silence = unvoiced");
        }

        beginTest ("Note names and cents");
        {
            expectEquals (pitchnotes::noteName (440.0f), juce::String ("A4"));
            expectEquals (pitchnotes::noteName (261.63f), juce::String ("C4"));
            expectEquals (pitchnotes::noteName (220.0f), juce::String ("A3"));
            expectEquals (pitchnotes::noteName (0.0f), juce::String::fromUTF8 ("–"));
            expectWithinAbsoluteError (pitchnotes::centsOff (440.0f * std::pow (2.0f, 30.0f / 1200.0f)), 30.0f, 0.1f);
            expectWithinAbsoluteError (pitchnotes::centsOff (440.0f * std::pow (2.0f, -40.0f / 1200.0f)), -40.0f, 0.1f);
        }

        beginTest ("Stage read-out: input note -> output note; unvoiced or off = –");
        {
            Harness h;
            auto r = h.proc.getRouting();
            if (! r.contains (ModuleId::pitch))
                r.insertDefault (ModuleId::pitch, 0);
            h.proc.setRouting (r);
            h.set (pid::pitchOn, 1.0f);
            h.set (pid::pitchSemitones, 3.0f);
            PitchStage stage (h.proc);
            stage.setBounds (0, 0, 900, 300);
            auto live = std::make_unique<LiveData>();
            double phase = 0.0;
            auto feed = [&] (float hz, float level, int blocks)
            {
                juce::AudioBuffer<float> buf (2, 480);
                juce::MidiBuffer midi;
                for (int b = 0; b < blocks; ++b)
                {
                    for (int i = 0; i < 480; ++i)
                    {
                        const float v = level * (float) std::sin (phase);
                        phase += juce::MathConstants<double>::twoPi * hz / 48000.0;
                        buf.setSample (0, i, v);
                        buf.setSample (1, i, v);
                    }
                    h.proc.processBlock (buf, midi);
                    live->settings = h.proc.readEffectiveSettings();
                    live->time += 0.01;
                    stage.update (*live);
                }
            };
            feed (220.0f, 0.3f, 60);
            expectEquals (stage.getInputText(), juce::String ("A3"));
            expectEquals (stage.getOutputText(), juce::String ("C4"), "+3 st");
            expect (stage.isVoiced());
            feed (220.0f, 0.0f, 60);
            expectEquals (stage.getInputText(), juce::String::fromUTF8 ("–"), "silence");
            expect (! stage.isVoiced());
            feed (220.0f, 0.3f, 60);
            h.set (pid::pitchOn, 0.0f);
            feed (220.0f, 0.3f, 5);
            expectEquals (stage.getOutputText(), juce::String::fromUTF8 ("–"), "off");
        }

        beginTest ("Panel: big Semitones and Formant in the visual; Fine, Mix, Formanten erhalten in the main row");
        {
            Harness h;
            auto panel = createDetailPanel (h.proc, (int) ModuleId::pitch);
            panel->setBounds (panelSize());
            const auto& main = panel->getMainControls();
            expectEquals ((int) main.size(), 3);
            if (main.size() == 3)
            {
                auto* fine = dynamic_cast<Knob*> (main[0]);
                auto* mix = dynamic_cast<Knob*> (main[1]);
                auto* keep = dynamic_cast<Toggle*> (main[2]);
                expect (fine != nullptr && fine->getParamId() == pid::pitchCents, "Fine");
                expect (mix != nullptr && mix->getParamId() == pid::pitchMix, "Mix");
                expect (keep != nullptr && keep->getParamId() == pid::pitchPreserveFormants, "Formanten erhalten");
            }
            Knob* semis = nullptr;
            Knob* formant = nullptr;
            for (auto* c : panel->getChildren())
                if (auto* k = dynamic_cast<Knob*> (c))
                {
                    if (k->getParamId() == pid::pitchSemitones) semis = k;
                    if (k->getParamId() == pid::pitchFormant) formant = k;
                }
            expect (semis != nullptr && formant != nullptr, "both big controls on the panel");
            if (semis != nullptr && formant != nullptr)
            {
                const auto visual = panel->getVisualArea();
                expect (visual.contains (semis->getBounds()) && visual.contains (formant->getBounds()), "inside the visual");
                expect (semis->getDialArea().getWidth() >= 120, "Semitones big: " + juce::String (semis->getDialArea().getWidth()));
                expect (formant->getDialArea().getWidth() >= 80, "Formant big: " + juce::String (formant->getDialArea().getWidth()));
                expect (semis->getDialArea().getWidth() > formant->getDialArea().getWidth(), "semitones the biggest");
                expect (semis->getBounds().getCentreX() < formant->getBounds().getCentreX(), "side by side, semitones left");
                // Snaps to semitones
                semis->slider.setValue (3.4, juce::sendNotificationSync);
                expectWithinAbsoluteError (h.proc.getState().getRawParameterValue (pid::pitchSemitones)->load(), 3.0f, 1.0e-4f, "snaps");
            }
            expect (panel->getMoreControls().empty() && ! panel->getMoreButton().isVisible(), "nothing left for Mehr");
            auto* stage = findChild<PitchStage> (*panel);
            expect (stage != nullptr, "the visual is the stage");
            if (stage != nullptr && semis != nullptr)
            {
                const auto c = stage->getDialCentre() + stage->getPosition().toFloat();
                expect (semis->getBounds().toFloat().contains (c), "tick ring around the semitone dial");
            }
            panel->setMoreOpen (true);   // nothing breaks with Mehr "open"
            expect (panel->getVisualArea().getHeight() >= 150);
        }
    }
};

static PitchPanelTests pitchPanelTests;
