#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ProductInfo.h"
#include "StateKeys.h"
#include "Parameters/RoutingMigration.h"
#include "DSP/Solo.h"
#include "Parameters/MacroBake.h"
#include "Text/I18n.h"

using namespace velvox;

namespace
{
    // A/B slots of 0.5 (removed in 0.6): read and dropped. The saved top-level state is the
    // slot that was active, so such a project loads exactly what was last heard.
    const juce::Identifier abTreeType { "ABSlots" };
}

VelvoxAudioProcessor::VelvoxAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "VelvoxState", createParameterLayout()),
      presetManager (apvts)
{
    cacheParameterPointers();
    apvts.state.setProperty (statekeys::routing, dsp::Routing::defaults().toString(), nullptr);
    // A new plugin: every module instance on the true dry/wet law (a loaded project replaces this)
    apvts.state.setProperty (statekeys::mixLaw, dsp::MixLaws::allTrue().toString(), nullptr);
    apvts.state.addListener (this);
    syncRoutingFromState();
    syncMixLawFromState();

    // On-switches of every module instance and the macros can change which instances
    // must be in the chain (and the latency): watched, handled on the message thread
    for (const auto& spec : getParameterSpecs())
    {
        const auto base = dsp::splitInstanceParamId (spec.id).first;
        if (base.endsWith ("On") || juce::String (spec.module) == "Macros")
            structureParams.add (spec.id);
    }
    for (auto& id : structureParams)
        apvts.addParameterListener (id, this);
    analyzerTimer.startTimerHz (15);

    // Undo: the plugin's own gestures, preset loads and the automatic mix (one step each)
    addListener (&gestureListener);
    presetManager.setUndoHistory (&undoHistory);
    // An edit while the auto-mix "Vorher" is shown ends that compare and keeps the edit; its undo step
    // leads back to the "Nachher" state (spec Q3 / Q8, final-pass ruling)
    undoHistory.takeCompareBase = [this] { return autoMix.endCompareForEdit(); };
    undoHistory.coalescesWithSideEffects = [this] (int index) { return macroMapper.isMacroIndex (index); };
    autoMix.onApplying = [this] (bool starting, bool asStep)
    {
        if (asStep)
        {
            if (starting) undoHistory.beginTransaction (i18n::tr ("Auto-Mix", "Auto Mix"));
            else          undoHistory.endTransaction();
        }
        else
        {
            if (starting) undoHistory.beginSuppress();
            else          undoHistory.endSuppress();
        }
    };
}

VelvoxAudioProcessor::~VelvoxAudioProcessor()
{
    analyzerTimer.stopTimer();
    cancelPendingUpdate();
    removeListener (&gestureListener);
    for (auto& id : structureParams)
        apvts.removeParameterListener (id, this);
    apvts.state.removeListener (this);
}

//==============================================================================
void VelvoxAudioProcessor::parameterChanged (const juce::String&, float)
{
    // Changes made on the message thread (UI, presets, tests) take effect at once;
    // automation arrives on the audio thread and is handled asynchronously
    if (juce::MessageManager::existsAndIsCurrentThread())
    {
        cancelPendingUpdate();
        handleAsyncUpdate();
        return;
    }
    // May arrive on the audio thread (automation): report on the message thread
    triggerAsyncUpdate();
}

void VelvoxAudioProcessor::handleAsyncUpdate()
{
    macroMapper.update();   // a Simple slider / automated macro moved: write its modules
    insertEngagedInstances();
    updateHostLatency();
}

void VelvoxAudioProcessor::insertEngagedInstances()
{
    auto routing = getRouting();
    const auto e = readEffectiveSettings();
    bool changed = false;

    for (int k = 0; k <= dsp::maxExtraInstances; ++k)
    {
        const auto b = e.instance (k);
        const auto engage = [&] (dsp::ModuleId id, bool on)
        {
            if (on && k < dsp::maxInstances (id) && ! routing.contains (id, k))
                changed = routing.insertDefault (id, k) || changed;
        };
        engage (dsp::ModuleId::sat, b.sat.enabled && b.sat.mix > 0.0f);
        engage (dsp::ModuleId::doubler, b.doubler.enabled && b.doubler.mix > 0.0f);
        engage (dsp::ModuleId::smash, b.smash.enabled);
        engage (dsp::ModuleId::smooth, b.smooth.enabled);
        engage (dsp::ModuleId::ride, b.ride.enabled);
        engage (dsp::ModuleId::delay, b.delayOn && b.delayMix > 0.0f);
        engage (dsp::ModuleId::reverb, b.reverbOn && b.reverbMix > 0.0f);
        if (k == 0)
        {
            engage (dsp::ModuleId::check, e.checkOn);
        }
    }

    // (SPACE turning a fresh insert into a send happens in the MacroMapper, when it switches the module on)

    if (changed)
        setRouting (routing);
}

