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
        input.setIndents (8, 6);
        input.onReturnKey = [this] { confirm(); };
        input.onEscapeKey = [this] { close(); };
        addChildComponent (input);
        addAndMakeVisible (confirmButton);
        addAndMakeVisible (cancelButton);
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
        return getLocalBounds().withSizeKeepingCentre (420, hasInput ? 200 : 170);
    }

    void DialogOverlay::paint (juce::Graphics& g)
    {
        g.fillAll (colours::background.withAlpha (0.72f));
        const auto card = cardBounds().toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (card, 10.0f);
        g.setColour (colours::line);
        g.drawRoundedRectangle (card.reduced (0.5f), 10.0f, 1.0f);

        auto r = cardBounds().reduced (22, 18);
        g.setColour (colours::text);
        g.setFont (font (18.0f, true));
        g.drawText (title, r.removeFromTop (26), juce::Justification::centredLeft, true);
        g.setColour (colours::textDim);
        g.setFont (font (14.0f));
        g.drawFittedText (message, r.removeFromTop (40), juce::Justification::topLeft, 2);
    }

    void DialogOverlay::resized()
    {
        auto r = cardBounds().reduced (22, 18);
        r.removeFromTop (26 + 40);
        if (hasInput)
            input.setBounds (r.removeFromTop (32));

        auto buttons = r.removeFromBottom (32);
        confirmButton.setBounds (buttons.removeFromRight (110));
        buttons.removeFromRight (10);
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
        : proc (p), dialog (d), bypass (p.getState(), pid::bypass, "Bypass", colours::warning)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &presetButton, &prev, &next, &save, &remove, &slotA, &slotB,
                                                                  &copy, &beginner, &pro, &bypass })
            addAndMakeVisible (c);

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
        auto r = getLocalBounds();
        g.setColour (colours::panel);
        g.fillRect (r);
        juce::ColourGradient line (colours::cyan, 0.0f, (float) r.getBottom(), colours::magenta, (float) r.getRight(), (float) r.getBottom(), false);
        line.addColour (0.5, colours::violet);
        g.setGradientFill (line);
        g.fillRect (r.removeFromBottom (2));

        auto brand = getLocalBounds().reduced (20, 0).removeFromLeft (230);
        g.setColour (colours::text);
        g.setFont (display (28.0f, true));
        g.drawText (product::name, brand.removeFromLeft (110), juce::Justification::centredLeft, false);
        g.setColour (colours::textDim);
        g.setFont (font (12.0f));
        g.drawText ("Vocal suite " + juce::String (product::versionString), brand, juce::Justification::centredLeft, false);
    }

    void TopBar::resized()
    {
        auto r = getLocalBounds().reduced (20, 13).withTrimmedBottom (2);
        r.removeFromLeft (240);

        auto right = r.removeFromRight (470);
        bypass.setBounds (right.removeFromRight (96));
        right.removeFromRight (16);
        pro.setBounds (right.removeFromRight (60));
        beginner.setBounds (right.removeFromRight (84));
        right.removeFromRight (16);
        copy.setBounds (right.removeFromRight (112));
        right.removeFromRight (6);
        slotB.setBounds (right.removeFromRight (34));
        right.removeFromRight (4);
        slotA.setBounds (right.removeFromRight (34));

        auto mid = r.withSizeKeepingCentre (std::min (r.getWidth() - 10, 440), r.getHeight());
        prev.setBounds (mid.removeFromLeft (30));
        mid.removeFromLeft (6);
        remove.setBounds (mid.removeFromRight (68));
        mid.removeFromRight (6);
        save.setBounds (mid.removeFromRight (58));
        mid.removeFromRight (6);
        next.setBounds (mid.removeFromRight (30));
        mid.removeFromRight (6);
        presetButton.setBounds (mid);
    }
}
