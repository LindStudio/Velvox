#include "PluginEditor.h"
#include "StateKeys.h"

using namespace velvox;
using namespace velvox::ui;

namespace
{
    const juce::Identifier uiWidthProperty { "uiWidth" };
    const juce::Identifier uiSelectionProperty { "uiSelection" };

    // Canvas layout (design units)
    constexpr int topBarH = 64;
    constexpr int margin = 16;
    constexpr int chainTop = 74;
    constexpr int chainH = 90;
    constexpr int bodyTop = chainTop + chainH + 6;
    constexpr int leftW = 78;
    constexpr int rightW = 164;
    constexpr int gap = 12;
    constexpr int frameRate = 60;

    juce::Rectangle<int> leftRail()  { return { margin, bodyTop, leftW, baseHeight - bodyTop - margin }; }
    juce::Rectangle<int> rightRail() { return { baseWidth - margin - rightW, bodyTop, rightW, baseHeight - bodyTop - margin }; }
    juce::Rectangle<int> bodyArea()  { return { margin + leftW + gap, bodyTop, baseWidth - 2 * margin - leftW - rightW - 2 * gap, baseHeight - bodyTop - margin }; }

    // Fade in and lift a freshly shown view into place
    void reveal (juce::Component& c, juce::Rectangle<int> finalBounds)
    {
        c.setAlpha (0.0f);
        c.setBounds (finalBounds.translated (0, 14));
        juce::Desktop::getInstance().getAnimator().animateComponent (&c, finalBounds, 1.0f, 260, false, 0.0, 0.0);
    }
}

//==============================================================================
VelvoxAudioProcessorEditor::Canvas::Canvas()
{
    addChildComponent (info);
}

void VelvoxAudioProcessorEditor::Canvas::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    // Aurora atmosphere: two slow clouds of light above the chain, brighter while the vocal plays
    const float drift = (float) phase;
    const float a = 0.06f + 0.07f * ambient;
    drawGlow (g, { 330.0f + 90.0f * std::sin (drift * 0.7f), -90.0f + 20.0f * std::sin (drift * 0.43f) }, 560.0f, a, colours::teal);
    drawGlow (g, { 960.0f + 100.0f * std::cos (drift * 0.53f), -70.0f + 24.0f * std::cos (drift * 0.37f) }, 600.0f, a * 0.95f, colours::blue);
    drawGlow (g, { baseWidth * 0.5f, -160.0f }, 520.0f, 0.035f);
    drawGlow (g, { baseWidth * 0.5f, (float) baseHeight + 260.0f }, 620.0f, 0.05f, colours::blue);

    for (auto rail : { leftRail(), rightRail() })
    {
        const auto r = rail.toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (r, 16.0f);
        drawGlass (g, r, 16.0f, 0.012f);
    }

    drawCaption (g, "Input", leftRail().toFloat().withHeight (34.0f), juce::Justification::centred, colours::textDim, 9.0f);
    drawCaption (g, "Output", rightRail().toFloat().withHeight (34.0f), juce::Justification::centred, colours::textDim, 9.0f);
}

void VelvoxAudioProcessorEditor::Canvas::showInfo (juce::Component& anchor, const juce::String& title, const juce::String& text)
{
    infoAnchor = &anchor;
    info.show (getLocalArea (&anchor, anchor.getLocalBounds()), title, text);
}

void VelvoxAudioProcessorEditor::Canvas::hideInfo (juce::Component& anchor)
{
    if (infoAnchor == &anchor || infoAnchor == nullptr)
        info.hide();
}

void VelvoxAudioProcessorEditor::Canvas::hideInfoIfAnchorGone()
{
    if (info.isVisible() && (infoAnchor == nullptr || ! infoAnchor->isShowing()))
        info.hide();
}

