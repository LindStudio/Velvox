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
    analyzer.setMode (raw.checkMode.index() == 1 ? dsp::VocalAnalyzer::Mode::capture : dsp::VocalAnalyzer::Mode::continuous);
    if (raw.checkOn.on())
    {
        analyzer.pump();
        if (juce::Time::getMillisecondCounterHiRes() - lastAnalysisMs > 500.0)
            refreshAnalysis();
    }
}

void VelvoxAudioProcessor::updateHostLatency()
{
    const int latency = chain.getLatencyFor (raw.pitchOn.on());
    if (latency != getLatencySamples())
        setLatencySamples (latency);
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
    bind (raw.duckSource, pid::duckSource);
    bind (raw.delayDuckAmount, pid::delayDuckAmount);       bind (raw.delayDuckThreshold, pid::delayDuckThreshold);
    bind (raw.delayDuckAttack, pid::delayDuckAttack);       bind (raw.delayDuckRelease, pid::delayDuckRelease);
    bind (raw.reverbDuckAmount, pid::reverbDuckAmount);     bind (raw.reverbDuckThreshold, pid::reverbDuckThreshold);
    bind (raw.reverbDuckAttack, pid::reverbDuckAttack);     bind (raw.reverbDuckRelease, pid::reverbDuckRelease);
    bind (raw.pitchOn, pid::pitchOn);                       bind (raw.pitchSemitones, pid::pitchSemitones);
    bind (raw.pitchCents, pid::pitchCents);                 bind (raw.pitchPreserveFormants, pid::pitchPreserveFormants);
    bind (raw.pitchFormant, pid::pitchFormant);             bind (raw.pitchMix, pid::pitchMix);
    bind (raw.chopOn, pid::chopOn);                         bind (raw.chopPattern, pid::chopPattern);
    bind (raw.chopRate, pid::chopRate);                     bind (raw.chopDepth, pid::chopDepth);
    bind (raw.chopSmooth, pid::chopSmooth);
    bind (raw.lowType, pid::lowType);           bind (raw.lowQ, pid::lowQ);
    bind (raw.highType, pid::highType);         bind (raw.highQ, pid::highQ);
    bind (raw.lpfOn, pid::lpfOn);               bind (raw.lpfFreq, pid::lpfFreq);
    bind (raw.lpfSlope, pid::lpfSlope);         bind (raw.satCharacter, pid::satCharacter);
    bind (raw.deessMode, pid::deessMode);       bind (raw.compDetector, pid::compDetector);
    bind (raw.compScHpf, pid::compScHpf);
    bind (raw.macroPresence, pid::macroPresence);   bind (raw.macroWarmth, pid::macroWarmth);
    bind (raw.macroAggression, pid::macroAggression); bind (raw.macroWidth, pid::macroWidth);
    bind (raw.macroSpace, pid::macroSpace);         bind (raw.macroDensity, pid::macroDensity);
    bind (raw.pulseOn, pid::pulseOn);           bind (raw.pulseRate, pid::pulseRate);
    bind (raw.pulseDepth, pid::pulseDepth);     bind (raw.pulseAttack, pid::pulseAttack);
    bind (raw.pulseRelease, pid::pulseRelease); bind (raw.pulseSmooth, pid::pulseSmooth);
    bind (raw.pulseInterp, pid::pulseInterp);   bind (raw.pulseTension, pid::pulseTension);
    bind (raw.pulseMix, pid::pulseMix);         bind (raw.pulseBpm, pid::pulseBpm);
    for (size_t i = 0; i < raw.pulseX.size(); ++i)
    {
        bind (raw.pulseX[i], pid::pulseX[i]);
        bind (raw.pulseY[i], pid::pulseY[i]);
    }
    bind (raw.smashOn, pid::smashOn);           bind (raw.smashAmount, pid::smashAmount);
    bind (raw.smashBlend, pid::smashBlend);     bind (raw.smashAttack, pid::smashAttack);
    bind (raw.smashRelease, pid::smashRelease); bind (raw.smashTone, pid::smashTone);
    bind (raw.smashOutput, pid::smashOutput);
    bind (raw.choirOn, pid::choirOn);           bind (raw.choirVoices, pid::choirVoices);
    bind (raw.choirDetune, pid::choirDetune);   bind (raw.choirTiming, pid::choirTiming);
    bind (raw.choirHumanize, pid::choirHumanize); bind (raw.choirWidth, pid::choirWidth);
    bind (raw.choirDensity, pid::choirDensity); bind (raw.choirMix, pid::choirMix);
    bind (raw.smoothOn, pid::smoothOn);         bind (raw.smoothAmount, pid::smoothAmount);
    bind (raw.smoothSensitivity, pid::smoothSensitivity); bind (raw.smoothLow, pid::smoothLow);
    bind (raw.smoothHigh, pid::smoothHigh);     bind (raw.smoothAttack, pid::smoothAttack);
    bind (raw.smoothRelease, pid::smoothRelease); bind (raw.smoothMode, pid::smoothMode);
    bind (raw.smoothOutput, pid::smoothOutput); bind (raw.smoothDelta, pid::smoothDelta);
    bind (raw.checkOn, pid::checkOn);           bind (raw.checkMode, pid::checkMode);
    bind (raw.rideOn, pid::rideOn);             bind (raw.rideTarget, pid::rideTarget);
    bind (raw.rideAmount, pid::rideAmount);     bind (raw.rideMaxBoost, pid::rideMaxBoost);
    bind (raw.rideMaxCut, pid::rideMaxCut);     bind (raw.rideResponse, pid::rideResponse);
    bind (raw.ridePhrase, pid::ridePhrase);     bind (raw.rideTransient, pid::rideTransient);
    bind (raw.rideSilence, pid::rideSilence);
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

    s.duckExternal = raw.duckSource.index() == 1;
    s.delayDuck = { raw.delayDuckAmount.get(), raw.delayDuckThreshold.get(), raw.delayDuckAttack.get(), raw.delayDuckRelease.get() };
    s.reverbDuck = { raw.reverbDuckAmount.get(), raw.reverbDuckThreshold.get(), raw.reverbDuckAttack.get(), raw.reverbDuckRelease.get() };

    s.pitch.enabled = raw.pitchOn.on();
    s.pitch.semitones = raw.pitchSemitones.get();
    s.pitch.cents = raw.pitchCents.get();
    s.pitch.preserveFormants = raw.pitchPreserveFormants.on();
    s.pitch.formantShift = raw.pitchFormant.get();
    s.pitch.mix = raw.pitchMix.get() * 0.01f;

    s.chop.enabled = raw.chopOn.on();
    s.chop.pattern = raw.chopPattern.index();
    s.chop.rate = raw.chopRate.index();
    s.chop.depth = raw.chopDepth.get() * 0.01f;
    s.chop.smoothMs = raw.chopSmooth.get();

    s.eq.lowType = raw.lowType.index();
    s.eq.lowQ = raw.lowQ.get();
    s.eq.highType = raw.highType.index();
    s.eq.highQ = raw.highQ.get();
    s.eq.lpfOn = raw.lpfOn.on();
    s.eq.lpfFreq = raw.lpfFreq.get();
    s.eq.lpfSlope = raw.lpfSlope.index();
    s.sat.character = raw.satCharacter.index();
    s.deess.mode = raw.deessMode.index();
    s.comp.detector = raw.compDetector.index();
    s.comp.sidechainHpfHz = raw.compScHpf.get();

    s.pulse.enabled = raw.pulseOn.on();
    s.pulse.rate = raw.pulseRate.index();
    s.pulse.depth = raw.pulseDepth.get() * 0.01f;
    s.pulse.attackMs = raw.pulseAttack.get();
    s.pulse.releaseMs = raw.pulseRelease.get();
    s.pulse.smoothMs = raw.pulseSmooth.get();
    s.pulse.interpolation = raw.pulseInterp.index();
    s.pulse.tension = raw.pulseTension.get() * 0.01f;
    s.pulse.mix = raw.pulseMix.get() * 0.01f;
    s.pulse.manualBpm = raw.pulseBpm.get();
    for (size_t i = 0; i < raw.pulseX.size(); ++i)
    {
        s.pulse.x[i] = raw.pulseX[i].get();
        s.pulse.y[i] = raw.pulseY[i].get();
    }

    s.smash.enabled = raw.smashOn.on();
    s.smash.amount = raw.smashAmount.get() * 0.01f;
    s.smash.blend = raw.smashBlend.get() * 0.01f;
    s.smash.attackMs = raw.smashAttack.get();
    s.smash.releaseMs = raw.smashRelease.get();
    s.smash.tone = raw.smashTone.get() * 0.01f;
    s.smash.outputDb = raw.smashOutput.get();

    s.choir.enabled = raw.choirOn.on();
    s.choir.voices = dsp::ChoirModule::voiceCountForIndex (raw.choirVoices.index());
    s.choir.detuneCents = raw.choirDetune.get();
    s.choir.timingMs = raw.choirTiming.get();
    s.choir.humanize = raw.choirHumanize.get() * 0.01f;
    s.choir.width = raw.choirWidth.get() * 0.01f;
    s.choir.density = raw.choirDensity.get() * 0.01f;
    s.choir.mix = raw.choirMix.get() * 0.01f;

    s.smooth.enabled = raw.smoothOn.on();
    s.smooth.amount = raw.smoothAmount.get() * 0.01f;
    s.smooth.sensitivity = raw.smoothSensitivity.get() * 0.01f;
    s.smooth.lowHz = raw.smoothLow.get();
    s.smooth.highHz = raw.smoothHigh.get();
    s.smooth.attackMs = raw.smoothAttack.get();
    s.smooth.releaseMs = raw.smoothRelease.get();
    s.smooth.mode = raw.smoothMode.index();
    s.smooth.outputDb = raw.smoothOutput.get();
    s.smooth.delta = raw.smoothDelta.on();

    s.checkOn = raw.checkOn.on();

    s.ride.enabled = raw.rideOn.on();
    s.ride.targetDb = raw.rideTarget.get();
    s.ride.amount = raw.rideAmount.get() * 0.01f;
    s.ride.maxBoostDb = raw.rideMaxBoost.get();
    s.ride.maxCutDb = raw.rideMaxCut.get();
    s.ride.response = raw.rideResponse.get() * 0.01f;
    s.ride.phrase = raw.ridePhrase.get() * 0.01f;
    s.ride.transient = raw.rideTransient.get() * 0.01f;
    s.ride.silenceDb = raw.rideSilence.get();

    return s;
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
    dsp::MacroValues m;
    m.presence = raw.macroPresence.get() * 0.01f;
    m.warmth = raw.macroWarmth.get() * 0.01f;
    m.aggression = raw.macroAggression.get() * 0.01f;
    m.width = raw.macroWidth.get() * 0.01f;
    m.space = raw.macroSpace.get() * 0.01f;
    m.density = raw.macroDensity.get() * 0.01f;
    return m;
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
    setLatencySamples (chain.getLatencyFor (raw.pitchOn.on()));
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

    measureLevels (main, std::min (mainIn, main.getNumChannels()), hasSidechain ? &sidechain : nullptr);
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
