#include "SettingsOverlay.h"
#include "UserSettings.h"
#include "../ProductInfo.h"

namespace vocalypse::ui
{
    using i18n::tr;

    namespace
    {
        constexpr int sheetW = 372;
        constexpr int pad = space::xl;
        constexpr int headerH = 56;
        constexpr int segmentH = 30;

        void styleSegments (juce::OwnedArray<juce::TextButton>& group)
        {
            for (int i = 0; i < group.size(); ++i)
            {
                int edges = 0;
                if (i > 0) edges |= juce::Button::ConnectedOnLeft;
                if (i < group.size() - 1) edges |= juce::Button::ConnectedOnRight;
                group[i]->setConnectedEdges (edges);
            }
        }

        juce::String scaleText (double s) { return juce::String (juce::roundToInt (s * 100.0)) + " %"; }
    }

    SettingsOverlay::SettingsOverlay()
    {
        setVisible (false);
        setWantsKeyboardFocus (true);

        const auto system = i18n::systemLanguage() == i18n::Language::german ? juce::String ("Deutsch") : juce::String ("English");
        for (const auto& text : { tr ("Automatisch", "Automatic"), juce::String ("Deutsch"), juce::String ("English") })
            languageButtons.add (new juce::TextButton (text));
        languageButtons[0]->setTooltip (i18n::format (tr ("Die Sprache deines Computers ({0})", "Your computer's language ({0})"), { system }));
        for (int i = 0; i < languageButtons.size(); ++i)
            languageButtons[i]->onClick = [this, i]
            {
                setGroup (languageButtons, i);
                if (onLanguage)
                    onLanguage ((i18n::Preference) i);
            };

        for (const auto& text : { "SIMPEL", "ADVANCED" })
            modeButtons.add (new juce::TextButton (text));
        for (int i = 0; i < modeButtons.size(); ++i)
            modeButtons[i]->onClick = [this, i]
            {
                setGroup (modeButtons, i);
                settings::set (settings::defaultMode, i == 1 ? "advanced" : "simple");
            };

        for (auto s : windowScales)
            scaleButtons.add (new juce::TextButton (scaleText (s)));
        for (int i = 0; i < scaleButtons.size(); ++i)
            scaleButtons[i]->onClick = [this, i]
            {
                setGroup (scaleButtons, i);
                settings::set (settings::defaultScale, windowScales[(size_t) i]);
                if (onWindowScale)
                    onWindowScale (windowScales[(size_t) i]);
            };

        for (auto* g : { &languageButtons, &modeButtons, &scaleButtons })
        {
            styleSegments (*g);
            for (auto* b : *g)
                addAndMakeVisible (b);
        }

        offerAnalysis.onClick = [this] { settings::set (settings::offerAnalysis, offerAnalysis.getToggleState()); };
        infoCards.onClick = [this] { settings::set (settings::infoCards, infoCards.getToggleState()); };
        addAndMakeVisible (offerAnalysis);
        addAndMakeVisible (infoCards);

        tourButton.setButtonText (tr ("Tour starten", "Start tour"));
        tourButton.onClick = [this] { if (onStartTutorial) onStartTutorial(); };
        addAndMakeVisible (tourButton);

        licenceButton.onClick = [this]
        {
            if (licence == nullptr)
                return;
            if (licence->status().access == licensing::Access::licensed)
            {
                licence->deactivate ([safe = juce::Component::SafePointer<SettingsOverlay> (this)] (bool, const juce::String& message)
                {
                    if (message.isNotEmpty())
                        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Lizenz", "Licence"), message);
                    if (safe != nullptr)
                        safe->syncLicence();
                });
            }
            else
            {
                showActivationDialog (*licence, getTopLevelComponent());
            }
        };
        addChildComponent (licenceButton);

        closeButton.setTooltip (tr ("Schließen", "Close"));
        closeButton.onClick = [this] { close(); };
        addAndMakeVisible (closeButton);

        rows = {
            { tr ("Sprache", "Language"),
              i18n::format (tr ("Automatisch nimmt die Sprache deines Computers ({0}).",
                                "Automatic follows your computer's language ({0})."), { system }), {} },
            { tr ("Ansicht beim Öffnen", "Start view"), tr ("Für neue Instanzen.", "For new instances."), {} },
            { tr ("Fenstergröße", "Window size"), tr ("Für neue Instanzen; das offene Fenster folgt sofort.",
                                                      "For new instances; the open window follows at once."), {} },
            { tr ("Analyse bei neuen Instanzen anbieten", "Offer the analysis on new instances"),
              tr ("Aus: kein Popup, analysieren geht trotzdem.",
                  "Off: no popup, analyzing still works."), {} },
            { tr ("Infokarten an Reglern", "Info cards on controls"),
              tr ("Name, Wert und Wirkung nach kurzem Verweilen.", "Name, value and effect after a short hover."), {} },
            { tr ("Tour", "Tour"), tr ("Eine Minute durch die Oberfläche.", "One minute through the interface."), {} },
        };
    }

