#include "PluginEditor.h"
#include "StateKeys.h"
#include "UI/UserSettings.h"
#include "UI/HelpContent.h"

using namespace velvox;
using namespace velvox::ui;

namespace
{
    // Window width in pixels. A new key for the 1440 x 900 layout, so projects
    // saved with the old, smaller canvas open at the new default size.
    const juce::Identifier uiWidthProperty { "uiWindowWidth" };
    const juce::Identifier uiSelectionProperty { "uiSelection" };
    const juce::Identifier uiAssistantProperty { "uiAssistant" };

    // "Remind me later" hides the welcome card until the plugin is loaded again
    bool tutorialPostponedThisSession = false;

    // Canvas layout (design units): header, module navigation, then the body
    // with the input rail, the selected module and the output rail
    constexpr int topBarH = 68;
    constexpr int margin = 16;
    constexpr int chainTop = topBarH + 8;
    constexpr int chainH = ChainView::preferredHeight;
    constexpr int bodyTop = chainTop + chainH + 8;
    constexpr int leftW = 150;
    constexpr int rightW = 150;
    constexpr int gap = 14;
    constexpr int railCaptionH = 40;
    constexpr int frameRate = 60;

    // Window size limits relative to the design canvas
    constexpr double minScale = 0.7, maxScale = 1.6;

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

    // Atmosphere: a faint, slow light above the header that breathes with the vocal
    const float drift = (float) phase;
    const float a = 0.025f + 0.035f * ambient;
    drawGlow (g, { 380.0f + 90.0f * std::sin (drift * 0.7f), -120.0f + 20.0f * std::sin (drift * 0.43f) }, 560.0f, a, colours::cyan);
    drawGlow (g, { 1080.0f + 100.0f * std::cos (drift * 0.53f), -110.0f + 24.0f * std::cos (drift * 0.37f) }, 600.0f, a * 0.8f, colours::blue);

    for (auto rail : { leftRail(), rightRail() })
    {
        const auto r = rail.toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (r, radius::panel);
        drawGlass (g, r, radius::panel, 0.012f);
    }

    for (auto [rail, text] : { std::pair (leftRail(), "Input"), std::pair (rightRail(), "Output") })
    {
        g.setColour (colours::text);
        g.setFont (font (15.0f, true));
        g.drawText (text, rail.toFloat().withHeight ((float) railCaptionH).withTrimmedTop (4.0f), juce::Justification::centred, false);
    }
}

