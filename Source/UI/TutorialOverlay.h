#pragma once

#include "Widgets.h"

namespace velvox::ui
{
    // First-start tour. A welcome card offers to start the tutorial, go straight
    // to the plugin or be reminded later. The tour then highlights one area at a
    // time with a short explanation next to it. Outside the explanation card all
    // clicks reach the plugin, so you can try each step while reading it.
    class TutorialOverlay final : public juce::Component
    {
    public:
        enum class Outcome { finished, skipped, later };

        struct Step
        {
            juce::String target;   // area key, resolved by targetFor
            juce::String title, text;
        };

        TutorialOverlay();

        static const std::vector<Step>& steps();

        // Welcome card first (true) or straight into the first step (false)
        void start (bool withWelcome);
        void goTo (int stepIndex);   // -1 = welcome card
        void finish (Outcome outcome);
        int getStep() const noexcept { return step; }
        bool isActive() const noexcept { return isVisible(); }

        // Area of the editor canvas to highlight for a target key
        std::function<juce::Rectangle<int> (const juce::String& target)> targetFor;
        std::function<void (Outcome)> onClose;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool hitTest (int x, int y) override;
        bool keyPressed (const juce::KeyPress&) override;

    private:
        juce::Rectangle<int> cardBounds() const;
        juce::Rectangle<float> spotlight() const;
        void layoutButtons();
        void tick();

        int step = -1;
        juce::Rectangle<int> target;
        std::array<Smoothed, 4> spot {};   // animated spotlight x, y, w, h
        bool spotValid = false;

        juce::TextButton startButton { "Start tutorial" }, pluginButton { "Go to the plugin" }, laterButton { "Remind me later" };
        juce::TextButton backButton { "Back" }, nextButton { "Next" }, skipButton { "Skip" };
        juce::VBlankAttachment vblank { this, [this] { tick(); } };
    };
}
