#include "VocalChain.h"

namespace velvox::dsp
{
    namespace
    {
        constexpr float clipThreshold = 0.891251f; // -1 dBFS

        template <typename Module>
        void runGated (ModuleGate& gate, Module& module, float* const* io, int numChannels, int numSamples) noexcept
        {
            if (gate.consumeWakeUp())
                module.reset();

            if (gate.isFullyBypassed())
                return;

            gate.captureDry (io, numChannels, numSamples);
            module.process (io, numChannels, numSamples);
            gate.mix (io, numChannels, numSamples);
        }

        template <typename Array>
        void prepareAll (Array& modules, double sampleRate, int bs)
        {
            for (auto& m : modules)
                m.prepare (sampleRate, bs);
        }

        template <typename Array>
        void resetAll (Array& modules)
        {
            for (auto& m : modules)
                m.reset();
        }
    }

    InstanceSettings ChainSettings::instance0() const noexcept
    {
        InstanceSettings b;
        b.eqOn = eqOn;          b.eqMix = eqMix;        b.eq = eq;
        b.compOn = compOn;      b.compMix = compMix;    b.comp = comp;
        b.deessOn = deessOn;    b.deess = deess;
        b.sat = sat;
        b.doubler = doubler;
        b.delayOn = delayOn;    b.delayMix = delayMix;  b.delay = delay;
        b.delaySync = delaySync; b.delayDivision = delayDivision; b.delayTimeMs = delayTimeMs;
        b.reverbOn = reverbOn;  b.reverbMix = reverbMix; b.reverb = reverb;
        b.delayLawMix = delayLawMix; b.reverbLawMix = reverbLawMix;
        b.delayDuck = delayDuck; b.reverbDuck = reverbDuck;
        b.smash = smash;
        b.choir = choir;
        b.smooth = smooth;
        b.ride = ride;
        return b;
    }

    void ChainSettings::setInstance0 (const InstanceSettings& b) noexcept
    {
        eqOn = b.eqOn;          eqMix = b.eqMix;        eq = b.eq;
        compOn = b.compOn;      compMix = b.compMix;    comp = b.comp;
        deessOn = b.deessOn;    deess = b.deess;
        sat = b.sat;
        doubler = b.doubler;
        delayOn = b.delayOn;    delayMix = b.delayMix;  delay = b.delay;
        delaySync = b.delaySync; delayDivision = b.delayDivision; delayTimeMs = b.delayTimeMs;
        reverbOn = b.reverbOn;  reverbMix = b.reverbMix; reverb = b.reverb;
        delayLawMix = b.delayLawMix; reverbLawMix = b.reverbLawMix;
        delayDuck = b.delayDuck; reverbDuck = b.reverbDuck;
        smash = b.smash;
        choir = b.choir;
        smooth = b.smooth;
        ride = b.ride;
    }

    float VocalChain::safetyClip (float x) noexcept
    {
        const float a = std::abs (x);
        if (a <= clipThreshold)
            return x;

        // Smooth knee from -1 dBFS that converges to 0 dBFS (slope 1 at the knee)
        constexpr float headroom = 1.0f - clipThreshold;
        const float y = clipThreshold + headroom * std::tanh ((a - clipThreshold) / headroom);
        return x < 0.0f ? -y : y;
    }

