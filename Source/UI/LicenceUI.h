#pragma once

#include "Widgets.h"
#include "../Text/I18n.h"
#include "../Licensing/License.h"

namespace vocalypse::ui
{
    // Asks for the key from the order mail and activates it (a small modal card)
    void showActivationDialog (licensing::Manager&, juce::Component* over);

    // Trial over / key invalid: covers the plugin, says why the vocal passes through unprocessed,
    // offers the key entry and the shop. Hidden whenever the licence is fine (or not checked at all).
    class LicenceLock final : public juce::Component, private juce::ChangeListener
    {
    public:
        LicenceLock();
        ~LicenceLock() override;
        void setLicence (licensing::Manager* m);

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void sync();
        juce::Rectangle<int> cardBounds() const;

        licensing::Manager* licence = nullptr;
        juce::TextButton enterButton, buyButton;
    };
}
