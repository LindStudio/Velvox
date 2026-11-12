#include "LicenceUI.h"

// Licence key entry and the lock card, shared by VOCALYPSE and the single plugins (moved out of
// SettingsOverlay 2026-11-12; the product name and shop link come from the licensing::Manager).
namespace vocalypse::ui
{
    using i18n::tr;

    namespace
    {
        constexpr int pad = space::xl;
    }

    void showActivationDialog (licensing::Manager& m, juce::Component* over)
    {
        auto* w = new juce::AlertWindow (i18n::format (tr ("{0} aktivieren", "Activate {0}"), { m.productName() }),
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
        buyButton.onClick = [this] { if (licence != nullptr && licence->buyUrl().isNotEmpty()) juce::URL (licence->buyUrl()).launchInDefaultBrowser(); };
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
        {
            licence->addChangeListener (this);
            buyButton.setButtonText (i18n::format (tr ("{0} kaufen", "Buy {0}"), { licence->productName() }));
        }
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
        const auto name = licence != nullptr ? licence->productName() : juce::String();
        g.drawFittedText (i18n::format (tr ("{0} lässt deine Vocal gerade unbearbeitet durch. Aktiviere es mit dem Schlüssel aus deiner "
                                            "Bestell-Mail - deine Projekte und Einstellungen bleiben, wie sie sind.",
                                            "{0} is passing your vocal through unprocessed. Activate it with the key from your order mail - "
                                            "your projects and settings stay as they are."), { name }),
                          r.removeFromTop (80.0f).toNearestInt(), juce::Justification::topLeft, 4, 1.0f);
    }
}