    void VocalChain::prepare (double sampleRate, int)
    {
        fs = sampleRate;
        const int bs = internalBlockSize;

        work.setSize (maxChannels, bs, false, true, false);
        dryForBypass.setSize (maxChannels, bs, false, true, false);
        reverbIn.setSize (maxChannels, bs, false, true, false);
        for (auto& b : delayWet)
            b.setSize (maxChannels, bs, false, true, false);
        for (auto& b : reverbWet)
            b.setSize (maxChannels, bs, false, true, false);

        prepareAll (eq, sampleRate, bs);
        prepareAll (comp, sampleRate, bs);
        prepareAll (deess, sampleRate, bs);
        prepareAll (saturation, sampleRate, bs);
        prepareAll (delay, sampleRate, bs);
        prepareAll (reverb, sampleRate, bs);
        prepareAll (doubler, sampleRate, bs);
        prepareAll (smash, sampleRate, bs);
        prepareAll (choir, sampleRate, bs);
        prepareAll (smooth, sampleRate, bs);
        prepareAll (ride, sampleRate, bs);
        for (auto& d : delayDucker)
            d.prepare (sampleRate);
        for (auto& d : reverbDucker)
            d.prepare (sampleRate);
        for (auto& g : delayDuckGains)
            g.assign ((size_t) bs, 1.0f);
        for (auto& g : reverbDuckGains)
            g.assign ((size_t) bs, 1.0f);

        for (auto& g : eqGate)
            g.prepare (sampleRate, bs);
        for (auto& g : compGate)
            g.prepare (sampleRate, bs);
        for (auto& g : deessGate)
            g.prepare (sampleRate, bs);

        pitch.prepare (sampleRate, bs);
        chop.prepare (sampleRate, bs);
        pulse.prepare (sampleRate, bs);

        // Room for the largest possible latency: every saturation and smooth instance plus pitch
        int maxLatency = pitch.getLatencySamples();
        for (auto& sat : saturation)
            maxLatency += sat.getLatencySamples();
        for (auto& r : ride)
            maxLatency += r.getLatencySamples();
        for (auto& sm : smooth)
            maxLatency += sm.getLatencySamples();
        bypassDelay.prepare (std::max (1, maxLatency));
        bypassDelay.setDelay (getLatencySamples());

        stereo.prepare (sampleRate, bs);
        matcher.prepare (sampleRate);
        correlation.prepare (sampleRate);
        outputLoudness.prepare (sampleRate);
        for (auto* s : { &autoGain, &bypassGain })
        {
            s->reset (sampleRate, 0.05);
            s->setCurrentAndTargetValue (1.0f);
        }

        for (auto& s : delayReturn)
            s.reset (sampleRate, 0.03);
        for (auto& s : reverbReturn)
            s.reset (sampleRate, 0.03);
        for (auto& s : delayDry)
        {
            s.reset (sampleRate, 0.03);
            s.setCurrentAndTargetValue (1.0f);
        }
        for (auto& s : reverbDry)
        {
            s.reset (sampleRate, 0.03);
            s.setCurrentAndTargetValue (1.0f);
        }
        for (auto* s : { &wetAmount, &delayIntoReverb })
            s->reset (sampleRate, 0.03);
        inputGain.reset (sampleRate, 0.03);
        outputGain.reset (sampleRate, 0.03);

        // No fade on (re)start: take the latest routing directly
        routeGain.reset (sampleRate, 0.008);
        routeGain.setCurrentAndTargetValue (1.0f);
        adoptRoutingImmediately = true;

        prepared = true;
        meters.prepareCount.fetch_add (1, std::memory_order_relaxed);
        meters.samplesProcessed.store (0, std::memory_order_relaxed);
        reset();
    }

    void VocalChain::reset()
    {
        resetAll (eq);
        resetAll (comp);
        resetAll (deess);
        resetAll (saturation);
        resetAll (delay);
        resetAll (reverb);
        resetAll (doubler);
        chop.reset();
        pulse.reset();
        resetAll (smash);
        resetAll (choir);
        for (auto& m : smooth)
            m.reset (false);   // a fresh start: nothing to fade in
        resetAll (ride);
        resetAll (delayDucker);
        resetAll (reverbDucker);
        bypassDelay.reset();
        stereo.reset();
        matcher.reset();
        correlation.reset();
        outputLoudness.reset();
    }

    void VocalChain::resetInstance (const ChainSlot& slot) noexcept
    {
        const int k = slot.instance;
        switch (slot.id)
        {
            case ModuleId::eq:      eq[(size_t) k].reset(); break;
            case ModuleId::comp:    comp[(size_t) k].reset(); break;
            case ModuleId::deess:   deess[(size_t) k].reset(); break;
            case ModuleId::sat:     saturation[(size_t) k].reset(); break;
            case ModuleId::doubler: doubler[(size_t) k].reset(); break;
            case ModuleId::delay:   delay[(size_t) k].reset(); delayDucker[(size_t) k].reset(); break;
            case ModuleId::reverb:  reverb[(size_t) k].reset(); reverbDucker[(size_t) k].reset(); break;
            case ModuleId::smash:   smash[(size_t) k].reset(); break;
            case ModuleId::choir:   choir[(size_t) k].reset(); break;
            // joining mid-stream: silent until its delay line is filled, then a fade-in (no restart click)
            case ModuleId::smooth:  smooth[(size_t) k].reset (! adoptRoutingImmediately); break;
            case ModuleId::ride:    ride[(size_t) k].reset(); break;
            case ModuleId::chop:    chop.reset(); break;
            case ModuleId::pulse:   pulse.reset(); break;
            case ModuleId::pitch:
            case ModuleId::check:   break;
        }
    }

