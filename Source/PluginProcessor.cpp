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
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "VelvoxState", createParameterLayout()),
      presetManager (apvts)
{
    cacheParameterPointers();
    apvts.state.setProperty (statekeys::routing, dsp::Routing::defaults().toString(), nullptr);
    apvts.state.addListener (this);
    syncRoutingFromState();
}

VelvoxAudioProcessor::~VelvoxAudioProcessor()
{
    apvts.state.removeListener (this);
}

const juce::String VelvoxAudioProcessor::getName() const
{
    return product::name;
}

void VelvoxAudioProcessor::cacheParameterPointers()
{
    const auto bind = [this] (Raw& r, const char* id)
    {
        r.p = apvts.getRawParameterValue (id);
        jassert (r.p != nullptr);
    };

    bind (raw.inputGain, pid::inputGain);         bind (raw.outputGain, pid::outputGain);
    bind (raw.bypass, pid::bypass);               bind (raw.safetyClip, pid::safetyClip);

    bind (raw.eqOn, pid::eqOn);                   bind (raw.eqMix, pid::eqMix);
    bind (raw.hpfOn, pid::hpfOn);                 bind (raw.hpfFreq, pid::hpfFreq);
    bind (raw.hpfSlope, pid::hpfSlope);           bind (raw.lowFreq, pid::lowFreq);
    bind (raw.lowGain, pid::lowGain);             bind (raw.midFreq, pid::midFreq);
    bind (raw.midGain, pid::midGain);             bind (raw.midQ, pid::midQ);
    bind (raw.highFreq, pid::highFreq);           bind (raw.highGain, pid::highGain);

    bind (raw.compOn, pid::compOn);               bind (raw.compThreshold, pid::compThreshold);
    bind (raw.compRatio, pid::compRatio);         bind (raw.compAttack, pid::compAttack);
    bind (raw.compRelease, pid::compRelease);     bind (raw.compKnee, pid::compKnee);
    bind (raw.compMakeup, pid::compMakeup);       bind (raw.compMix, pid::compMix);

    bind (raw.deessOn, pid::deessOn);             bind (raw.deessFreq, pid::deessFreq);
    bind (raw.deessThreshold, pid::deessThreshold); bind (raw.deessRange, pid::deessRange);
    bind (raw.deessListen, pid::deessListen);

    bind (raw.satOn, pid::satOn);                 bind (raw.satDrive, pid::satDrive);
    bind (raw.satOutput, pid::satOutput);         bind (raw.satMix, pid::satMix);

    bind (raw.delayOn, pid::delayOn);             bind (raw.delayTime, pid::delayTime);
    bind (raw.delaySync, pid::delaySync);         bind (raw.delayDivision, pid::delayDivision);
    bind (raw.delayFeedback, pid::delayFeedback); bind (raw.delayMix, pid::delayMix);
    bind (raw.delayLowCut, pid::delayLowCut);     bind (raw.delayHighCut, pid::delayHighCut);
    bind (raw.delayPingPong, pid::delayPingPong);

    bind (raw.reverbOn, pid::reverbOn);           bind (raw.reverbType, pid::reverbType);
    bind (raw.reverbMix, pid::reverbMix);         bind (raw.reverbDecay, pid::reverbDecay);
    bind (raw.reverbPreDelay, pid::reverbPreDelay); bind (raw.reverbSize, pid::reverbSize);
    bind (raw.reverbDamping, pid::reverbDamping); bind (raw.reverbLowCut, pid::reverbLowCut);
    bind (raw.reverbWidth, pid::reverbWidth);
    bind (raw.delayToReverb, pid::delayToReverb);
    bind (raw.stereoWidth, pid::stereoWidth);     bind (raw.monoBelow, pid::monoBelow);
    bind (raw.levelMatchMode, pid::levelMatchMode); bind (raw.levelMatchWindow, pid::levelMatchWindow);
    bind (raw.levelMatchRange, pid::levelMatchRange);
    bind (raw.doublerOn, pid::doublerOn);         bind (raw.doublerMix, pid::doublerMix);
    bind (raw.doublerWidth, pid::doublerWidth);   bind (raw.doublerDelay, pid::doublerDelay);
    bind (raw.doublerDetune, pid::doublerDetune); bind (raw.doublerMonoSafe, pid::doublerMonoSafe);
    bind (raw.doublerTone, pid::doublerTone);
}

