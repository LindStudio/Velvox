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

    // Header of Headliner 0.6 (height 44), left to right:
    //   logo · preset selector ‹ name ▾ › · SIMPEL | ADVANCED · tabs AUTO MIX | VOCAL CHECK · … · Bypass · ?
    // The mode decides how the AUTO MIX tab looks (Simple: spectrum + six macros; Advanced: chain +
    // module panel); VOCAL CHECK shows the check panel in both modes.
    class TopBar final : public juce::Component
    {
    public:
        static constexpr int height = 44;
        enum class Mode { simple, advanced };
        enum class Tab { autoMix, vocalCheck };

        TopBar (VelvoxAudioProcessor& processor, DialogOverlay& dialog);
        ~TopBar() override;

        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;
        void refresh();

        std::function<void (Mode)> onModeChange;
        std::function<void (Tab)> onTabChange;
        void setMode (Mode m);                 // shows the state (no callback)
        void selectMode (Mode m);              // as a click
        void setTab (Tab t);
        void selectTab (Tab t);
        Mode getMode() const noexcept { return mode; }
        Tab getTab() const noexcept { return tab; }

        // Clicking the preset name opens the preset list (the editor shows it); without a
        // handler a plain menu opens
        std::function<void()> onPresetList;
        std::function<void()> onHelp;

        juce::TextButton& getHelpButton() noexcept { return help; }
        juce::TextButton& getPresetButton() noexcept { return presetButton; }
        juce::TextButton& getPrevButton() noexcept { return prev; }
        juce::TextButton& getNextButton() noexcept { return next; }
        juce::Button& getModeButton (Mode m) noexcept { return m == Mode::simple ? *modeButtons[0] : *modeButtons[1]; }
        juce::Button& getTabButton (Tab t) noexcept { return t == Tab::autoMix ? *tabs[0] : *tabs[1]; }
        juce::Rectangle<int> getPresetArea() const;
        juce::Rectangle<int> getModeArea() const;      // SIMPEL | ADVANCED
        juce::Rectangle<int> getTabArea() const;       // AUTO MIX | VOCAL CHECK
        juce::Rectangle<int> getBypassArea() const { return bypass.getBounds(); }

        // Save the current settings as a user preset (asks for a name)
        void requestSave();

    private:
        void showPresetMenu();

        VelvoxAudioProcessor& proc;
        DialogOverlay& dialog;

        juce::TextButton presetButton, prev { juce::String::fromUTF8 ("\xe2\x80\xb9") }, next { juce::String::fromUTF8 ("\xe2\x80\xba") };
        juce::OwnedArray<juce::Button> modeButtons;   // SIMPEL, ADVANCED
        juce::OwnedArray<juce::Button> tabs;          // AUTO MIX, VOCAL CHECK
        Mode mode = Mode::simple;
        Tab tab = Tab::autoMix;
        Toggle bypass;
        juce::TextButton help { "?" };
        juce::Rectangle<int> logoArea;
        bool shownModified = false;
        int modifiedCheck = 0;

        juce::String shownPreset;
    };
}