    void VocalChain::applyInstance (int k, const InstanceSettings& b, const ChainSettings& s) noexcept
    {
        const auto at = [k] (ModuleId id) { return k < maxInstances (id); };

        if (at (ModuleId::eq))
        {
            eq[(size_t) k].setSettings (b.eq);
            eqGate[(size_t) k].setTargets (b.eqOn, b.eqMix);
        }
        if (at (ModuleId::comp))
        {
            comp[(size_t) k].setSettings (b.comp);
            compGate[(size_t) k].setTargets (b.compOn, b.compMix);
        }
        if (at (ModuleId::deess))
        {
            deess[(size_t) k].setSettings (b.deess);
            // Listen mode must output the band alone, so it forces 100 % wet
            deessGate[(size_t) k].setTargets (b.deessOn, 1.0f);
        }
        if (at (ModuleId::sat))      saturation[(size_t) k].setSettings (b.sat);
        if (at (ModuleId::doubler))
        {
            auto d = b.doubler;
            d.mixLaw = s.mixLaws.lawOf (ModuleId::doubler, k);
            doubler[(size_t) k].setSettings (d);
        }
        if (at (ModuleId::smash))
        {
            auto sm = b.smash;
            sm.mixLaw = s.mixLaws.lawOf (ModuleId::smash, k);
            smash[(size_t) k].setSettings (sm);
        }
        if (at (ModuleId::choir))    choir[(size_t) k].setSettings (b.choir);
        if (at (ModuleId::smooth))   smooth[(size_t) k].setSettings (b.smooth);
        if (at (ModuleId::ride))     ride[(size_t) k].setSettings (b.ride);

        if (at (ModuleId::delay))
        {
            delayDucker[(size_t) k].setSettings (b.delayDuck);
            auto delaySettings = b.delay;
            delaySettings.timeMs = b.delayTimeMs;
            bool synced = false;
            if (b.delaySync)
            {
                const float t = DelayModule::timeForTempo (s.hostBpm, b.delayDivision);
                if (t > 0.0f)
                {
                    delaySettings.timeMs = t;
                    synced = true;
                }
            }
            if (k == 0)
            {
                meters.tempoSynced.store (synced, std::memory_order_relaxed);
                meters.effectiveDelayMs.store (delaySettings.timeMs, std::memory_order_relaxed);
            }
            delay[(size_t) k].setSettings (delaySettings);
            delayLaw[(size_t) k] = s.mixLaws.lawOf (ModuleId::delay, k);
            delayReturn[(size_t) k].setTargetValue (b.delayOn ? juce::jlimit (0.0f, 1.0f, b.delayMix) : 0.0f);
            delayDry[(size_t) k].setTargetValue (b.delayOn ? additiveDryGain (b.delayLawMix >= 0.0f ? b.delayLawMix : b.delayMix, delayLaw[(size_t) k]) : 1.0f);
        }

        if (at (ModuleId::reverb))
        {
            reverbDucker[(size_t) k].setSettings (b.reverbDuck);
            reverb[(size_t) k].setSettings (b.reverb);
            reverbLaw[(size_t) k] = s.mixLaws.lawOf (ModuleId::reverb, k);
            reverbReturn[(size_t) k].setTargetValue (b.reverbOn ? juce::jlimit (0.0f, 1.0f, b.reverbMix) : 0.0f);
            reverbDry[(size_t) k].setTargetValue (b.reverbOn ? additiveDryGain (b.reverbLawMix >= 0.0f ? b.reverbLawMix : b.reverbMix, reverbLaw[(size_t) k]) : 1.0f);
        }
    }

