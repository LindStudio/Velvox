#include "PluginEditor.h"
#include "StateKeys.h"
#include "UI/UserSettings.h"
#include "UI/HelpContent.h"

using namespace velvox;
using namespace velvox::ui;

namespace
{
    // Window width in pixels (key of the 1440 x 900 layout)
    // Window size as a scale of the design canvas (0.4 stored a pixel width under "uiWindowWidth")
    const juce::Identifier uiScaleProperty { "uiScale" };
    const juce::Identifier uiSelectionProperty { "uiSelection" };

    // "Remind me later" hides the welcome card until the plugin is loaded again
    bool tutorialPostponedThisSession = false;

    // Canvas layout of the design (1384 x 1040): header 56, module chain 330, detail area
    // 460 (module panel 1180 wide, IN/OUT meters to its right), Vocal Control below
    constexpr int topBarH = TopBar::height;
    constexpr int margin = 12;
    constexpr int gap = 8;
    constexpr int chainTop = topBarH + gap;
    constexpr int chainH = 330;
    constexpr int bodyTop = chainTop + chainH + gap;
    constexpr int bodyH = 460;
    constexpr int panelW = 1180;
    constexpr int controlTop = bodyTop + bodyH + gap;
    constexpr int frameRate = 60;

    // Window size limits relative to the design canvas
    constexpr double minScale = 0.8, maxScale = 1.4;   // below 0.8 the 9 px knob names of the design drop under 7 px

    juce::Rectangle<int> chainArea()   { return { margin, chainTop, baseWidth - 2 * margin, chainH }; }
    juce::Rectangle<int> bodyArea()    { return { margin, bodyTop, panelW, bodyH }; }
    juce::Rectangle<int> meterArea()   { return { margin + panelW + gap, bodyTop, baseWidth - 2 * margin - panelW - gap, bodyH }; }
    juce::Rectangle<int> controlArea() { return { margin, controlTop, baseWidth - 2 * margin, baseHeight - controlTop - margin }; }
    juce::Rectangle<int> wideArea()    { return chainArea().getUnion (bodyArea()).getUnion (meterArea()); }   // AUTO MIX, VOCAL CHECK

    void drawDesignPanel (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (colours::panel);
        g.fillRoundedRectangle (r, 12.0f);
        g.setColour (colours::border);
        g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);
    }

    // Fade in and lift a freshly shown view into place
    void reveal (juce::Component& c, juce::Rectangle<int> finalBounds)
    {
        c.setAlpha (0.0f);
        c.setBounds (finalBounds.translated (0, 14));
        juce::Desktop::getInstance().getAnimator().animateComponent (&c, finalBounds, 1.0f, motion::revealMs, false, 0.0, 0.0);
    }
}

//==============================================================================
VelvoxAudioProcessorEditor::Canvas::Canvas()
{
    addChildComponent (info);
    setOpaque (true);
}