//==============================================================================
VelvoxAudioProcessorEditor::VelvoxAudioProcessorEditor (VelvoxAudioProcessor& p)
    : AudioProcessorEditor (&p),
      proc (p),
      spectrum (p.getChain().meters.inputTap, p.getChain().meters.outputTap),
      scopeL ((size_t) dsp::StereoTap::capacity), scopeR ((size_t) dsp::StereoTap::capacity),
      topBar (p, dialog),
      chainView (p),
      beginnerView (p),
      inputGain (p.getState(), pid::inputGain, "Gain"),
      outputGain (p.getState(), pid::outputGain, "Gain"),
      safetyClip (p.getState(), pid::safetyClip, "Clip guard")
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (canvas);
    canvas.setBounds (0, 0, baseWidth, baseHeight);

    for (auto* c : std::initializer_list<juce::Component*> { &topBar, &chainView, &beginnerView, &inputMeter, &outputMeter,
                                                              &inputGain, &outputGain, &safetyClip, &monoCheck, &correlation })
        canvas.addAndMakeVisible (c);
    canvas.addChildComponent (dialog);

    beginnerView.setSpectrumSource (&spectrum);
    chainView.setLiveData (&live);

    inputMeter.setTooltip ("Level entering the chain (after input gain), loudness in LUFS");
    outputMeter.setTooltip ("Level leaving the plugin. The red lamp shows overs above 0 dBFS - click it to reset.");
    outputMeter.setScaleOnLeft (false);
    safetyClip.setTooltip ("Soft clipper from -1 dBFS that keeps the output below 0 dBFS");
    correlation.setTooltip ("Phase correlation: +1 = mono-safe, below 0 = parts cancel in mono");
    monoCheck.setClickingTogglesState (true);
    monoCheck.setTooltip ("Listen in mono (not saved with the project)");
    monoCheck.onClick = [this] { proc.setMonoMonitor (monoCheck.getToggleState()); };
    monoCheck.setToggleState (proc.isMonoMonitor(), juce::dontSendNotification);
    inputGain.setMaxDialSize (50);
    outputGain.setMaxDialSize (50);

    topBar.onModeChange = [this] (bool pro) { setProMode (pro); };
    chainView.onSelect = [this] (int id) { select (id); };

    // ---- Layout on the design canvas
    topBar.setBounds (0, 0, baseWidth, topBarH);
    chainView.setBounds (margin, chainTop, baseWidth - 2 * margin, chainH);

    auto left = leftRail().reduced (6, 0).withTrimmedTop (34).withTrimmedBottom (8);
    inputGain.setBounds (left.removeFromBottom (92));
    left.removeFromBottom (10);
    inputMeter.setBounds (left.reduced (2, 0));

    auto right = rightRail().reduced (10, 0).withTrimmedTop (34).withTrimmedBottom (8);
    outputGain.setBounds (right.removeFromBottom (92));
    right.removeFromBottom (10);
    auto buttons = right.removeFromBottom (30);
    monoCheck.setBounds (buttons.removeFromLeft (50));
    buttons.removeFromLeft (6);
    safetyClip.setBounds (buttons);
    right.removeFromBottom (18);
    correlation.setBounds (right.removeFromBottom (26).reduced (6, 0));
    right.removeFromBottom (18);
    outputMeter.setBounds (right.withSizeKeepingCentre (96, right.getHeight()));

    dialog.setBounds (0, 0, baseWidth, baseHeight);

    // ---- View state (stored with the project, never affects audio)
    const auto mode = proc.getState().state.getProperty (statekeys::uiMode, "beginner").toString();
    selected = (int) proc.getState().state.getProperty (uiSelectionProperty, 0);
    chainView.setSelected (selected);
    setProMode (mode == "pro");
    constructed = true;

    // ---- Window: resizable, fixed aspect ratio, remembered per project
    setResizable (true, true);
    setResizeLimits (baseWidth * 3 / 4, baseHeight * 3 / 4, baseWidth * 8 / 5, baseHeight * 8 / 5);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) baseWidth / (double) baseHeight);

    const int savedWidth = (int) proc.getState().state.getProperty (uiWidthProperty, baseWidth);
    const int w = juce::jlimit (baseWidth * 3 / 4, baseWidth * 8 / 5, savedWidth);
    setSize (w, juce::roundToInt (w * (double) baseHeight / baseWidth));

    startTimerHz (frameRate);
}

VelvoxAudioProcessorEditor::~VelvoxAudioProcessorEditor()
{
    stopTimer();
    auto& animator = juce::Desktop::getInstance().getAnimator();
    animator.cancelAnimation (&beginnerView, false);
    if (detail != nullptr)
        animator.cancelAnimation (detail.get(), false);
    setLookAndFeel (nullptr);
}

void VelvoxAudioProcessorEditor::setProMode (bool pro)
{
    const bool changed = pro != proMode;
    proMode = pro;
    topBar.setProMode (pro);
    proc.getState().state.setProperty (statekeys::uiMode, pro ? "pro" : "beginner", nullptr);
    beginnerView.setVisible (! pro);
    beginnerView.setBounds (bodyArea());
    if (! pro && changed && constructed)
        reveal (beginnerView, bodyArea());
    select (selected);
}