    void VocalChain::applySettings (const ChainSettings& s) noexcept
    {
        inputGain.setTargetValue (dbToGain (s.inputGainDb));
        outputGain.setTargetValue (dbToGain (s.outputGainDb));
        wetAmount.setTargetValue (s.bypass ? 0.0f : 1.0f);

        applyInstance (0, s.instance0(), s);
        for (int k = 1; k <= maxExtraInstances; ++k)
            applyInstance (k, s.extra[(size_t) k - 1], s);

        pitch.setSettings (s.pitch);
        chop.setSettings (s.chop);
        pulse.setSettings (s.pulse);
        check.setEnabled (s.checkOn);
        delayIntoReverb.setTargetValue (juce::jlimit (0.0f, 1.0f, s.delayIntoReverb));

        stereo.setSettings (s.stereo);
        matcher.setWindowSeconds (s.levelMatchWindowSeconds);
        matcher.setRangeDb (s.levelMatchRangeDb);

        if (s.routing.isValid())
            pendingRouting = s.routing;
        // The pitch shifter adds latency only while it is on AND in the chain
        pendingPitchOn = s.pitch.enabled && pendingRouting.contains (ModuleId::pitch);

        if (adoptRoutingImmediately)
        {
            applyPendingStructure();
            adoptRoutingImmediately = false;
        }

        // Tempo-locked modules after latent ones (SMOOTH, saturation, pitch) see the signal late; the
        // host shifts the output back by the whole latency, so they follow the song position delayed
        // by the latency in front of them and stay on the grid
        const auto transportFor = [&] (ModuleId id)
        {
            if (! s.hostPlaying || ! (s.hostBpm > 20.0 && s.hostBpm < 999.0) || fs <= 0.0)
                return s.hostPpq;
            return std::max (0.0, s.hostPpq - latencyBefore (id) / fs * s.hostBpm / 60.0);
        };
        chop.setTransport (s.hostBpm, transportFor (ModuleId::chop), s.hostPlaying);
        pulse.setTransport (s.hostBpm, transportFor (ModuleId::pulse), s.hostPlaying);
    }

    int VocalChain::latencyBefore (ModuleId id) const noexcept
    {
        int latency = 0;
        for (const auto& slot : activeRouting)
        {
            if (slot.id == id)
                return latency;
            if (slot.id == ModuleId::sat)
                latency += saturation[(size_t) slot.instance].getLatencySamples();
            else if (slot.id == ModuleId::smooth)
                latency += smooth[(size_t) slot.instance].getLatencySamples();
            else if (slot.id == ModuleId::pitch && activePitchOn)
                latency += pitch.getLatencySamples();
        }
        return latency;
    }

    void VocalChain::process (juce::AudioBuffer<float>& buffer, int numChannels, const ChainSettings& s,
                              const juce::AudioBuffer<float>* sidechain) noexcept
    {
        jassert (prepared);
        if (! prepared)
            return;

        const int ioChannels = juce::jlimit (0, maxChannels, std::min (numChannels, buffer.getNumChannels()));
        const int numSamples = buffer.getNumSamples();
        if (ioChannels == 0 || numSamples == 0)
            return;
        monoInput = ioChannels == 1;

        sanitize (buffer.getArrayOfWritePointers(), ioChannels, numSamples);
        applySettings (s);

        for (int start = 0; start < numSamples; start += internalBlockSize)
        {
            const int n = std::min (internalBlockSize, numSamples - start);

            // The chain always runs in stereo; a mono input is duplicated
            for (int ch = 0; ch < maxChannels; ++ch)
                work.copyFrom (ch, 0, buffer, std::min (ch, ioChannels - 1), start, n);

            // External ducking source for this chunk (falls back to internal)
            scChannels = 0;
            if (s.duckExternal && sidechain != nullptr && sidechain->getNumChannels() > 0
                && sidechain->getNumSamples() >= start + n)
            {
                scChannels = std::min (maxChannels, sidechain->getNumChannels());
                for (int ch = 0; ch < scChannels; ++ch)
                    scChunk[(size_t) ch] = sidechain->getReadPointer (ch, start);
            }
            meters.sidechainInUse.store (scChannels > 0, std::memory_order_relaxed);

            processChunk (work.getArrayOfWritePointers(), maxChannels, n, s);

            if (ioChannels == 1)
            {
                buffer.copyFrom (0, start, work, 0, 0, n);
                buffer.addFrom (0, start, work, 1, 0, n);
                buffer.applyGain (0, start, n, 0.5f);
            }
            else
            {
                for (int ch = 0; ch < ioChannels; ++ch)
                    buffer.copyFrom (ch, start, work, ch, 0, n);
            }
        }

        if (sanitize (buffer.getArrayOfWritePointers(), ioChannels, numSamples))
        {
            reset(); // something produced invalid values: start from a clean state
            meters.nonFiniteResets.fetch_add (1, std::memory_order_relaxed);
            meters.lastResetAtSample.store (meters.samplesProcessed.load (std::memory_order_relaxed), std::memory_order_relaxed);
        }
        meters.samplesProcessed.fetch_add (numSamples, std::memory_order_relaxed);

        for (int ch = 0; ch < ioChannels; ++ch)
            meters.output[(size_t) ch].push (buffer.getMagnitude (ch, 0, numSamples));
        if (ioChannels == 1)
            meters.output[1].push (buffer.getMagnitude (0, 0, numSamples));
    }

