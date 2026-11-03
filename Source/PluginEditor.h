#pragma once

#include "PluginProcessor.h"
#include "UI/VocalypseLookAndFeel.h"
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
#include "UI/VocalCheckView.h"
#include "UI/PresetBrowser.h"
#include "UI/SettingsOverlay.h"

// The whole UI is laid out on a fixed design canvas (ui::baseWidth x ui::baseHeight = 1000 x 600)
// that is scaled uniformly to the window size, so every window size keeps the same proportions
// and stays sharp (vector drawing).
//
//   header      logo, presets ‹ name ▾ ›, SIMPEL | ADVANCED, AUTO MIX | VOCAL CHECK, bypass, help
//   AUTO MIX    Simple:   spectrum + "Vocal analysieren" | six macro faders, IN/OUT at the bottom
//               Advanced: chain row (+ IN/OUT) on top, the selected module's panel below
//   VOCAL CHECK score ring + finding tiles (VocalCheckView) over the whole body (both modes)
//   overlays    analysis, auto-mix details, module browser, preset list, dialogs, help, tutorial, hover info
// View state (mode, tab, selection) is stored with the project and never affects the audio.
//
// VocalypseView is the whole interface; VocalypseAudioProcessorEditor (below) is the window that
// hosts it and builds a fresh one when the interface language changes (1.0), so every text,
// including those set once in constructors, follows the new language at once.
class VocalypseView final : public juce::Component,
                            private juce::Timer,
                            private juce::ChangeListener
{
public:
    using Mode = vocalypse::ui::TopBar::Mode;
    using Tab = vocalypse::ui::TopBar::Tab;

    // What to show again after the view was rebuilt for a language change
    enum class Reopen { none, welcome, settings };

    explicit VocalypseView (VocalypseAudioProcessor&, Reopen reopen = Reopen::none);
    ~VocalypseView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;   // Ctrl+Z, Ctrl+Y, Ctrl+Shift+Z

    // The language was changed (welcome card or settings): the window rebuilds the view.
    // Called on the message thread from a click inside this view - the window defers the rebuild.
    std::function<void (Reopen)> onLanguageChange;
    // A window size was chosen in the settings (scale of the design canvas)
    std::function<void (double)> onWindowScale;

    // Settings sheet (gear in the header) and the first-start welcome card
    void showSettings();
    void showWelcome();

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
    vocalypse::ui::HelpOverlay& getHelp() noexcept { return help; }
    vocalypse::ui::TutorialOverlay& getTutorial() noexcept { return tutorial; }
    vocalypse::ui::ChainView& getChainView() noexcept { return chainView; }
    vocalypse::ui::TopBar& getTopBar() noexcept { return topBar; }
    vocalypse::ui::SimpleView& getSimpleView() noexcept { return simpleView; }
    vocalypse::ui::VocalCheckView& getCheckView() noexcept { return checkView; }
    vocalypse::ui::AutoMixView& getAutoMixView() noexcept { return mixView; }
    bool isMixDetailsOpen() const noexcept { return detailsOverlay.isVisible(); }
    vocalypse::ui::ModuleBrowser& getBrowser() noexcept { return browser; }
    vocalypse::ui::PresetBrowser& getPresetBrowser() noexcept { return presetBrowser; }
    vocalypse::ui::AnalysisOverlay& getAnalysisOverlay() noexcept { return analysis; }
    vocalypse::ui::DetailPanel* getDetail() noexcept { return detail.get(); }
    vocalypse::ui::CompactMeters& getMeters() noexcept { return meters; }
    vocalypse::ui::SettingsOverlay& getSettings() noexcept { return settingsOverlay; }

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
    class Canvas final : public juce::Component, public vocalypse::ui::InfoPopupHost
    {
    public:
        Canvas();
        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void showInfo (juce::Component& anchor, const juce::String& title, const juce::String& value, const juce::String& text) override;
        void hideInfo (juce::Component& anchor) override;
        void hideInfoIfAnchorGone();

        bool bypassed = false;  // amber frame and label while the global bypass is on
        float bypassLabelY = 70.0f;   // vertical centre of that label (set by the view, see applyView)
        vocalypse::ui::InfoPopup info;

    private:
        juce::Image backdrop;   // coloured light behind the panels, rendered once per size
        float backdropScale = 0.0f;
        juce::Component::SafePointer<juce::Component> infoAnchor;
    };

    // The auto-mix report in a card over the canvas (click outside or Esc closes)
    class DetailsOverlay final : public juce::Component
    {
    public:
        explicit DetailsOverlay (vocalypse::ui::AutoMixView& view);
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;
        juce::Rectangle<int> cardBounds() const;

    private:
        vocalypse::ui::AutoMixView& view;
        juce::TextButton close { juce::String::fromUTF8 ("\xc3\x97") };
    };

    VocalypseAudioProcessor& proc;
    juce::SharedResourcePointer<vocalypse::ui::EmbeddedFonts> fonts; // before anything that draws text
    vocalypse::ui::VocalypseLookAndFeel lookAndFeel;
    vocalypse::ui::SpectrumSource spectrum;
    vocalypse::ui::LiveData live;
    vocalypse::ui::LiveData panelLive;   // live data of the selected instance when it is not the first one
    std::vector<float> scopeL, scopeR;

    Canvas canvas;
    vocalypse::ui::DialogOverlay dialog;
    vocalypse::ui::HelpOverlay help;
    vocalypse::ui::TutorialOverlay tutorial;
    vocalypse::ui::SettingsOverlay settingsOverlay;
    vocalypse::ui::ModuleBrowser browser;
    vocalypse::ui::PresetBrowser presetBrowser;
    vocalypse::ui::TopBar topBar;
    vocalypse::ui::ChainView chainView;
    vocalypse::ui::SimpleView simpleView;
    vocalypse::ui::VocalCheckView checkView;   // the VOCAL CHECK tab (both modes)
    vocalypse::ui::AutoMixView mixView;
    DetailsOverlay detailsOverlay { mixView };
    vocalypse::ui::AnalysisOverlay analysis;
    std::unique_ptr<vocalypse::ui::DetailPanel> detail;

    vocalypse::ui::CompactMeters meters;

    Mode mode = Mode::simple;
    Tab tab = Tab::autoMix;
    bool constructed = false;
    int selected = 0;
    int frame = 0;

    juce::TooltipWindow tooltips { this, vocalypse::ui::motion::hoverInfoDelayMs };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalypseView)
};

