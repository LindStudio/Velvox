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
        // Header tab: capitals, active = 2 px underline in the accent colour
        class TabButton final : public juce::Button
        {
        public:
            TabButton (const juce::String& text, juce::Colour accentColour) : juce::Button (text), accent (accentColour) {}

            void paintButton (juce::Graphics& g, bool highlighted, bool) override
            {
                const auto r = getLocalBounds().toFloat();
                const bool on = getToggleState();
                const auto f = semiBold (10.5f, 0.12f);
                g.setColour (on ? colours::textStrong : (highlighted ? colours::textSoft : colours::textMuted));
                g.setFont (f);
                g.drawText (getButtonText(), r, juce::Justification::centred, false);
                if (on)
                {
                    const float w = std::min (r.getWidth() - 10.0f, (float) juce::GlyphArrangement::getStringWidthInt (f, getButtonText()) + 8.0f);
                    const auto line = juce::Rectangle<float> (w, 2.0f).withCentre ({ r.getCentreX(), r.getBottom() - 4.0f });
                    g.setColour (accent.withAlpha (0.35f));
                    g.fillRoundedRectangle (line.expanded (1.5f), 2.0f);
                    g.setColour (accent);
                    g.fillRoundedRectangle (line, 1.0f);
                }
            }

        private:
            juce::Colour accent;
        };

        // SIMPEL | ADVANCED: one half of a pill switch
        class ModeButton final : public juce::Button
        {
        public:
            ModeButton (const juce::String& text, bool isLeft) : juce::Button (text), left (isLeft) {}

            void paintButton (juce::Graphics& g, bool highlighted, bool) override
            {
                auto r = getLocalBounds().toFloat().reduced (left ? 2.0f : 0.0f, 2.0f).withTrimmedRight (left ? 0.0f : 2.0f);
                const bool on = getToggleState();
                if (on)
                {
                    g.setColour (colours::cyan.withAlpha (0.16f));
                    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
                    g.setColour (colours::cyan.withAlpha (0.7f));
                    g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, 1.0f);
                }
                g.setColour (on ? colours::textStrong : (highlighted ? colours::textSoft : colours::textMuted));
                g.setFont (semiBold (10.0f, 0.14f));
                g.drawText (getButtonText(), r, juce::Justification::centred, false);
            }

        private:
            bool left;
        };

        juce::String de (const char* utf8) { return juce::String::fromUTF8 (utf8); }
    }

    TopBar::TopBar (VelvoxAudioProcessor& p, DialogOverlay& d)
        : proc (p), dialog (d), bypass (p.getState(), pid::bypass, "Bypass")
    {
        modeButtons.add (new ModeButton ("SIMPEL", true));
        modeButtons.add (new ModeButton ("ADVANCED", false));
        modeButtons[0]->setTooltip (de ("Simpel: Spektrum, „Vocal analysieren“ und sechs Makros"));
        modeButtons[1]->setTooltip (de ("Advanced: die Modul-Kette mit dem Panel des gewählten Moduls"));
        for (int i = 0; i < 2; ++i)
        {
            const auto m = i == 0 ? Mode::simple : Mode::advanced;
            modeButtons[i]->onClick = [this, m] { selectMode (m); };
            addAndMakeVisible (modeButtons[i]);
        }

        tabs.add (new TabButton ("AUTO MIX", colours::blue));
        tabs.add (new TabButton ("VOCAL CHECK", colours::violet));
        tabs[0]->setTooltip (de ("Der Mix: im Modus Simpel die Makros, in Advanced die Kette"));
        tabs[1]->setTooltip (de ("Vocal Check: Diagnose der Aufnahme mit Vorschlägen"));
        for (int i = 0; i < 2; ++i)
        {
            const auto t = i == 0 ? Tab::autoMix : Tab::vocalCheck;
            tabs[i]->onClick = [this, t] { selectTab (t); };
            addAndMakeVisible (tabs[i]);
        }

        for (auto* c : std::initializer_list<juce::Component*> { &presetButton, &prev, &next, &bypass, &help })
            addAndMakeVisible (c);

        // ‹ name ▾ › as one connected group
        prev.setConnectedEdges (juce::Button::ConnectedOnRight);
        presetButton.setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
        next.setConnectedEdges (juce::Button::ConnectedOnLeft);
        presetButton.setColour (juce::TextButton::textColourOffId, colours::text);
        presetButton.setColour (juce::TextButton::buttonColourId, colours::accent.withAlpha (0.025f));

        presetButton.setTooltip ("Presets");
        presetButton.onClick = [this] { if (onPresetList) onPresetList(); else showPresetMenu(); };
        prev.setTooltip ("Vorheriges Preset");
        next.setTooltip (de ("Nächstes Preset"));
        prev.onClick = [this] { proc.getPresetManager().loadPrevious(); };
        next.onClick = [this] { proc.getPresetManager().loadNext(); };

        bypass.setTooltip (de ("Unbearbeitete Vocal hören (latenzkompensiert)"));
        bypass.getProperties().set ("danger", true);   // drawn in the bypass colour while on

        help.setTooltip ("Hilfe");
        help.onClick = [this] { if (onHelp) onHelp(); };
        setMode (mode);
        setTab (tab);
        refresh();
    }

    TopBar::~TopBar() = default;

    void TopBar::setMode (Mode m)
    {
        mode = m;
        modeButtons[0]->setToggleState (m == Mode::simple, juce::dontSendNotification);
        modeButtons[1]->setToggleState (m == Mode::advanced, juce::dontSendNotification);
        // The first tab names what it shows: the macros (AUTO MIX) in Simple, the module chain in Advanced
        tabs[0]->setButtonText (m == Mode::simple ? "AUTO MIX" : "MODULE");
        tabs[0]->setTooltip (m == Mode::simple ? de ("Auto Mix: Spektrum, „Vocal analysieren“ und die Makros")
                                               : de ("Module: die Kette mit dem Panel des gewählten Moduls (Auto Mix: Modus Simpel)"));
    }

    void TopBar::selectMode (Mode m)
    {
        setMode (m);
        if (onModeChange)
            onModeChange (m);
    }

    void TopBar::setTab (Tab t)
    {
        tab = t;
        tabs[0]->setToggleState (t == Tab::autoMix, juce::dontSendNotification);
        tabs[1]->setToggleState (t == Tab::vocalCheck, juce::dontSendNotification);
    }

    void TopBar::selectTab (Tab t)
    {
        setTab (t);
        if (onTabChange)
            onTabChange (t);
    }

    void TopBar::showPresetMenu()
    {
        // Fallback without the preset list (never used inside the editor)
        auto& pm = proc.getPresetManager();
        const auto& entries = pm.getEntries();
        const int current = pm.getCurrentIndex();
        juce::PopupMenu menu;
        for (int i = 0; i < entries.size(); ++i)
            menu.addItem (i + 1, entries[i].name, true, i == current);
        juce::Component::SafePointer<TopBar> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton).withMinimumWidth (presetButton.getWidth()),
                            [safe] (int result) { if (safe != nullptr && result > 0) safe->proc.getPresetManager().loadPreset (result - 1); });
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
            presetButton.setButtonText (name.isEmpty() ? juce::String ("Kein Preset") : name);
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
                    presetButton.setButtonText (modified ? shownPreset + " *" : shownPreset);
                presetButton.setTooltip (modified ? de ("Presets - das aktuelle wurde verändert") : juce::String ("Presets"));
            }
        }
    }

    void TopBar::requestSave()
    {
        auto& pm = proc.getPresetManager();
        const int idx = pm.getCurrentIndex();
        const auto suggestion = idx >= 0 && ! pm.getEntries()[idx].isFactory ? pm.getCurrentPresetName() : juce::String();

        juce::Component::SafePointer<TopBar> safe (this);
        dialog.show ("Preset speichern", de ("Gespeichert in ") + pm.getUserPresetDirectory().getFullPathName(),
                     true, suggestion, "Speichern",
                     [safe] (const juce::String& presetName)
                     {
                         if (safe == nullptr)
                             return;
                         if (! safe->proc.getPresetManager().saveUserPreset (presetName))
                             safe->dialog.show ("Preset nicht gespeichert",
                                                de ("Gib einen Namen ein und prüfe, ob der Preset-Ordner beschreibbar ist."),
                                                false, {}, "OK", nullptr);
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

        // Logo: icon + HEADLINER (bold)
        const auto big = display (15.0f, true).withExtraKerningFactor (0.06f);
        const auto area = logoArea.toFloat();
        const float iconW = 22.0f;
        const auto icon = juce::Rectangle<float> (iconW, iconW).withPosition (area.getX(), area.getCentreY() - iconW * 0.5f);
        drawGlow (g, icon.getCentre(), 20.0f, 0.22f, colours::blue);
        drawBrand (g, Brand::icon, icon);
        g.setColour (colours::textStrong);
        g.setFont (big);
        g.drawText ("HEADLINER", area.withTrimmedLeft (iconW + 8.0f), juce::Justification::centredLeft, false);

        // Pill behind SIMPEL | ADVANCED
        const auto pill = getModeArea().toFloat();
        g.setColour (colours::control);
        g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
        g.setColour (colours::border);
        g.drawRoundedRectangle (pill.reduced (0.5f), pill.getHeight() * 0.5f, 1.0f);
    }

    void TopBar::paintOverChildren (juce::Graphics& g)
    {
        // ▾ on the preset field
        const auto b = presetButton.getBounds().toFloat();
        const float ax = b.getRight() - 14.0f, ay = b.getCentreY();
        juce::Path arrow;
        arrow.startNewSubPath (ax - 4.0f, ay - 2.0f);
        arrow.lineTo (ax, ay + 2.0f);
        arrow.lineTo (ax + 4.0f, ay - 2.0f);
        g.setColour (colours::textDim);
        g.strokePath (arrow, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void TopBar::resized()
    {
        auto r = getLocalBounds().reduced (12, 0);
        const int h = 28;
        const auto centred = [&] (juce::Rectangle<int> a, int height) { return a.withSizeKeepingCentre (a.getWidth(), height); };

        logoArea = r.removeFromLeft (132);
        r.removeFromLeft (8);

        // ‹ name ▾ ›
        auto preset = centred (r.removeFromLeft (236), h);
        prev.setBounds (preset.removeFromLeft (26));
        next.setBounds (preset.removeFromRight (26));
        presetButton.setBounds (preset);
        r.removeFromLeft (14);

        // SIMPEL | ADVANCED
        auto modes = centred (r.removeFromLeft (164), 26);
        modeButtons[0]->setBounds (modes.removeFromLeft (70));
        modeButtons[1]->setBounds (modes);
        r.removeFromLeft (14);

        // Tabs
        auto tabRow = r.removeFromLeft (196);
        tabs[0]->setBounds (tabRow.removeFromLeft (84));
        tabs[1]->setBounds (tabRow);

        // Right: bypass, help
        help.setBounds (centred (r.removeFromRight (28), 28));
        r.removeFromRight (8);
        bypass.setBounds (centred (r.removeFromRight (84), h));
    }

    juce::Rectangle<int> TopBar::getPresetArea() const
    {
        return prev.getBounds().getUnion (next.getBounds());
    }

    juce::Rectangle<int> TopBar::getModeArea() const
    {
        return modeButtons[0]->getBounds().getUnion (modeButtons[1]->getBounds());
    }

    juce::Rectangle<int> TopBar::getTabArea() const
    {
        return tabs[0]->getBounds().getUnion (tabs[1]->getBounds());
    }
}