void VelvoxAudioProcessor::pumpAnalyzer()
{
    if (getSampleRate() > 0.0)
        analyzer.setSampleRate (getSampleRate());
    analyzer.setMode (reader.checkMode() == 1 ? dsp::VocalAnalyzer::Mode::capture : dsp::VocalAnalyzer::Mode::continuous);
    if (reader.checkOn())
    {
        analyzer.pump();
        if (juce::Time::getMillisecondCounterHiRes() - lastAnalysisMs > 500.0)
            refreshAnalysis();
    }
}

bool VelvoxAudioProcessor::isInstanceAudible (dsp::ModuleId id, int k) const
{
    using M = dsp::ModuleId;
    if (! getRouting().contains (id, k))
        return false;
    const auto e = readEffectiveSettings();
    const auto b = e.instance (k);
    switch (id)
    {
        case M::eq:      return b.eqOn && b.eqMix > 0.0f;
        case M::comp:    return b.compOn && b.compMix > 0.0f;
        case M::deess:   return b.deessOn;
        case M::sat:     return b.sat.enabled && b.sat.mix > 0.0f;
        case M::doubler: return b.doubler.enabled && b.doubler.mix > 0.0f;
        case M::delay:   return b.delayOn && b.delayMix > 0.0f;
        case M::reverb:  return b.reverbOn && b.reverbMix > 0.0f;
        case M::smash:   return b.smash.enabled;
        case M::smooth:  return b.smooth.enabled;
        case M::ride:    return b.ride.enabled;
        case M::check:   return e.checkOn;
    }
    return false;
}

void VelvoxAudioProcessor::refreshAnalysis()
{
    latestAnalysis = analyzer.analyse (vocalCheck.getGenre());
    lastAnalysisMs = juce::Time::getMillisecondCounterHiRes();

    // Does the chain already take care of a finding? VOCAL CHECK measures at its own
    // place: an active module after it works on the problem, one before it is already
    // part of the measurement (and may need more), the auto mix may have set it up.
    const auto routing = getRouting();
    const int checkAt = routing.indexOf (dsp::ModuleId::check);
    const auto& report = autoMix.getReport();
    for (auto& f : latestAnalysis.findings)
    {
        if (! f.module.has_value())
            continue;
        const auto m = *f.module;
        const juce::String name (dsp::moduleDisplayName (m));
        for (int i = 0; i < routing.size(); ++i)
        {
            const auto& slot = routing[i];
            if (slot.id != m || ! isInstanceAudible (m, slot.instance))
                continue;
            const bool after = checkAt < 0 || i > checkAt;
            if (after)
            {
                f.handling = dsp::Finding::Handling::handledLater;
                // Stored with the finding and shown later: both languages (i18n::show picks one)
                f.handlingText = i18n::both (i18n::format (juce::String::fromUTF8 ("{0} liegt hinter VOCAL CHECK - es bearbeitet das schon (in dieser Messung nicht sichtbar)."), { name }),
                                             i18n::format ("{0} is on after VOCAL CHECK - it already works on this (not visible in this measurement).", { name }));
                break;
            }
            f.handling = dsp::Finding::Handling::appliedBefore;
            f.handlingText = i18n::both (i18n::format (juce::String::fromUTF8 ("{0} liegt vor VOCAL CHECK - gemessen wird danach, es braucht also eventuell mehr."), { name }),
                                         i18n::format ("{0} is on before VOCAL CHECK - this is measured after it, so it may need more.", { name }));
        }
        if (f.handling == dsp::Finding::Handling::notHandled && report.valid && report.changedModules.contains (dsp::moduleKey (m)))
        {
            f.handling = dsp::Finding::Handling::autoMix;
            f.handlingText = i18n::both (i18n::format (juce::String::fromUTF8 ("Der Auto Mix hat {0} eingestellt."), { name }),
                                         i18n::format ("The automatic mix set up {0}.", { name }));
        }
    }
}

