#include "TopBar.h"
#include "../PluginProcessor.h"
#include "../ProductInfo.h"

namespace velvox::ui
{
    //==========================================================================
    DialogOverlay::DialogOverlay()
    {
        setWantsKeyboardFocus (true);
        input.setFont (font (15.0f));
        input.setIndents (10, 7);
        input.onReturnKey = [this] { confirm(); };
        input.onEscapeKey = [this] { close(); };
        addChildComponent (input);
        addAndMakeVisible (confirmButton);
        addAndMakeVisible (cancelButton);
        confirmButton.setToggleState (true, juce::dontSendNotification); // primary action: lit
        confirmButton.onClick = [this] { confirm(); };
        cancelButton.onClick = [this] { close(); };
        setVisible (false);
    }

    void DialogOverlay::show (const juce::String& t, const juce::String& m, bool withTextInput,
                              const juce::String& initialText, const juce::String& confirmLabel,
                              std::function<void (const juce::String&)> onConfirm)
    {
        title = t;
        message = m;
        hasInput = withTextInput;
        callback = std::move (onConfirm);
        confirmButton.setButtonText (confirmLabel);
        input.setVisible (hasInput);
        input.setText (initialText, false);

        setVisible (true);
        toFront (true);
        resized();
        repaint();

        if (hasInput)
        {
            input.grabKeyboardFocus();
            input.selectAll();
        }
        else
        {
            grabKeyboardFocus();
        }
    }

    void DialogOverlay::close()
    {
        callback = nullptr;
        setVisible (false);
    }

    void DialogOverlay::confirm()
    {
        auto cb = std::move (callback);
        const auto text = input.getText();
        setVisible (false);
        if (cb)
            cb (text);
    }

    juce::Rectangle<int> DialogOverlay::cardBounds() const
    {
        return getLocalBounds().withSizeKeepingCentre (500, hasInput ? 236 : 194);
    }

    void DialogOverlay::paint (juce::Graphics& g)
    {
        g.fillAll (colours::background.withAlpha (0.78f));
        const auto card = cardBounds().toFloat();
        drawGlow (g, card.getCentre(), card.getWidth() * 0.8f, 0.05f);
        g.setColour (colours::panel);
        g.fillRoundedRectangle (card, 16.0f);
        drawGlass (g, card, 16.0f, 0.03f);

        auto r = cardBounds().reduced (26, 22);
        g.setColour (colours::text);
        g.setFont (font (20.0f, true));
        g.drawText (title, r.removeFromTop (28), juce::Justification::centredLeft, true);
        r.removeFromTop (6);
        g.setColour (colours::textDim);
        g.setFont (font (14.0f));
        g.drawFittedText (message, r.removeFromTop (40), juce::Justification::topLeft, 2);
    }

    void DialogOverlay::resized()
    {
        auto r = cardBounds().reduced (26, 22);
        r.removeFromTop (28 + 6 + 40);
        if (hasInput)
            input.setBounds (r.removeFromTop (38));

        auto buttons = r.removeFromBottom (36);
        confirmButton.setBounds (buttons.removeFromRight (112));
        buttons.removeFromRight (8);
        cancelButton.setBounds (buttons.removeFromRight (100));
    }

