#include "PluginEditor.h"
#include "StateKeys.h"

using namespace velvox;
using namespace velvox::ui;

namespace
{
    const juce::Identifier uiWidthProperty { "uiWidth" };
    const juce::Identifier uiSelectionProperty { "uiSelection" };

    // Canvas layout (design units)
    constexpr int topBarH = 60;
    constexpr int margin = 16;
    constexpr int chainTop = 70;
    constexpr int chainH = 104;
    constexpr int bodyTop = chainTop + chainH + 10;
    constexpr int leftW = 92;
    constexpr int rightW = 170;
    constexpr int gap = 12;
}

void VelvoxAudioProcessorEditor::Canvas::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    // Faint neon haze behind the chain - the one decorative gesture
    g.setGradientFill (juce::ColourGradient (colours::violet.withAlpha (0.10f), (float) baseWidth * 0.5f, (float) chainTop + chainH * 0.5f,
                                             colours::background.withAlpha (0.0f), (float) baseWidth * 0.5f, (float) bodyTop + 120.0f, true));
    g.fillRect (0, topBarH, baseWidth, bodyTop + 60 - topBarH);

    g.setColour (colours::textDim);
    g.setFont (font (12.5f, true));
    g.drawText ("Input", juce::Rectangle<int> (margin, bodyTop, leftW, 20), juce::Justification::centred, false);
    g.drawText ("Output", juce::Rectangle<int> (baseWidth - margin - rightW, bodyTop, rightW, 20), juce::Justification::centred, false);
}

VelvoxAudioProcessorEditor::VelvoxAudioProcessorEditor (VelvoxAudioProcessor& p)
    : AudioProcessorEditor (&p),
      proc (p),
      topBar (p, dialog),
      chainView (p),
      beginnerView (p),
      inputGain (p.getState(), pid::inputGain, "Gain", colours::io),
      outputGain (p.getState(), pid::outputGain, "Gain", colours::io),
      safetyClip (p.getState(), pid::safetyClip, "Clip guard", colours::cyan)
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (canvas);
    canvas.setBounds (0, 0, baseWidth, baseHeight);

    for (auto* c : std::initializer_list<juce::Component*> { &topBar, &chainView, &beginnerView, &inputMeter, &outputMeter,
                                                              &inputGain, &outputGain, &safetyClip, &monoCheck, &correlation })
        canvas.addAndMakeVisible (c);
    canvas.addChildComponent (dialog);

    inputMeter.setTooltip ("Level entering the chain (after input gain), loudness in LUFS");
    outputMeter.setTooltip ("Level leaving the plugin. The red lamp shows overs above 0 dBFS - click it to reset.");
    safetyClip.setTooltip ("Soft clipper from -1 dBFS that keeps the output below 0 dBFS");
    correlation.setTooltip ("Phase correlation: +1 = mono-safe, below 0 = parts cancel in mono");
    monoCheck.setClickingTogglesState (true);
    monoCheck.setTooltip ("Listen in mono (not saved with the project)");
    monoCheck.onClick = [this] { proc.setMonoMonitor (monoCheck.getToggleState()); };
    monoCheck.setToggleState (proc.isMonoMonitor(), juce::dontSendNotification);

    topBar.onModeChange = [this] (bool pro) { setProMode (pro); };
    chainView.onSelect = [this] (int id) { select (id); };

    // ---- Layout on the design canvas
    topBar.setBounds (0, 0, baseWidth, topBarH);
    chainView.setBounds (margin, chainTop, baseWidth - 2 * margin, chainH);

    auto left = juce::Rectangle<int> (margin, bodyTop + 22, leftW, baseHeight - bodyTop - 22 - margin);
    inputGain.setBounds (left.removeFromBottom (96));
    left.removeFromBottom (8);
    inputMeter.setBounds (left.reduced (10, 0));

    auto right = juce::Rectangle<int> (baseWidth - margin - rightW, bodyTop + 22, rightW, baseHeight - bodyTop - 22 - margin);
    outputGain.setBounds (right.removeFromBottom (96));
    right.removeFromBottom (6);
    safetyClip.setBounds (right.removeFromBottom (28).reduced (18, 0));
    right.removeFromBottom (8);
    monoCheck.setBounds (right.removeFromBottom (28).reduced (18, 0));
    right.removeFromBottom (8);
    correlation.setBounds (right.removeFromBottom (34).reduced (14, 0));
    right.removeFromBottom (10);
    outputMeter.setBounds (right.withSizeKeepingCentre (84, right.getHeight()));

    dialog.setBounds (0, 0, baseWidth, baseHeight);

    // ---- View state (stored with the project, never affects audio)
    const auto mode = proc.getState().state.getProperty (statekeys::uiMode, "beginner").toString();
    selected = (int) proc.getState().state.getProperty (uiSelectionProperty, 0);
    chainView.setSelected (selected);
    setProMode (mode == "pro");

    // ---- Window: resizable, fixed aspect ratio, remembered per project
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