//==============================================================================
// The plugin window: resizable at a fixed aspect ratio (size remembered per project as "uiScale"),
// hosts one VocalypseView and replaces it when the interface language changes.
class VocalypseAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    using Mode = VocalypseView::Mode;
    using Tab = VocalypseView::Tab;
    using Reopen = VocalypseView::Reopen;

    explicit VocalypseAudioProcessorEditor (VocalypseAudioProcessor&);
    ~VocalypseAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override { return view->keyPressed (key); }

    // Builds a fresh view in the current language (state, view mode and selection live in the project)
    void rebuild (Reopen reopen);

    VocalypseView& getView() noexcept { return *view; }

    // The view's interface (tests, snapshot renderer)
    void showSettings()                                 { view->showSettings(); }
    void showWelcome()                                  { view->showWelcome(); }
    void showHelp (const juce::String& pageKey)         { view->showHelp (pageKey); }
    void startTutorial (bool withWelcome)               { view->startTutorial (withWelcome); }
    void startAnalysis()                                { view->startAnalysis(); }
    void showMixDetails()                               { view->showMixDetails(); }
    void setMode (Mode m)                               { view->setMode (m); }
    void setTab (Tab t)                                 { view->setTab (t); }
    Mode getMode() const noexcept                       { return view->getMode(); }
    Tab getTab() const noexcept                         { return view->getTab(); }
    bool isAdvanced() const noexcept                    { return view->isAdvanced(); }
    bool isModulePanelShown() const noexcept            { return view->isModulePanelShown(); }
    void selectModule (int moduleIdOrOutput)            { view->selectModule (moduleIdOrOutput); }
    void openModule (int moduleIdOrOutput)              { view->openModule (moduleIdOrOutput); }
    vocalypse::ui::HelpOverlay& getHelp() noexcept         { return view->getHelp(); }
    vocalypse::ui::TutorialOverlay& getTutorial() noexcept { return view->getTutorial(); }
    vocalypse::ui::ChainView& getChainView() noexcept      { return view->getChainView(); }
    vocalypse::ui::TopBar& getTopBar() noexcept            { return view->getTopBar(); }
    vocalypse::ui::SimpleView& getSimpleView() noexcept    { return view->getSimpleView(); }
    vocalypse::ui::VocalCheckView& getCheckView() noexcept { return view->getCheckView(); }
    vocalypse::ui::AutoMixView& getAutoMixView() noexcept  { return view->getAutoMixView(); }
    bool isMixDetailsOpen() const noexcept              { return view->isMixDetailsOpen(); }
    vocalypse::ui::ModuleBrowser& getBrowser() noexcept    { return view->getBrowser(); }
    vocalypse::ui::PresetBrowser& getPresetBrowser() noexcept { return view->getPresetBrowser(); }
    vocalypse::ui::AnalysisOverlay& getAnalysisOverlay() noexcept { return view->getAnalysisOverlay(); }
    vocalypse::ui::DetailPanel* getDetail() noexcept       { return view->getDetail(); }
    vocalypse::ui::CompactMeters& getMeters() noexcept     { return view->getMeters(); }
    vocalypse::ui::SettingsOverlay& getSettings() noexcept { return view->getSettings(); }

    static juce::Rectangle<int> bodyArea()  { return VocalypseView::bodyArea(); }
    static juce::Rectangle<int> chainArea() { return VocalypseView::chainArea(); }
    static juce::Rectangle<int> meterArea() { return VocalypseView::meterArea(); }
    static juce::Rectangle<int> panelArea() { return VocalypseView::panelArea(); }

private:
    void attach();

    VocalypseAudioProcessor& proc;
    std::unique_ptr<VocalypseView> view;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalypseAudioProcessorEditor)
};
