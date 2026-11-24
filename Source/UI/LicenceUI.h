#pragma once

#include "Widgets.h"
#include "../Text/I18n.h"
#include "../Licensing/License.h"

namespace vocalypse::ui
{
    // Asks for the key from the email and activates it (a small modal card in the plugin's look; the accent comes
    // from LicenceLock::accentColourId of over's look-and-feel when it sets one)
    void showActivationDialog (licensing::Manager&, juce::Component* over);
    // The key dialog's look (a single plugin can give its own, EMBER 2026-11-24); unset = the VOCALYPSE look.
    // The dialog owns what this makes (it may outlive the editor).
    void setActivationDialogLook (std::function<std::shared_ptr<juce::LookAndFeel>()> makeLook);

    // Trial over / key invalid: covers the plugin, says why the vocal passes through unprocessed,
    // offers the key entry and the shop. Hidden whenever the licence is fine (or not checked at all).
    class LicenceLock final : public juce::Component, private juce::ChangeListener, private juce::Timer
    {
    public:
        // The card's accent: set it in the product's look-and-feel (default: the VOCALYPSE fire)
        enum ColourIds { accentColourId = 0x2a11c001 };

        LicenceLock();
        ~LicenceLock() override;
        void setLicence (licensing::Manager* m);

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void timerCallback() override;
        void sync();
        juce::Rectangle<int> cardBounds() const;
        juce::Colour accent() const;

        licensing::Manager* licence = nullptr;
        juce::TextButton enterButton, buyButton;
        bool busy = false;   // activating: the card says so
        int ticks = 0;
    };
}
