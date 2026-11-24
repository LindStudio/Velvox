#include "LicenceUI.h"
#include "VocalypseLookAndFeel.h"

// Licence key entry and the lock card, shared by VOCALYPSE and the single plugins (moved out of
// SettingsOverlay 2026-11-12; the product name and shop link come from the licensing::Manager).
namespace vocalypse::ui
{
    using i18n::tr;

    namespace
    {
        constexpr int pad = space::xl;

        juce::Colour accentOf (const juce::LookAndFeel& lf)
        {
            return lf.isColourSpecified (LicenceLock::accentColourId) ? lf.findColour (LicenceLock::accentColourId) : colours::fire;
        }

        std::function<std::shared_ptr<juce::LookAndFeel>()>& dialogLook()
        {
            static std::function<std::shared_ptr<juce::LookAndFeel>()> make;
            return make;
        }
    }

    void setActivationDialogLook (std::function<std::shared_ptr<juce::LookAndFeel>()> makeLook)
    {
        dialogLook() = std::move (makeLook);
    }

    void showActivationDialog (licensing::Manager& m, juce::Component* over)
    {
        auto* w = new juce::AlertWindow (i18n::format (tr ("{0} aktivieren", "Activate {0}"), { m.productName() }),
                                         m.freeKey() ? tr ("Kopier den Lizenzschlüssel aus der E-Mail von Lind Audio und füg ihn hier ein.",
                                                           "Paste the licence key from the Lind Audio email here.")
                                                     : tr ("Kopier den Lizenzschlüssel aus deiner Bestell-E-Mail und füg ihn hier ein.",
                                                           "Paste the licence key from your order email here."),
                                         juce::MessageBoxIconType::NoIcon, over);
        // The plugin's look (the window is its own desktop window and may outlive the editor: it owns its look,
        // which the modal callback keeps alive until the window is gone)
        std::shared_ptr<juce::LookAndFeel> look = dialogLook() ? dialogLook()() : nullptr;
        if (look == nullptr)
            look = std::make_shared<VocalypseLookAndFeel>();
        if (over != nullptr)
        {
            const auto a = accentOf (over->getLookAndFeel());
            look->setColour (juce::AlertWindow::outlineColourId, a.withAlpha (0.7f));
            look->setColour (juce::TextEditor::focusedOutlineColourId, a.withAlpha (0.7f));
            look->setColour (juce::TextEditor::highlightColourId, a.withAlpha (0.25f));
            look->setColour (LicenceLock::accentColourId, a);
        }
        w->setLookAndFeel (look.get());
        w->addTextEditor ("key", {}, tr ("Lizenzschlüssel", "Licence key"));
        w->addButton (tr ("Aktivieren", "Activate"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton (tr ("Abbrechen", "Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        // The window is its own desktop window: the plugin may close (and the Manager go) while it is open
        w->enterModalState (true, juce::ModalCallbackFunction::create ([w, &m, life = m.lifetime(), look] (int result)
        {
            const auto key = w->getTextEditorContents ("key");
            if (result != 1 || life.expired())
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
        enterButton.setButtonText (tr ("Schlüssel eingeben", "Enter key"));
        enterButton.getProperties().set ("fire", true);
        enterButton.onClick = [this] { if (licence != nullptr) showActivationDialog (*licence, getTopLevelComponent()); };
        buyButton.onClick = [this] { if (licence != nullptr && licence->buyUrl().isNotEmpty()) juce::URL (licence->buyUrl()).launchInDefaultBrowser(); };
        addAndMakeVisible (enterButton);
        addAndMakeVisible (buyButton);
    }

    LicenceLock::~LicenceLock()
    {
        stopTimer();
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
            buyButton.setButtonText (licence->freeKey() ? tr ("Gratis-Schlüssel holen", "Get free key")
                                                        : i18n::format (tr ("{0} kaufen", "Buy {0}"), { licence->productName() }));
            // a free product: nobody has a key at the first start, so getting one is the main button (EMBER review 2026-11-19)
            const bool getFirst = licence->freeKey();
            buyButton.getProperties().set ("fire", getFirst);
            enterButton.getProperties().set ("fire", ! getFirst);
            resized();
            repaint();
        }
        sync();
    }

    void LicenceLock::changeListenerCallback (juce::ChangeBroadcaster*) { sync(); }

    void LicenceLock::sync()
    {
        const bool show = licence != nullptr && licence->status().access == licensing::Access::locked;
        setVisible (show);
        if (show)
        {
            toFront (false);
            startTimer (250);
        }
        else
            stopTimer();
        repaint();
    }

    void LicenceLock::timerCallback()
    {
        if (licence == nullptr)
            return;
        if (++ticks % 4 == 0)
            licence->reloadIfChanged();   // may unlock: sync() runs through the change message
        const bool nowBusy = licence->isBusy();
        enterButton.setEnabled (! nowBusy);
        if (nowBusy != busy)
        {
            busy = nowBusy;
            repaint();
        }
    }

    juce::Rectangle<int> LicenceLock::cardBounds() const
    {
        return getLocalBounds().withSizeKeepingCentre (476, 220);
    }

    void LicenceLock::resized()
    {
        auto r = cardBounds().reduced (pad).removeFromBottom (34);
        // the main button (fire) on the left
        const bool getFirst = licence != nullptr && licence->freeKey();
        auto& first = getFirst ? buyButton : enterButton;
        auto& second = getFirst ? enterButton : buyButton;
        first.setBounds (r.removeFromLeft (r.getWidth() / 2 - 6));
        r.removeFromLeft (12);
        second.setBounds (r);
    }

    juce::Colour LicenceLock::accent() const { return accentOf (getLookAndFeel()); }

    void LicenceLock::paint (juce::Graphics& g)
    {
        g.fillAll (colours::scrim.withMultipliedAlpha (0.85f));
        const auto card = cardBounds().toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (card, 12.0f);
        g.setColour (accent());
        g.drawRoundedRectangle (card.reduced (0.5f), 12.0f, 1.2f);

        auto r = card.reduced ((float) pad);
        g.setColour (colours::text);
        g.setFont (font (typesize::heading + 1.5f, true));
        const bool revoked = licence != nullptr && licence->record().revoked;
        const bool hadTrial = licence != nullptr && licence->trialDays() > 0;
        const bool free = licence != nullptr && licence->freeKey();
        const auto name = licence != nullptr ? licence->productName() : juce::String();
        g.drawText (revoked    ? tr ("Schlüssel nicht mehr gültig", "Key no longer valid")
                    : hadTrial ? tr ("Die Testzeit ist vorbei", "The trial is over")
                               : i18n::format (tr ("{0} braucht einen Lizenzschlüssel", "{0} needs a licence key"), { name }),
                    r.removeFromTop (30.0f), juce::Justification::centredLeft, true);
        r.removeFromTop (8.0f);
        g.setColour (colours::textDim);
        g.setFont (font (typesize::small));
        if (free && ! revoked)
            g.drawFittedText (i18n::format (tr ("{0} ist kostenlos. Klick auf „Gratis-Schlüssel holen“, gib deine E-Mail-Adresse ein "
                                                "und trag den Schlüssel aus der E-Mail unter „Schlüssel eingeben“ ein. Bis dahin läuft "
                                                "dein Gesang unbearbeitet durch.",
                                                "{0} is free. Click “Get free key”, enter your email address, then enter the key from "
                                                "the email under “Enter key”. Until then your vocal passes through unprocessed."), { name }),
                              r.removeFromTop (80.0f).toNearestInt(), juce::Justification::topLeft, 4, 1.0f);
        else
        g.drawFittedText (free ? i18n::format (tr ("{0} lässt deinen Gesang gerade unbearbeitet durch. Gib deinen Schlüssel neu ein oder "
                                                   "hol dir einen neuen – deine Projekte und Einstellungen bleiben, wie sie sind.",
                                                   "{0} is passing your vocal through unprocessed. Enter your key again or get a new one – "
                                                   "your projects and settings stay as they are."), { name })
                               : i18n::format (tr ("{0} lässt deinen Gesang gerade unbearbeitet durch. Aktiviere es mit dem Schlüssel aus "
                                                   "deiner Bestell-E-Mail – deine Projekte und Einstellungen bleiben, wie sie sind.",
                                                   "{0} is passing your vocal through unprocessed. Activate it with the key from your order "
                                                   "email – your projects and settings stay as they are."), { name }),
                          r.removeFromTop (80.0f).toNearestInt(), juce::Justification::topLeft, 4, 1.0f);
        if (busy)
        {
            // the key is on its way to the server: say so instead of a silent, greyed-out button
            g.setColour (accent());
            g.setFont (font (typesize::small, true));
            g.drawText (tr ("Wird aktiviert … einen Moment.", "Activating … one moment."),
                        card.reduced ((float) pad).removeFromBottom (34.0f + 22.0f).removeFromTop (18.0f), juce::Justification::centredLeft, true);
        }
    }
}
