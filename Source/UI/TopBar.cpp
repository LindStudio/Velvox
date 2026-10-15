#include "TopBar.h"
#include "../PluginProcessor.h"
#include "../ProductInfo.h"

namespace velvox::ui
{
    //==========================================================================
    DialogOverlay::DialogOverlay()
    {
        setWantsKeyboardFocus (true);
        input.setFont (font (14.0f));
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
        return getLocalBounds().withSizeKeepingCentre (440, hasInput ? 214 : 176);
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
        g.setFont (font (17.0f, true));
        g.drawText (title, r.removeFromTop (26), juce::Justification::centredLeft, true);
        r.removeFromTop (4);
        g.setColour (colours::textDim);
        g.setFont (font (12.5f));
        g.drawFittedText (message, r.removeFromTop (36), juce::Justification::topLeft, 2);
    }

    void DialogOverlay::resized()
    {
        auto r = cardBounds().reduced (26, 22);
        r.removeFromTop (26 + 4 + 36);
        if (hasInput)
            input.setBounds (r.removeFromTop (34));

        auto buttons = r.removeFromBottom (32);
        confirmButton.setBounds (buttons.removeFromRight (104));
        buttons.removeFromRight (8);
        cancelButton.setBounds (buttons.removeFromRight (92));
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
                                                                  &copy, &beginner, &pro, &bypass })
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

        beginner.setTooltip ("Six simple controls and live tips");
        pro.setTooltip ("Every parameter of every module");
        beginner.onClick = [this] { setProMode (false); if (onModeChange) onModeChange (false); };
        pro.onClick = [this] { setProMode (true); if (onModeChange) onModeChange (true); };

        bypass.setTooltip ("Hear the unprocessed vocal (latency compensated). For a fair comparison set Level match to Bypass match.");
        refresh();
    }

    void TopBar::setProMode (bool isPro)
    {
        pro.setToggleState (isPro, juce::dontSendNotification);
        beginner.setToggleState (! isPro, juce::dontSendNotification);
    }

    void TopBar::showPresetMenu()
    {
        auto& pm = proc.getPresetManager();
        const auto& entries = pm.getEntries();
        const int current = pm.getCurrentIndex();

        // Keep categories in the order they first appear (factory first, user last)
        juce::StringArray categories;
        for (auto& e : entries)
            categories.addIfNotAlreadyThere (e.category.isEmpty() ? juce::String ("Other") : e.category);

        juce::PopupMenu menu;
        for (auto& cat : categories)
        {
            menu.addSectionHeader (cat);
            for (int i = 0; i < entries.size(); ++i)
            {
                const auto entryCat = entries[i].category.isEmpty() ? juce::String ("Other") : entries[i].category;
                if (entryCat == cat)
                    menu.addItem (i + 1, entries[i].name, true, i == current);
            }
        }

        juce::Component::SafePointer<TopBar> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton).withMinimumWidth (presetButton.getWidth()),
                            [safe] (int result)
                            {
                                if (safe != nullptr && result > 0)
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
            presetButton.setButtonText (name.isEmpty() ? juce::String ("No preset loaded") : name);
        }

        const int idx = pm.getCurrentIndex();
        remove.setEnabled (idx >= 0 && ! pm.getEntries()[idx].isFactory);

        if (proc.getActiveSlot() != shownSlot)
        {
            shownSlot = proc.getActiveSlot();
            slotA.setToggleState (shownSlot == 0, juce::dontSendNotification);
            slotB.setToggleState (shownSlot == 1, juce::dontSendNotification);
            copy.setButtonText (shownSlot == 0 ? "Copy A to B" : "Copy B to A");
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

        // Brand: a lit orb and the word mark
        auto brand = getLocalBounds().reduced (24, 0).removeFromLeft (220).toFloat();
        const auto orb = juce::Point<float> (brand.getX() + 8.0f, brand.getCentreY());
        drawGlow (g, orb, 18.0f, 0.35f);
        g.setColour (colours::accent);
        g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (orb));
        brand.removeFromLeft (26.0f);

        g.setColour (colours::text);
        g.setFont (font (19.0f, true).withExtraKerningFactor (0.02f));
        const auto nameText = juce::String (product::name).toLowerCase();
        g.drawText (nameText, brand.withY (brand.getCentreY() - 19.0f).withHeight (24.0f), juce::Justification::centredLeft, false);
        drawCaption (g, "Vocal suite  " + juce::String (product::versionString), brand.withY (brand.getCentreY() + 6.0f).withHeight (12.0f),
                     juce::Justification::centredLeft, colours::textFaint, 8.5f);
    }

    void TopBar::paintOverChildren (juce::Graphics& g)
    {
        // Chevron on the preset field
        const auto b = presetButton.getBounds().toFloat();
        const float ax = b.getRight() - 14.0f, ay = b.getCentreY();
        juce::Path arrow;
        arrow.startNewSubPath (ax - 3.5f, ay - 1.75f);
        arrow.lineTo (ax, ay + 1.75f);
        arrow.lineTo (ax + 3.5f, ay - 1.75f);
        g.setColour (colours::textDim);
        g.strokePath (arrow, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void TopBar::resized()
    {
        auto r = getLocalBounds().reduced (24, 15);
        r.removeFromLeft (220);

        auto right = r.removeFromRight (448);
        bypass.setBounds (right.removeFromRight (100));
        right.removeFromRight (14);
        pro.setBounds (right.removeFromRight (52));
        beginner.setBounds (right.removeFromRight (84));
        right.removeFromRight (14);
        copy.setBounds (right.removeFromRight (100));
        right.removeFromRight (6);
        slotB.setBounds (right.removeFromRight (34));
        slotA.setBounds (right.removeFromRight (34));

        auto mid = r.withSizeKeepingCentre (std::min (r.getWidth() - 20, 520), r.getHeight());
        remove.setBounds (mid.removeFromRight (62));
        mid.removeFromRight (6);
        save.setBounds (mid.removeFromRight (54));
        mid.removeFromRight (10);
        prev.setBounds (mid.removeFromLeft (34));
        next.setBounds (mid.removeFromRight (34));
        presetButton.setBounds (mid);
    }
}
