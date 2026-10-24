#include "PluginEditor.h"
#include "StateKeys.h"
#include "UI/UserSettings.h"
#include "UI/HelpContent.h"

using namespace velvox;
using namespace velvox::ui;

namespace
{
    // Window size as a scale of the design canvas (0.4 stored a pixel width under "uiWindowWidth")
    const juce::Identifier uiScaleProperty { "uiScale" };
    const juce::Identifier uiSelectionProperty { "uiSelection" };

    // "Remind me later" hides the welcome card until the plugin is loaded again
    bool tutorialPostponedThisSession = false;

    // Canvas layout (1000 x 600): header 44, body below with an 8 px margin
    constexpr int topBarH = TopBar::height;
    constexpr int margin = 8;
    constexpr int gap = 6;
    constexpr int chainH = ChainView::preferredHeight;
    constexpr int meterW = 92;
    constexpr int frameRate = 60;

    // Window size limits relative to the design canvas: the smallest text of the design is 9 px,
    // which must stay at least 8 px on screen (spec 8)
    constexpr double minScale = 8.0 / 9.0, maxScale = 2.0;

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
        c.setBounds (finalBounds.translated (0, 10));
        juce::Desktop::getInstance().getAnimator().animateComponent (&c, finalBounds, 1.0f, motion::revealMs, false, 0.0, 0.0);
    }
}

juce::Rectangle<int> VelvoxAudioProcessorEditor::bodyArea()  { return { margin, topBarH + gap, baseWidth - 2 * margin, baseHeight - topBarH - gap - margin }; }
juce::Rectangle<int> VelvoxAudioProcessorEditor::chainArea() { return bodyArea().withHeight (chainH).withTrimmedRight (meterW + gap); }
juce::Rectangle<int> VelvoxAudioProcessorEditor::meterArea() { return bodyArea().withHeight (chainH).withTrimmedLeft (bodyArea().getWidth() - meterW); }
juce::Rectangle<int> VelvoxAudioProcessorEditor::panelArea() { return bodyArea().withTrimmedTop (chainH + gap); }

//==============================================================================
VelvoxAudioProcessorEditor::Canvas::Canvas()
{
    addChildComponent (info);
    setOpaque (true);
}

void VelvoxAudioProcessorEditor::Canvas::paint (juce::Graphics& g)
{
    // The atmosphere is static: rendered once per physical size and then only blitted
    const float scale = juce::jlimit (0.5f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (backdrop.isNull() || std::abs (scale - backdropScale) > 0.01f)
    {
        backdropScale = scale;
        backdrop = juce::Image (juce::Image::RGB, juce::roundToInt (baseWidth * scale), juce::roundToInt (baseHeight * scale), false);
        juce::Graphics b (backdrop);
        b.addTransform (juce::AffineTransform::scale (scale));
        b.fillAll (colours::windowBg);
        drawGlow (b, { 500.0f, -90.0f }, 460.0f, 0.07f, colours::blue);
    }
    g.drawImage (backdrop, juce::Rectangle<float> ((float) baseWidth, (float) baseHeight));
}

void VelvoxAudioProcessorEditor::Canvas::paintOverChildren (juce::Graphics& g)
{
    if (! bypassed)
        return;

    // Global bypass: amber frame around everything and a label that cannot be missed
    const auto border = getLocalBounds().toFloat().reduced (1.5f);
    g.setColour (colours::bypass.withAlpha (0.8f));
    g.drawRoundedRectangle (border, 6.0f, 2.0f);

    const auto text = juce::String::fromUTF8 ("Bypass - du hörst die unbearbeitete Vocal");
    const float w = (float) juce::GlyphArrangement::getStringWidthInt (caps (captionSize (9.0f)), text) + 36.0f;
    const auto pill = juce::Rectangle<float> (w, 22.0f).withCentre ({ baseWidth * 0.5f, (float) baseHeight - 18.0f });
    g.setColour (colours::background);
    g.fillRoundedRectangle (pill, 11.0f);
    g.setColour (colours::bypass.withAlpha (0.18f));
    g.fillRoundedRectangle (pill, 11.0f);
    g.setColour (colours::bypass.withAlpha (0.8f));
    g.drawRoundedRectangle (pill.reduced (0.5f), 11.0f, 1.0f);
    drawCaption (g, text, pill, juce::Justification::centred, colours::bypass, 9.0f);
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
VelvoxAudioProcessorEditor::DetailsOverlay::DetailsOverlay (AutoMixView& v) : view (v)
{
    setWantsKeyboardFocus (true);
    addAndMakeVisible (view);
    close.setTooltip (juce::String::fromUTF8 ("Schließen"));
    close.onClick = [this] { setVisible (false); };
    addAndMakeVisible (close);
    setVisible (false);
}

juce::Rectangle<int> VelvoxAudioProcessorEditor::DetailsOverlay::cardBounds() const
{
    return getLocalBounds().reduced (40, 34);
}

void VelvoxAudioProcessorEditor::DetailsOverlay::paint (juce::Graphics& g)
{
    g.fillAll (colours::scrim);
    const auto card = cardBounds().toFloat();
    g.setColour (colours::background);
    g.fillRoundedRectangle (card, radius::panel);
    drawGlass (g, card, radius::panel, 0.02f);
}

void VelvoxAudioProcessorEditor::DetailsOverlay::resized()
{
    auto card = cardBounds();
    close.setBounds (card.getRight() - 40, card.getY() + 10, 28, 28);
    view.setBounds (card.reduced (16, 14).withTrimmedRight (36));
}

void VelvoxAudioProcessorEditor::DetailsOverlay::mouseDown (const juce::MouseEvent& e)
{
    if (! cardBounds().contains (e.getPosition()))
        setVisible (false);
}

bool VelvoxAudioProcessorEditor::DetailsOverlay::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        setVisible (false);
        return true;
    }
    return false;
}

