#pragma once

#include "Widgets.h"
#include "../Presets/PresetManager.h"

namespace velvox::ui
{
    class DialogOverlay;

    // Preset list of 0.6 (spec 2.8), opened from the header's ‹ name ▾ ›:
    //   left    categories: Alle, the factory categories, Eigene (user presets), Favoriten
    //   top     search field
    //   list    one row per preset: star (favourite, kept in the user settings), name, category;
    //           hover / selection shows the preset's description at the bottom
    //   keys    Up / Down select, Enter or double-click loads (one undo step), Esc closes
    //   bottom  Speichern, Speichern als …, Umbenennen …, Löschen (user presets)
    class PresetBrowser final : public juce::Component, private juce::TextEditor::Listener
    {
    public:
        PresetBrowser (PresetManager& presets, DialogOverlay& dialog);
        ~PresetBrowser() override;

        void open();
        void close();
        bool isOpen() const noexcept { return isVisible(); }

        static juce::String allCategory()        { return "Alle"; }
        static juce::String userCategory()       { return "Eigene"; }
        static juce::String favouritesCategory() { return "Favoriten"; }
        juce::StringArray getCategories() const;
        void setCategory (const juce::String& c);
        juce::String getCategory() const noexcept { return category; }
        void setSearchText (const juce::String& text);

        // Rows: indices into PresetManager::getEntries(), in display order
        const std::vector<int>& visibleEntries() const noexcept { return rows; }
        juce::Rectangle<int> rowBounds (int entryIndex) const;
        juce::Rectangle<int> starBounds (int entryIndex) const;
        juce::Rectangle<int> categoryBounds (const juce::String& c) const;

        bool isFavourite (int entryIndex) const;
        void toggleFavourite (int entryIndex);

        void selectEntry (int entryIndex);
        int getSelectedEntry() const noexcept { return selected; }
        void moveSelection (int steps);
        bool loadSelected();
        juce::String getInfoText() const;

        // Saving (user presets only for rename / delete)
        bool saveCurrent();                         // overwrite the loaded user preset, else ask for a name
        bool saveAs (const juce::String& name);
        bool renameSelected (const juce::String& newName);
        bool deleteSelected();

        juce::TextButton& getSaveButton() noexcept { return saveButton; }
        juce::TextButton& getSaveAsButton() noexcept { return saveAsButton; }
        juce::TextButton& getRenameButton() noexcept { return renameButton; }
        juce::TextButton& getDeleteButton() noexcept { return deleteButton; }

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        bool keyPressed (const juce::KeyPress&) override;

    private:
        class SearchField;
        void textEditorTextChanged (juce::TextEditor&) override { relayout(); }
        void textEditorReturnKeyPressed (juce::TextEditor&) override { loadSelected(); }
        void textEditorEscapeKeyPressed (juce::TextEditor&) override { close(); }
        void relayout();
        void updateButtons();
        juce::Rectangle<int> panelBounds() const;
        juce::Rectangle<int> listArea() const;
        juce::String favouriteKey (int entryIndex) const;
        juce::StringArray favourites() const;
        int rowAt (juce::Point<int> p) const;

        PresetManager& pm;
        DialogOverlay& dialog;
        std::unique_ptr<SearchField> search;
        juce::TextButton saveButton { "Speichern" }, saveAsButton { juce::String::fromUTF8 ("Speichern als …") },
                         renameButton { juce::String::fromUTF8 ("Umbenennen …") }, deleteButton { juce::String::fromUTF8 ("Löschen") };
        juce::String category = allCategory();
        std::vector<int> rows;
        int selected = -1, hovered = -1;
        float scroll = 0.0f;
        static constexpr int rowH = 28;
    };
}
