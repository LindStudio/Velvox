#pragma once

#include "Widgets.h"
#include "../DSP/Instances.h"

class VelvoxAudioProcessor;

namespace velvox::ui
{
    // Module browser of 0.6 (spec 2.7): a popup over the editor with a search field and a grid of module
    // cards grouped by category - Clean-up, Dynamics, Colour, Width / Voices, Space, Rhythm, Analysis.
    // Each card shows the module's icon and name; hovering shows its one-line description at the bottom.
    // A module whose instances are all in use is greyed out with the reason and cannot be chosen.
    // Keys: type to search, Up / Down (and Left / Right while the search is empty) move the highlight,
    // Enter adds the highlighted module, Esc closes.
    class ModuleBrowser final : public juce::Component, private juce::TextEditor::Listener
    {
    public:
        enum class Category { cleanUp = 0, dynamics, colour, widthVoices, space, rhythm, analysis };
        static constexpr int numCategories = 7;
        static Category categoryOf (dsp::ModuleId id) noexcept;
        static juce::String categoryName (Category c);

        explicit ModuleBrowser (VelvoxAudioProcessor& p);
        ~ModuleBrowser() override;

        // Opens for an insert position (serial index) - `atEnd` when added behind the chain
        void open (int insertIndex, bool atEnd);
        void close();
        bool isOpen() const noexcept { return isVisible(); }

        // Called with the chosen module; the editor inserts it and opens its controls
        std::function<void (dsp::ModuleId id, int insertIndex, bool atEnd)> onChoose;

        // For tests: modules that the search shows, in display order (grouped by category)
        std::vector<dsp::ModuleId> visibleModules() const;
        void setSearchText (const juce::String& text);
        juce::Rectangle<int> cardBoundsFor (dsp::ModuleId id) const;
        juce::Rectangle<int> groupBoundsFor (Category c) const;   // caption + cards of a category
        std::optional<dsp::ModuleId> getHighlighted() const;
        void moveHighlight (int steps);
        juce::String reasonUnavailable (dsp::ModuleId id) const;    // empty = can be added
        juce::String getInfoText() const { return infoText; }       // description line (hover / highlight)
        bool chooseHighlighted();

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;

    private:
        class SearchField;
        void textEditorTextChanged (juce::TextEditor&) override { relayout(); }
        void textEditorReturnKeyPressed (juce::TextEditor&) override { chooseHighlighted(); }
        void textEditorEscapeKeyPressed (juce::TextEditor&) override { close(); }
        void relayout();
        void choose (dsp::ModuleId id);
        void updateInfo();
        juce::Rectangle<int> panelBounds() const;
        int freeInstances (dsp::ModuleId id) const;

        VelvoxAudioProcessor& proc;
        std::unique_ptr<SearchField> search;
        int insertIndex = 0;
        bool atEnd = true;

        struct Card { dsp::ModuleId id; juce::Rectangle<int> bounds; };
        struct Group { Category category; juce::Rectangle<int> bounds; };
        std::vector<Card> cards;
        std::vector<Group> groups;
        int hoveredCard = -1, highlighted = -1;
        juce::String infoText;
    };
}