void VelvoxAudioProcessor::updateHostLatency()
{
    const int latency = chain.getLatencyFor (getRouting());
    if (latency != getLatencySamples())
        setLatencySamples (latency);
}

const juce::String VelvoxAudioProcessor::getName() const
{
    return product::name;
}

void VelvoxAudioProcessor::cacheParameterPointers()
{
    reader.bind ([this] (const char* id) { return apvts.getRawParameterValue (id); });
}

dsp::ChainSettings VelvoxAudioProcessor::readSettings() const noexcept
{
    auto s = reader.read (getRouting(), monoMonitor.load (std::memory_order_relaxed));
    s.mixLaws = getMixLaws();
    return s;
}

dsp::MacroValues VelvoxAudioProcessor::readMacros() const noexcept
{
    return reader.readMacros();
}

//==============================================================================
dsp::Routing VelvoxAudioProcessor::getRouting() const noexcept
{
    return routingShared.load();
}

void VelvoxAudioProcessor::setRouting (const dsp::Routing& routing)
{
    // The state property is the source of truth; the listener updates the atomic
    apvts.state.setProperty (statekeys::routing, routing.toString(), nullptr);
}

bool VelvoxAudioProcessor::isStateWriterThread() noexcept
{
    // Without a message manager (command-line tools) there is only one thread that writes
    auto* mm = juce::MessageManager::getInstanceWithoutCreating();
    return mm == nullptr || mm->currentThreadHasLockedMessageManager();
}

// routingShared / mixLawShared are SeqLockValues: ONE writer (the message thread, or a thread holding
// the MessageManagerLock), any number of lock-free readers (audio thread, UI). Two concurrent writers
// would corrupt the sequence counter. Every store goes through these two functions, which run from the
// state listener, the constructor and setStateInformation - all on the message thread in every host
// we support. The assertion catches a host that restores state on another thread (debug builds).
void VelvoxAudioProcessor::syncRoutingFromState()
{
    jassert (isStateWriterThread());   // SeqLockValue: single writer
    const auto text = apvts.state.getProperty (statekeys::routing).toString();
    const auto routing = text.isEmpty() ? dsp::Routing::defaults() : dsp::Routing::fromString (text);
    routingShared.store (routing);
    // A soloed instance that left the chain: the solo ends (nothing stays bypassed by it)
    if (const int solo = getSolo(); solo >= 0 && ! routing.contains (dsp::selectionModule (solo), dsp::selectionInstance (solo)))
        soloCode.store (-1);
    if (juce::MessageManager::existsAndIsCurrentThread())
        triggerAsyncUpdate();   // latency follows the saturation / smooth / ride instances in the chain
}

void VelvoxAudioProcessor::setSolo (int code)
{
    if (code >= 0)
    {
        const auto id = dsp::selectionModule (code);
        if ((int) id < 0 || (int) id >= dsp::numModules || ! dsp::canSolo (id)
            || ! getRouting().contains (id, dsp::selectionInstance (code)))
            return;
    }
    soloCode.store (code < 0 ? -1 : code);
}

bool VelvoxAudioProcessor::toggleSolo (int code)
{
    setSolo (isSoloed (code) ? -1 : code);
    return isSoloed (code);
}

void VelvoxAudioProcessor::syncMixLawFromState()
{
    jassert (isStateWriterThread());   // SeqLockValue: single writer (see syncRoutingFromState)
    // Absent (projects from 0.5 and older) = every instance on the legacy law
    mixLawShared.store (dsp::MixLaws::fromString (apvts.state.getProperty (statekeys::mixLaw).toString()));
}

bool VelvoxAudioProcessor::usesLegacyMixLaw (dsp::ModuleId id, int instance) const noexcept
{
    return dsp::hasAdditiveMix (id) && getMixLaws().isLegacy (id, instance);
}

void VelvoxAudioProcessor::setLegacyMixLaw (dsp::ModuleId id, int instance, bool legacy)
{
    auto laws = dsp::MixLaws::fromString (apvts.state.getProperty (statekeys::mixLaw).toString());
    if (laws.isLegacy (id, instance) == legacy)
        return;
    laws.setLegacy (id, instance, legacy);
    // The state property is the source of truth; the listener updates the lock-free copy
    apvts.state.setProperty (statekeys::mixLaw, laws.toString(), nullptr);
}

