#pragma once

#include "Widgets.h"
#include "MascotView.h"
#include "../Text/I18n.h"

namespace vocalypse::ui
{
    // First-start tour. The welcome card (1.0) asks for the interface language (Deutsch | English,
    // preselected: the computer's language) and offers: analyse the vocal (primary), a short tour,
    // or straight to the plugin (Esc = remind me later). The tour then highlights one area at a
    // time with a short explanation next to it. Outside the explanation card all clicks reach the
    // plugin, so you can try each step while reading it.
    class TutorialOverlay final : public juce::Component
    {
    public:
        enum class Outcome { finished, skipped, later, analyse };

        struct Step
        {
            juce::String target;   // area key, resolved by targetFor
            juce::String title, text;
        };

        TutorialOverlay();

        static const std::vector<Step>& steps();

        // Welcome card first (true) or straight into the first step (false)
        void start (bool withWelcome);
        void goTo (int stepIndex);   // -1 = welcome card
        void finish (Outcome outcome);
        int getStep() const noexcept { return step; }
        bool isActive() const noexcept { return isVisible(); }

        // Area of the editor canvas to highlight for a target key
        std::function<juce::Rectangle<int> (const juce::String& target)> targetFor;
        std::function<void (Outcome)> onClose;
        // A step was entered (not called again when the same step is only laid out anew)
        std::function<void (const juce::String& target)> onStep;
        // A language was picked on the welcome card (the editor stores it and rebuilds the interface)
        std::function<void (i18n::Language)> onLanguage;

        juce::Button& getLanguageButton (i18n::Language l) noexcept { return l == i18n::Language::german ? germanButton : englishButton; }
        juce::Button& getAnalyseButton() noexcept { return analyseButton; }
        juce::Button& getTourButton() noexcept { return startButton; }
        juce::Button& getPluginButton() noexcept { return pluginButton; }
        MascotView& getWelcomeMascot() noexcept { return mascot; }
        juce::Rectangle<int> getCardBounds() const { return cardBounds(); }
        juce::Rectangle<int> getTargetBounds() const noexcept { return target; }

        void paint (juce::Graphics&) override;
        void resized() override;
        bool hitTest (int x, int y) override;
        bool keyPressed (const juce::KeyPress&) override;

    private:
        // Where the step card goes: below, above, left or right of the target, never on it (review
        // 2026-10-28); inside only when the target fills the window. The side gets a small pointer.
        enum class Side { below, above, left, right, inside };
        juce::Rectangle<int> cardBounds (Side* side = nullptr) const;
        juce::Rectangle<float> spotlight() const;
        void layoutButtons();
        void tick();

        int step = -1, announced = -2;   // announced: the step onStep was last called for
        juce::Rectangle<int> target;
        std::array<Smoothed, 4> spot {};   // animated spotlight x, y, w, h
        bool spotValid = false;

        MascotView mascot;          // Memphy on the welcome card, popping out over its top edge
        double welcomeTime = 0.0, lastTick = 0.0;
        juce::TextButton germanButton { "Deutsch" }, englishButton { "English" };
        juce::TextButton analyseButton, startButton, pluginButton;
        juce::TextButton backButton, nextButton, skipButton;
        juce::VBlankAttachment vblank { this, [this] { tick(); } };
    };
}
