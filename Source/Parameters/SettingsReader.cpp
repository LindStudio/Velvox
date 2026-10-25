#include "SettingsReader.h"

namespace velvox
{
    void SettingsReader::bind (const Lookup& lookup)
    {
        bindParams (raw, lookup, 0);
        for (int k = 1; k <= dsp::maxExtraInstances; ++k)
            bindParams (extraRaw[(size_t) k - 1], lookup, k);
    }

    // Binds every field. For instance k >= 1 the module parameters use their bank IDs
    // ("lowGain_2"); fields without a bank (global, output, macros, single-instance
    // modules) fall back to the instance-1 parameter and are never used from that bank.
    void SettingsReader::bindParams (Params& raw, const Lookup& lookup, int instance)
    {
        const auto bind = [&lookup, instance] (Raw& r, const char* id)
        {
            r.p = nullptr;
            if (instance > 0)
                r.p = lookup (dsp::instanceParamId (id, instance).toRawUTF8());
            if (r.p == nullptr)
                r.p = lookup (id);
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
    for (size_t i = 0; i < raw.smoothWFreq.size(); ++i)
    {
        bind (raw.smoothWFreq[i], pid::smoothWFreq[i]);
        bind (raw.smoothWGain[i], pid::smoothWGain[i]);
    }
    bind (raw.checkOn, pid::checkOn);           bind (raw.checkMode, pid::checkMode);
    bind (raw.rideOn, pid::rideOn);             bind (raw.rideTarget, pid::rideTarget);
    bind (raw.rideAmount, pid::rideAmount);     bind (raw.rideMaxBoost, pid::rideMaxBoost);
    bind (raw.rideMaxCut, pid::rideMaxCut);     bind (raw.rideResponse, pid::rideResponse);
    bind (raw.ridePhrase, pid::ridePhrase);     bind (raw.rideTransient, pid::rideTransient);
    bind (raw.rideSilence, pid::rideSilence);
    bind (raw.reverbEarly, pid::reverbEarly);   bind (raw.reverbDiffusion, pid::reverbDiffusion);
    bind (raw.reverbMod, pid::reverbMod);       bind (raw.reverbBass, pid::reverbBass);
    bind (raw.reverbHighCut, pid::reverbHighCut);
    bind (raw.delaySpread, pid::delaySpread);   bind (raw.delayDrive, pid::delayDrive);
    bind (raw.delayDiffusion, pid::delayDiffusion); bind (raw.delayMod, pid::delayMod);
    for (int n = 0; n < dsp::maxEqBands; ++n)
    {
        auto& b = raw.eqBands[(size_t) n];
        const auto bindBand = [&] (Raw& r, const char* field) { bind (r, pid::eqBandId (n + 1, field).toRawUTF8()); };
        bindBand (b.on, pid::eqband::on);       bindBand (b.type, pid::eqband::type);
        bindBand (b.freq, pid::eqband::freq);   bindBand (b.gain, pid::eqband::gain);
        bindBand (b.q, pid::eqband::q);         bindBand (b.slope, pid::eqband::slope);
        bindBand (b.role, pid::eqband::role);
    }
    bind (raw.eqLegacyResponse, pid::eqLegacyResponse);
    }

    dsp::ChainSettings SettingsReader::read (const dsp::Routing& routing, bool monoMonitor) const noexcept
    {
        auto s = readParams (raw);
        s.routing = routing;
        s.monoMonitor = monoMonitor;
        for (int k = 1; k <= dsp::maxExtraInstances; ++k)
            s.extra[(size_t) k - 1] = readParams (extraRaw[(size_t) k - 1]).instance0();
        return s;
    }

    dsp::ChainSettings SettingsReader::readParams (const Params& raw) noexcept
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
    s.delay.spreadMs = raw.delaySpread.get();
    s.delay.drive = raw.delayDrive.get() * 0.01f;
    s.delay.diffusion = raw.delayDiffusion.get() * 0.01f;
    s.delay.modulation = raw.delayMod.get() * 0.01f;

    s.reverbOn = raw.reverbOn.on();
    s.reverbMix = raw.reverbMix.get() * 0.01f;
    s.reverb.type = raw.reverbType.index();
    s.reverb.decaySeconds = raw.reverbDecay.get();
    s.reverb.preDelayMs = raw.reverbPreDelay.get();
    s.reverb.size = raw.reverbSize.get() * 0.01f;
    s.reverb.dampingHz = raw.reverbDamping.get();
    s.reverb.lowCutHz = raw.reverbLowCut.get();
    s.reverb.width = raw.reverbWidth.get() * 0.01f;
    s.reverb.early = raw.reverbEarly.get() * 0.01f;
    s.reverb.diffusion = raw.reverbDiffusion.get() * 0.01f;
    s.reverb.modulation = raw.reverbMod.get() * 0.01f;
    s.reverb.bassMultiplier = raw.reverbBass.get();
    s.reverb.highCutHz = raw.reverbHighCut.get();

    s.delayIntoReverb = raw.delayToReverb.get() * 0.01f;

    s.stereo.width = raw.stereoWidth.get() * 0.01f;
    s.stereo.monoBelowHz = raw.monoBelow.get();

    s.levelMatch = (dsp::ChainSettings::LevelMatch) juce::jlimit (0, 2, raw.levelMatchMode.index());
    static constexpr float windows[] { 0.4f, 3.0f, 10.0f };
    s.levelMatchWindowSeconds = windows[juce::jlimit (0, 2, raw.levelMatchWindow.index())];
    s.levelMatchRangeDb = raw.levelMatchRange.get();

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
    for (size_t n = 0; n < raw.eqBands.size(); ++n)
    {
        const auto& b = raw.eqBands[n];
        s.eq.bands[n] = { b.on.on(), b.type.index(), b.freq.get(), b.gain.get(), b.q.get(), b.slope.index(), b.role.index() };
    }
    s.eq.legacyResponse = raw.eqLegacyResponse.on();
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
    for (size_t i = 0; i < raw.smoothWFreq.size(); ++i)
    {
        s.smooth.weightFreq[i] = raw.smoothWFreq[i].get();
        s.smooth.weightGain[i] = raw.smoothWGain[i].get();
    }

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

    dsp::MacroValues SettingsReader::readMacros() const noexcept
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

    //==========================================================================
    OfflineParameters::OfflineParameters()
    {
        for (const auto& spec : getParameterSpecs())
            store[spec.id] = std::make_unique<std::atomic<float>> (spec.defaultValue);
        reader.bind ([this] (const char* id) -> std::atomic<float>*
        {
            auto it = store.find (id);
            return it != store.end() ? it->second.get() : nullptr;
        });
    }

    void OfflineParameters::set (const juce::String& id, float value)
    {
        auto it = store.find (id);
        const auto* spec = findParameterSpec (id);
        if (it == store.end() || spec == nullptr || ! std::isfinite (value))
            return;
        it->second->store (juce::jlimit (spec->minValue, spec->maxValue, value));
    }

    void OfflineParameters::setAll (const std::map<juce::String, float>& values)
    {
        for (const auto& [id, v] : values)
            set (id, v);
    }

    float OfflineParameters::get (const juce::String& id) const
    {
        auto it = store.find (id);
        return it != store.end() ? it->second->load() : 0.0f;
    }
}
