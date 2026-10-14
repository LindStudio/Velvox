#include "PluginEditor.h"

using namespace velvox;
using namespace velvox::ui;

namespace
{
    const juce::Identifier uiWidthProperty { "uiWidth" };

    // Canvas layout (design units)
    constexpr int topBarH = 60;
    constexpr int chainH = 48;
    constexpr int margin = 16;
    constexpr int gap = 12;
    constexpr int sideW = 92;
    constexpr int topRowH = 322;

    juce::Rectangle<int> sideColumnTitle (juce::Rectangle<int>& col)
    {
        return col.removeFromTop (22);
    }
}

void VelvoxAudioProcessorEditor::Canvas::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    // Side column captions
    g.setColour (colours::textDim);
    g.setFont (font (13.0f, true));
    const int top = topBarH + chainH + 4;
    g.drawText ("Input", juce::Rectangle<int> (margin, top, sideW, 22), juce::Justification::centred, false);
    g.drawText ("Output", juce::Rectangle<int> (baseWidth - margin - sideW, top, sideW, 22), juce::Justification::centred, false);
}

VelvoxAudioProcessorEditor::VelvoxAudioProcessorEditor (VelvoxAudioProcessor& p)
    : AudioProcessorEditor (&p),
      proc (p),
      topBar (p, dialog),
      chainStrip (p.getState()),
      eqPanel (p.getState()),
      compPanel (p.getState()),
      deessPanel (p.getState()),
      satPanel (p.getState()),
      delayPanel (p.getState()),
      reverbPanel (p.getState()),
      inputGain (p.getState(), pid::inputGain, "Gain", colours::io),
      outputGain (p.getState(), pid::outputGain, "Gain", colours::io),
      safetyClip (p.getState(), pid::safetyClip, "Clip guard", colours::io)
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (canvas);
    canvas.setBounds (0, 0, baseWidth, baseHeight);

    for (auto* c : std::initializer_list<juce::Component*> { &topBar, &chainStrip, &eqPanel, &compPanel, &deessPanel, &satPanel,
                                                              &delayPanel, &reverbPanel, &inputMeter, &outputMeter,
                                                              &inputGain, &outputGain, &safetyClip })
        canvas.addAndMakeVisible (c);
    canvas.addChildComponent (dialog);

    inputMeter.setTooltip ("Level entering the chain (after input gain)");
    outputMeter.setTooltip ("Output level. The red lamp shows overs above 0 dBFS - click it to reset.");
    safetyClip.setTooltip ("Soft clipper from -1 dBFS that keeps the output below 0 dBFS");

    // Layout on the design canvas
    const int contentTop = topBarH + chainH;
    topBar.setBounds (0, 0, baseWidth, topBarH);
    chainStrip.setBounds (margin + sideW, topBarH, baseWidth - 2 * (margin + sideW), chainH);

    auto main = juce::Rectangle<int> (margin + sideW + gap, contentTop, baseWidth - 2 * (margin + sideW + gap), baseHeight - contentTop - margin);
    auto topRow = main.removeFromTop (topRowH);
    main.removeFromTop (gap);
    const int eqW = (int) ((topRow.getWidth() - gap) * 0.64f);
    eqPanel.setBounds (topRow.removeFromLeft (eqW));
    topRow.removeFromLeft (gap);
    compPanel.setBounds (topRow);

    const int bottomW = (main.getWidth() - 3 * gap) / 4;
    for (auto* panel : std::initializer_list<juce::Component*> { &deessPanel, &satPanel, &delayPanel, &reverbPanel })
    {
        panel->setBounds (main.removeFromLeft (bottomW));
        main.removeFromLeft (gap);
    }

    auto layoutSide = [&] (juce::Rectangle<int> col, LevelMeter& meter, Knob& knob, juce::Component* extra)
    {
        sideColumnTitle (col);
        knob.setBounds (col.removeFromBottom (92));
        if (extra != nullptr)
        {
            col.removeFromBottom (6);
            extra->setBounds (col.removeFromBottom (24));
        }
        col.removeFromBottom (10);
        meter.setBounds (col.reduced (14, 0));
    };

    const auto sideArea = juce::Rectangle<int> (0, contentTop + 4, sideW, baseHeight - contentTop - margin - 4);
    layoutSide (sideArea.withX (margin), inputMeter, inputGain, nullptr);
    layoutSide (sideArea.withX (baseWidth - margin - sideW), outputMeter, outputGain, &safetyClip);

    dialog.setBounds (0, 0, baseWidth, baseHeight);

    // Window size: resizable with fixed aspect ratio, remembered per project
    setResizable (true, true);
    setResizeLimits (baseWidth * 3 / 4, baseHeight * 3 / 4, baseWidth * 8 / 5, baseHeight * 8 / 5);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) baseWidth / (double) baseHeight);

    const int savedWidth = (int) proc.getState().state.getProperty (uiWidthProperty, baseWidth);
    const int w = juce::jlimit (baseWidth * 3 / 4, baseWidth * 8 / 5, savedWidth);
    setSize (w, juce::roundToInt (w * (double) baseHeight / baseWidth));

    startTimerHz (30);
}

VelvoxAudioProcessorEditor::~VelvoxAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void VelvoxAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
}

void VelvoxAudioProcessorEditor::resized()
{
    const float scale = (float) getWidth() / (float) baseWidth;
    canvas.setTransform (juce::AffineTransform::scale (scale));
    proc.getState().state.setProperty (uiWidthProperty, getWidth(), nullptr);
}

void VelvoxAudioProcessorEditor::timerCallback()
{
    auto& chain = proc.getChain();

    inputMeter.push (chain.meters.input[0].pull(), chain.meters.input[1].pull(), false);
    outputMeter.push (chain.meters.output[0].pull(), chain.meters.output[1].pull(),
                      chain.meters.clipped.exchange (false));

    topBar.refresh();
    chainStrip.refresh();

    for (auto* panel : std::initializer_list<ModulePanel*> { &eqPanel, &compPanel, &deessPanel, &satPanel, &delayPanel, &reverbPanel })
        panel->refresh (proc);
}