    void SettingsOverlay::setLicence (licensing::Manager* m)
    {
        licence = m != nullptr && m->isOn() ? m : nullptr;
        if (licence != nullptr && rows.size() == 6)
            rows.push_back ({ tr ("Lizenz", "Licence"), {}, {} });
        syncLicence();
        resized();
    }

    void SettingsOverlay::syncLicence()
    {
        licenceButton.setVisible (licence != nullptr);
        if (licence == nullptr)
            return;
        const bool active = licence->status().access == licensing::Access::licensed;
        licenceButton.setButtonText (active ? tr ("Freigeben", "Release") : tr ("Aktivieren", "Activate"));
        licenceButton.setTooltip (active ? tr ("Gibt diesen Computer frei, damit du den Schlüssel auf einem anderen aktivieren kannst.",
                                               "Releases this computer so you can activate the key on another one.")
                                         : tr ("Schlüssel aus der Bestell-Mail eingeben.", "Enter the key from the order mail."));
        if (rows.size() > 6)
            rows[6].hint = licence->describe();
        repaint();
    }

    void SettingsOverlay::setGroup (juce::OwnedArray<juce::TextButton>& group, int selected)
    {
        for (int i = 0; i < group.size(); ++i)
            group[i]->setToggleState (i == selected, juce::dontSendNotification);
    }

    void SettingsOverlay::syncFromSettings()
    {
        setGroup (languageButtons, (int) settings::languagePreference());
        setGroup (modeButtons, settings::getString (settings::defaultMode) == "advanced" ? 1 : 0);

        const double scale = settings::getString (settings::defaultScale, "1").getDoubleValue();
        int best = 0;
        for (int i = 1; i < (int) windowScales.size(); ++i)
            if (std::abs (windowScales[(size_t) i] - scale) < std::abs (windowScales[(size_t) best] - scale))
                best = i;
        setGroup (scaleButtons, best);

        offerAnalysis.setToggleState (settings::getBool (settings::offerAnalysis, true), juce::dontSendNotification);
        infoCards.setToggleState (settings::getBool (settings::infoCards, true), juce::dontSendNotification);
        syncLicence();
    }

    void SettingsOverlay::open()
    {
        syncFromSettings();
        setVisible (true);
        toFront (true);
        grabKeyboardFocus();
    }

    void SettingsOverlay::close()
    {
        setVisible (false);
    }

    juce::Rectangle<int> SettingsOverlay::sheetBounds() const
    {
        return getLocalBounds().removeFromRight (sheetW);
    }

