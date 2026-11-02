#pragma once

#include "Widgets.h"
#include "../AutoMix/AutoMixController.h"

namespace velvox::ui
{
    // The automatic-mix popup: a glass card over the plugin.
    //
    //   intro      "Lass uns deine Vocal mixen." - genre and vibe (0.8), the beat sidechain explained with
    //              its live status - Auto Mix starten / Später (every start comes through here)
    //   listening  waiting for signal (live input level, no-signal / too-quiet hints)
    //   capturing  progress of the bounded capture (clipping and stopped-transport hints); 0.8: no early
    //              analysis - it starts by itself once the target amount of singing was heard
    //   analysing  progress of analysis, planning and verification
    //   done       confirmation, stays until clicked
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
        void mouseUp (const juce::MouseEvent&) override;

        enum class View { intro, listening, capturing, analysing, done, failed };
        View getView() const noexcept { return view; }

        // Live: is a beat playing on the sidechain (set by the editor; none = no beat)
        std::function<bool()> beatPresent;

        // For tests
        juce::TextButton& getPrimaryButton() noexcept { return primary; }
        juce::Button& getGenreChip (automix::Genre g) noexcept { return *genreChips[(size_t) g]; }
        juce::Button& getVibeChip (automix::Vibe v) noexcept { return *vibeChips[(size_t) v]; }
        juce::String getBeatStatusText() const { return beatStatus; }
        // The intro sentence and the sidechain help, in the interface language
        static juce::String introText();
        static juce::String beatHelpText();
        juce::TextButton& getSecondaryButton() noexcept { return secondary; }
        juce::String getStatusText() const { return status; }
        juce::String getWarningText() const { return warning; }

    private:
        juce::Rectangle<int> cardBounds() const;
        void setView (View v);
        void layoutButtons();
        void layoutChips();
        void syncChips();
        struct IntroLayout { juce::Rectangle<float> title, body, genreLabel, genreRow, vibeLabel, vibeRow, beatBox; };
        IntroLayout introLayout() const;

        automix::AutoMixController& controller;
        View view = View::intro;
        juce::String status, warning, beatStatus;
        std::vector<std::unique_ptr<juce::Button>> genreChips, vibeChips;
        float progress = 0.0f, level = -100.0f;
        double viewStartMs = 0.0;
        Smoothed shownProgress, fade;
        juce::TextButton primary, secondary;
    };
}
