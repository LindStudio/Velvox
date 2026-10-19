#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ProductInfo.h"
#include "StateKeys.h"

using namespace velvox;

namespace
{
    const juce::Identifier abTreeType { "ABSlots" };
    const juce::Identifier activeSlotAttr { "active" };
    const juce::Identifier slotIndexAttr { "slot" };
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
    apvts.state.addListener (this);
    syncRoutingFromState();

    apvts.addParameterListener (pid::pitchOn, this);
    analyzerTimer.startTimerHz (15);
}

VelvoxAudioProcessor::~VelvoxAudioProcessor()
{
    analyzerTimer.stopTimer();
    cancelPendingUpdate();
    apvts.removeParameterListener (pid::pitchOn, this);
    apvts.state.removeListener (this);
}

//==============================================================================
void VelvoxAudioProcessor::parameterChanged (const juce::String&, float)
{
    // May arrive on the audio thread (automation): report on the message thread
    triggerAsyncUpdate();
}

void VelvoxAudioProcessor::handleAsyncUpdate()
{
    updateHostLatency();
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

void VelvoxAudioProcessor::updateHostLatency()
{
    const int latency = chain.getLatencyFor (reader.pitchOn());
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
    return reader.read (routingPacked.load (std::memory_order_acquire), monoMonitor.load (std::memory_order_relaxed));
}

std::optional<float> VelvoxAudioProcessor::effectiveValueOf (const juce::String& id) const
{
    const auto e = readEffectiveSettings();
    if (id == pid::highGain)         return e.eq.highGain;
    if (id == pid::lowGain)          return e.eq.lowGain;
    if (id == pid::deessRange)       return e.deess.rangeDb;
    if (id == pid::satDrive)         return e.sat.driveDb;
    if (id == pid::satMix)           return e.sat.enabled ? e.sat.mix * 100.0f : 0.0f;
    if (id == pid::compThreshold)    return e.comp.thresholdDb;
    if (id == pid::compRatio)        return e.comp.ratio;
    if (id == pid::compAttack)       return e.comp.attackMs;
    if (id == pid::compMakeup)       return e.comp.makeupDb;
    if (id == pid::doublerMix)       return e.doubler.enabled ? e.doubler.mix * 100.0f : 0.0f;
    if (id == pid::stereoWidth)      return e.stereo.width * 100.0f;
    if (id == pid::reverbMix)        return e.reverbOn ? e.reverbMix * 100.0f : 0.0f;
    if (id == pid::delayMix)         return e.delayOn ? e.delayMix * 100.0f : 0.0f;
    if (id == pid::reverbDuckAmount) return e.reverbDuck.amountDb;
    if (id == pid::delayDuckAmount)  return e.delayDuck.amountDb;
    return std::nullopt;
}

dsp::MacroValues VelvoxAudioProcessor::readMacros() const noexcept
{
    return reader.readMacros();
}

//==============================================================================
dsp::Routing VelvoxAudioProcessor::getRouting() const noexcept
{
    return dsp::Routing::unpack (routingPacked.load (std::memory_order_acquire));
}

void VelvoxAudioProcessor::setRouting (const dsp::Routing& routing)
{
    // The state property is the source of truth; the listener updates the atomic
    apvts.state.setProperty (statekeys::routing, routing.toString(), nullptr);
}

void VelvoxAudioProcessor::syncRoutingFromState()
{
    const auto text = apvts.state.getProperty (statekeys::routing).toString();
    const auto routing = text.isEmpty() ? dsp::Routing::defaults() : dsp::Routing::fromString (text);
    routingPacked.store (routing.pack(), std::memory_order_release);
}

void VelvoxAudioProcessor::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    if (tree == apvts.state && property == statekeys::routing)
        syncRoutingFromState();
}

void VelvoxAudioProcessor::valueTreeRedirected (juce::ValueTree&)
{
    syncRoutingFromState();
}

//==============================================================================
void VelvoxAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    chain.prepare (sampleRate, samplesPerBlock);
    setLatencySamples (chain.getLatencyFor (reader.pitchOn()));
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

    auto settings = readEffectiveSettings();

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

    // Persist the A/B slots alongside the parameters
    juce::ValueTree ab (abTreeType);
    ab.setProperty (activeSlotAttr, activeSlot, nullptr);
    for (int i = 0; i < 2; ++i)
    {
        if (! slotValid[(size_t) i])
            continue;
        auto t = slots[(size_t) i].toValueTree();
        t.setProperty (slotIndexAttr, i, nullptr);
        ab.appendChild (t, nullptr);
    }

    auto existing = state.getChildWithName (abTreeType);
    if (existing.isValid())
        state.removeChild (existing, nullptr);
    state.appendChild (ab, nullptr);

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

    slotValid = { false, false };
    activeSlot = 0;

    const auto ab = tree.getChildWithName (abTreeType);
    if (ab.isValid())
    {
        activeSlot = juce::jlimit (0, 1, (int) ab.getProperty (activeSlotAttr, 0));
        for (const auto& child : ab)
        {
            const int idx = (int) child.getProperty (slotIndexAttr, -1);
            if (idx == 0 || idx == 1)
            {
                slots[(size_t) idx] = ParameterSnapshot::fromValueTree (child);
                slotValid[(size_t) idx] = true;
            }
        }
        tree.removeChild (ab, nullptr);
    }

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

    if (! tree.hasProperty (statekeys::routing))
        tree.setProperty (statekeys::routing, dsp::Routing::defaults().toString(), nullptr);

    tree.setProperty (statekeys::stateVersion, statekeys::currentStateVersion, nullptr);
    apvts.replaceState (tree);

    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (! savedIds.contains (rp->getParameterID()))
                rp->setValueNotifyingHost (rp->getDefaultValue());

    syncRoutingFromState();
    autoMix.readState (autoMixHolder, savedVersion < 3);
}

//==============================================================================
void VelvoxAudioProcessor::selectSlot (int slot)
{
    slot = juce::jlimit (0, 1, slot);
    if (slot == activeSlot)
        return;

    // Store what we are leaving, then recall the target (or keep current if empty)
    slots[(size_t) activeSlot] = ParameterSnapshot::capture (apvts, "AB");
    slotValid[(size_t) activeSlot] = true;

    if (slotValid[(size_t) slot])
        slots[(size_t) slot].applyTo (apvts, true);

    activeSlot = slot;
}

void VelvoxAudioProcessor::copyActiveSlotToOther()
{
    const int other = 1 - activeSlot;
    slots[(size_t) other] = ParameterSnapshot::capture (apvts, "AB");
    slotValid[(size_t) other] = true;
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VelvoxAudioProcessor();
}