dsp::ChainSettings VelvoxAudioProcessor::readSettings() const noexcept
{
    dsp::ChainSettings s;

    s.inputGainDb = raw.inputGain.get();
    s.outputGainDb = raw.outputGain.get();
    s.bypass = raw.bypass.on();
    s.safetyClip = raw.safetyClip.on();

    s.eqOn = raw.eqOn.on();
    s.eqMix = raw.eqMix.get() * 0.01f;
    s.eq.hpfOn = raw.hpfOn.on();
    s.eq.hpfFreq = raw.hpfFreq.get();
    s.eq.hpfSlope = raw.hpfSlope.index();
    s.eq.lowFreq = raw.lowFreq.get();
    s.eq.lowGain = raw.lowGain.get();
    s.eq.midFreq = raw.midFreq.get();
    s.eq.midGain = raw.midGain.get();
    s.eq.midQ = raw.midQ.get();
    s.eq.highFreq = raw.highFreq.get();
    s.eq.highGain = raw.highGain.get();

    s.compOn = raw.compOn.on();
    s.compMix = raw.compMix.get() * 0.01f;
    s.comp.thresholdDb = raw.compThreshold.get();
    s.comp.ratio = raw.compRatio.get();
    s.comp.attackMs = raw.compAttack.get();
    s.comp.releaseMs = raw.compRelease.get();
    s.comp.kneeDb = raw.compKnee.get();
    s.comp.makeupDb = raw.compMakeup.get();

    s.deessOn = raw.deessOn.on();
    s.deess.freq = raw.deessFreq.get();
    s.deess.thresholdDb = raw.deessThreshold.get();
    s.deess.rangeDb = raw.deessRange.get();
    s.deess.listen = raw.deessListen.on();

    s.sat.enabled = raw.satOn.on();
    s.sat.driveDb = raw.satDrive.get();
    s.sat.outputDb = raw.satOutput.get();
    s.sat.mix = raw.satMix.get() * 0.01f;

    s.delayOn = raw.delayOn.on();
    s.delayMix = raw.delayMix.get() * 0.01f;
    s.delayTimeMs = raw.delayTime.get();
    s.delaySync = raw.delaySync.on();
    s.delayDivision = raw.delayDivision.index();
    s.delay.feedback = raw.delayFeedback.get() * 0.01f;
    s.delay.lowCutHz = raw.delayLowCut.get();
    s.delay.highCutHz = raw.delayHighCut.get();
    s.delay.pingPong = raw.delayPingPong.on();

    s.reverbOn = raw.reverbOn.on();
    s.reverbMix = raw.reverbMix.get() * 0.01f;
    s.reverb.type = raw.reverbType.index();
    s.reverb.decaySeconds = raw.reverbDecay.get();
    s.reverb.preDelayMs = raw.reverbPreDelay.get();
    s.reverb.size = raw.reverbSize.get() * 0.01f;
    s.reverb.dampingHz = raw.reverbDamping.get();
    s.reverb.lowCutHz = raw.reverbLowCut.get();
    s.reverb.width = raw.reverbWidth.get() * 0.01f;

    s.delayIntoReverb = raw.delayToReverb.get() * 0.01f;
    s.routing = dsp::Routing::unpack (routingPacked.load (std::memory_order_acquire));

    s.stereo.width = raw.stereoWidth.get() * 0.01f;
    s.stereo.monoBelowHz = raw.monoBelow.get();

    s.levelMatch = (dsp::ChainSettings::LevelMatch) juce::jlimit (0, 2, raw.levelMatchMode.index());
    static constexpr float windows[] { 0.4f, 3.0f, 10.0f };
    s.levelMatchWindowSeconds = windows[juce::jlimit (0, 2, raw.levelMatchWindow.index())];
    s.levelMatchRangeDb = raw.levelMatchRange.get();
    s.monoMonitor = monoMonitor.load (std::memory_order_relaxed);

    s.doubler.enabled = raw.doublerOn.on();
    s.doubler.mix = raw.doublerMix.get() * 0.01f;
    s.doubler.width = raw.doublerWidth.get() * 0.01f;
    s.doubler.delayMs = raw.doublerDelay.get();
    s.doubler.detuneCents = raw.doublerDetune.get();
    s.doubler.monoSafe = raw.doublerMonoSafe.get() * 0.01f;
    s.doubler.toneHz = raw.doublerTone.get();

    return s;
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
    setLatencySamples (chain.getLatencySamples());
}

void VelvoxAudioProcessor::releaseResources()
{
}

bool VelvoxAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

juce::AudioProcessorParameter* VelvoxAudioProcessor::getBypassParameter() const
{
    return apvts.getParameter (pid::bypass);
}

void VelvoxAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    auto settings = readSettings();

    if (auto* hostPlayHead = getPlayHead())
        if (const auto position = hostPlayHead->getPosition())
            if (const auto bpm = position->getBpm())
                settings.hostBpm = *bpm;

    chain.process (buffer, std::min (numIn, numOut), settings);
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