void VelvoxAudioProcessorEditor::select (int id)
{
    const bool changed = id != selected || detail == nullptr;
    selected = id;
    proc.getState().state.setProperty (uiSelectionProperty, id, nullptr);
    chainView.setSelected (id);

    if (detail != nullptr)
    {
        juce::Desktop::getInstance().getAnimator().cancelAnimation (detail.get(), false);
        canvas.removeChildComponent (detail.get());
    }
    detail.reset();

    if (proMode)
    {
        detail = createDetailPanel (proc, id, &spectrum, &live);
        canvas.addAndMakeVisible (*detail);
        detail->setBounds (bodyArea());
        detail->refresh();
        if (constructed && changed)
            reveal (*detail, bodyArea());
    }

    canvas.info.toFront (false);
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

// Pulls every meter exactly once per frame; all views read the shared result
void VelvoxAudioProcessorEditor::updateLiveData()
{
    auto& chain = proc.getChain();
    auto& m = chain.meters;

    const float inL = m.input[0].pull(), inR = m.input[1].pull();
    const float outL = m.output[0].pull(), outR = m.output[1].pull();
    inputMeter.push (inL, inR, false);
    outputMeter.push (outL, outR, m.clipped.exchange (false));
    inputMeter.setLoudness (m.inputLufs.load());
    outputMeter.setLoudness (m.outputLufs.load());

    live.inputDb.push (juce::Decibels::gainToDecibels (std::max (inL, inR), -100.0f));
    live.outputDb.push (juce::Decibels::gainToDecibels (std::max (outL, outR), -100.0f));
    live.compGr.push (chain.getCompressor().gainReductionDb.pull());
    live.deessGr.push (chain.getDeEsser().gainReductionDb.pull());
    live.delayDuck.push (chain.getDelayDucker().gainReductionDb.pull());
    live.reverbDuck.push (chain.getReverbDucker().gainReductionDb.pull());
    live.inputLufs.push (m.inputLufs.load());
    live.outputLufs.push (m.outputLufs.load());

    // Goniometer: keep the newest samples
    const int n = m.scopeTap.pull (scopeL.data(), scopeR.data(), (int) scopeL.size());
    if (n > 0)
    {
        constexpr int size = LiveData::scopeSize;
        const int take = std::min (n, size);
        std::move (live.scopeL.begin() + take, live.scopeL.end(), live.scopeL.begin());
        std::move (live.scopeR.begin() + take, live.scopeR.end(), live.scopeR.begin());
        std::copy_n (scopeL.data() + n - take, take, live.scopeL.data() + size - take);
        std::copy_n (scopeR.data() + n - take, take, live.scopeR.data() + size - take);
    }

    live.settings = proc.readEffectiveSettings();
    live.time = juce::Time::getMillisecondCounterHiRes() * 0.001;
    live.sampleRate = proc.getSampleRate() > 0.0 ? proc.getSampleRate() : 48000.0;
    live.effectiveDelayMs = m.effectiveDelayMs.load();
    live.chopStep = chain.getChopStep();
    live.hasSignal = m.inputLufs.load() > -70.0f;

    spectrum.update (live.sampleRate);
    correlation.push (m.correlation.load());
}

void VelvoxAudioProcessorEditor::timerCallback()
{
    updateLiveData();
    ++frame;
    canvas.hideInfoIfAnchorGone();

    topBar.refresh();
    chainView.refresh();

    if (detail != nullptr)
        detail->refresh();
    if (beginnerView.isVisible())
    {
        beginnerView.refreshSpectrum();
        if (frame % 4 == 0)
            beginnerView.setHints (computeHints());
    }

    // Atmosphere: drifts slowly, breathes with the vocal; only the sky above the body is redrawn
    ambient.target = live.level();
    ambient.tick (0.05f);
    canvas.phase = live.time * 0.25;
    if (frame % 3 == 0)
    {
        canvas.ambient = ambient.current;
        canvas.repaint (0, 0, baseWidth, bodyTop);
    }

    if (monoCheck.getToggleState() != proc.isMonoMonitor())
        monoCheck.setToggleState (proc.isMonoMonitor(), juce::dontSendNotification);
}