    void SettingsOverlay::resized()
    {
        auto sheet = sheetBounds();
        closeButton.setBounds (sheet.getRight() - pad - 28, sheet.getY() + (headerH - 28) / 2, 28, 28);

        auto r = sheet.withTrimmedTop (headerH).reduced (pad, 0);
        const auto placeRow = [&] (int index, int controlH) -> juce::Rectangle<int>
        {
            auto row = r.removeFromTop (index < 3 ? 48 + controlH + 14 : 54);
            rows[(size_t) index].bounds = row;
            return row;
        };

        // Rows with a segment group: title, control, hint
        juce::OwnedArray<juce::TextButton>* groups[] = { &languageButtons, &modeButtons, &scaleButtons };
        for (int i = 0; i < 3; ++i)
        {
            auto row = placeRow (i, segmentH);
            auto control = row.withTrimmedTop (30).withHeight (segmentH);
            auto& g = *groups[i];
            const int w = control.getWidth() / g.size();
            for (int k = 0; k < g.size(); ++k)
                g[k]->setBounds (k == g.size() - 1 ? control : control.removeFromLeft (w));
        }

        // Rows with a switch or a button on the right
        offerAnalysis.setBounds (placeRow (3, 0).removeFromRight (30).withSizeKeepingCentre (30, 18));
        infoCards.setBounds (placeRow (4, 0).removeFromRight (30).withSizeKeepingCentre (30, 18));
        tourButton.setBounds (placeRow (5, 0).removeFromRight (112).withSizeKeepingCentre (112, 28));
        if (rows.size() > 6)
            licenceButton.setBounds (placeRow (6, 0).removeFromRight (112).withSizeKeepingCentre (112, 28));
    }

    void SettingsOverlay::paint (juce::Graphics& g)
    {
        g.fillAll (colours::scrim.withMultipliedAlpha (0.7f));

        const auto sheet = sheetBounds().toFloat();
        g.setColour (colours::panel);
        g.fillRect (sheet);
        drawGlass (g, sheet, 0.0f, 0.02f);
        g.setColour (colours::border);
        g.drawVerticalLine ((int) sheet.getX(), sheet.getY(), sheet.getBottom());

        auto header = sheet.withHeight ((float) headerH).reduced ((float) pad, 0.0f);
        g.setColour (colours::text);
        g.setFont (font (typesize::heading + 1.5f, true));
        g.drawText (tr ("Einstellungen", "Settings"), header, juce::Justification::centredLeft, true);
        g.setColour (colours::borderSoft);
        g.drawHorizontalLine (headerH - 1, sheet.getX(), sheet.getRight());

        for (size_t i = 0; i < rows.size(); ++i)
        {
            const auto& row = rows[i];
            auto r = row.bounds.toFloat();
            const bool segmented = i < 3;
            auto text = segmented ? r : r.withTrimmedRight (124.0f);
            auto titleArea = text.removeFromTop (segmented ? 30.0f : 30.0f).withTrimmedTop (10.0f);
            g.setColour (colours::text);
            g.setFont (font (typesize::small, true));
            g.drawText (row.title, titleArea, juce::Justification::centredLeft, true);

            auto hintArea = segmented ? r.withTrimmedTop (30.0f + segmentH + 4.0f).withHeight (16.0f) : text.withHeight (16.0f);
            g.setColour (colours::textDim);
            g.setFont (font (typesize::caption));
            g.drawFittedText (row.hint, hintArea.toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);

            if (i + 1 < rows.size())
            {
                g.setColour (colours::borderSoft);
                g.drawHorizontalLine ((int) r.getBottom(), r.getX(), r.getRight());
            }
        }

        // Footer: version
        auto footer = sheet.withTrimmedTop (sheet.getHeight() - 44.0f).reduced ((float) pad, 0.0f);
        g.setColour (colours::textDim);
        g.setFont (font (typesize::caption));
        g.drawText (juce::String (product::name) + " " + product::versionString + "  " + juce::String::fromUTF8 ("\xc2\xb7")
                        + "  " + product::companyName,
                    footer, juce::Justification::centredLeft, true);
    }

