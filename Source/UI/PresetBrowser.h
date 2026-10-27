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
    //   click   loads the preset (one undo step), the list stays open
    //   keys    Up / Down select, Enter loads the selection and closes, Esc or a click outside closes
    //   bottom  Speichern, Speichern als …, Umbenennen …, Löschen (user presets)
    class PresetBrowser final : public juce::Component, private juce::TextEditor::Listener
    {
    public:
        PresetBrowser (PresetManager& presets, DialogOverlay& dialog);
        ~PresetBrowser() override;

        void open();
        void close();
        bool isOpen() const noexcept { return isVisible(); }

        // Category keys of the list (in the interface language); factory categories are their English names
        static juce::String allCategory();
        static juce::String userCategory();
        static juce::String favouritesCategory();
        static juce::String categoryLabel (const juce::String& category);   // a category as shown
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
        bool loadEntry (int entryIndex);   // loads it (unless it is the loaded preset), the list stays open
        bool loadSelected();               // loads the selection and closes the list
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
        juce::TextButton saveButton, saveAsButton, renameButton, deleteButton;   // texts set in the constructor
        juce::String category = allCategory();
        std::vector<int> rows;
        int selected = -1, hovered = -1;
        float scroll = 0.0f;
        static constexpr int rowH = 28;
    };
}