    void VocalChain::processChunk (float* const* io, int numChannels, int n, const ChainSettings& s) noexcept
    {
        // A mono track is duplicated internally; measure loudness on one channel
        // so LUFS values match what a meter on the mono track would show.
        const int loudnessChannels = monoInput ? 1 : numChannels;

        // 1) Untouched input: loudness reference, and latency-delayed copy for the bypass
        matcher.measureInput (io, loudnessChannels, n);
        for (int ch = 0; ch < numChannels; ++ch)
            dryForBypass.copyFrom (ch, 0, io[ch], n);
        bypassDelay.process (dryForBypass.getArrayOfWritePointers(), numChannels, n);

        // 2) Input gain + input meter
        for (int i = 0; i < n; ++i)
        {
            const float g = inputGain.getNextValue();
            for (int ch = 0; ch < numChannels; ++ch)
                io[ch][i] *= g;
        }
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto range = juce::FloatVectorOperations::findMinAndMax (io[ch], n);
            meters.input[(size_t) ch].push (std::max (range.getEnd(), -range.getStart()));
        }
        meters.inputTap.push (io, numChannels, n);

        // A pending routing change starts with a short fade-out of the old routing
        if (structureChangePending())
            routeGain.setTargetValue (0.0f);

        // 3) Serial module instances in routing order
        for (const auto& slot : activeRouting)
            processSerialModule (slot, io, numChannels, n);

        // 4) Parallel sends, fed from the end of the serial chain. They return
        //    wet only, so the dry signal is present exactly once.
        processSends (io, numChannels, n, s);

        applyRoutingFade (io, numChannels, n);

        // 5) Stereo width (side only - the mono sum is never changed)
        stereo.process (io, numChannels, n);

        // 6) Level matching: compare processed loudness with the input
        matcher.measureProcessed (io, loudnessChannels, n);
        matcher.update (n);
        const float correctionDb = matcher.getCorrectionDb();
        meters.inputLufs.store (matcher.getInputLufs(), std::memory_order_relaxed);
        meters.processedLufs.store (matcher.getProcessedLufs(), std::memory_order_relaxed);
        meters.levelMatchDb.store (correctionDb, std::memory_order_relaxed);

        using LM = ChainSettings::LevelMatch;
        autoGain.setTargetValue (s.levelMatch == LM::autoGain ? dbToGain (correctionDb) : 1.0f);

        // Bypass is matched to what you hear when processing is on
        float bypassDb = 0.0f;
        if (s.levelMatch == LM::bypassMatch)
            bypassDb = s.outputGainDb - correctionDb;
        else if (s.levelMatch == LM::autoGain)
            bypassDb = s.outputGainDb;
        bypassGain.setTargetValue (dbToGain (juce::jlimit (-24.0f, 24.0f, bypassDb)));

