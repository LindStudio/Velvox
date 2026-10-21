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
    TopBar::TopBar (VelvoxAudioProcessor& p, DialogOverlay& d)
        : proc (p), dialog (d), bypass (p.getState(), pid::bypass, "Bypass")
    {
        for (auto* c : std::initializer_list<juce::Component*> { &presetButton, &prev, &next, &save, &remove, &slotA, &slotB,
                                                                  &copy, &beginner, &pro, &analyse, &bypass, &help })
            addAndMakeVisible (c);

        // Segmented groups
        prev.setConnectedEdges (juce::Button::ConnectedOnRight);
        presetButton.setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
        next.setConnectedEdges (juce::Button::ConnectedOnLeft);
        slotA.setConnectedEdges (juce::Button::ConnectedOnRight);
        slotB.setConnectedEdges (juce::Button::ConnectedOnLeft);
        beginner.setConnectedEdges (juce::Button::ConnectedOnRight);
        pro.setConnectedEdges (juce::Button::ConnectedOnLeft);

        presetButton.setColour (juce::TextButton::textColourOffId, colours::text);
        presetButton.setColour (juce::TextButton::buttonColourId, colours::accent.withAlpha (0.025f));

        presetButton.setTooltip ("Choose a preset");
        presetButton.onClick = [this] { showPresetMenu(); };
        prev.setTooltip ("Previous preset");
        next.setTooltip ("Next preset");
        save.setTooltip ("Save the current settings as a user preset");
        remove.setTooltip ("Delete the selected user preset");
        prev.onClick = [this] { proc.getPresetManager().loadPrevious(); };
        next.onClick = [this] { proc.getPresetManager().loadNext(); };
        save.onClick = [this] { requestSave(); };
        remove.onClick = [this] { requestDelete(); };

        slotA.setTooltip ("Settings A - switch to compare two versions");
        slotB.setTooltip ("Settings B - switch to compare two versions");
        copy.setTooltip ("Copy the active settings to the other slot");
        slotA.onClick = [this] { proc.selectSlot (0); };
        slotB.onClick = [this] { proc.selectSlot (1); };
        copy.onClick = [this] { proc.copyActiveSlotToOther(); };

        beginner.setTooltip (juce::String::fromUTF8 ("Übersicht des automatischen Mixes, Vergleich und Feinschliff"));
        pro.setTooltip (juce::String::fromUTF8 ("Alle Parameter des gewählten Moduls"));
        beginner.onClick = [this] { setProMode (false); if (onModeChange) onModeChange (false); };
        pro.onClick = [this] { setProMode (true); if (onModeChange) onModeChange (true); };
        analyse.getProperties().set ("primary", true);
        analyse.setTooltip (juce::String::fromUTF8 ("Vocal während der Wiedergabe analysieren und automatisch mischen. Der bisherige Zustand bleibt wiederherstellbar."));
        analyse.onClick = [this] { if (onAnalyse) onAnalyse(); };

        bypass.setTooltip ("Hear the unprocessed vocal (latency compensated). For a fair comparison set Level match to Bypass match.");
        bypass.getProperties().set ("danger", true);   // drawn in the bypass colour while on

        help.setTooltip ("Help: how Headliner works and every module explained (offline)");
        help.onClick = [this] { if (onHelp) onHelp(); };
        refresh();
    }

    void TopBar::setProMode (bool isPro)
    {
        proMode = isPro;
        pro.setToggleState (isPro, juce::dontSendNotification);
        beginner.setToggleState (! isPro, juce::dontSendNotification);
    }

    void TopBar::setHasMix (bool hasMix)
    {
        const juce::String text (hasMix ? "Neu analysieren" : "Analysieren");
        if (analyse.getButtonText() != text)
            analyse.setButtonText (text);
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

        const int idx = pm.getCurrentIndex();
        remove.setEnabled (idx >= 0 && ! pm.getEntries()[idx].isFactory);

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

        // A/B: the active slot is lit; the copy button also says whether A and B differ
        // (checked a few times per second - it compares every parameter and the chain)
        if (++abCheckCounter >= 12 || proc.getActiveSlot() != shownSlot)
        {
            abCheckCounter = 0;
            shownSlot = proc.getActiveSlot();
            slotA.setToggleState (shownSlot == 0, juce::dontSendNotification);
            slotB.setToggleState (shownSlot == 1, juce::dontSendNotification);
            const bool equal = proc.slotsAreEqual();
            const juce::String text = equal ? "A = B" : (shownSlot == 0 ? "Copy A to B" : "Copy B to A");
            if (copy.getButtonText() != text)
            {
                copy.setButtonText (text);
                copy.setTooltip (equal ? "A and B are identical. Change something to compare two versions - or click to copy anyway."
                                       : juce::String ("A and B differ. Click to copy the active slot (") + (shownSlot == 0 ? "A" : "B")
                                             + ") to the other one.");
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

        // Brand: HEADLINER icon and word mark (by Lind Audio)
        auto brand = getLocalBounds().reduced (20, 0).removeFromLeft (230).toFloat();
        const auto iconArea = brand.removeFromLeft (brand.getHeight() * 0.62f).withSizeKeepingCentre (brand.getHeight() * 0.62f, brand.getHeight() * 0.62f);
        drawGlow (g, iconArea.getCentre(), 26.0f, 0.22f, colours::blue);
        drawBrand (g, Brand::icon, iconArea);
        brand.removeFromLeft (10.0f);
        drawBrand (g, Brand::wordmark, brand.withSizeKeepingCentre (brand.getWidth(), std::min (brand.getHeight() * 0.62f, 34.0f)),
                   juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid);
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
    }

    void TopBar::resized()
    {
        auto r = getLocalBounds().reduced (24, 0).withSizeKeepingCentre (getWidth() - 48, 38);
        r.removeFromLeft (184);

        // Global switches on the right: A/B, view (Auto Mix | Module), analyse, bypass, help
        auto right = r.removeFromRight (742);
        help.setBounds (right.removeFromRight (64));
        right.removeFromRight (10);
        bypass.setBounds (right.removeFromRight (110));
        right.removeFromRight (12);
        analyse.setBounds (right.removeFromRight (148));
        right.removeFromRight (12);
        pro.setBounds (right.removeFromRight (84));
        beginner.setBounds (right.removeFromRight (96));
        right.removeFromRight (16);
        copy.setBounds (right.removeFromRight (112));
        right.removeFromRight (8);
        slotB.setBounds (right.removeFromRight (40));
        slotA.setBounds (right.removeFromRight (40));

        // Preset browser in the middle
        auto mid = r.withSizeKeepingCentre (std::min (r.getWidth() - 20, 580), r.getHeight());
        remove.setBounds (mid.removeFromRight (76));
        mid.removeFromRight (8);
        save.setBounds (mid.removeFromRight (68));
        mid.removeFromRight (12);
        prev.setBounds (mid.removeFromLeft (40));
        next.setBounds (mid.removeFromRight (40));
        presetButton.setBounds (mid);
    }
}

namespace velvox::ui
{
    juce::Rectangle<int> TopBar::getPresetArea() const
    {
        return prev.getBounds().getUnion (remove.getBounds());
    }

    juce::Rectangle<int> TopBar::getCompareArea() const
    {
        return slotA.getBounds().getUnion (copy.getBounds());
    }

    juce::Rectangle<int> TopBar::getModeArea() const
    {
        return beginner.getBounds().getUnion (pro.getBounds());
    }
}
