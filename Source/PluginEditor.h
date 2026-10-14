#pragma once

#include "PluginProcessor.h"
#include "UI/VelvoxLookAndFeel.h"
#include "UI/TopBar.h"
#include "UI/ChainView.h"
#include "UI/DetailPanels.h"
#include "UI/BeginnerView.h"

// The whole UI is laid out on a fixed design canvas (ui::baseWidth x
// ui::baseHeight) that is scaled uniformly to the window size, so every
// window size keeps the same proportions and stays sharp (vector drawing).
//
//   top bar      presets, A/B, Beginner | Pro, bypass
//   chain view   signal flow, drag & drop routing, parallel sends
//   body         [input meter] [Beginner macros | Pro detail panel] [output meter, mono, correlation]
class VelvoxAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VelvoxAudioProcessorEditor (VelvoxAudioProcessor&);
    ~VelvoxAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void setProMode (bool pro);
    void select (int moduleIdOrOutput);
    std::vector<velvox::ui::Hint> computeHints();

    class Canvas final : public juce::Component
    {
    public:
        void paint (juce::Graphics&) override;
    };

    VelvoxAudioProcessor& proc;
    velvox::ui::VelvoxLookAndFeel lookAndFeel;

    Canvas canvas;
    velvox::ui::DialogOverlay dialog;
    velvox::ui::TopBar topBar;
    velvox::ui::ChainView chainView;
    velvox::ui::BeginnerView beginnerView;
    std::unique_ptr<velvox::ui::DetailPanel> detail;

    velvox::ui::LevelMeter inputMeter, outputMeter;
    velvox::ui::Knob inputGain, outputGain;
    velvox::ui::Toggle safetyClip;
    juce::TextButton monoCheck { "Mono check" };
    velvox::ui::CorrelationView correlation;

    bool proMode = false;
    int selected = 0;

    juce::TooltipWindow tooltips { this, 700 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VelvoxAudioProcessorEditor)
};
