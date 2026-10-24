#pragma once

#include "Widgets.h"
#include "Segments.h"
#include "../AutoMix/AutoMixController.h"

class VelvoxAudioProcessor;

namespace velvox::ui
{
    // Main view of the automatic mix:
    //  A  Vocal status    analysis, material, edited modules, live input level, beat sidechain, output
    //  B  Automatic mix   the processing that was actually set up, with reasons (click = open module)
    //  C  compare         previous state / mix (real parameter switch) and the level-matched original
    //  D  fine-tuning     density and the fair compare (the other macros: VOCAL CONTROL)
    //  E  re-analyse      always available
    class AutoMixView final : public juce::Component, private juce::ChangeListener
    {
    public:
        explicit AutoMixView (VelvoxAudioProcessor& processor);
        ~AutoMixView() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        // Live read-outs (input level, sidechain); called by the editor ~10 times a second
        void refresh();

        std::function<void()> onAnalyse;                    // "Audio analysieren" / "Neu analysieren"
        std::function<void (int moduleIdOrOutput)> onOpenModule;

        // For tests
        juce::TextButton& getAnalyseButton() noexcept { return analyse; }
        juce::TextButton& getPreviousButton() noexcept { return previous; }
        juce::TextButton& getMixButton() noexcept { return mix; }
        juce::TextButton& getOriginalButton() noexcept { return original; }
        juce::TextButton& getRestoreButton() noexcept { return restore; }
        int getNumStepRows() const noexcept;

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void rebuild();
        void updateButtons();

        class StepList;

        VelvoxAudioProcessor& proc;
        automix::AutoMixController& autoMix;

        juce::TextButton analyse, previous { "Vorher" }, mix { "Headliner Mix" }, original { "Original" }, restore;
        std::unique_ptr<StepList> steps;
        juce::Viewport stepView;
        std::unique_ptr<Knob> density;          // the five other macros live in VOCAL CONTROL
        std::unique_ptr<Segments> levelMatch;

        juce::Rectangle<int> statusArea, stepsArea, macroArea, headerArea;
        juce::String inputStatus, sidechainStatus;
        juce::Colour inputColour;
    };
}
