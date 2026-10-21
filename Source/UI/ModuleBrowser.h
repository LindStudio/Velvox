#pragma once

#include "Widgets.h"
#include "../DSP/Instances.h"

class VelvoxAudioProcessor;

namespace velvox::ui
{
    // Module browser: the way to add a module (or another instance of one) to the chain.
    // An overlay inside the editor canvas with category navigation, search and a card per
    // module (icon, name, purpose, how many instances are in use). Analysis tools are
    // drawn as outlined cards and say that they never change the sound.
    class ModuleBrowser final : public juce::Component, private juce::TextEditor::Listener
    {
    public:
        enum class Category { all = 0, cleanTone, dynamics, colourWidth, space, creative, analysis };
        static Category categoryOf (dsp::ModuleId id) noexcept;
        static const char* categoryName (Category c) noexcept;

        explicit ModuleBrowser (VelvoxAudioProcessor& p);

        // Opens for an insert position (serial index) - `atEnd` when added behind the chain
        void open (int insertIndex, bool atEnd);
        void close();
        bool isOpen() const noexcept { return isVisible(); }

        // Called with the chosen module; the editor inserts it and opens its controls
        std::function<void (dsp::ModuleId id, int insertIndex, bool atEnd)> onChoose;

        // For tests: modules that the current category and search show, in display order
        std::vector<dsp::ModuleId> visibleModules() const;
        void setCategory (Category c);
        void setSearchText (const juce::String& text);
        juce::Rectangle<int> cardBoundsFor (dsp::ModuleId id) const;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;

    private:
        void textEditorTextChanged (juce::TextEditor&) override { relayout(); }
        void textEditorReturnKeyPressed (juce::TextEditor&) override;
        void textEditorEscapeKeyPressed (juce::TextEditor&) override { close(); }
        void relayout();
        void choose (dsp::ModuleId id);
        juce::Rectangle<int> panelBounds() const;
        juce::Rectangle<int> chipBounds (int index) const;
        int freeInstances (dsp::ModuleId id) const;

        VelvoxAudioProcessor& proc;
        juce::TextEditor search;
        juce::TextButton closeButton { "Close" };
        Category category = Category::all;
        int insertIndex = 0;
        bool atEnd = true;

        struct Card { dsp::ModuleId id; juce::Rectangle<int> bounds; };
        std::vector<Card> cards;
        int hoveredCard = -1, hoveredChip = -1;
    };
}
