#pragma once

#include "Widgets.h"
#include "../Text/I18n.h"

namespace velvox::ui
{
    // Settings sheet (gear in the header, 1.0): a panel sliding in from the right over the canvas.
    // Everything here is a per-user preference (UserSettings), never part of a project:
    //   Language        Automatic (the computer's language) | Deutsch | English
    //   Start view      SIMPEL | ADVANCED for new instances
    //   Window size     100 / 125 / 150 / 200 % (new instances; the open window follows at once)
    //   Offer the analysis popup on new instances, info cards on controls
    //   Tour            start again
    class SettingsOverlay final : public juce::Component
    {
    public:
        SettingsOverlay();

        void open();
        void close();
        bool isOpen() const noexcept { return isVisible(); }

        // The language preference was changed (the editor stores it and rebuilds the interface)
        std::function<void (i18n::Preference)> onLanguage;
        // A window size was chosen (scale of the 1000 x 600 design canvas)
        std::function<void (double scale)> onWindowScale;
        std::function<void()> onStartTutorial;

        static constexpr std::array<double, 4> windowScales { 1.0, 1.25, 1.5, 2.0 };

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;
        void mouseDown (const juce::MouseEvent&) override;

        // For tests
        juce::Button& getLanguageButton (i18n::Preference p) { return *languageButtons[(int) p]; }
        juce::Button& getModeButton (bool advanced) { return *modeButtons[advanced ? 1 : 0]; }
        juce::Button& getScaleButton (int index) { return *scaleButtons[index]; }
        juce::ToggleButton& getOfferAnalysisToggle() noexcept { return offerAnalysis; }
        juce::ToggleButton& getInfoCardsToggle() noexcept { return infoCards; }
        juce::Rectangle<int> sheetBounds() const;

    private:
        void syncFromSettings();
        void setGroup (juce::OwnedArray<juce::TextButton>& group, int selected);

        juce::OwnedArray<juce::TextButton> languageButtons, modeButtons, scaleButtons;
        juce::ToggleButton offerAnalysis, infoCards;
        juce::TextButton tourButton, closeButton { juce::String::fromUTF8 ("\xc3\x97") };

        // Row layout (sheet coordinates), computed in resized() and used by paint()
        struct Row { juce::String title, hint; juce::Rectangle<int> bounds; };
        std::vector<Row> rows;
    };
}
