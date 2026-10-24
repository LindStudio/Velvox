#pragma once

#include "PluginProcessor.h"
#include "UI/VelvoxLookAndFeel.h"
#include "UI/TopBar.h"
#include "UI/ChainView.h"
#include "UI/DetailPanels.h"
#include "UI/AutoMixView.h"
#include "UI/AnalysisOverlay.h"
#include "UI/Spectrum.h"
#include "UI/LiveData.h"
#include "UI/HelpOverlay.h"
#include "UI/TutorialOverlay.h"
#include "UI/ModuleBrowser.h"
#include "UI/SimpleView.h"

// The whole UI is laid out on a fixed design canvas (ui::baseWidth x ui::baseHeight = 1000 x 600)
// that is scaled uniformly to the window size, so every window size keeps the same proportions
// and stays sharp (vector drawing).
//
//   header      logo, presets ‹ name ▾ ›, SIMPEL | ADVANCED, AUTO MIX | VOCAL CHECK, bypass, help
//   AUTO MIX    Simple:   spectrum + "Vocal analysieren" | six macro faders, IN/OUT at the bottom
//               Advanced: chain row (+ IN/OUT) on top, the selected module's panel below
//   VOCAL CHECK the check panel over the whole body (both modes)
//   overlays    analysis, auto-mix details, module browser, preset list, dialogs, help, tutorial, hover info
// View state (mode, tab, selection) is stored with the project and never affects the audio.
class VelvoxAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                         private juce::Timer,
                                         private juce::ChangeListener
{
public:
    using Mode = velvox::ui::TopBar::Mode;
    using Tab = velvox::ui::TopBar::Tab;

    explicit VelvoxAudioProcessorEditor (VelvoxAudioProcessor&);
    ~VelvoxAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;   // Ctrl+Z, Ctrl+Y, Ctrl+Shift+Z

    // Help and tutorial
    void showHelp (const juce::String& pageKey);
    void startTutorial (bool withWelcome);

    // Automatic mix: arm the capture and show its progress
    void startAnalysis();
    // The auto-mix report (steps, Density, fair compare) as an overlay
    void showMixDetails();

    // View state
    void setMode (Mode m);
    void setTab (Tab t);
    Mode getMode() const noexcept { return mode; }
    Tab getTab() const noexcept { return tab; }
    bool isAdvanced() const noexcept { return mode == Mode::advanced; }
    bool isModulePanelShown() const noexcept { return mode == Mode::advanced && tab == Tab::autoMix; }
    void selectModule (int moduleIdOrOutput) { select (moduleIdOrOutput); }
    // Shows a module in Advanced (from a report step or a Vocal Check finding)
    void openModule (int moduleIdOrOutput);

    // For tests and the snapshot renderer
    velvox::ui::HelpOverlay& getHelp() noexcept { return help; }
    velvox::ui::TutorialOverlay& getTutorial() noexcept { return tutorial; }
    velvox::ui::ChainView& getChainView() noexcept { return chainView; }
    velvox::ui::TopBar& getTopBar() noexcept { return topBar; }
    velvox::ui::SimpleView& getSimpleView() noexcept { return simpleView; }
    velvox::ui::AutoMixView& getAutoMixView() noexcept { return mixView; }
    bool isMixDetailsOpen() const noexcept { return detailsOverlay.isVisible(); }
    velvox::ui::ModuleBrowser& getBrowser() noexcept { return browser; }
    velvox::ui::AnalysisOverlay& getAnalysisOverlay() noexcept { return analysis; }
    velvox::ui::DetailPanel* getDetail() noexcept { return detail.get(); }
    velvox::ui::CompactMeters& getMeters() noexcept { return meters; }

    // Canvas layout (design units)
    static juce::Rectangle<int> bodyArea();
    static juce::Rectangle<int> chainArea();
    static juce::Rectangle<int> meterArea();
    static juce::Rectangle<int> panelArea();

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void updateLiveData();
    void applyView (bool animate);
    void storeView();
    void select (int moduleIdOrOutput);
    void raiseOverlays();
    juce::Rectangle<int> tutorialTarget (const juce::String& key);

    // Design canvas: draws the atmosphere (cached) and hosts the hover info card
    class Canvas final : public juce::Component, public velvox::ui::InfoPopupHost
    {
    public:
        Canvas();
        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void showInfo (juce::Component& anchor, const juce::String& title, const juce::String& value, const juce::String& text) override;
        void hideInfo (juce::Component& anchor) override;
        void hideInfoIfAnchorGone();

        bool bypassed = false;  // amber frame and label while the global bypass is on
        velvox::ui::InfoPopup info;

    private:
        juce::Image backdrop;   // coloured light behind the panels, rendered once per size
        float backdropScale = 0.0f;
        juce::Component::SafePointer<juce::Component> infoAnchor;
    };

    // The auto-mix report in a card over the canvas (click outside or Esc closes)
    class DetailsOverlay final : public juce::Component
    {
    public:
        explicit DetailsOverlay (velvox::ui::AutoMixView& view);
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;
        juce::Rectangle<int> cardBounds() const;

    private:
        velvox::ui::AutoMixView& view;
        juce::TextButton close { juce::String::fromUTF8 ("\xc3\x97") };
    };

    VelvoxAudioProcessor& proc;
    juce::SharedResourcePointer<velvox::ui::EmbeddedFonts> fonts; // before anything that draws text
    velvox::ui::VelvoxLookAndFeel lookAndFeel;
    velvox::ui::SpectrumSource spectrum;
    velvox::ui::LiveData live;
    velvox::ui::LiveData panelLive;   // live data of the selected instance when it is not the first one
    std::vector<float> scopeL, scopeR;

    Canvas canvas;
    velvox::ui::DialogOverlay dialog;
    velvox::ui::HelpOverlay help;
    velvox::ui::TutorialOverlay tutorial;
    velvox::ui::ModuleBrowser browser;
    velvox::ui::TopBar topBar;
    velvox::ui::ChainView chainView;
    velvox::ui::SimpleView simpleView;
    velvox::ui::AutoMixView mixView;
    DetailsOverlay detailsOverlay { mixView };
    velvox::ui::AnalysisOverlay analysis;
    std::unique_ptr<velvox::ui::DetailPanel> detail;

    velvox::ui::CompactMeters meters;

    Mode mode = Mode::simple;
    Tab tab = Tab::autoMix;
    bool constructed = false;
    int selected = 0;
    int frame = 0;

    juce::TooltipWindow tooltips { this, velvox::ui::motion::hoverInfoDelayMs };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VelvoxAudioProcessorEditor)
};
