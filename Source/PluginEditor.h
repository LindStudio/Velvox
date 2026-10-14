#pragma once

#include "PluginProcessor.h"
#include "UI/VelvoxLookAndFeel.h"
#include "UI/ModulePanels.h"
#include "UI/TopBar.h"

// The whole UI is laid out on a fixed design canvas (ui::baseWidth x
// ui::baseHeight) that is scaled uniformly to the window size, so every
// window size keeps the same proportions and stays sharp (vector drawing).
class VelvoxAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VelvoxAudioProcessorEditor (VelvoxAudioProcessor&);
    ~VelvoxAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

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
    velvox::ui::ChainStrip chainStrip;

    velvox::ui::EqPanel eqPanel;
    velvox::ui::CompressorPanel compPanel;
    velvox::ui::DeEsserPanel deessPanel;
    velvox::ui::SaturationPanel satPanel;
    velvox::ui::DelayPanel delayPanel;
    velvox::ui::ReverbPanel reverbPanel;

    velvox::ui::LevelMeter inputMeter, outputMeter;
    velvox::ui::Knob inputGain, outputGain;
    velvox::ui::Toggle safetyClip;

    juce::TooltipWindow tooltips { this, 700 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VelvoxAudioProcessorEditor)
};
