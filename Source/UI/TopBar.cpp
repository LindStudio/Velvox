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
        for (auto* c : std::initializer_list<juce::Component*> { &prev, &next, &save, &remove, &presetBox, &slotA, &slotB, &copy, &bypass })
            addAndMakeVisible (c);

        presetBox.setTextWhenNothingSelected ("No preset loaded");
        presetBox.setTooltip ("Factory and user presets");
        presetBox.onChange = [this]
        {
            const int id = presetBox.getSelectedId();
            if (id > 0 && proc.getPresetManager().getCurrentIndex() != id - 1)
                proc.getPresetManager().loadPreset (id - 1);
        };

        prev.setTooltip ("Previous preset");
        next.setTooltip ("Next preset");
        save.setTooltip ("Save the current settings as a user preset");
        remove.setTooltip ("Delete the selected user preset");
        prev.onClick = [this] { proc.getPresetManager().loadPrevious(); };
        next.onClick = [this] { proc.getPresetManager().loadNext(); };
        save.onClick = [this] { requestSave(); };
        remove.onClick = [this] { requestDelete(); };

        slotA.setClickingTogglesState (false);
        slotB.setClickingTogglesState (false);
        slotA.setTooltip ("Settings A - switch to compare two versions");
        slotB.setTooltip ("Settings B - switch to compare two versions");
        copy.setTooltip ("Copy the active settings to the other slot");
        slotA.onClick = [this] { proc.selectSlot (0); };
        slotB.onClick = [this] { proc.selectSlot (1); };
        copy.onClick = [this] { proc.copyActiveSlotToOther(); };

        bypass.setTooltip ("Hear the unprocessed signal (latency compensated, not level matched)");

        refresh();
    }

    void TopBar::rebuildPresetList()
    {
        auto& pm = proc.getPresetManager();
        presetBox.clear (juce::dontSendNotification);

        bool userHeading = false;
        presetBox.addSectionHeading ("Factory");
        const auto& entries = pm.getEntries();
        for (int i = 0; i < entries.size(); ++i)
        {
            if (! entries[i].isFactory && ! userHeading)
            {
                presetBox.addSeparator();
                presetBox.addSectionHeading ("User");
                userHeading = true;
            }
            presetBox.addItem (entries[i].name, i + 1);
        }
    }

    void TopBar::refresh()
    {
        auto& pm = proc.getPresetManager();

        juce::StringArray names;
        for (auto& e : pm.getEntries())
            names.add (e.name + (e.isFactory ? "" : "*"));

        if (names != shownNames)
        {
            shownNames = names;
            rebuildPresetList();
            shownCurrent = "\x01"; // force selection update
        }

        const auto current = pm.getCurrentPresetName();
        if (current != shownCurrent)
        {
            shownCurrent = current;
            const int idx = pm.getCurrentIndex();
            presetBox.setSelectedId (idx >= 0 ? idx + 1 : 0, juce::dontSendNotification);
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
                     [safe] (const juce::String& name)
                     {
                         if (safe == nullptr)
                             return;
                         if (! safe->proc.getPresetManager().saveUserPreset (name))
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
        g.setColour (colours::line);
        g.drawHorizontalLine (r.getBottom() - 1, 0.0f, (float) r.getWidth());

        auto brand = r.reduced (20, 0).removeFromLeft (220);
        g.setColour (colours::text);
        g.setFont (font (24.0f, true));
        g.drawText (product::name, brand.removeFromLeft (100), juce::Justification::centredLeft, false);
        g.setColour (colours::textDim);
        g.setFont (font (13.0f));
        g.drawText ("Vocal chain " + juce::String (product::versionString), brand, juce::Justification::centredLeft, false);
    }

    void TopBar::resized()
    {
        auto r = getLocalBounds().reduced (20, 14);
        r.removeFromLeft (240);

        auto right = r.removeFromRight (360);
        bypass.setBounds (right.removeFromRight (100));
        right.removeFromRight (16);
        copy.setBounds (right.removeFromRight (120));
        right.removeFromRight (8);
        slotB.setBounds (right.removeFromRight (36));
        right.removeFromRight (4);
        slotA.setBounds (right.removeFromRight (36));

        auto mid = r.withSizeKeepingCentre (std::min (r.getWidth(), 470), r.getHeight());
        prev.setBounds (mid.removeFromLeft (32));
        mid.removeFromLeft (6);
        remove.setBounds (mid.removeFromRight (72));
        mid.removeFromRight (6);
        save.setBounds (mid.removeFromRight (64));
        mid.removeFromRight (6);
        next.setBounds (mid.removeFromRight (32));
        mid.removeFromRight (6);
        presetBox.setBounds (mid);
    }

    //==========================================================================
    ChainStrip::ChainStrip (APVTS& state) : apvts (state)
    {
        nodes = {
            { "Input", nullptr, colours::io, {} },
            { "EQ", pid::eqOn, colours::eq, {} },
            { "Compressor", pid::compOn, colours::comp, {} },
            { "De-esser", pid::deessOn, colours::deess, {} },
            { "Saturation", pid::satOn, colours::sat, {} },
            { "Delay", pid::delayOn, colours::delay, {} },
            { "Reverb", pid::reverbOn, colours::reverb, {} },
            { "Output", nullptr, colours::io, {} },
        };
        lastState.assign (nodes.size(), false);
    }

    void ChainStrip::layoutNodes()
    {
        const auto r = getLocalBounds().toFloat().reduced (8.0f, 2.0f);
        const float h = 17.0f;
        // 7 columns: Input, EQ, Comp, De-ess, Sat, (Delay | Reverb), Output
        const float colW = r.getWidth() / 7.0f;
        const float w = std::min (colW - 44.0f, 104.0f);
        const float cy = r.getCentreY();

        for (size_t i = 0; i < nodes.size(); ++i)
        {
            int col = (int) i;
            float y = cy;
            if (i == 5) { col = 5; y = cy - 11.5f; }
            if (i == 6) { col = 5; y = cy + 11.5f; }
            if (i == 7) col = 6;
            nodes[i].bounds = juce::Rectangle<float> (w, h).withCentre ({ r.getX() + colW * ((float) col + 0.5f), y });
        }
    }

    void ChainStrip::refresh()
    {
        bool changed = false;
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            const bool on = nodes[i].paramId == nullptr || apvts.getRawParameterValue (nodes[i].paramId)->load() > 0.5f;
            if (on != lastState[i])
            {
                lastState[i] = on;
                changed = true;
            }
        }
        if (changed)
            repaint();
    }

    void ChainStrip::paint (juce::Graphics& g)
    {
        layoutNodes();
        const auto& in = nodes[0].bounds;
        const auto& sat = nodes[4].bounds;
        const auto& dly = nodes[5].bounds;
        const auto& rev = nodes[6].bounds;
        const auto& out = nodes[7].bounds;
        const float cy = in.getCentreY();

        // Spine: colour runs along the chain
        juce::ColourGradient grad (colours::eq, in.getRight(), cy, colours::reverb, out.getX(), cy, false);
        grad.addColour (0.33, colours::comp);
        grad.addColour (0.5, colours::deess);
        grad.addColour (0.66, colours::sat);
        grad.addColour (0.82, colours::delay);
        g.setGradientFill (grad);
        g.fillRect (juce::Rectangle<float> (in.getRight(), cy - 1.0f, out.getX() - in.getRight(), 2.0f));

        // Parallel branches for delay and reverb (the dry line continues straight through)
        const float splitX = sat.getRight() + (dly.getX() - sat.getRight()) * 0.5f;
        const float joinX = dly.getRight() + (out.getX() - dly.getRight()) * 0.5f;
        juce::Path branches;
        for (const auto* b : { &dly, &rev })
        {
            branches.startNewSubPath (splitX, cy);
            branches.lineTo (splitX, b->getCentreY());
            branches.lineTo (b->getX(), b->getCentreY());
            branches.startNewSubPath (b->getRight(), b->getCentreY());
            branches.lineTo (joinX, b->getCentreY());
            branches.lineTo (joinX, cy);
        }
        g.setColour (colours::line.brighter (0.3f));
        g.strokePath (branches, juce::PathStrokeType (1.0f));

        for (size_t i = 0; i < nodes.size(); ++i)
        {
            const auto& n = nodes[i];
            const bool on = lastState[i];
            const bool hover = (int) i == hovered && n.paramId != nullptr;

            g.setColour (on ? n.colour.withAlpha (0.2f).overlaidWith (colours::panel.withAlpha (0.5f)) : colours::panel);
            g.fillRoundedRectangle (n.bounds, n.bounds.getHeight() * 0.5f);
            g.setColour (on ? n.colour : (hover ? colours::textDim : colours::line.brighter (0.15f)));
            g.drawRoundedRectangle (n.bounds, n.bounds.getHeight() * 0.5f, 1.0f);
            g.setColour (on ? colours::text : colours::textDim);
            g.setFont (font (12.0f));
            g.drawText (n.label, n.bounds, juce::Justification::centred, false);
        }
    }

    int ChainStrip::nodeAt (juce::Point<float> p) const
    {
        for (size_t i = 0; i < nodes.size(); ++i)
            if (nodes[i].paramId != nullptr && nodes[i].bounds.expanded (2.0f).contains (p))
                return (int) i;
        return -1;
    }

    void ChainStrip::mouseDown (const juce::MouseEvent& e)
    {
        layoutNodes();
        const int i = nodeAt (e.position);
        if (i < 0)
            return;

        if (auto* p = apvts.getParameter (nodes[(size_t) i].paramId))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
            p->endChangeGesture();
        }
    }

    void ChainStrip::mouseMove (const juce::MouseEvent& e)
    {
        layoutNodes();
        const int i = nodeAt (e.position);
        if (i != hovered)
        {
            hovered = i;
            setMouseCursor (i >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            setTooltip (i >= 0 ? "Click to switch " + nodes[(size_t) i].label + " on or off" : juce::String());
            repaint();
        }
    }

    void ChainStrip::mouseExit (const juce::MouseEvent&)
    {
        hovered = -1;
        repaint();
    }
}
