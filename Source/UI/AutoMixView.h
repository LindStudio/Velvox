#pragma once

#include "Widgets.h"
#include "Segments.h"
#include "../AutoMix/AutoMixController.h"

class VocalypseAudioProcessor;

namespace vocalypse::ui
{
    // Details of the automatic mix (opened from Simple mode with "Details", shown in a card):
    //  A  Vocal status    analysis, material, edited modules, live input level, beat sidechain, output
    //  B  Automatic mix   the processing that was actually set up, with reasons (click = open module)
    //  D  fine-tuning     Density and the fair compare (the six other macros are the Simple-mode faders)
    //  E  re-analyse and restore the state before
    // Vorher / Nachher lives next to "Vocal analysieren" in Simple mode; bypass is in the header.
    class AutoMixView final : public juce::Component, private juce::ChangeListener
    {
    public:
        explicit AutoMixView (VocalypseAudioProcessor& processor);
        ~AutoMixView() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        // Live read-outs (input level, sidechain); called by the editor ~10 times a second
        void refresh();

        std::function<void()> onAnalyse;                    // "Audio analysieren" / "Neu analysieren"
        std::function<void (int moduleIdOrOutput)> onOpenModule;

        // For tests
        juce::TextButton& getAnalyseButton() noexcept { return analyse; }
        juce::TextButton& getRestoreButton() noexcept { return restore; }
        Knob& getDensityKnob() noexcept { return *density; }
        Segments& getLevelMatch() noexcept { return *levelMatch; }
        int getNumStepRows() const noexcept;
        // "label: value" of the vocal status (review 2026-10-28: each number says where it comes from)
        juce::StringArray getStatusLines() const;
        juce::Rectangle<int> getStepListBounds() const { return stepView.getBounds(); }

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void rebuild();
        void updateButtons();
        struct StatusRow { juce::String label, value; juce::Colour colour; };
        std::vector<StatusRow> statusRows() const;

        class StepList;

        VocalypseAudioProcessor& proc;
        automix::AutoMixController& autoMix;

        juce::TextButton analyse, restore;
        std::unique_ptr<StepList> steps;
        juce::Viewport stepView;
        std::unique_ptr<Knob> density;          // the six other macros are the Simple-mode faders
        std::unique_ptr<Segments> levelMatch;

        juce::Rectangle<int> statusArea, stepsArea, macroArea, headerArea;
        juce::String inputStatus, sidechainStatus;
        juce::Colour inputColour;
    };
}