        // 7) Auto gain, output gain, clip detection, safety clipper
        bool clipped = false;
        for (int i = 0; i < n; ++i)
        {
            const float g = outputGain.getNextValue() * autoGain.getNextValue();
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float y = io[ch][i] * g;
                if (std::abs (y) > 1.0f)
                    clipped = true;
                if (s.safetyClip)
                    y = safetyClip (y);
                io[ch][i] = y;
            }
        }
        if (clipped)
            meters.clipped.store (true, std::memory_order_relaxed);

        // 8) Global bypass crossfade against the latency-aligned (and optionally
        //    loudness-matched) input
        if (wetAmount.isSmoothing() || wetAmount.getCurrentValue() < 1.0f)
        {
            for (int i = 0; i < n; ++i)
            {
                const float w = wetAmount.getNextValue();
                const float bg = bypassGain.getNextValue();
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    float d = dryForBypass.getSample (ch, i) * bg;
                    if (bg > 1.0f && s.safetyClip)
                        d = safetyClip (d);
                    io[ch][i] = d + (io[ch][i] - d) * w;
                }
            }
        }
        else
        {
            bypassGain.skip (n);
        }

        // 9) Correlation of what leaves the plugin, then the mono listening aid
        correlation.process (io[0], io[numChannels > 1 ? 1 : 0], n);
        meters.correlation.store (correlation.getCorrelation(), std::memory_order_relaxed);
        outputLoudness.process (io, loudnessChannels, n);
        meters.outputLufs.store (outputLoudness.getLufs(), std::memory_order_relaxed);
        meters.outputTap.push (io, numChannels, n);
        meters.scopeTap.push (io, numChannels, n);

        if (s.monoMonitor && numChannels > 1)
        {
            for (int i = 0; i < n; ++i)
            {
                const float m = 0.5f * (io[0][i] + io[1][i]);
                io[0][i] = m;
                io[1][i] = m;
            }
        }
    }

    //==========================================================================
    bool VocalChain::wakeIfNeeded (int k, bool isDelay) noexcept
    {
        auto& ret = isDelay ? delayReturn[(size_t) k] : reverbReturn[(size_t) k];
        bool& active = isDelay ? delayActive[(size_t) k] : reverbActive[(size_t) k];
        const bool needed = ret.getTargetValue() > 0.0f || ret.isSmoothing();
        if (needed && ! active)
        {
            if (isDelay)
                delay[(size_t) k].reset();
            else
                reverb[(size_t) k].reset();
        }
        active = needed;
        return needed;
    }

    void VocalChain::processSerialModule (const ChainSlot& slot, float* const* io, int numChannels, int n) noexcept
    {
        const auto k = (size_t) slot.instance;
        switch (slot.id)
        {
            case ModuleId::eq:      runGated (eqGate[k], eq[k], io, numChannels, n); break;
            case ModuleId::comp:    runGated (compGate[k], comp[k], io, numChannels, n); break;
            case ModuleId::deess:   runGated (deessGate[k], deess[k], io, numChannels, n); break;
            case ModuleId::sat:     saturation[k].process (io, numChannels, n); break; // own on/off + mix, constant latency

            case ModuleId::doubler: doubler[k].process (io, numChannels, n); break; // own on/off, adds voices only
            case ModuleId::pitch:
                if (activePitchOn)
                    pitch.process (io, numChannels, n);
                break;

            case ModuleId::chop:    chop.process (io, numChannels, n); break; // own on/off, zero latency

            // 0.3 modules: each has its own click-free on/off and does nothing at all while off
            case ModuleId::pulse:   pulse.process (io, numChannels, n); break;
            case ModuleId::smash:   smash[k].process (io, numChannels, n); break;
            case ModuleId::choir:   choir[k].process (io, numChannels, n); break;
            case ModuleId::smooth:  smooth[k].process (io, numChannels, n); break;
            case ModuleId::check:   check.process (io, numChannels, n); break; // read only
            case ModuleId::ride:    ride[k].process (io, numChannels, n); break;

            case ModuleId::delay:
            case ModuleId::reverb:
            {
                if (! slot.insert)
                    break;   // send: runs in processSends

                const bool isDelay = slot.id == ModuleId::delay;
                auto& ret = isDelay ? delayReturn[k] : reverbReturn[k];
                auto& dry = isDelay ? delayDry[k] : reverbDry[k];
                auto& wet = isDelay ? delayWet[k] : reverbWet[k];

                if (! wakeIfNeeded ((int) k, isDelay))
                {
                    ret.skip (n);
                    dry.skip (n);
                    break;
                }

                // Ducking follows the module input (or the sidechain)
                const float* duck = isDelay ? duckGains (delayDucker[k], delayDuckGains[k], io, numChannels, n)
                                            : duckGains (reverbDucker[k], reverbDuckGains[k], io, numChannels, n);

                if (isDelay)
                    delay[k].process (io, wet.getArrayOfWritePointers(), numChannels, n);
                else
                    reverb[k].process (io, wet.getArrayOfWritePointers(), numChannels, n);

                const auto law = isDelay ? delayLaw[k] : reverbLaw[k];
                if (law == MixLaw::legacy)
                {
                    // Legacy insert (0.1-0.5): out = in + wet * mix (same law as the send)
                    for (int i = 0; i < n; ++i)
                    {
                        const float g = ret.getNextValue() * (duck != nullptr ? duck[i] : 1.0f);
                        for (int ch = 0; ch < numChannels; ++ch)
                            io[ch][i] += g * wet.getSample (ch, i);
                    }
                    dry.skip (n);
                }
                else
                {
                    // True dry/wet: the dry fades out above 50 % of the user's Mix (see MixLaw.h);
                    // ducking acts on the wet only
                    for (int i = 0; i < n; ++i)
                    {
                        const float dryGain = dry.getNextValue();
                        const float g = ret.getNextValue() * (duck != nullptr ? duck[i] : 1.0f);
                        for (int ch = 0; ch < numChannels; ++ch)
                            io[ch][i] = io[ch][i] * dryGain + g * wet.getSample (ch, i);
                    }
                }
                break;
            }
        }
    }

    void VocalChain::processSends (float* const* io, int numChannels, int n, const ChainSettings&) noexcept
    {
        // Which effect instances run as sends (and are audible)
        std::array<bool, numDelays> delayNeeded {};
        std::array<bool, numReverbs> reverbNeeded {};
        std::array<bool, numDelays> delaySend {};
        std::array<bool, numReverbs> reverbSend {};
        for (const auto& slot : activeRouting)
        {
            if (slot.insert)
                continue;
            if (slot.id == ModuleId::delay)
                delaySend[slot.instance] = true;
            else if (slot.id == ModuleId::reverb)
                reverbSend[slot.instance] = true;
        }

        bool any = false;
        std::array<const float*, numDelays> delayDuck {};
        std::array<const float*, numReverbs> reverbDuck {};
        for (int k = 0; k < numDelays; ++k)
        {
            delayNeeded[(size_t) k] = delaySend[(size_t) k] && wakeIfNeeded (k, true);
            if (delayNeeded[(size_t) k])
            {
                // Duck gains are derived from the dry vocal at the send tap (or the sidechain)
                delayDuck[(size_t) k] = duckGains (delayDucker[(size_t) k], delayDuckGains[(size_t) k], io, numChannels, n);
                delay[(size_t) k].process (io, delayWet[(size_t) k].getArrayOfWritePointers(), numChannels, n);
                any = true;
            }
        }

        for (int k = 0; k < numReverbs; ++k)
        {
            reverbNeeded[(size_t) k] = reverbSend[(size_t) k] && wakeIfNeeded (k, false);
            if (! reverbNeeded[(size_t) k])
                continue;
            any = true;
            reverbDuck[(size_t) k] = duckGains (reverbDucker[(size_t) k], reverbDuckGains[(size_t) k], io, numChannels, n);

            const float* const* reverbSource = io;

            // Delay -> reverb (first instances): the delay return (at its mix level) also feeds the reverb
            const bool coupled = k == 0 && delayNeeded[0] && (delayIntoReverb.getTargetValue() > 0.0f || delayIntoReverb.isSmoothing());
            if (coupled)
            {
                const float delayLevel = delayReturn[0].getCurrentValue();
                for (int i = 0; i < n; ++i)
                {
                    const float c = delayIntoReverb.getNextValue() * delayLevel;
                    for (int ch = 0; ch < numChannels; ++ch)
                        reverbIn.setSample (ch, i, io[ch][i] + c * delayWet[0].getSample (ch, i));
                }
                reverbSource = reverbIn.getArrayOfReadPointers();
            }
            else if (k == 0)
            {
                delayIntoReverb.skip (n);
            }

            reverb[(size_t) k].process (reverbSource, reverbWet[(size_t) k].getArrayOfWritePointers(), numChannels, n);
        }

        if (any)
        {
            for (int i = 0; i < n; ++i)
            {
                std::array<float, numDelays> dg {};
                std::array<float, numReverbs> rg {};
                for (int k = 0; k < numDelays; ++k)
                    if (delayNeeded[(size_t) k])
                        dg[(size_t) k] = delayReturn[(size_t) k].getNextValue() * (delayDuck[(size_t) k] != nullptr ? delayDuck[(size_t) k][i] : 1.0f);
                for (int k = 0; k < numReverbs; ++k)
                    if (reverbNeeded[(size_t) k])
                        rg[(size_t) k] = reverbReturn[(size_t) k].getNextValue() * (reverbDuck[(size_t) k] != nullptr ? reverbDuck[(size_t) k][i] : 1.0f);

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    float add = 0.0f;
                    for (int k = 0; k < numDelays; ++k)
                        if (delayNeeded[(size_t) k])
                            add += dg[(size_t) k] * delayWet[(size_t) k].getSample (ch, i);
                    for (int k = 0; k < numReverbs; ++k)
                        if (reverbNeeded[(size_t) k])
                            add += rg[(size_t) k] * reverbWet[(size_t) k].getSample (ch, i);
                    io[ch][i] += add;
                }
            }
        }

        // Returns that are not processed still follow their targets
        for (int k = 0; k < numDelays; ++k)
            if (! delayNeeded[(size_t) k] && ! activeRouting.isInsert (ModuleId::delay, k))
                delayReturn[(size_t) k].skip (n);
        for (int k = 0; k < numReverbs; ++k)
            if (! reverbNeeded[(size_t) k] && ! activeRouting.isInsert (ModuleId::reverb, k))
                reverbReturn[(size_t) k].skip (n);
        // The dry gain of an insert (unused by sends and absent instances) follows its target too
        for (int k = 0; k < numDelays; ++k)
            if (! activeRouting.isInsert (ModuleId::delay, k))
                delayDry[(size_t) k].skip (n);
        for (int k = 0; k < numReverbs; ++k)
            if (! activeRouting.isInsert (ModuleId::reverb, k))
                reverbDry[(size_t) k].skip (n);
        if (! reverbNeeded[0])
            delayIntoReverb.skip (n);
    }

    const float* VocalChain::duckGains (Ducker& ducker, std::vector<float>& gains, const float* const* internal,
                                        int numChannels, int n) noexcept
    {
        if (! ducker.isActive())
            return nullptr;

        if (scChannels > 0)
            ducker.computeGains (scChunk.data(), scChannels, n, gains.data());
        else
            ducker.computeGains (internal, numChannels, n, gains.data());
        return gains.data();
    }

    void VocalChain::applyRoutingFade (float* const* io, int numChannels, int n) noexcept
    {
        if (! routeGain.isSmoothing() && routeGain.getCurrentValue() >= 1.0f)
            return;

        for (int i = 0; i < n; ++i)
        {
            const float g = routeGain.getNextValue();
            for (int ch = 0; ch < numChannels; ++ch)
                io[ch][i] *= g;
        }

        // Faded out completely: switch routing, then fade back in
        if (! routeGain.isSmoothing() && routeGain.getTargetValue() <= 0.0f)
        {
            applyPendingStructure();
            routeGain.setTargetValue (1.0f);
        }
    }

    bool VocalChain::structureChangePending() const noexcept
    {
        return pendingRouting != activeRouting || pendingPitchOn != activePitchOn;
    }

    void VocalChain::applyPendingStructure() noexcept
    {
        if (activeRouting != pendingRouting || activePitchOn != pendingPitchOn)
            meters.structureSwitches.fetch_add (1, std::memory_order_relaxed);

        // Instances that join the chain start from a clean state (no stale tail or
        // envelope from an earlier use)
        for (const auto& slot : pendingRouting)
            if (! activeRouting.contains (slot.id, slot.instance))
                resetInstance (slot);

        const int oldLatency = getLatencySamples();
        activeRouting = pendingRouting;

        const bool pitchChanged = pendingPitchOn != activePitchOn;
        activePitchOn = pendingPitchOn;
        if (pitchChanged && activePitchOn)
            pitch.activate();
        if (pitchChanged || getLatencySamples() != oldLatency)
            bypassDelay.setDelay (getLatencySamples());
    }

    int VocalChain::getLatencyFor (const Routing& routing, bool pitchOn) const noexcept
    {
        int latency = 0;
        for (const auto& slot : routing)
        {
            if (slot.id == ModuleId::sat)
                latency += saturation[(size_t) slot.instance].getLatencySamples();
            else if (slot.id == ModuleId::ride)   // lookahead, constant while in the chain
                latency += ride[(size_t) slot.instance].getLatencySamples();
            else if (slot.id == ModuleId::smooth)   // STFT: constant while in the chain (also when off)
                latency += smooth[(size_t) slot.instance].getLatencySamples();
        }
        if (pitchOn && routing.contains (ModuleId::pitch))
            latency += pitch.getLatencySamples();
        return latency;
    }
}