//==============================================================================
VelvoxAudioProcessorEditor::VelvoxAudioProcessorEditor (VelvoxAudioProcessor& p)
    : AudioProcessorEditor (&p),
      proc (p),
      spectrum (p.getChain().meters.inputTap, p.getChain().meters.outputTap),
      scopeL ((size_t) dsp::StereoTap::capacity), scopeR ((size_t) dsp::StereoTap::capacity),
      browser (p),
      topBar (p, dialog),
      chainView (p),
      simpleView (p, &spectrum),
      mixView (p),
      analysis (p.getAutoMix())
{
    setLookAndFeel (&lookAndFeel);
    setWantsKeyboardFocus (true);

    addAndMakeVisible (canvas);
    canvas.setBounds (0, 0, baseWidth, baseHeight);

    for (auto* c : std::initializer_list<juce::Component*> { &topBar, &chainView, &simpleView, &meters })
        canvas.addAndMakeVisible (c);
    for (auto* c : std::initializer_list<juce::Component*> { &detailsOverlay, &analysis, &dialog, &help, &tutorial, &browser })
        canvas.addChildComponent (c);

    chainView.setLiveData (&live);

    meters.setTooltip (juce::String::fromUTF8 ("Pegel und Lautheit (LUFS) am Eingang und Ausgang. Die rote Lampe zeigt Übersteuerungen - Klick setzt sie zurück."));
    meters.setStacked (true);

    topBar.onModeChange = [this] (Mode m) { setMode (m); };
    topBar.onTabChange = [this] (Tab t) { setTab (t); };
    topBar.onHelp = [this]
    {
        showHelp (detail != nullptr && isModulePanelShown() ? help::keyForModule (selected < 0 ? selected : (int) dsp::selectionModule (selected))
                                                            : juce::String ("start"));
    };
    simpleView.onAnalyse = [this] { startAnalysis(); };
    simpleView.onShowDetails = [this] { showMixDetails(); };
    mixView.onAnalyse = [this] { detailsOverlay.setVisible (false); startAnalysis(); };
    mixView.onOpenModule = [this] (int id) { detailsOverlay.setVisible (false); openModule (id); };

    chainView.onSelect = [this] (int id) { select (id); };
    chainView.onHelp = [this] (int code) { showHelp (help::keyForModule (code < 0 ? code : (int) dsp::selectionModule (code))); };
    chainView.onAddModule = [this] (int insertIndex, bool atEnd)
    {
        help.close();
        browser.open (insertIndex, atEnd);
        raiseOverlays();
    };
    browser.onChoose = [this] (dsp::ModuleId id, int insertIndex, bool atEnd)
    {
        // 0.6 (spec Q2): delay and reverb join as inserts, so "Mix" is true dry/wet; "Als Send" stays in the menu
        chainView.insertModule (id, atEnd ? -1 : insertIndex, false);
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
    simpleView.setBounds (bodyArea());
    meters.setBounds (meterArea().reduced (6, 4));
    for (auto* overlay : std::initializer_list<juce::Component*> { &detailsOverlay, &analysis, &dialog, &help, &tutorial, &browser })
        overlay->setBounds (0, 0, baseWidth, baseHeight);

    // ---- View state (stored with the project, never affects audio)
    // uiMode: "beginner" (0.3 - 0.5 AUTO MIX) = Simple, "pro" = Advanced, "check" (0.5) = Advanced + VOCAL CHECK.
    // uiTab (0.6): "automix" / "check".
    const auto& state = proc.getState().state;
    const auto storedMode = state.getProperty (statekeys::uiMode, "beginner").toString();
    const auto storedTab = state.getProperty (statekeys::uiTab, juce::String()).toString();
    mode = storedMode == "pro" || storedMode == "check" ? Mode::advanced : Mode::simple;
    tab = storedTab == "check" || (storedTab.isEmpty() && storedMode == "check") ? Tab::vocalCheck : Tab::autoMix;
    selected = (int) state.getProperty (uiSelectionProperty, 0);
    chainView.setSelected (selected);
    applyView (false);
    constructed = true;

    // ---- Window: resizable, fixed aspect ratio, remembered per project ("uiScale", a scale of
    // the design canvas). Never larger than the work area of the screen.
    const int minW = (int) std::ceil (baseWidth * minScale), maxW = juce::roundToInt (baseWidth * maxScale);
    // Read before the limits are set: constraining the initial 0 x 0 size calls resized(), which stores a scale
    const juce::var storedScale = state.getProperty (uiScaleProperty);
    setResizable (true, true);
    setResizeLimits (minW, (int) std::ceil (baseHeight * minScale), maxW, juce::roundToInt (baseHeight * maxScale));
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) baseWidth / (double) baseHeight);

    int fitW = maxW;
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto area = display->userArea;
        fitW = (int) std::min (area.getWidth() * 0.94, area.getHeight() * 0.94 * baseWidth / (double) baseHeight);
    }
    const int defaultW = juce::jlimit (minW, std::max (minW, fitW), baseWidth);
    const double savedScale = storedScale.isVoid() ? defaultW / (double) baseWidth : (double) storedScale;
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
    animator.cancelAnimation (&simpleView, false);
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