void VelvoxAudioProcessorEditor::Canvas::paint (juce::Graphics& g)
{
    // The atmosphere is static: coloured light behind the glass panels, rendered
    // once per physical size and then only blitted (no idle redraws, no CPU load)
    const float scale = juce::jlimit (0.5f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (backdrop.isNull() || std::abs (scale - backdropScale) > 0.01f)
    {
        backdropScale = scale;
        backdrop = juce::Image (juce::Image::RGB, juce::roundToInt (baseWidth * scale), juce::roundToInt (baseHeight * scale), false);
        juce::Graphics b (backdrop);
        b.addTransform (juce::AffineTransform::scale (scale));
        b.fillAll (colours::windowBg);
        drawGlow (b, { 690.0f, -120.0f }, 640.0f, 0.07f, colours::blue);
        drawDesignPanel (b, controlArea().toFloat());
    }
    g.drawImage (backdrop, juce::Rectangle<float> ((float) baseWidth, (float) baseHeight));
    if (showMeterPanel)
        drawDesignPanel (g, meterArea().toFloat());
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

void VelvoxAudioProcessorEditor::Canvas::showInfo (juce::Component& anchor, const juce::String& title, const juce::String& value, const juce::String& text)
{
    infoAnchor = &anchor;
    info.show (getLocalArea (&anchor, anchor.getLocalBounds()), title, value, text);
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
      browser (p),
      chainView (p),
      mixView (p),
      analysis (p.getAutoMix()),
      vocalControl (p, live),
      safetyClip (p.getState(), pid::safetyClip, "Clip guard")
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (canvas);
    canvas.setBounds (0, 0, baseWidth, baseHeight);

    for (auto* c : std::initializer_list<juce::Component*> { &topBar, &chainView, &mixView, &inputMeter, &outputMeter,
                                                              &vocalControl, &safetyClip, &monoCheck, &correlation })
        canvas.addAndMakeVisible (c);
    canvas.addChildComponent (analysis);
    canvas.addChildComponent (dialog);
    canvas.addChildComponent (help);
    canvas.addChildComponent (tutorial);
    canvas.addChildComponent (browser);

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

    topBar.onViewChange = [this] (TopBar::View v) { setView (v); };
    topBar.onHelp = [this]
    {
        showHelp (detail != nullptr && proMode ? help::keyForModule (selected < 0 ? selected : (int) dsp::selectionModule (selected)) : juce::String ("start"));
    };
    mixView.onAnalyse = [this] { startAnalysis(); };
    mixView.onOpenModule = [this] (int id) { openModule (id); };
    analysis.onFinished = [this] { setView (TopBar::View::autoMix); };

    chainView.onSelect = [this] (int id)
    {
        // A module card always opens that module with all its controls
        if (! proMode)
            openModule (id);
        else
            select (id);
    };
    chainView.onHelp = [this] (int code) { showHelp (help::keyForModule (code < 0 ? code : (int) dsp::selectionModule (code))); };
    chainView.onAddModule = [this] (int insertIndex, bool atEnd)
    {
        help.close();
        browser.open (insertIndex, atEnd);
        raiseOverlays();
    };
    browser.onChoose = [this] (dsp::ModuleId id, int insertIndex, bool atEnd)
    {
        // Added behind the chain, delay and reverb start as parallel sends (the usual vocal setup)
        chainView.insertModule (id, atEnd ? -1 : insertIndex, atEnd && dsp::Routing::canBeSend (id));
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
    chainView.setBounds (chainArea());

    // IN / OUT meters next to the module panel; clip guard, mono and correlation below.
    // The levels themselves: input gain in the OUTPUT panel, output = VOCAL LEVEL.
    auto meters = meterArea().reduced (10, 14);
    correlation.setBounds (meters.removeFromBottom (24));
    meters.removeFromBottom (8);
    auto switches = meters.removeFromBottom (26);
    safetyClip.setBounds (switches.removeFromLeft (switches.getWidth() / 2 + 14));
    monoCheck.setBounds (switches.reduced (4, 0));
    meters.removeFromBottom (12);
    inputMeter.setBounds (meters.removeFromLeft (meters.getWidth() / 2).reduced (4, 0));
    outputMeter.setBounds (meters.reduced (4, 0));
    vocalControl.setBounds (controlArea());

    for (auto* overlay : std::initializer_list<juce::Component*> { &analysis, &dialog, &help, &tutorial, &browser })
        overlay->setBounds (0, 0, baseWidth, baseHeight);

    // ---- View state (stored with the project, never affects audio)
    // uiMode: "beginner" (stored by 0.3 / 0.4) = AUTO MIX, "pro" = CHAIN, "check" = VOCAL CHECK
    const auto mode = proc.getState().state.getProperty (statekeys::uiMode, "beginner").toString();
    selected = (int) proc.getState().state.getProperty (uiSelectionProperty, 0);
    chainView.setSelected (selected);
    setView (mode == "pro" ? TopBar::View::chain : (mode == "check" ? TopBar::View::vocalCheck : TopBar::View::autoMix));
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

    // Stored as a scale of the design canvas ("uiScale"): a pixel width saved by an older
    // layout (uiWindowWidth, 1440 x 900) would open the new, taller window off the screen.
    // Never larger than the work area of the screen.
    int fitW = baseWidth;
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto area = display->userArea;
        fitW = (int) std::min (area.getWidth() * 0.94, area.getHeight() * 0.94 * baseWidth / (double) baseHeight);
    }
    const int defaultW = juce::jlimit (minW, baseWidth, fitW);
    const double savedScale = (double) proc.getState().state.getProperty (uiScaleProperty, defaultW / (double) baseWidth);
    const int w = juce::jlimit (minW, std::max (minW, std::min (maxW, fitW)), juce::roundToInt (savedScale * baseWidth));
    setSize (w, juce::roundToInt (w * (double) baseHeight / baseWidth));

    auto& autoMix = proc.getAutoMix();
    autoMix.addChangeListener (this);
    chainView.setAutoMixModules (autoMix.getReport().changedModules);

    startTimerHz (frameRate);

    // First open of a new instance: the analysis popup. A capture or analysis
    // that is still running (the editor was closed meanwhile) shows its progress.
    using Phase = automix::AutoMixController::Phase;
    const auto phase = autoMix.getPhase();
    if (phase == Phase::listening || phase == Phase::capturing || phase == Phase::analysing)
        analysis.showProgress();
    else if (autoMix.shouldShowPrompt())
        analysis.showIntro();
    else if (! settings::getBool (settings::tutorialDone) && ! tutorialPostponedThisSession)
        startTutorial (true);
    raiseOverlays();
}

VelvoxAudioProcessorEditor::~VelvoxAudioProcessorEditor()
{
    stopTimer();
    proc.getAutoMix().removeChangeListener (this);
    auto& animator = juce::Desktop::getInstance().getAnimator();
    animator.cancelAnimation (&mixView, false);
    if (detail != nullptr)
        animator.cancelAnimation (detail.get(), false);
    setLookAndFeel (nullptr);
}

void VelvoxAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // A new mix, a restored state or a compare switch: every view follows at once
    auto& autoMix = proc.getAutoMix();
    chainView.setAutoMixModules (autoMix.getReport().changedModules);
    if (detail != nullptr)
        detail->refresh();
}

void VelvoxAudioProcessorEditor::startAnalysis()
{
    help.close();
    if (tutorial.isActive())
        tutorial.finish (TutorialOverlay::Outcome::later);
    proc.getAutoMix().start();
    analysis.showProgress();
    raiseOverlays();
}

void VelvoxAudioProcessorEditor::openModule (int id)
{
    selected = id;
    setView (TopBar::View::chain);
}

void VelvoxAudioProcessorEditor::setView (TopBar::View v)
{
    const bool changed = v != view;
    view = v;
    proMode = v == TopBar::View::chain;
    topBar.setView (v);
    proc.getState().state.setProperty (statekeys::uiMode,
                                       v == TopBar::View::chain ? "pro" : (v == TopBar::View::vocalCheck ? "check" : "beginner"), nullptr);

    // CHAIN: module columns, module panel, meters. AUTO MIX and VOCAL CHECK use the whole area.
    for (auto* c : std::initializer_list<juce::Component*> { &chainView, &inputMeter, &outputMeter,
                                                              &safetyClip, &monoCheck, &correlation })
        c->setVisible (proMode);
    canvas.showMeterPanel = proMode;
    canvas.repaint();

    mixView.setVisible (v == TopBar::View::autoMix);
    mixView.setBounds (wideArea());
    if (v == TopBar::View::autoMix && changed && constructed)
        reveal (mixView, wideArea());
    chainView.setSelected (proMode ? selected : -2);
    select (selected);
}

void VelvoxAudioProcessorEditor::select (int id)
{
    // A selection that is no longer in the chain (removed, older project): the output stage
    if (id >= 0 && ! proc.getRouting().contains (dsp::selectionModule (id), dsp::selectionInstance (id)))
        id = ChainView::outputTile;
    const bool changed = id != selected || detail == nullptr;
    selected = id;
    proc.getState().state.setProperty (uiSelectionProperty, id, nullptr);
    if (proMode)
        chainView.setSelected (id);

    if (detail != nullptr)
    {
        juce::Desktop::getInstance().getAnimator().cancelAnimation (detail.get(), false);
        canvas.removeChildComponent (detail.get());
    }
    detail.reset();

    // VOCAL CHECK shows the check panel over the whole area
    if (view == TopBar::View::vocalCheck)
    {
        detail = createDetailPanel (proc, (int) dsp::ModuleId::check, &spectrum, &live);
        detail->onOpenModule = [safe = juce::Component::SafePointer<VelvoxAudioProcessorEditor> (this)] (int moduleId)
        {
            juce::MessageManager::callAsync ([safe, moduleId] { if (safe != nullptr) safe->openModule (moduleId); });
        };
        detail->onHelp = [this] (int moduleId) { showHelp (help::keyForModule (moduleId)); };
        canvas.addAndMakeVisible (*detail);
        detail->setBounds (wideArea());
        detail->refresh();
    }

    if (proMode)
    {
        detail = createDetailPanel (proc, id, &spectrum, id >= 0 && dsp::selectionInstance (id) > 0 ? &panelLive : &live);
        // Open another module (e.g. from a VOCAL CHECK finding) after the current click has finished
        detail->onOpenModule = [safe = juce::Component::SafePointer<VelvoxAudioProcessorEditor> (this)] (int moduleId)
        {
            juce::MessageManager::callAsync ([safe, moduleId] { if (safe != nullptr) safe->select (moduleId); });
        };
        detail->onHelp = [this] (int moduleId) { showHelp (help::keyForModule (moduleId)); };   // module number (help is per module)
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
    browser.toFront (false);
    analysis.toFront (false);
    dialog.toFront (false);
    help.toFront (false);
    tutorial.toFront (false);
    if (analysis.isVisible())
        analysis.toFront (true);
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
    if (key == "body")    return proMode ? bodyArea() : wideArea();
    return chainView.getBounds();   // "chain", "chainAndModules"
}

void VelvoxAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
}

void VelvoxAudioProcessorEditor::resized()
{
    const float scale = (float) getWidth() / (float) baseWidth;
    canvas.setTransform (juce::AffineTransform::scale (scale));
    proc.getState().state.setProperty (uiScaleProperty, getWidth() / (double) baseWidth, nullptr);
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
    // Gain reduction / movement of every instance, pulled once per frame. Instance 1 feeds the
    // shared histories; the selected other instance feeds panelLive.
    const int selInstance = selected >= 0 ? dsp::selectionInstance (selected) : 0;
    const auto selModule = selected >= 0 ? dsp::selectionModule (selected) : dsp::ModuleId::eq;
    for (int k = 0; k <= dsp::maxExtraInstances; ++k)
    {
        using M = dsp::ModuleId;
        const auto has = [k] (M id) { return k < dsp::maxInstances (id); };
        const float comp = has (M::comp) ? chain.getCompressor (k).gainReductionDb.pull() : 0.0f;
        const float deess = has (M::deess) ? chain.getDeEsser (k).gainReductionDb.pull() : 0.0f;
        const float dduck = has (M::delay) ? chain.getDelayDucker (k).gainReductionDb.pull() : 0.0f;
        const float rduck = has (M::reverb) ? chain.getReverbDucker (k).gainReductionDb.pull() : 0.0f;
        const float smashGr = has (M::smash) ? chain.getSmash (k).gainReductionDb.pull() : 0.0f;
        const float rideGain = has (M::ride) ? chain.getRide (k).currentGainDb.load() : 0.0f;
        const float rideLevel = has (M::ride) ? chain.getRide (k).detectedLevelDb.load() : -100.0f;
        auto& act = live.activity;
        act[(size_t) M::comp][(size_t) k] = juce::jlimit (0.0f, 1.0f, comp / 12.0f);
        act[(size_t) M::deess][(size_t) k] = juce::jlimit (0.0f, 1.0f, deess / 8.0f);
        act[(size_t) M::delay][(size_t) k] = juce::jlimit (0.0f, 1.0f, dduck / 12.0f);
        act[(size_t) M::reverb][(size_t) k] = juce::jlimit (0.0f, 1.0f, rduck / 12.0f);
        act[(size_t) M::smash][(size_t) k] = juce::jlimit (0.0f, 1.0f, smashGr / 15.0f);
        act[(size_t) M::ride][(size_t) k] = juce::jlimit (0.0f, 1.0f, std::abs (rideGain) / 8.0f);
        if (has (M::smooth))
            act[(size_t) M::smooth][(size_t) k] = juce::jlimit (0.0f, 1.0f, chain.getSmooth (k).getMaxCutDb() / 8.0f);
        if (k == 0)
        {
            act[(size_t) M::pulse][0] = juce::jlimit (0.0f, 1.0f, 1.0f - chain.getPulse().displayGain.load());
            live.compGr.push (comp);
            live.deessGr.push (deess);
            live.delayDuck.push (dduck);
            live.reverbDuck.push (rduck);
            live.smashGr.push (smashGr);
            live.rideGain.push (rideGain);
            live.rideLevel.push (rideLevel);
        }
        else if (k == selInstance && selected >= 0)
        {
            panelLive.compGr.push (selModule == M::comp ? comp : 0.0f);
            panelLive.deessGr.push (selModule == M::deess ? deess : 0.0f);
            panelLive.delayDuck.push (dduck);
            panelLive.reverbDuck.push (rduck);
            panelLive.smashGr.push (smashGr);
            panelLive.rideGain.push (rideGain);
            panelLive.rideLevel.push (rideLevel);
        }
    }
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

    // The selected instance k > 0: its views read instance k through the instance-0 fields
    if (selInstance > 0)
    {
        panelLive.inputDb = live.inputDb;
        panelLive.outputDb = live.outputDb;
        panelLive.inputLufs = live.inputLufs;
        panelLive.outputLufs = live.outputLufs;
        panelLive.scopeL = live.scopeL;
        panelLive.scopeR = live.scopeR;
        panelLive.activity = live.activity;
        panelLive.settings = live.settings;
        panelLive.settings.setInstance0 (live.settings.instance (selInstance));
        panelLive.time = live.time;
        panelLive.sampleRate = live.sampleRate;
        panelLive.effectiveDelayMs = live.effectiveDelayMs;
        panelLive.chopStep = live.chopStep;
        panelLive.hasSignal = live.hasSignal;
    }

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
    vocalControl.refresh();

    if (detail != nullptr)
        detail->refresh();
    if (mixView.isVisible() && frame % 6 == 0)
        mixView.refresh();
    if (analysis.isVisible() && frame % 2 == 0)
        analysis.update();

    const bool bypassed = proc.getState().getRawParameterValue (pid::bypass)->load() > 0.5f;
    if (bypassed != canvas.bypassed)
    {
        canvas.bypassed = bypassed;
        canvas.repaint();
    }

    if (monoCheck.getToggleState() != proc.isMonoMonitor())
        monoCheck.setToggleState (proc.isMonoMonitor(), juce::dontSendNotification);
}