    bool SettingsOverlay::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::escapeKey)
        {
            close();
            return true;
        }
        return false;
    }

    void SettingsOverlay::mouseDown (const juce::MouseEvent& e)
    {
        if (! sheetBounds().contains (e.getPosition()))
            close();
    }

    //==============================================================================
    void showActivationDialog (licensing::Manager& m, juce::Component* over)
    {
        auto* w = new juce::AlertWindow (tr ("VOCALYPSE aktivieren", "Activate VOCALYPSE"),
                                         tr ("Kopier den Lizenzschlüssel aus deiner Bestell-Mail hier hinein.",
                                             "Paste the licence key from your order mail here."),
                                         juce::MessageBoxIconType::NoIcon, over);
        w->addTextEditor ("key", {}, tr ("Lizenzschlüssel", "Licence key"));
        w->addButton (tr ("Aktivieren", "Activate"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton (tr ("Abbrechen", "Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([w, &m] (int result)
        {
            const auto key = w->getTextEditorContents ("key");
            if (result != 1)
                return;
            m.activate (key, [] (bool ok, const juce::String& message)
            {
                juce::AlertWindow::showMessageBoxAsync (ok ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
                                                        tr ("Lizenz", "Licence"), message);
            });
        }), true);
    }

    LicenceLock::LicenceLock()
    {
        setVisible (false);
        enterButton.setButtonText (tr ("Lizenzschlüssel eingeben", "Enter licence key"));
        enterButton.getProperties().set ("fire", true);
        enterButton.onClick = [this] { if (licence != nullptr) showActivationDialog (*licence, getTopLevelComponent()); };
        buyButton.setButtonText (tr ("VOCALYPSE kaufen", "Buy VOCALYPSE"));
        buyButton.onClick = [] { juce::URL (licensing::Manager::buyUrl()).launchInDefaultBrowser(); };
        addAndMakeVisible (enterButton);
        addAndMakeVisible (buyButton);
    }

    LicenceLock::~LicenceLock()
    {
        if (licence != nullptr)
            licence->removeChangeListener (this);
    }

    void LicenceLock::setLicence (licensing::Manager* m)
    {
        if (licence != nullptr)
            licence->removeChangeListener (this);
        licence = m != nullptr && m->isOn() ? m : nullptr;
        if (licence != nullptr)
            licence->addChangeListener (this);
        sync();
    }

    void LicenceLock::changeListenerCallback (juce::ChangeBroadcaster*) { sync(); }

    void LicenceLock::sync()
    {
        const bool show = licence != nullptr && licence->status().access == licensing::Access::locked;
        setVisible (show);
        if (show)
            toFront (false);
        repaint();
    }

    juce::Rectangle<int> LicenceLock::cardBounds() const
    {
        return getLocalBounds().withSizeKeepingCentre (440, 220);
    }

    void LicenceLock::resized()
    {
        auto r = cardBounds().reduced (pad).removeFromBottom (34);
        enterButton.setBounds (r.removeFromLeft (r.getWidth() / 2 - 6));
        r.removeFromLeft (12);
        buyButton.setBounds (r);
    }

    void LicenceLock::paint (juce::Graphics& g)
    {
        g.fillAll (colours::scrim.withMultipliedAlpha (0.85f));
        const auto card = cardBounds().toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (card, 12.0f);
        g.setColour (colours::fire);
        g.drawRoundedRectangle (card.reduced (0.5f), 12.0f, 1.2f);

        auto r = card.reduced ((float) pad);
        g.setColour (colours::text);
        g.setFont (font (typesize::heading + 1.5f, true));
        const bool revoked = licence != nullptr && licence->record().revoked;
        g.drawText (revoked ? tr ("Lizenz nicht mehr gültig", "Licence no longer valid") : tr ("Die Testzeit ist vorbei", "The trial is over"),
                    r.removeFromTop (30.0f), juce::Justification::centredLeft, true);
        r.removeFromTop (8.0f);
        g.setColour (colours::textDim);
        g.setFont (font (typesize::small));
        g.drawFittedText (tr ("VOCALYPSE lässt deine Vocal gerade unbearbeitet durch. Aktiviere es mit dem Schlüssel aus deiner "
                              "Bestell-Mail - deine Projekte und Einstellungen bleiben, wie sie sind.",
                              "VOCALYPSE is passing your vocal through unprocessed. Activate it with the key from your order mail - "
                              "your projects and settings stay as they are."),
                          r.removeFromTop (80.0f).toNearestInt(), juce::Justification::topLeft, 4, 1.0f);
    }
}
