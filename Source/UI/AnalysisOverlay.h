#pragma once

#include "Widgets.h"
#include "../AutoMix/AutoMixController.h"

namespace velvox::ui
{
    // The automatic-mix popup: a glass card over the plugin.
    //
    //   intro      "Let's mix your vocal." - Audio analysieren / Später
    //   listening  waiting for signal (live input level, no-signal / too-quiet hints)
    //   capturing  progress of the bounded capture (clipping and stopped-transport hints)
    //   analysing  progress of analysis, planning and verification
    //   done       short confirmation, closes by itself
    //   failed     reason and retry
    //
    // It only reads the controller's status and calls its workflow methods;
    // everything keeps working (and the audio keeps running) when it is closed.
    class AnalysisOverlay final : public juce::Component
    {
    public:
        explicit AnalysisOverlay (automix::AutoMixController& controller);

        // Shows the intro card (first open of a new instance)
        void showIntro();
        // Shows the card for whatever the controller is doing right now
        void showProgress();
        void close();
        bool isOpen() const noexcept { return isVisible(); }

        // Called by the editor timer (about 30 Hz while visible)
        void update();

        std::function<void()> onFinished;   // a new mix was applied (the editor opens the overview)

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;

        enum class View { intro, listening, capturing, analysing, done, failed };
        View getView() const noexcept { return view; }

        // For tests
        juce::TextButton& getPrimaryButton() noexcept { return primary; }
        juce::TextButton& getSecondaryButton() noexcept { return secondary; }
        juce::String getStatusText() const { return status; }
        juce::String getWarningText() const { return warning; }

    private:
        juce::Rectangle<int> cardBounds() const;
        void setView (View v);
        void layoutButtons();

        automix::AutoMixController& controller;
        View view = View::intro;
        juce::String status, warning;
        float progress = 0.0f, level = -100.0f;
        double viewStartMs = 0.0, doneAtMs = 0.0;
        Smoothed shownProgress, fade;
        juce::TextButton primary, secondary;
    };
}
