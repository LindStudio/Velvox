#pragma once

#include "PluginProcessor.h"
#include "UI/VelvoxLookAndFeel.h"
#include "UI/TopBar.h"
#include "UI/ChainView.h"
#include "UI/DetailPanels.h"
#include "UI/BeginnerView.h"
#include "UI/Spectrum.h"
#include "UI/LiveData.h"
#include "UI/HelpOverlay.h"
#include "UI/TutorialOverlay.h"
#include "UI/AssistantView.h"

// The whole UI is laid out on a fixed design canvas (ui::baseWidth x
// ui::baseHeight) that is scaled uniformly to the window size, so every
// window size keeps the same proportions and stays sharp (vector drawing).
//
//   top bar      presets, A/B, Beginner | Pro, bypass
//   chain view   signal flow, drag & drop routing, parallel sends
//   body         [input rail] [Beginner hero + macros | Pro module view + controls] [output rail]
class VelvoxAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VelvoxAudioProcessorEditor (VelvoxAudioProcessor&);
    ~VelvoxAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Help and first-start tutorial
    void showHelp (const juce::String& pageKey);
    void startTutorial (bool withWelcome);

    // For tests and the snapshot renderer
    velvox::ui::HelpOverlay& getHelp() noexcept { return help; }
    velvox::ui::TutorialOverlay& getTutorial() noexcept { return tutorial; }
    velvox::ui::ChainView& getChainView() noexcept { return chainView; }
    velvox::ui::TopBar& getTopBar() noexcept { return topBar; }
    velvox::ui::BeginnerView& getBeginnerView() noexcept { return beginnerView; }
    velvox::ui::AssistantView& getAssistantView() noexcept { return assistantView; }
    bool isAssistantOpen() const noexcept { return assistantOpen; }
    void setAssistantOpen (bool open);
    velvox::ui::DetailPanel* getDetail() noexcept { return detail.get(); }
    bool isProMode() const noexcept { return proMode; }
    void selectModule (int moduleIdOrOutput) { select (moduleIdOrOutput); }

private:
    void timerCallback() override;
    void updateLiveData();
    void setProMode (bool pro);
    void select (int moduleIdOrOutput);
    void raiseOverlays();
    juce::Rectangle<int> tutorialTarget (const juce::String& key);
    std::vector<velvox::ui::Hint> computeHints();

    // Design canvas: draws the aurora atmosphere and hosts the hover info card
    class Canvas final : public juce::Component, public velvox::ui::InfoPopupHost
    {
    public:
        Canvas();
        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void showInfo (juce::Component& anchor, const juce::String& title, const juce::String& text) override;
        void hideInfo (juce::Component& anchor) override;
        void hideInfoIfAnchorGone();

        float ambient = 0.0f;   // 0..1, follows the output level
        double phase = 0.0;     // slow drift of the nebula
        bool bypassed = false;  // amber frame and label while the global bypass is on
        velvox::ui::InfoPopup info;

    private:
        juce::Component::SafePointer<juce::Component> infoAnchor;
    };

    VelvoxAudioProcessor& proc;
    juce::SharedResourcePointer<velvox::ui::EmbeddedFonts> fonts; // before anything that draws text
    velvox::ui::VelvoxLookAndFeel lookAndFeel;
    velvox::ui::SpectrumSource spectrum;
    velvox::ui::LiveData live;
    std::vector<float> scopeL, scopeR;

    Canvas canvas;
    velvox::ui::DialogOverlay dialog;
    velvox::ui::HelpOverlay help;
    velvox::ui::TutorialOverlay tutorial;
    velvox::ui::TopBar topBar;
    velvox::ui::ChainView chainView;
    velvox::ui::BeginnerView beginnerView;
    velvox::ui::AssistantView assistantView;
    std::unique_ptr<velvox::ui::DetailPanel> detail;

    velvox::ui::LevelMeter inputMeter, outputMeter;
    velvox::ui::Knob inputGain, outputGain;
    velvox::ui::Toggle safetyClip;
    juce::TextButton monoCheck { "Mono" };
    velvox::ui::CorrelationView correlation;

    bool proMode = false;
    bool assistantOpen = false;
    bool constructed = false;
    int selected = 0;
    int frame = 0;
    velvox::ui::Smoothed ambient;

    juce::TooltipWindow tooltips { this, 1000 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VelvoxAudioProcessorEditor)
};