bool VelvoxAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    // Global undo (spec 8 Q8): Ctrl+Z, Ctrl+Y, Ctrl+Shift+Z (Cmd on macOS)
    const auto mods = key.getModifiers();
    if (! mods.isCommandDown())
        return false;
    const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter() != 0 ? key.getTextCharacter() : (juce::juce_wchar) key.getKeyCode());
    const int code = key.getKeyCode();
    const bool z = c == 'z' || code == 'Z' || code == 'z';
    const bool y = c == 'y' || code == 'Y' || code == 'y';
    if (z && ! mods.isShiftDown())
        return chainView.undo() || true;
    if (y || (z && mods.isShiftDown()))
        return chainView.redo() || true;
    return false;
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

void VelvoxAudioProcessorEditor::showMixDetails()
{
    help.close();
    detailsOverlay.setVisible (true);
    mixView.refresh();
    raiseOverlays();
    detailsOverlay.grabKeyboardFocus();
}

void VelvoxAudioProcessorEditor::openModule (int id)
{
    selected = id;
    mode = Mode::advanced;
    tab = Tab::autoMix;
    applyView (true);
}

void VelvoxAudioProcessorEditor::setMode (Mode m)
{
    if (m == mode)
        return;
    mode = m;
    applyView (true);
}

void VelvoxAudioProcessorEditor::setTab (Tab t)
{
    if (t == tab)
        return;
    tab = t;
    applyView (true);
}

void VelvoxAudioProcessorEditor::storeView()
{
    auto& state = proc.getState().state;
    state.setProperty (statekeys::uiMode, mode == Mode::advanced ? "pro" : "beginner", nullptr);
    state.setProperty (statekeys::uiTab, tab == Tab::vocalCheck ? "check" : "automix", nullptr);
}