    bool DialogOverlay::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::escapeKey)
        {
            close();
            return true;
        }
        if (key == juce::KeyPress::returnKey)
        {
            confirm();
            return true;
        }
        return false;
    }

    void DialogOverlay::mouseDown (const juce::MouseEvent& e)
    {
        if (! cardBounds().contains (e.getPosition()))
            close();
    }


    //==========================================================================
    namespace
    {
        // Header tab of the design: capitals, active = 2 px underline in the accent colour
        class TabButton final : public juce::Button
        {
        public:
            TabButton (const juce::String& text, juce::Colour accentColour) : juce::Button (text), accent (accentColour) {}

            void paintButton (juce::Graphics& g, bool highlighted, bool) override
            {
                const auto r = getLocalBounds().toFloat();
                const bool on = getToggleState();
                g.setColour (on ? colours::textStrong : (highlighted ? colours::textSoft : colours::textMuted));
                g.setFont (semiBold (11.5f, 0.12f));
                g.drawText (getButtonText(), r, juce::Justification::centred, false);
                if (on)
                {
                    const float w = std::min (r.getWidth() - 12.0f, (float) juce::GlyphArrangement::getStringWidthInt (semiBold (11.5f, 0.12f), getButtonText()) + 8.0f);
                    const auto line = juce::Rectangle<float> (w, 2.0f).withCentre ({ r.getCentreX(), r.getBottom() - 6.0f });
                    g.setColour (accent.withAlpha (0.35f));
                    g.fillRoundedRectangle (line.expanded (1.5f), 2.0f);
                    g.setColour (accent);
                    g.fillRoundedRectangle (line, 1.0f);
                }
            }

        private:
            juce::Colour accent;
        };
    }

    TopBar::TopBar (VelvoxAudioProcessor& p, DialogOverlay& d)
        : proc (p), dialog (d), bypass (p.getState(), pid::bypass, "Bypass")
    {
        tabs.add (new TabButton ("CHAIN", colours::teal));
        tabs.add (new TabButton ("AUTO MIX", colours::blue));
        tabs.add (new TabButton ("VOCAL CHECK", colours::violet));
        const View tabViews[] { View::chain, View::autoMix, View::vocalCheck };
        for (int i = 0; i < tabs.size(); ++i)
        {
            const auto v = tabViews[i];
            tabs[i]->onClick = [this, v] { selectView (v); };
            addAndMakeVisible (tabs[i]);
        }
        tabs[0]->setTooltip (juce::String::fromUTF8 ("Die Modul-Kette mit allen Reglern des gewählten Moduls"));
        tabs[1]->setTooltip (juce::String::fromUTF8 ("Automatischer Mix: analysieren, Ergebnis, Vergleich"));
        tabs[2]->setTooltip (juce::String::fromUTF8 ("Vocal Check: Diagnose der Aufnahme mit Vorschlägen"));

        for (auto* c : std::initializer_list<juce::Component*> { &presetButton, &prev, &next, &save, &actions, &bypass, &help })
            addAndMakeVisible (c);

        // Segmented groups
        prev.setConnectedEdges (juce::Button::ConnectedOnRight);
        presetButton.setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
        next.setConnectedEdges (juce::Button::ConnectedOnLeft);

        presetButton.setColour (juce::TextButton::textColourOffId, colours::text);
        presetButton.setColour (juce::TextButton::buttonColourId, colours::accent.withAlpha (0.025f));

        presetButton.setTooltip ("Choose a preset");
        presetButton.onClick = [this] { showPresetMenu(); };
        prev.setTooltip ("Previous preset");
        next.setTooltip ("Next preset");
        save.setTooltip ("Save the current settings as a user preset");
        actions.setTooltip ("Preset menu: save as, delete, discard changes");
        prev.onClick = [this] { proc.getPresetManager().loadPrevious(); };
        next.onClick = [this] { proc.getPresetManager().loadNext(); };
        save.onClick = [this] { requestSave(); };
        actions.onClick = [this] { showPresetActions(); };

        bypass.setTooltip ("Hear the unprocessed vocal (latency compensated). For a fair comparison set Level match to Bypass match.");
        bypass.getProperties().set ("danger", true);   // drawn in the bypass colour while on

        help.setTooltip ("Help: how Headliner works and every module explained (offline)");
        help.onClick = [this] { if (onHelp) onHelp(); };
        setView (view);
        refresh();
    }

    TopBar::~TopBar() = default;

    void TopBar::setView (View v)
    {
        view = v;
        tabs[0]->setToggleState (v == View::chain, juce::dontSendNotification);
        tabs[1]->setToggleState (v == View::autoMix, juce::dontSendNotification);
        tabs[2]->setToggleState (v == View::vocalCheck, juce::dontSendNotification);
    }

    void TopBar::selectView (View v)
    {
        setView (v);
        if (onViewChange)
            onViewChange (v);
    }

    void TopBar::showPresetMenu()
    {
        auto& pm = proc.getPresetManager();
        const auto& entries = pm.getEntries();
        const int current = pm.getCurrentIndex();

        constexpr int saveItem = 100000, reloadItem = 100001;

        // Factory presets grouped by character (categories in the order they first appear) ...
        juce::StringArray categories;
        for (auto& e : entries)
            if (e.isFactory)
                categories.addIfNotAlreadyThere (e.category.isEmpty() ? juce::String ("Other") : e.category);

        juce::PopupMenu menu;
        for (auto& cat : categories)
        {
            menu.addSectionHeader ("Factory  -  " + cat);
            for (int i = 0; i < entries.size(); ++i)
            {
                const auto entryCat = entries[i].category.isEmpty() ? juce::String ("Other") : entries[i].category;
                if (entries[i].isFactory && entryCat == cat)
                    menu.addItem (i + 1, entries[i].name, true, i == current);
            }
        }

        // ... then the user's own presets
        menu.addSeparator();
        menu.addSectionHeader ("User presets");
        bool anyUser = false;
        for (int i = 0; i < entries.size(); ++i)
        {
            if (entries[i].isFactory)
                continue;
            anyUser = true;
            menu.addItem (i + 1, entries[i].name, true, i == current);
        }
        if (! anyUser)
            menu.addItem (-1, "None yet - use Save to store your settings", false, false);

        menu.addSeparator();
        if (current >= 0 && pm.isCurrentPresetModified())
            menu.addItem (reloadItem, "Discard changes (reload \"" + entries[current].name + "\")");
        menu.addItem (saveItem, "Save current settings as preset...");

        juce::Component::SafePointer<TopBar> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton).withMinimumWidth (presetButton.getWidth()),
                            [safe, current] (int result)
                            {
                                if (safe == nullptr || result <= 0)
                                    return;
                                if (result == saveItem)
                                    safe->requestSave();
                                else if (result == reloadItem)
                                    safe->proc.getPresetManager().loadPreset (current);
                                else
                                    safe->proc.getPresetManager().loadPreset (result - 1);
                            });
    }

    void TopBar::showPresetActions()
    {
        auto& pm = proc.getPresetManager();
        const int current = pm.getCurrentIndex();
        const bool isUser = current >= 0 && ! pm.getEntries()[current].isFactory;

        enum { saveAsItem = 1, deleteItem, reloadItem };
        juce::PopupMenu menu;
        menu.addItem (saveAsItem, "Save preset as...");
        menu.addItem (reloadItem, "Discard changes", current >= 0 && pm.isCurrentPresetModified());
        menu.addSeparator();
        menu.addItem (deleteItem, "Delete preset", isUser);

        juce::Component::SafePointer<TopBar> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&actions),
                            [safe, current] (int result)
                            {
                                if (safe == nullptr)
                                    return;
                                if (result == saveAsItem)
                                    safe->requestSave();
                                else if (result == deleteItem)
                                    safe->requestDelete();
                                else if (result == reloadItem)
                                    safe->proc.getPresetManager().loadPreset (current);
                            });
    }

    void TopBar::refresh()
    {
        auto& pm = proc.getPresetManager();
        const auto name = pm.getCurrentPresetName();
        if (name != shownPreset || presetButton.getButtonText().isEmpty())
        {
            shownPreset = name;
            shownModified = false;
            modifiedCheck = 6;   // re-check right away
            presetButton.setButtonText (name.isEmpty() ? juce::String ("No preset loaded") : name);
        }

        // "edited" marker: comparing every parameter is cheap, but 10 times a second is plenty
        if (++modifiedCheck >= 6)
        {
            modifiedCheck = 0;
            const bool modified = pm.isCurrentPresetModified();
            if (modified != shownModified)
            {
                shownModified = modified;
                if (shownPreset.isNotEmpty())
                    presetButton.setButtonText (modified ? shownPreset + "  (edited)" : shownPreset);
                presetButton.setTooltip (modified ? "Choose a preset - the current one was edited (save it to keep your version)"
                                                  : "Choose a preset");
            }
        }
    }

    void TopBar::requestSave()
    {
        auto& pm = proc.getPresetManager();
        const int idx = pm.getCurrentIndex();
        const auto suggestion = idx >= 0 && ! pm.getEntries()[idx].isFactory ? pm.getCurrentPresetName() : juce::String();

        juce::Component::SafePointer<TopBar> safe (this);
        dialog.show ("Save preset", "Saved to " + pm.getUserPresetDirectory().getFullPathName(),
                     true, suggestion, "Save",
                     [safe] (const juce::String& presetName)
                     {
                         if (safe == nullptr)
                             return;
                         if (! safe->proc.getPresetManager().saveUserPreset (presetName))
                             safe->dialog.show ("Preset not saved",
                                                "Enter a name, and check that the presets folder is writable.",
                                                false, {}, "OK", nullptr);
                         safe->refresh();
                     });
    }

    void TopBar::requestDelete()
    {
        auto& pm = proc.getPresetManager();
        const int idx = pm.getCurrentIndex();
        if (idx < 0 || pm.getEntries()[idx].isFactory)
            return;

        juce::Component::SafePointer<TopBar> safe (this);
        dialog.show ("Delete preset", "Delete \"" + pm.getEntries()[idx].name + "\"? This removes the file from disk.",
                     false, {}, "Delete",
                     [safe, idx] (const juce::String&)
                     {
                         if (safe == nullptr)
                             return;
                         safe->proc.getPresetManager().deleteUserPreset (idx);
                         safe->refresh();
                     });
    }

    void TopBar::paint (juce::Graphics& g)
    {
        const auto r = getLocalBounds().toFloat();

        // Hairline that fades out towards both edges
        juce::ColourGradient line (colours::accent.withAlpha (0.0f), r.getX(), r.getBottom(), colours::accent.withAlpha (0.0f), r.getRight(), r.getBottom(), false);
        line.addColour (0.5, colours::accent.withAlpha (0.10f));
        g.setGradientFill (line);
        g.fillRect (r.withTop (r.getBottom() - 1.0f));

        // Logo: icon, "HEADLINER" (bold 20) + "VOCAL" (light, #A78BFA), "BY LIND AUDIO" below
        const auto big = display (20.0f / 0.93f, true).withExtraKerningFactor (0.04f);
        const auto light = font (20.0f / 0.93f).withExtraKerningFactor (0.04f);
        const auto small = semiBold (8.0f, 0.35f);
        const float wHead = (float) juce::GlyphArrangement::getStringWidthInt (big, "HEADLINER");
        const float wVocal = (float) juce::GlyphArrangement::getStringWidthInt (light, " VOCAL");
        const float iconW = 26.0f, gapW = 10.0f;
        const auto area = logoArea.toFloat();
        float x = area.getCentreX() - (iconW + gapW + wHead + wVocal) * 0.5f;
        const auto icon = juce::Rectangle<float> (iconW, iconW).withPosition (x, area.getCentreY() - iconW * 0.5f);
        drawGlow (g, icon.getCentre(), 24.0f, 0.22f, colours::blue);
        drawBrand (g, Brand::icon, icon);
        x += iconW + gapW;
        const auto textTop = area.getCentreY() - 15.0f;
        g.setColour (colours::textStrong);
        g.setFont (big);
        g.drawText ("HEADLINER", juce::Rectangle<float> (wHead + 2.0f, 24.0f).withPosition (x, textTop), juce::Justification::centredLeft, false);
        g.setColour (juce::Colour (0xffa78bfa));
        g.setFont (light);
        g.drawText (" VOCAL", juce::Rectangle<float> (wVocal + 2.0f, 24.0f).withPosition (x + wHead, textTop), juce::Justification::centredLeft, false);
        g.setColour (colours::textFaint);
        g.setFont (small);
        g.drawText ("BY LIND AUDIO", juce::Rectangle<float> (wHead + wVocal, 10.0f).withPosition (x, textTop + 24.0f), juce::Justification::centred, false);
    }

    void TopBar::paintOverChildren (juce::Graphics& g)
    {
        // Chevron on the preset field
        const auto b = presetButton.getBounds().toFloat();
        const float ax = b.getRight() - 16.0f, ay = b.getCentreY();
        juce::Path arrow;
        arrow.startNewSubPath (ax - 4.0f, ay - 2.0f);
        arrow.lineTo (ax, ay + 2.0f);
        arrow.lineTo (ax + 4.0f, ay - 2.0f);
        g.setColour (colours::textDim);
        g.strokePath (arrow, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const auto iconColour = [] (const juce::Button& btn)
        {
            return btn.isEnabled() ? (btn.isMouseOver() ? colours::text : colours::textDim) : colours::textFaint;
        };
        const juce::PathStrokeType stroke (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        // Save: floppy disk
        {
            const auto s = save.getBounds().toFloat().withSizeKeepingCentre (16.0f, 16.0f);
            juce::Path p;
            p.startNewSubPath (s.getX() + 2.0f, s.getY());
            p.lineTo (s.getRight() - 3.5f, s.getY());
            p.lineTo (s.getRight(), s.getY() + 3.5f);
            p.lineTo (s.getRight(), s.getBottom() - 2.0f);
            p.quadraticTo (s.getRight(), s.getBottom(), s.getRight() - 2.0f, s.getBottom());
            p.lineTo (s.getX() + 2.0f, s.getBottom());
            p.quadraticTo (s.getX(), s.getBottom(), s.getX(), s.getBottom() - 2.0f);
            p.lineTo (s.getX(), s.getY() + 2.0f);
            p.quadraticTo (s.getX(), s.getY(), s.getX() + 2.0f, s.getY());
            p.closeSubPath();
            p.addRectangle (s.getX() + 4.0f, s.getY(), 7.0f, 4.5f);           // shutter
            p.addRectangle (s.getX() + 3.5f, s.getBottom() - 6.5f, 9.0f, 6.5f); // label
            g.setColour (iconColour (save));
            g.strokePath (p, stroke);
        }

        // Menu: three lines
        {
            const auto m = actions.getBounds().toFloat().withSizeKeepingCentre (16.0f, 12.0f);
            g.setColour (iconColour (actions));
            for (int i = 0; i < 3; ++i)
            {
                const float y = m.getY() + (float) i * 6.0f;
                g.drawLine (m.getX(), y, m.getRight(), y, 1.5f);
            }
        }
    }

    void TopBar::resized()
    {
        auto r = getLocalBounds().reduced (16, 0).withSizeKeepingCentre (getWidth() - 32, 36);

        // Left: preset box 290 x 36 (< name >), then the save and menu icons
        auto preset = r.removeFromLeft (290);
        prev.setBounds (preset.removeFromLeft (32));
        next.setBounds (preset.removeFromRight (32));
        presetButton.setBounds (preset);
        r.removeFromLeft (10);
        save.setBounds (r.removeFromLeft (36));
        r.removeFromLeft (4);
        actions.setBounds (r.removeFromLeft (36));
        r.removeFromLeft (12);

        // Right: tabs, A/B with copy, bypass, help
        auto right = r.removeFromRight (612);
        help.setBounds (right.removeFromRight (36));
        right.removeFromRight (8);
        bypass.setBounds (right.removeFromRight (92));
        right.removeFromRight (10);
        right.removeFromRight (18);
        const auto tabRow = right.withSizeKeepingCentre (right.getWidth(), getHeight());
        auto tabArea = tabRow;
        tabs[2]->setBounds (tabArea.removeFromRight (116));
        tabs[1]->setBounds (tabArea.removeFromRight (92));
        tabs[0]->setBounds (tabArea.removeFromRight (72));

        logoArea = r.withSizeKeepingCentre (r.getWidth(), getHeight());
    }
}

namespace velvox::ui
{
    juce::Rectangle<int> TopBar::getPresetArea() const
    {
        return prev.getBounds().getUnion (actions.getBounds());
    }

    juce::Rectangle<int> TopBar::getCompareArea() const
    {
        return bypass.getBounds();
    }

    juce::Rectangle<int> TopBar::getModeArea() const
    {
        return tabs[0]->getBounds().getUnion (tabs[2]->getBounds());
    }
}