void VelvoxAudioProcessorEditor::Canvas::paintOverChildren (juce::Graphics& g)
{
    if (! bypassed)
        return;

    // Global bypass: amber frame around everything and a label that cannot be missed
    const auto border = getLocalBounds().toFloat().reduced (1.5f);
    g.setColour (colours::bypass.withAlpha (0.8f));
    g.drawRoundedRectangle (border, 6.0f, 2.0f);

    const juce::String text ("Bypassed  -  you hear the original vocal");
    const float w = (float) juce::GlyphArrangement::getStringWidthInt (caps (captionSize (9.5f)), text) + 40.0f;
    const auto pill = juce::Rectangle<float> (w, 24.0f).withCentre ({ baseWidth * 0.5f, (float) chainTop - 4.0f });
    g.setColour (colours::background);
    g.fillRoundedRectangle (pill, 12.0f);
    g.setColour (colours::bypass.withAlpha (0.18f));
    g.fillRoundedRectangle (pill, 12.0f);
    g.setColour (colours::bypass.withAlpha (0.8f));
    g.drawRoundedRectangle (pill.reduced (0.5f), 12.0f, 1.0f);
    drawCaption (g, text, pill, juce::Justification::centred, colours::bypass, 9.5f);
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
      assistantView (p),
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
    canvas.addChildComponent (assistantView);
    canvas.addChildComponent (dialog);
    canvas.addChildComponent (help);
    canvas.addChildComponent (tutorial);

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
    inputGain.setMaxDialSize (dialsize::rail);
    outputGain.setMaxDialSize (dialsize::rail);

    topBar.onModeChange = [this] (bool pro) { setProMode (pro); };
    topBar.onAssistant = [this] { setAssistantOpen (true); };
    topBar.onHelp = [this]
    {
        showHelp (assistantOpen ? juce::String ("assistant")
                                : (detail != nullptr && proMode ? help::keyForModule (selected) : juce::String ("start")));
    };
    assistantView.onOpenModule = [this] (int id)
    {
        // "Modul oeffnen" from a suggestion: show that module with all its controls
        selected = id;
        setProMode (true);
    };
    chainView.onSelect = [this] (int id)
    {
        if (assistantOpen)
        {
            // A module tab closes the assistant and shows the module in the current view
            selected = id;
            setProMode (proMode);
            if (! proMode)
                beginnerView.showModule (id);
            return;
        }
        select (id);
        if (! proMode)
            beginnerView.showModule (id);
    };
    chainView.onHelp = [this] (int id) { showHelp (help::keyForModule (id)); };
    beginnerView.onHelp = [this] (int id) { showHelp (help::keyForModule (id)); };
    beginnerView.onEditInPro = [this] (int id)
    {
        selected = id;
        setProMode (true);
    };
    help.onStartTutorial = [this] { startTutorial (false); };
    tutorial.targetFor = [this] (const juce::String& key) { return tutorialTarget (key); };
    tutorial.onClose = [] (TutorialOverlay::Outcome outcome)
    {
        if (outcome == TutorialOverlay::Outcome::later)
            tutorialPostponedThisSession = true;
        else
            settings::set (settings::tutorialDone, true);
    };

    // ---- Layout on the design canvas
    topBar.setBounds (0, 0, baseWidth, topBarH);
    chainView.setBounds (margin, chainTop, baseWidth - 2 * margin, chainH);

    auto left = leftRail().reduced (14, 0).withTrimmedTop (railCaptionH).withTrimmedBottom (12);
    inputGain.setBounds (left.removeFromBottom (128));
    left.removeFromBottom (12);
    inputMeter.setBounds (left);

    auto right = rightRail().reduced (14, 0).withTrimmedTop (railCaptionH).withTrimmedBottom (12);
    outputGain.setBounds (right.removeFromBottom (128));
    right.removeFromBottom (12);
    safetyClip.setBounds (right.removeFromBottom (34));
    right.removeFromBottom (8);
    monoCheck.setBounds (right.removeFromBottom (34));
    right.removeFromBottom (16);
    correlation.setBounds (right.removeFromBottom (32).reduced (4, 0));
    right.removeFromBottom (14);
    outputMeter.setBounds (right);

    dialog.setBounds (0, 0, baseWidth, baseHeight);
    help.setBounds (0, 0, baseWidth, baseHeight);
    tutorial.setBounds (0, 0, baseWidth, baseHeight);

    // ---- View state (stored with the project, never affects audio)
    const auto mode = proc.getState().state.getProperty (statekeys::uiMode, "beginner").toString();
    selected = (int) proc.getState().state.getProperty (uiSelectionProperty, 0);
    chainView.setSelected (selected);
    setProMode (mode == "pro");
    if ((bool) proc.getState().state.getProperty (uiAssistantProperty, false))
        setAssistantOpen (true);
    constructed = true;

    // ---- Window: resizable, fixed aspect ratio, remembered per project.
    // The default is the full design size when the screen has room for it,
    // otherwise the largest size that fits (Windows display scaling included:
    // the display area is in logical pixels).
    const int minW = juce::roundToInt (baseWidth * minScale), maxW = juce::roundToInt (baseWidth * maxScale);
    setResizable (true, true);
    setResizeLimits (minW, juce::roundToInt (baseHeight * minScale), maxW, juce::roundToInt (baseHeight * maxScale));
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) baseWidth / (double) baseHeight);

    int fitW = baseWidth;
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto area = display->userArea;
        fitW = (int) std::min (area.getWidth() * 0.94, area.getHeight() * 0.88 * baseWidth / (double) baseHeight);
    }
    const int defaultW = juce::jlimit (minW, baseWidth, fitW);
    const int savedWidth = (int) proc.getState().state.getProperty (uiWidthProperty, defaultW);
    const int w = juce::jlimit (minW, maxW, savedWidth);
    setSize (w, juce::roundToInt (w * (double) baseHeight / baseWidth));

    startTimerHz (frameRate);

    // First start: welcome card with the offer of a short tour
    if (! settings::getBool (settings::tutorialDone) && ! tutorialPostponedThisSession)
        startTutorial (true);
}