void VelvoxAudioProcessor::switchToTrueMixLaw (dsp::ModuleId id, int instance)
{
    // A user command that changes the sound: one undo step (the law is part of every snapshot)
    undoHistory.perform (i18n::tr ("Mix-Kurve umstellen", "Switch mix curve"), [&] { setLegacyMixLaw (id, instance, false); });
}

void VelvoxAudioProcessor::markFreshInstances (const dsp::Routing& before, const dsp::Routing& after)
{
    // The browser adds an instance with default settings while it is off and switches it on
    // afterwards; automation, macros and undo bring an instance back while it is on. Only the
    // fresh ones change their law - an old project keeps its sound.
    const auto e = readEffectiveSettings();
    for (const auto& slot : after)
    {
        if (! dsp::hasAdditiveMix (slot.id) || before.contains (slot.id, slot.instance))
            continue;
        const auto b = e.instance (slot.instance);
        const bool on = slot.id == dsp::ModuleId::delay   ? b.delayOn
                      : slot.id == dsp::ModuleId::reverb  ? b.reverbOn
                      : slot.id == dsp::ModuleId::doubler ? b.doubler.enabled
                                                          : b.smash.enabled;
        if (! on)
            setLegacyMixLaw (slot.id, slot.instance, false);
    }
}

void VelvoxAudioProcessor::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    if (tree == apvts.state && property == statekeys::mixLaw)
        syncMixLawFromState();

    if (tree == apvts.state && property == statekeys::routing)
    {
        const auto before = getRouting();
        syncRoutingFromState();
        if (! loadingState)
            markFreshInstances (before, getRouting());
    }
}

void VelvoxAudioProcessor::valueTreeRedirected (juce::ValueTree&)
{
    syncRoutingFromState();
    syncMixLawFromState();
}

//==============================================================================
void VelvoxAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    chain.prepare (sampleRate, samplesPerBlock);
    audioRouting = getRouting();
    setLatencySamples (chain.getLatencyFor (audioRouting));
}

void VelvoxAudioProcessor::releaseResources()
{
}

bool VelvoxAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    if (layouts.getMainInputChannelSet() != out)
        return false;

    // Optional sidechain: off, mono or stereo
    if (layouts.inputBuses.size() > 1)
    {
        const auto& sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

juce::AudioProcessorParameter* VelvoxAudioProcessor::getBypassParameter() const
{
    return apvts.getParameter (pid::bypass);
}

void VelvoxAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto startTicks = juce::Time::getHighResolutionTicks();
    struct LoadMeter
    {
        CallbackLoad& load;
        juce::int64 start;
        int numSamples;
        double fs;
        ~LoadMeter()
        {
            if (numSamples <= 0 || fs <= 0.0)
                return;
            const double seconds = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start);
            const float ratio = (float) (seconds * fs / numSamples);
            if (ratio > load.worstRatio.load (std::memory_order_relaxed) && numSamples >= 32)
                load.worstRatio.store (ratio, std::memory_order_relaxed);
            if (ratio > 1.0f && numSamples >= 32)
                load.overruns.fetch_add (1, std::memory_order_relaxed);
            const float avg = load.averageRatio.load (std::memory_order_relaxed);
            load.averageRatio.store (avg + 0.01f * (ratio - avg), std::memory_order_relaxed);
        }
    } loadMeter { callbackLoad, startTicks, buffer.getNumSamples(), getSampleRate() };

    // Routing: lock-free copy; while the message thread is writing it, the previous one stays
    routingShared.tryLoad (audioRouting);
    mixLawShared.tryLoad (audioMixLaws);
    auto settings = reader.read (audioRouting, monoMonitor.load (std::memory_order_relaxed));   // 0.6.1: no macro layer
    settings.mixLaws = audioMixLaws;
    // Module solo (listening aid): every other instance bypassed, latency unchanged
    if (const int solo = soloCode.load (std::memory_order_relaxed); solo >= 0)
        if (audioRouting.contains (dsp::selectionModule (solo), dsp::selectionInstance (solo)))
            dsp::applySolo (settings, dsp::selectionModule (solo), dsp::selectionInstance (solo));

    if (auto* hostPlayHead = getPlayHead())
        if (const auto position = hostPlayHead->getPosition())
        {
            if (const auto bpm = position->getBpm())
                settings.hostBpm = *bpm;
            if (const auto ppq = position->getPpqPosition())
                settings.hostPpq = *ppq;
            settings.hostPlaying = position->getIsPlaying();
        }

    // Defensive: a host that hands over fewer channels than the negotiated
    // layout must not crash us - process what is there, without sidechain.
    const int expectedChannels = std::max (getTotalNumInputChannels(), getTotalNumOutputChannels());
    if (buffer.getNumChannels() < expectedChannels)
    {
        chain.process (buffer, std::min (buffer.getNumChannels(), getMainBusNumOutputChannels()), settings, nullptr);
        return;
    }

    // Main bus only for processing; the sidechain bus (if enabled) is read-only input
    auto main = getBusBuffer (buffer, false, 0);
    const int mainIn = getMainBusNumInputChannels();
    for (int ch = mainIn; ch < main.getNumChannels(); ++ch)
        main.clear (ch, 0, main.getNumSamples());

    juce::AudioBuffer<float> sidechain;
    const bool hasSidechain = getBusCount (true) > 1 && getBus (true, 1) != nullptr
                              && getBus (true, 1)->isEnabled() && getBus (true, 1)->getNumberOfChannels() > 0;
    if (hasSidechain)
        sidechain = getBusBuffer (buffer, true, 1);

    const int vocalChannels = std::min (mainIn, main.getNumChannels());
    measureLevels (main, vocalChannels, hasSidechain ? &sidechain : nullptr);

    // Automatic mix: copies the unprocessed input (and the beat) while a capture
    // is armed - a bounded, pre-allocated buffer, nothing else on this thread
    autoMix.push (main.getArrayOfReadPointers(), vocalChannels,
                  hasSidechain ? sidechain.getArrayOfReadPointers() : nullptr, hasSidechain ? sidechain.getNumChannels() : 0,
                  main.getNumSamples(), settings.hostPlaying);

    chain.process (main, std::min (mainIn, main.getNumChannels()), settings, hasSidechain ? &sidechain : nullptr);
}