void VelvoxAudioProcessorEditor::setProMode (bool pro)
{
    proMode = pro;
    topBar.setProMode (pro);
    proc.getState().state.setProperty (statekeys::uiMode, pro ? "pro" : "beginner", nullptr);
    beginnerView.setVisible (! pro);
    select (selected);
}

void VelvoxAudioProcessorEditor::select (int id)
{
    selected = id;
    proc.getState().state.setProperty (uiSelectionProperty, id, nullptr);
    chainView.setSelected (id);

    if (detail != nullptr)
        canvas.removeChildComponent (detail.get());
    detail.reset();

    const auto body = juce::Rectangle<int> (margin + leftW + gap, bodyTop, baseWidth - 2 * margin - leftW - rightW - 2 * gap,
                                            baseHeight - bodyTop - margin);
    beginnerView.setBounds (body);

    if (proMode)
    {
        detail = createDetailPanel (proc, id);
        canvas.addAndMakeVisible (*detail);
        detail->setBounds (body);
        detail->refresh();
    }

    dialog.toFront (false);
}

std::vector<Hint> VelvoxAudioProcessorEditor::computeHints()
{
    std::vector<Hint> hints;
    const auto& m = proc.getChain().meters;
    const float inLufs = m.inputLufs.load();
    const bool signal = inLufs > -70.0f;

    if (inputMeter.getPeakDb() > -0.5f)
        hints.push_back ({ Hint::Level::warning, "Input is clipping - turn Input gain down until the meter stays below 0." });
    else if (signal && inLufs < -36.0f)
        hints.push_back ({ Hint::Level::info, "Input is quiet - raise Input gain so the controls react properly (about -20 LUFS is ideal)." });

    if (outputMeter.isClipLatched())
        hints.push_back ({ Hint::Level::warning, "Output went over 0 dBFS - lower Output gain or keep Clip guard on (click the red lamp to reset)." });

    if (signal && m.correlation.load() < 0.0f)
        hints.push_back ({ Hint::Level::warning, "Mono check: parts of the sound cancel in mono - reduce Width or raise the doubler's Mono safe." });

    if (proc.getState().getRawParameterValue (pid::pitchOn)->load() > 0.5f)
        hints.push_back ({ Hint::Level::info, "Pitch is on and adds latency; FL Studio compensates it automatically." });

    if (proc.getState().getRawParameterValue (pid::levelMatchMode)->load() < 0.5f)
        hints.push_back ({ Hint::Level::info, "Tip: set Fair compare to Bypass match, then use Bypass to hear the difference at equal loudness." });
    else if (signal)
        hints.push_back ({ Hint::Level::good, "Fair compare is on: Bypass plays at the same loudness as the processed vocal." });

    if (! signal)
        hints.insert (hints.begin(), { Hint::Level::info, "Play your vocal to see levels and live tips." });

    if (hints.size() > 4)
        hints.resize (4);
    return hints;
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
    auto& m = chain.meters;

    inputMeter.push (m.input[0].pull(), m.input[1].pull(), false);
    inputMeter.setLoudness (m.inputLufs.load());
    outputMeter.push (m.output[0].pull(), m.output[1].pull(), m.clipped.exchange (false));
    outputMeter.setLoudness (m.outputLufs.load());
    correlation.push (m.correlation.load());

    topBar.refresh();
    chainView.refresh();

    if (detail != nullptr)
        detail->refresh();
    if (beginnerView.isVisible())
        beginnerView.setHints (computeHints());

    if (monoCheck.getToggleState() != proc.isMonoMonitor())
        monoCheck.setToggleState (proc.isMonoMonitor(), juce::dontSendNotification);
}
