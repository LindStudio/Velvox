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

// The whole UI is laid out on a fixed design canvas (ui::baseWidth x
// ui::baseHeight) that is scaled uniformly to the window size, so every
// window size keeps the same proportions and stays sharp (vector drawing).
//
//   top bar      presets, A/B, Auto Mix | Module, Analysieren, bypass, help
//   chain view   signal flow, drag & drop routing, parallel sends
//   body         [input rail] [Auto Mix overview | module view + controls] [output rail]
//   overlays     analysis popup, dialogs, help, tutorial, hover info
class VelvoxAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                         private juce::Timer,
                                         private juce::ChangeListener
{
public:
    explicit VelvoxAudioProcessorEditor (VelvoxAudioProcessor&);
    ~VelvoxAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Help and tutorial
    void showHelp (const juce::String& pageKey);
    void startTutorial (bool withWelcome);

    // Automatic mix: arm the capture and show its progress
    void startAnalysis();

    // For tests and the snapshot renderer
    velvox::ui::HelpOverlay& getHelp() noexcept { return help; }
    velvox::ui::TutorialOverlay& getTutorial() noexcept { return tutorial; }
    velvox::ui::ChainView& getChainView() noexcept { return chainView; }
    velvox::ui::TopBar& getTopBar() noexcept { return topBar; }
    velvox::ui::AutoMixView& getAutoMixView() noexcept { return mixView; }
    velvox::ui::ModuleBrowser& getBrowser() noexcept { return browser; }
    velvox::ui::AnalysisOverlay& getAnalysisOverlay() noexcept { return analysis; }
    velvox::ui::DetailPanel* getDetail() noexcept { return detail.get(); }
    bool isProMode() const noexcept { return proMode; }
    void selectModule (int moduleIdOrOutput) { select (moduleIdOrOutput); }

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void updateLiveData();
    void setProMode (bool pro);
    void select (int moduleIdOrOutput);
    void openModule (int moduleIdOrOutput);
    void raiseOverlays();
    juce::Rectangle<int> tutorialTarget (const juce::String& key);

    // Design canvas: draws the glass atmosphere (cached) and hosts the hover info card
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
        juce::Image backdrop;   // coloured light behind the glass, rendered once per size
        float backdropScale = 0.0f;
        juce::Component::SafePointer<juce::Component> infoAnchor;
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
    velvox::ui::AutoMixView mixView;
    velvox::ui::AnalysisOverlay analysis;
    std::unique_ptr<velvox::ui::DetailPanel> detail;

    velvox::ui::LevelMeter inputMeter, outputMeter;
    velvox::ui::Knob inputGain, outputGain;
    velvox::ui::Toggle safetyClip;
    juce::TextButton monoCheck { "Mono" };
    velvox::ui::CorrelationView correlation;

    bool proMode = false;
    bool constructed = false;
    int selected = 0;
    int frame = 0;

    juce::TooltipWindow tooltips { this, 900 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VelvoxAudioProcessorEditor)
};
