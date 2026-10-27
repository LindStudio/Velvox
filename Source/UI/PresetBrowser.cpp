#include "PresetBrowser.h"
#include "TopBar.h"
#include "UserSettings.h"
#include "../Text/I18n.h"

namespace velvox::ui
{
    namespace
    {
        constexpr int categoryW = 150, catRowH = 26;

        void drawStar (juce::Graphics& g, juce::Rectangle<float> r, bool filled, juce::Colour c)
        {
            juce::Path star;
            star.addStar (r.getCentre(), 5, r.getWidth() * 0.22f, r.getWidth() * 0.5f, 0.0f);
            g.setColour (c);
            if (filled)
                g.fillPath (star);
            else
                g.strokePath (star, juce::PathStrokeType (1.1f));
        }
    }

    // Search field that passes Up / Down to the list
    class PresetBrowser::SearchField final : public juce::TextEditor
    {
    public:
        explicit SearchField (PresetBrowser& b) : owner (b) {}
        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key == juce::KeyPress::downKey) { owner.moveSelection (1); return true; }
            if (key == juce::KeyPress::upKey)   { owner.moveSelection (-1); return true; }
            return juce::TextEditor::keyPressed (key);
        }
    private:
        PresetBrowser& owner;
    };

    PresetBrowser::PresetBrowser (PresetManager& presets, DialogOverlay& d) : pm (presets), dialog (d)
    {
        setWantsKeyboardFocus (true);
        search = std::make_unique<SearchField> (*this);
        search->setTextToShowWhenEmpty (i18n::tr ("Presets suchen", "Search presets"), colours::textFaint);
        search->setFont (font (13.5f));
        search->setIndents (10, 0);
        search->setJustification (juce::Justification::centredLeft);
        search->addListener (this);
        addAndMakeVisible (*search);

        saveButton.setButtonText (i18n::tr ("Speichern", "Save"));
        saveAsButton.setButtonText (i18n::tr ("Speichern als …", "Save as …"));
        renameButton.setButtonText (i18n::tr ("Umbenennen …", "Rename …"));
        deleteButton.setButtonText (i18n::tr ("Löschen", "Delete"));
        saveButton.setTooltip (i18n::tr ("Das geladene eigene Preset überschreiben (sonst: neuer Name)",
                                         "Overwrite the loaded user preset (otherwise: a new name)"));
        saveButton.onClick = [this] { saveCurrent(); };
        saveAsButton.onClick = [this]
        {
            juce::Component::SafePointer<PresetBrowser> safe (this);
            dialog.show (i18n::tr ("Preset speichern", "Save preset"),
                         i18n::format (i18n::tr ("Gespeichert in {0}", "Saved in {0}"), { pm.getUserPresetDirectory().getFullPathName() }), true,
                         pm.getCurrentPresetName(), i18n::tr ("Speichern", "Save"),
                         [safe] (const juce::String& name) { if (safe != nullptr) safe->saveAs (name); });
        };
        renameButton.onClick = [this]
        {
            if (! juce::isPositiveAndBelow (selected, pm.getEntries().size()))
                return;
            juce::Component::SafePointer<PresetBrowser> safe (this);
            dialog.show (i18n::tr ("Preset umbenennen", "Rename preset"), {}, true, pm.getEntries()[selected].name, i18n::tr ("Umbenennen", "Rename"),
                         [safe] (const juce::String& name) { if (safe != nullptr) safe->renameSelected (name); });
        };
        deleteButton.onClick = [this]
        {
            if (! juce::isPositiveAndBelow (selected, pm.getEntries().size()))
                return;
            juce::Component::SafePointer<PresetBrowser> safe (this);
            dialog.show (i18n::tr ("Preset löschen", "Delete preset"),
                         i18n::format (i18n::tr ("„{0}“ löschen? Die Datei wird entfernt.", "Delete \"{0}\"? The file is removed."),
                                       { pm.getEntries()[selected].name }), false, {},
                         i18n::tr ("Löschen", "Delete"), [safe] (const juce::String&) { if (safe != nullptr) safe->deleteSelected(); });
        };
        for (auto* b : { &saveButton, &saveAsButton, &renameButton, &deleteButton })
            addAndMakeVisible (b);
        setVisible (false);
    }

    PresetBrowser::~PresetBrowser() = default;

    juce::String PresetBrowser::allCategory()        { return i18n::tr ("Alle", "All"); }
    juce::String PresetBrowser::userCategory()       { return i18n::tr ("Eigene", "User"); }
    juce::String PresetBrowser::favouritesCategory() { return i18n::tr ("Favoriten", "Favorites"); }

    juce::String PresetBrowser::categoryLabel (const juce::String& c)
    {
        // Factory categories are English data (the preset files); shown in the interface language
        static const std::pair<const char*, const char*> names[] {
            { "Utility", "Basis" }, { "Clean", "Klar" }, { "Punchy", "Druckvoll" }, { "Aggressive", "Aggressiv" },
            { "Wide", "Breit" }, { "Creative", "Kreativ" }, { "Classic (v0.1)", "Klassisch (v0.1)" }, { "Other", "Andere" },
        };
        if (i18n::isGerman())
            for (const auto& [english, german] : names)
                if (c == english)
                    return juce::String::fromUTF8 (german);
        return c;
    }

    void PresetBrowser::open()
    {
        pm.refresh();
        search->clear();
        selected = pm.getCurrentIndex();
        setVisible (true);
        toFront (true);
        relayout();
        search->grabKeyboardFocus();
    }

    void PresetBrowser::close()
    {
        setVisible (false);
    }

    juce::StringArray PresetBrowser::getCategories() const
    {
        juce::StringArray c { allCategory() };
        for (const auto& e : pm.getEntries())
            if (e.isFactory)
                c.addIfNotAlreadyThere (e.category.isEmpty() ? juce::String ("Other") : e.category);
        c.add (userCategory());
        c.add (favouritesCategory());
        return c;
    }

    void PresetBrowser::setCategory (const juce::String& c)
    {
        category = c;
        scroll = 0.0f;
        relayout();
    }

    void PresetBrowser::setSearchText (const juce::String& text)
    {
        search->setText (text, juce::dontSendNotification);
        relayout();
    }

    juce::String PresetBrowser::favouriteKey (int i) const
    {
        const auto& e = pm.getEntries()[i];
        return (e.isFactory ? "f:" : "u:") + e.name;
    }

    juce::StringArray PresetBrowser::favourites() const
    {
        return juce::StringArray::fromLines (settings::getString (settings::presetFavourites));
    }

    bool PresetBrowser::isFavourite (int i) const
    {
        return juce::isPositiveAndBelow (i, pm.getEntries().size()) && favourites().contains (favouriteKey (i));
    }

    void PresetBrowser::toggleFavourite (int i)
    {
        if (! juce::isPositiveAndBelow (i, pm.getEntries().size()))
            return;
        auto f = favourites();
        f.removeEmptyStrings();
        const auto key = favouriteKey (i);
        if (f.contains (key))
            f.removeString (key);
        else
            f.add (key);
        settings::set (settings::presetFavourites, f.joinIntoString ("\n"));
        relayout();
    }

    void PresetBrowser::relayout()
    {
        rows.clear();
        const auto& entries = pm.getEntries();
        const auto words = juce::StringArray::fromTokens (search->getText().toLowerCase(), " ", "");
        const auto favs = favourites();
        for (int i = 0; i < entries.size(); ++i)
        {
            const auto& e = entries.getReference (i);
            const auto cat = e.isFactory ? (e.category.isEmpty() ? juce::String ("Other") : e.category) : userCategory();
            bool show = category == allCategory() || category == cat
                        || (category == favouritesCategory() && favs.contains ((e.isFactory ? "f:" : "u:") + e.name));
            const auto haystack = (e.name + " " + cat + " " + categoryLabel (cat) + " " + e.description).toLowerCase();   // packed: both languages
            for (const auto& w : words)
                show = show && haystack.contains (w);
            if (show)
                rows.push_back (i);
        }
        const float maxScroll = std::max (0.0f, (float) rows.size() * rowH - (float) listArea().getHeight());
        scroll = juce::jlimit (0.0f, maxScroll, scroll);
        updateButtons();
        repaint();
    }

    void PresetBrowser::updateButtons()
    {
        const bool user = juce::isPositiveAndBelow (selected, pm.getEntries().size()) && ! pm.getEntries()[selected].isFactory;
        renameButton.setEnabled (user);
        deleteButton.setEnabled (user);
    }

    juce::Rectangle<int> PresetBrowser::panelBounds() const
    {
        return getLocalBounds().withSizeKeepingCentre (std::min (640, getWidth() - 40), std::min (470, getHeight() - 40));
    }

    juce::Rectangle<int> PresetBrowser::listArea() const
    {
        auto p = panelBounds().reduced (16);
        p.removeFromTop (44);          // title + search
        p.removeFromBottom (74);       // description + buttons
        p.removeFromLeft (categoryW + 12);
        return p;
    }

    juce::Rectangle<int> PresetBrowser::categoryBounds (const juce::String& c) const
    {
        auto p = panelBounds().reduced (16).withTrimmedTop (44);
        const int i = getCategories().indexOf (c);
        if (i < 0)
            return {};
        return { p.getX(), p.getY() + i * catRowH, categoryW, catRowH - 2 };
    }

    juce::Rectangle<int> PresetBrowser::rowBounds (int entryIndex) const
    {
        const auto it = std::find (rows.begin(), rows.end(), entryIndex);
        if (it == rows.end())
            return {};
        const auto list = listArea();
        return { list.getX(), list.getY() + (int) (it - rows.begin()) * rowH - juce::roundToInt (scroll), list.getWidth(), rowH };
    }

    juce::Rectangle<int> PresetBrowser::starBounds (int entryIndex) const
    {
        const auto r = rowBounds (entryIndex);
        return r.isEmpty() ? r : r.withWidth (28);
    }

    int PresetBrowser::rowAt (juce::Point<int> p) const
    {
        if (! listArea().contains (p))
            return -1;
        for (auto i : rows)
            if (rowBounds (i).contains (p))
                return i;
        return -1;
    }

    void PresetBrowser::selectEntry (int i)
    {
        selected = i;
        // Keep it in view
        const auto r = rowBounds (i), list = listArea();
        if (! r.isEmpty())
        {
            if (r.getY() < list.getY())
                scroll -= (float) (list.getY() - r.getY());
            else if (r.getBottom() > list.getBottom())
                scroll += (float) (r.getBottom() - list.getBottom());
        }
        updateButtons();
        repaint();
    }

    void PresetBrowser::moveSelection (int steps)
    {
        if (rows.empty())
            return;
        auto it = std::find (rows.begin(), rows.end(), selected);
        int pos = it == rows.end() ? (steps > 0 ? -1 : (int) rows.size()) : (int) (it - rows.begin());
        pos = juce::jlimit (0, (int) rows.size() - 1, pos + steps);
        selectEntry (rows[(size_t) pos]);
    }

    bool PresetBrowser::loadEntry (int i)
    {
        if (! juce::isPositiveAndBelow (i, pm.getEntries().size()))
            return false;
        // The loaded preset again: nothing to do (a click must not throw away the changes made to it)
        if (i == pm.getCurrentIndex())
            return true;
        return pm.loadPreset (i);
    }

    bool PresetBrowser::loadSelected()
    {
        const bool ok = loadEntry (selected);
        if (ok)
            close();
        return ok;
    }

    juce::String PresetBrowser::getInfoText() const
    {
        const int shown = hovered >= 0 ? hovered : selected;
        if (! juce::isPositiveAndBelow (shown, pm.getEntries().size()))
            return rows.empty() ? i18n::tr ("Kein Preset gefunden.", "No preset found.") : juce::String();
        const auto& e = pm.getEntries()[shown];
        return e.description.isNotEmpty() ? i18n::show (e.description) : (e.isFactory ? categoryLabel (e.category) : userCategory());
    }

    bool PresetBrowser::saveAs (const juce::String& name)
    {
        if (! pm.saveUserPreset (name))
            return false;
        selected = pm.getCurrentIndex();
        relayout();
        return true;
    }

    bool PresetBrowser::saveCurrent()
    {
        const int current = pm.getCurrentIndex();
        if (current >= 0 && ! pm.getEntries()[current].isFactory)
            return saveAs (pm.getEntries()[current].name);
        saveAsButton.onClick();   // a factory preset or none: ask for a name
        return false;
    }

    bool PresetBrowser::renameSelected (const juce::String& newName)
    {
        if (! juce::isPositiveAndBelow (selected, pm.getEntries().size()))
            return false;
        const bool fav = isFavourite (selected);
        const auto oldKey = favouriteKey (selected);
        if (! pm.renameUserPreset (selected, newName))
            return false;
        selected = -1;
        const auto clean = juce::File::createLegalFileName (newName.trim());
        for (int i = 0; i < pm.getEntries().size(); ++i)
            if (! pm.getEntries()[i].isFactory && pm.getEntries()[i].name == clean)
                selected = i;
        if (fav)
        {
            auto f = favourites();
            f.removeString (oldKey);
            f.add ("u:" + clean);
            settings::set (settings::presetFavourites, f.joinIntoString ("\n"));
        }
        relayout();
        return true;
    }

    bool PresetBrowser::deleteSelected()
    {
        if (! juce::isPositiveAndBelow (selected, pm.getEntries().size()) || pm.getEntries()[selected].isFactory)
            return false;
        if (! pm.deleteUserPreset (selected))
            return false;
        selected = -1;
        relayout();
        return true;
    }

    //==========================================================================
    void PresetBrowser::paint (juce::Graphics& g)
    {
        g.fillAll (colours::scrim);
        const auto panel = panelBounds().toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (panel, radius::panel);
        drawGlass (g, panel, radius::panel, 0.03f);

        auto p = panelBounds().reduced (16);
        g.setColour (colours::text);
        g.setFont (font (16.0f, true));
        g.drawText ("Presets", p.removeFromTop (30).withWidth (categoryW), juce::Justification::centredLeft, false);

        // Categories
        const auto favs = favourites();
        for (const auto& c : getCategories())
        {
            const auto r = categoryBounds (c).toFloat();
            const bool active = c == category;
            if (active)
            {
                g.setColour (colours::cyan.withAlpha (0.14f));
                g.fillRoundedRectangle (r, 6.0f);
            }
            g.setColour (active ? colours::textStrong : colours::textDim);
            g.setFont (medium (12.5f));
            g.drawText (categoryLabel (c), r.reduced (10.0f, 0.0f), juce::Justification::centredLeft, false);
        }

        // Rows
        const auto list = listArea();
        g.setColour (colours::panelInset);
        g.fillRoundedRectangle (list.toFloat(), 8.0f);
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (list);
            const auto& entries = pm.getEntries();
            const int current = pm.getCurrentIndex();
            for (auto i : rows)
            {
                const auto r = rowBounds (i);
                if (r.getBottom() < list.getY() || r.getY() > list.getBottom())
                    continue;
                const auto& e = entries.getReference (i);
                if (i == selected || i == hovered)
                {
                    g.setColour (colours::cyan.withAlpha (i == selected ? 0.14f : 0.06f));
                    g.fillRect (r);
                }
                drawStar (g, starBounds (i).toFloat().withSizeKeepingCentre (13.0f, 13.0f), favs.contains (favouriteKey (i)),
                          favs.contains (favouriteKey (i)) ? colours::bypass : colours::textFaint);
                auto text = r.withTrimmedLeft (30).withTrimmedRight (10);
                g.setColour (i == current ? colours::cyan : colours::text);
                g.setFont (font (13.0f, i == current));
                g.drawText (e.name, text, juce::Justification::centredLeft, true);
                g.setColour (colours::textFaint);
                g.setFont (font (11.0f));
                g.drawText (e.isFactory ? categoryLabel (e.category) : userCategory(), text, juce::Justification::centredRight, false);
            }
        }

        // Description of the hovered / selected preset
        g.setColour (colours::textDim);
        g.setFont (font (12.0f));
        g.drawFittedText (getInfoText(), panelBounds().reduced (16).removeFromBottom (74).removeFromTop (30), juce::Justification::centredLeft, 2, 0.9f);
    }

    void PresetBrowser::resized()
    {
        auto p = panelBounds().reduced (16);
        auto top = p.removeFromTop (30);
        top.removeFromLeft (categoryW + 12);
        search->setBounds (top);
        auto buttons = p.removeFromBottom (32);
        for (auto* b : { &deleteButton, &renameButton, &saveAsButton, &saveButton })
        {
            b->setBounds (buttons.removeFromRight (b == &saveAsButton || b == &renameButton ? 124 : 96));
            buttons.removeFromRight (8);
        }
        relayout();
    }

    void PresetBrowser::mouseMove (const juce::MouseEvent& e)
    {
        const int r = rowAt (e.getPosition());
        if (r != hovered)
        {
            hovered = r;
            repaint();
        }
    }

    void PresetBrowser::mouseExit (const juce::MouseEvent&)
    {
        hovered = -1;
        repaint();
    }

    void PresetBrowser::mouseDown (const juce::MouseEvent& e)
    {
        const auto pos = e.getPosition();
        if (! panelBounds().contains (pos))
        {
            close();
            return;
        }
        for (const auto& c : getCategories())
            if (categoryBounds (c).contains (pos))
            {
                setCategory (c);
                return;
            }
        const int r = rowAt (pos);
        if (r < 0)
            return;
        if (starBounds (r).contains (pos))
        {
            toggleFavourite (r);
            return;
        }
        // One click loads the preset; the list stays open to try the next one (Esc, Enter or a click outside close it)
        selectEntry (r);
        loadEntry (r);
        repaint();
    }

    void PresetBrowser::mouseDoubleClick (const juce::MouseEvent&)
    {
        // The first click has loaded the preset already; the list stays open
    }

    void PresetBrowser::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
    {
        // The list keeps the wheel for scrolling
        const float maxScroll = std::max (0.0f, (float) rows.size() * rowH - (float) listArea().getHeight());
        scroll = juce::jlimit (0.0f, maxScroll, scroll - w.deltaY * 200.0f);
        repaint();
    }

    bool PresetBrowser::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::escapeKey) { close(); return true; }
        if (key == juce::KeyPress::returnKey) { loadSelected(); return true; }
        if (key == juce::KeyPress::downKey)   { moveSelection (1); return true; }
        if (key == juce::KeyPress::upKey)     { moveSelection (-1); return true; }
        return false;
    }
}