// Short-term RMS of the vocal input and of the sidechain: a few multiply-adds
// per sample, no allocation, no locks
void VelvoxAudioProcessor::measureLevels (const juce::AudioBuffer<float>& vocal, int vocalChannels,
                                          const juce::AudioBuffer<float>* sidechain) noexcept
{
    const int n = vocal.getNumSamples();
    if (n <= 0)
        return;
    const double fs = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
    const float a = 1.0f - (float) std::exp (-(double) n / (0.3 * fs));

    const auto meanSquare = [n] (const juce::AudioBuffer<float>& b, int channels)
    {
        double sum = 0.0;
        for (int ch = 0; ch < channels; ++ch)
        {
            const float* d = b.getReadPointer (ch);
            for (int i = 0; i < n; ++i)
                sum += (double) d[i] * d[i];
        }
        return channels > 0 ? (float) (sum / ((double) n * channels)) : 0.0f;
    };

    vocalMeanSquare += a * (meanSquare (vocal, vocalChannels) - vocalMeanSquare);
    vocalRmsDb.store (juce::Decibels::gainToDecibels (std::sqrt (vocalMeanSquare), -100.0f), std::memory_order_relaxed);

    const bool connected = sidechain != nullptr && sidechain->getNumChannels() > 0 && sidechain->getNumSamples() >= n;
    scConnected.store (connected, std::memory_order_relaxed);
    scMeanSquare += a * ((connected ? meanSquare (*sidechain, sidechain->getNumChannels()) : 0.0f) - scMeanSquare);
    if (! std::isfinite (scMeanSquare) || ! std::isfinite (vocalMeanSquare))
        scMeanSquare = vocalMeanSquare = 0.0f;
    scRmsDb.store (juce::Decibels::gainToDecibels (std::sqrt (scMeanSquare), -100.0f), std::memory_order_relaxed);
}

VelvoxAudioProcessor::SidechainStatus VelvoxAudioProcessor::getSidechainStatus() const noexcept
{
    SidechainStatus s;
    s.connected = scConnected.load (std::memory_order_relaxed);
    s.beatRmsDb = scRmsDb.load (std::memory_order_relaxed);
    s.vocalRmsDb = vocalRmsDb.load (std::memory_order_relaxed);
    return s;
}

//==============================================================================
juce::AudioProcessorEditor* VelvoxAudioProcessor::createEditor()
{
    return new VelvoxAudioProcessorEditor (*this);
}

