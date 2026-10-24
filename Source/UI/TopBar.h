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

    // Product name, preset browser (by category), A/B, Beginner/Pro and bypass
    class TopBar final : public juce::Component
    {
    public:
        TopBar (VelvoxAudioProcessor& processor, DialogOverlay& dialog);

        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;
        void refresh();

        std::function<void (bool pro)> onModeChange;
        std::function<void()> onAnalyse;       // "Analysieren": start an automatic mix (always available)
        juce::TextButton& getAnalyseButton() noexcept { return analyse; }
        void setHasMix (bool hasMix);
        std::function<void()> onHelp;
        juce::TextButton& getHelpButton() noexcept { return help; }
        juce::Rectangle<int> getPresetArea() const;
        juce::Rectangle<int> getCompareArea() const;   // A / B and copy
        juce::Rectangle<int> getModeArea() const;
        juce::Rectangle<int> getBypassArea() const { return bypass.getBounds(); }
        void setProMode (bool pro);

    private:
        void showPresetMenu();
        void showPresetActions();
        void requestSave();
        void requestDelete();

        VelvoxAudioProcessor& proc;
        DialogOverlay& dialog;

        juce::TextButton presetButton, prev { juce::String::fromUTF8 ("\xe2\x80\xb9") }, next { juce::String::fromUTF8 ("\xe2\x80\xba") },
                         save, actions;   // icon buttons (drawn in paintOverChildren): save, menu
        juce::TextButton slotA { "A" }, slotB { "B" }, copy { "Copy A to B" };
        int abCheckCounter = 0;
        juce::TextButton beginner { "Auto Mix" }, pro { "Module" }, analyse { "Analysieren" };
        bool proMode = false;
        Toggle bypass;
        juce::TextButton help { "Help" };
        bool shownModified = false;
        int modifiedCheck = 0;

        juce::String shownPreset;
        int shownSlot = -1;
    };
}
