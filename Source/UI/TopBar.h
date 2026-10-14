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

    // Product name, preset browser, A/B comparison and global bypass
    class TopBar final : public juce::Component
    {
    public:
        TopBar (VelvoxAudioProcessor& processor, DialogOverlay& dialog);

        void paint (juce::Graphics&) override;
        void resized() override;
        void refresh();

    private:
        void rebuildPresetList();
        void requestSave();
        void requestDelete();

        VelvoxAudioProcessor& proc;
        DialogOverlay& dialog;

        juce::TextButton prev { "<" }, next { ">" }, save { "Save" }, remove { "Delete" };
        juce::ComboBox presetBox;
        juce::TextButton slotA { "A" }, slotB { "B" }, copy { "Copy A to B" };
        Toggle bypass;

        juce::StringArray shownNames;
        juce::String shownCurrent;
        int shownSlot = -1;
    };

    // Overview of the signal path. Click a module to switch it on or off.
    class ChainStrip final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit ChainStrip (APVTS& state);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void refresh();

    private:
        struct Node
        {
            juce::String label;
            const char* paramId;
            juce::Colour colour;
            juce::Rectangle<float> bounds;
        };

        void layoutNodes();
        int nodeAt (juce::Point<float>) const;

        APVTS& apvts;
        std::vector<Node> nodes;
        std::vector<bool> lastState;
        int hovered = -1;
    };
}