//==============================================================================
void VelvoxAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();

    // Never write the 0.5 A/B slots again
    if (auto existing = state.getChildWithName (abTreeType); existing.isValid())
        state.removeChild (existing, nullptr);

    autoMix.writeState (state);
    state.setProperty (statekeys::stateVersion, statekeys::currentStateVersion, nullptr);

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void VelvoxAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    eqbands::ScopedBridgeSuspend noEqBridge (apvts);   // values are restored as they were saved
    soloCode.store (-1);   // a loaded project is heard as it is (solo is a listening aid, never saved)

    if (const auto ab = tree.getChildWithName (abTreeType); ab.isValid())
        tree.removeChild (ab, nullptr);

    // Automatic mix (report, previous state, first-open prompt). Projects from
    // versions without it count as "already mixed": no analysis prompt there.
    const int savedVersion = (int) tree.getProperty (statekeys::stateVersion, 1);
    const auto autoMixTree = tree.getChildWithName (automix::AutoMixController::treeType);
    if (autoMixTree.isValid())
        tree.removeChild (autoMixTree, nullptr);
    juce::ValueTree autoMixHolder ("Holder");
    if (autoMixTree.isValid())
        autoMixHolder.appendChild (autoMixTree.createCopy(), nullptr);

    // --- Migration (any older state version) ---
    // JUCE keeps the CURRENT value for parameters that are missing from a saved
    // state. Collect what the state contains so missing (newer) parameters can
    // be reset to their defaults - an old project then sounds exactly as before.
    juce::StringArray savedIds;
    for (const auto& child : tree)
        if (child.hasProperty ("id"))
            savedIds.add (child.getProperty ("id").toString());

    // v0.1 projects have no routing: every module in the default order (migrated below)
    const auto savedRouting = tree.getProperty (statekeys::routing).toString();

    tree.setProperty (statekeys::stateVersion, statekeys::currentStateVersion, nullptr);
    // Mix law: a state without it (0.5 and older) keeps the legacy law on every instance;
    // written explicitly so the next save carries it. Loading never marks instances fresh.
    const juce::ScopedValueSetter<bool> loading (loadingState, true);
    tree.setProperty (statekeys::mixLaw, dsp::MixLaws::fromString (tree.getProperty (statekeys::mixLaw).toString()).toString(), nullptr);
    apvts.replaceState (tree);

    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (! savedIds.contains (rp->getParameterID()))
                rp->setValueNotifyingHost (rp->getDefaultValue());

    // 0.1-0.5 projects have no EQ bands: the fixed bands become bands 1-5, sound-identical
    if (! eqbands::containsBandValues (savedIds))
        eqbands::migrateState (eqbands::accessFor (apvts), true);

    // 0.2 - 0.6.0 projects: the macros were an offset layer in the audio path. Baked into the module
    // parameters once (exactly what dsp::applyMacros produced); the macro values stay as the Simple
    // sliders' positions. Before the routing migration, which prunes by what is audible.
    if (savedVersion < 4)
    {
        auto laws = dsp::MixLaws::fromString (apvts.state.getProperty (statekeys::mixLaw).toString());
        const auto routing = savedRouting.isEmpty() ? dsp::Routing::legacyFull() : dsp::Routing::fromString (savedRouting);
        if (macrobake::bake (eqbands::accessFor (apvts), routing, &laws))
            apvts.state.setProperty (statekeys::mixLaw, laws.toString(), nullptr);
    }

    // 0.1 - 0.3 routings list every module: keep only what can be heard (sound unchanged)
    apvts.state.setProperty (statekeys::routing, migrateRoutingString (savedRouting, apvts), nullptr);

    // A host writes raw normalised values (a switch can hold 0.37) and JUCE's bool parameter keeps them;
    // replaceState only writes a parameter whose snapped value changes. The host reads the normalised
    // value back after a load (VST3 setComponentState): it must be the loaded one, not the raw leftover.
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (const float snapped = rp->convertTo0to1 (rp->convertFrom0to1 (rp->getValue())); snapped != rp->getValue())
                rp->setValue (snapped);   // same plain value: nothing to notify

    syncRoutingFromState();
    syncMixLawFromState();
    macroMapper.syncPositions();   // the loaded positions are where the sliders stand; nothing to map
    autoMix.readState (autoMixHolder, savedVersion < 3);

    // Another project (or the host restoring one): the history of the old state means nothing
    undoHistory.clear();
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VelvoxAudioProcessor();
}