VelvoxAudioProcessorEditor::~VelvoxAudioProcessorEditor()
{
    stopTimer();
    auto& animator = juce::Desktop::getInstance().getAnimator();
    animator.cancelAnimation (&beginnerView, false);
    animator.cancelAnimation (&assistantView, false);
    if (detail != nullptr)
        animator.cancelAnimation (detail.get(), false);
    setLookAndFeel (nullptr);
}

void VelvoxAudioProcessorEditor::setAssistantOpen (bool open)
{
    if (! open)
    {
        setProMode (proMode);
        return;
    }

    assistantOpen = true;
    proc.getState().state.setProperty (uiAssistantProperty, true, nullptr);
    topBar.setAssistantOpen (true);
    chainView.setSelected (-2);   // no module card is "open" while the assistant is shown
    beginnerView.setVisible (false);
    if (detail != nullptr)
    {
        juce::Desktop::getInstance().getAnimator().cancelAnimation (detail.get(), false);
        canvas.removeChildComponent (detail.get());
        detail.reset();
    }
    assistantView.setVisible (true);
    if (constructed)
        reveal (assistantView, bodyArea());
    else
        assistantView.setBounds (bodyArea());
    assistantView.refresh();
    raiseOverlays();
}

void VelvoxAudioProcessorEditor::setProMode (bool pro)
{
    const bool wasAssistant = assistantOpen;
    if (assistantOpen)
    {
        assistantOpen = false;
        proc.getState().state.setProperty (uiAssistantProperty, false, nullptr);
        juce::Desktop::getInstance().getAnimator().cancelAnimation (&assistantView, false);
        assistantView.setVisible (false);
    }
    const bool changed = pro != proMode || wasAssistant;
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

    if (proMode && ! assistantOpen)
    {
        detail = createDetailPanel (proc, id, &spectrum, &live);
        // Open another module (e.g. from a VOCAL CHECK finding) after the current click has finished
        detail->onOpenModule = [safe = juce::Component::SafePointer<VelvoxAudioProcessorEditor> (this)] (int moduleId)
        {
            juce::MessageManager::callAsync ([safe, moduleId] { if (safe != nullptr) safe->select (moduleId); });
        };
        detail->onHelp = [this] (int id) { showHelp (help::keyForModule (id)); };
        canvas.addAndMakeVisible (*detail);
        detail->setBounds (bodyArea());
        detail->refresh();
        if (constructed && changed)
            reveal (*detail, bodyArea());
    }

    raiseOverlays();
}

void VelvoxAudioProcessorEditor::raiseOverlays()
{
    canvas.info.toFront (false);
    dialog.toFront (false);
    help.toFront (false);
    tutorial.toFront (false);
}

void VelvoxAudioProcessorEditor::showHelp (const juce::String& pageKey)
{
    help.open (pageKey);
    raiseOverlays();
}

void VelvoxAudioProcessorEditor::startTutorial (bool withWelcome)
{
    help.close();
    tutorial.start (withWelcome);
    raiseOverlays();
}

juce::Rectangle<int> VelvoxAudioProcessorEditor::tutorialTarget (const juce::String& key)
{
    // Top bar sits at the canvas origin, so its child bounds are canvas coordinates
    if (key == "presets") return topBar.getPresetArea();
    if (key == "global")  return topBar.getCompareArea().getUnion (topBar.getBypassArea());
    if (key == "compare") return topBar.getCompareArea().getUnion (topBar.getBypassArea());
    if (key == "modes")   return topBar.getModeArea();
    if (key == "help")    return topBar.getHelpButton().getBounds();
    if (key == "body")    return bodyArea();
    return chainView.getBounds();   // "chain", "chainAndModules"
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
    live.smashGr.push (chain.getSmash().gainReductionDb.pull());
    live.rideGain.push (chain.getRide().currentGainDb.load());
    live.rideLevel.push (chain.getRide().detectedLevelDb.load());
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
    if (assistantOpen && frame % 6 == 0)
        assistantView.refresh();
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

    const bool bypassed = proc.getState().getRawParameterValue (pid::bypass)->load() > 0.5f;
    if (bypassed != canvas.bypassed)
    {
        canvas.bypassed = bypassed;
        canvas.repaint();
    }

    if (monoCheck.getToggleState() != proc.isMonoMonitor())
        monoCheck.setToggleState (proc.isMonoMonitor(), juce::dontSendNotification);
}
