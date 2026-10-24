#pragma once

#include "Widgets.h"

class VelvoxAudioProcessor;

namespace velvox::ui
{
    // In-editor modal card (no separate OS window - safer inside DAW hosts)
    class DialogOverlay final : public juce::Component
    {
    public:
        DialogOverlay();

        void show (const juce::String& title, const juce::String& message, bool withTextInput,
                   const juce::String& initialText, const juce::String& confirmLabel,
                   std::function<void (const juce::String&)> onConfirm);
        void close();

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;
        void mouseDown (const juce::MouseEvent&) override;

    private:
        juce::Rectangle<int> cardBounds() const;
        void confirm();

        juce::String title, message;
        bool hasInput = false;
        juce::TextEditor input;
        juce::TextButton confirmButton, cancelButton { "Cancel" };
        std::function<void (const juce::String&)> callback;
    };

    // Header of the HEADLINER VOCAL design (height 56): preset box with < > on the left,
    // the logo in the middle, on the right the tabs CHAIN | AUTO MIX | VOCAL CHECK
    // (active = underline in colour), A/B, copy, bypass and help.
    class TopBar final : public juce::Component
    {
    public:
        static constexpr int height = 56;
        enum class View { chain, autoMix, vocalCheck };

        TopBar (VelvoxAudioProcessor& processor, DialogOverlay& dialog);
        ~TopBar() override;

        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;
        void refresh();

        std::function<void (View)> onViewChange;
        void setView (View v);                 // shows the tab as active (no callback)
        void selectView (View v);              // as a click on the tab
        View getView() const noexcept { return view; }

        std::function<void()> onHelp;
        juce::TextButton& getHelpButton() noexcept { return help; }
        juce::Rectangle<int> getPresetArea() const;
        juce::Rectangle<int> getCompareArea() const;   // A / B and copy
        juce::Rectangle<int> getModeArea() const;      // the three tabs
        juce::Rectangle<int> getBypassArea() const { return bypass.getBounds(); }

    private:
        void showPresetMenu();
        void showPresetActions();
        void requestSave();
        void requestDelete();

        VelvoxAudioProcessor& proc;
        DialogOverlay& dialog;

        juce::TextButton presetButton, prev { juce::String::fromUTF8 ("\xe2\x80\xb9") }, next { juce::String::fromUTF8 ("\xe2\x80\xba") },
                         save, actions;   // icon buttons (drawn in paintOverChildren): save, menu
        juce::OwnedArray<juce::Button> tabs;   // CHAIN, AUTO MIX, VOCAL CHECK
        View view = View::autoMix;
        Toggle bypass;
        juce::TextButton help { "?" };
        juce::Rectangle<int> logoArea;
        bool shownModified = false;
        int modifiedCheck = 0;

        juce::String shownPreset;
    };
}
