#pragma once

#include "Widgets.h"
#include "../Assistant/AssistantTypes.h"
#include "../Assistant/AssistantSession.h"

class VelvoxAudioProcessor;

namespace velvox::ui
{
    // The Assistant page: describe the sound in words, review the proposed
    // parameter changes, apply the selected ones, undo.
    //
    // UI only: interpretation (assistant::IntentParser), planning
    // (assistant::AssistantPlanner) and applying (assistant::AssistantSession)
    // live in their own components. Everything runs on the message thread and
    // locally - no network, nothing on the audio thread.
    class AssistantView final : public juce::Component
    {
    public:
        explicit AssistantView (VelvoxAudioProcessor& processor);
        ~AssistantView() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void refresh();   // ~10 Hz from the editor: sidechain status, button states

        // Status shown under the input (exactly one of these at a time)
        enum class Status { ready, analysing, proposalsReady, applied, noMatch, understood, question, conflict, stale, undone, failed };
        Status getStatus() const noexcept { return status; }

        // For tests and the tutorial / snapshot renderer
        void setText (const juce::String& text);
        juce::String getText() const { return input.getText(); }
        void analyseNow();                      // synchronous
        void requestAnalysis();                 // as the button does (deferred one message-loop turn)
        bool applySelected();
        bool undoLast();
        void resetPlan();
        const assistant::Plan& getPlan() const noexcept { return plan; }
        void setSelected (int changeIndex, bool shouldBeSelected);
        juce::TextButton& getAnalyseButton() noexcept { return analyseButton; }
        juce::TextButton& getApplyButton() noexcept { return applyButton; }
        juce::TextButton& getUndoButton() noexcept { return undoButton; }
        juce::TextButton& getResetButton() noexcept { return resetButton; }
        juce::ToggleButton& getAutoApplyButton() noexcept { return autoApply; }
        juce::String getStatusText() const;

        std::function<void (int moduleId)> onOpenModule;

    private:
        // Multi-line input: Enter analyses, Shift+Enter starts a new line
        class PromptEditor final : public juce::TextEditor
        {
        public:
            std::function<void()> onSubmit;
            bool keyPressed (const juce::KeyPress& key) override;
        };

        class SuggestionList;

        void setStatus (Status s, const juce::String& detail = {});
        void updateButtons();
        void rebuildList();

        VelvoxAudioProcessor& proc;
        assistant::AssistantSession session;
        assistant::Plan plan;
        juce::String planText;   // the text the plan was made from

        PromptEditor input;
        juce::TextButton analyseButton, applyButton, undoButton, resetButton;
        juce::ToggleButton autoApply;   // "Sofort anwenden": apply right after the analysis
        juce::OwnedArray<juce::TextButton> examples;
        juce::Viewport viewport;
        std::unique_ptr<SuggestionList> list;

        Status status = Status::ready;
        juce::String statusDetail;
        bool beatNotConnected = false;   // the last analysis wanted the beat, but no sidechain

        // Sidechain pill
        bool scConnected = false, scSignal = false;

        juce::Rectangle<int> headerArea, labelArea, statusArea, listHeaderArea, hintArea;
        bool planApplied = false;   // the shown plan was applied (apply is disabled until the next analysis)
        bool pendingAnalysis = false;
    };
}