void VelvoxAudioProcessorEditor::applyView (bool animate)
{
    topBar.setMode (mode);
    topBar.setTab (tab);
    storeView();

    const bool simple = tab == Tab::autoMix && mode == Mode::simple;
    const bool advanced = tab == Tab::autoMix && mode == Mode::advanced;
    simpleView.setVisible (simple);
    for (auto* c : std::initializer_list<juce::Component*> { &chainView, &meters })
        c->setVisible (advanced);
    if (simple && animate && constructed)
        reveal (simpleView, bodyArea());
    else
        simpleView.setBounds (bodyArea());

    chainView.setSelected (advanced ? selected : -2);
    select (selected);
    canvas.repaint();
}

void VelvoxAudioProcessorEditor::select (int id)
{
    // A selection that is no longer in the chain (removed, older project): the output stage
    if (id >= 0 && ! proc.getRouting().contains (dsp::selectionModule (id), dsp::selectionInstance (id)))
        id = ChainView::outputTile;
    const bool changed = id != selected || detail == nullptr;
    selected = id;
    proc.getState().state.setProperty (uiSelectionProperty, id, nullptr);
    if (isModulePanelShown())
        chainView.setSelected (id);

    if (detail != nullptr)
    {
        juce::Desktop::getInstance().getAnimator().cancelAnimation (detail.get(), false);
        canvas.removeChildComponent (detail.get());
    }
    detail.reset();

    const auto openFromPanel = [safe = juce::Component::SafePointer<VelvoxAudioProcessorEditor> (this)] (int moduleId)
    {
        // After the current click has finished
        juce::MessageManager::callAsync ([safe, moduleId] { if (safe != nullptr) safe->openModule (moduleId); });
    };

    // VOCAL CHECK: the check panel over the whole body (both modes)
    if (tab == Tab::vocalCheck)
    {
        detail = createDetailPanel (proc, (int) dsp::ModuleId::check, &spectrum, &live);
        detail->onOpenModule = openFromPanel;
        detail->onHelp = [this] (int moduleId) { showHelp (help::keyForModule (moduleId)); };
        canvas.addAndMakeVisible (*detail);
        detail->setBounds (bodyArea());
        detail->refresh();
    }
    else if (isModulePanelShown())
    {
        detail = createDetailPanel (proc, id, &spectrum, id >= 0 && dsp::selectionInstance (id) > 0 ? &panelLive : &live);
        detail->onOpenModule = openFromPanel;
        detail->onHelp = [this] (int moduleId) { showHelp (help::keyForModule (moduleId)); };
        canvas.addAndMakeVisible (*detail);
        detail->setBounds (panelArea());
        detail->refresh();
        if (constructed && changed)
            reveal (*detail, panelArea());
    }

    raiseOverlays();
}

void VelvoxAudioProcessorEditor::raiseOverlays()
{
    canvas.info.toFront (false);
    for (auto* c : std::initializer_list<juce::Component*> { &detailsOverlay, &browser, &analysis, &dialog, &help, &tutorial })
        c->toFront (false);
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
    if (key == "modes")   return topBar.getModeArea();
    if (key == "tabs")    return topBar.getTabArea();
    if (key == "global")  return topBar.getBypassArea();
    if (key == "help")    return topBar.getHelpButton().getBounds();
    if (key == "macros")  return simpleView.getMacroArea() + bodyArea().getPosition();
    if (key == "analyse") return simpleView.getSpectrumArea() + bodyArea().getPosition();
    if (key == "body")    return isModulePanelShown() ? panelArea() : bodyArea();
    return chainArea();   // "chain", "chainAndModules"
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
    live.clipped = m.clipped.exchange (false);
    meters.setLevels (live.inputDb.ago (0), live.outputDb.ago (0), live.inputLufs.ago (0), live.outputLufs.ago (0), live.clipped);

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
}

void VelvoxAudioProcessorEditor::timerCallback()
{
    updateLiveData();
    ++frame;
    canvas.hideInfoIfAnchorGone();

    topBar.refresh();
    if (chainView.isVisible())
        chainView.refresh();
    if (simpleView.isVisible())
        simpleView.refresh (live);

    if (detail != nullptr)
        detail->refresh();
    if (detailsOverlay.isVisible() && frame % 6 == 0)
        mixView.refresh();
    if (analysis.isVisible() && frame % 2 == 0)
        analysis.update();

    const bool bypassed = proc.getState().getRawParameterValue (pid::bypass)->load() > 0.5f;
    if (bypassed != canvas.bypassed)
    {
        canvas.bypassed = bypassed;
        canvas.repaint();
    }
}
