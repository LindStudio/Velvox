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
        delayWet.setSize (maxChannels, bs, false, true, false);
        reverbWet.setSize (maxChannels, bs, false, true, false);
        reverbIn.setSize (maxChannels, bs, false, true, false);

        eq.prepare (sampleRate, bs);
        compressor.prepare (sampleRate, bs);
        deesser.prepare (sampleRate, bs);
        saturation.prepare (sampleRate, bs);
        delay.prepare (sampleRate, bs);
        reverb.prepare (sampleRate, bs);

        eqGate.prepare (sampleRate, bs);
        compGate.prepare (sampleRate, bs);
        deessGate.prepare (sampleRate, bs);

        bypassDelay.prepare (std::max (1, saturation.getLatencySamples()));
        bypassDelay.setDelay (saturation.getLatencySamples());

        for (auto* s : { &delayReturn, &reverbReturn, &wetAmount, &delayIntoReverb })
            s->reset (sampleRate, 0.03);
        inputGain.reset (sampleRate, 0.03);
        outputGain.reset (sampleRate, 0.03);

        // No fade on (re)start: take the latest routing directly
        routeGain.reset (sampleRate, 0.008);
        routeGain.setCurrentAndTargetValue (1.0f);
        adoptRoutingImmediately = true;

        prepared = true;
        reset();
    }

    void VocalChain::reset()
    {
        eq.reset();
        compressor.reset();
        deesser.reset();
        saturation.reset();
        delay.reset();
        reverb.reset();
        bypassDelay.reset();
    }

    void VocalChain::applySettings (const ChainSettings& s) noexcept
    {
        inputGain.setTargetValue (dbToGain (s.inputGainDb));
        outputGain.setTargetValue (dbToGain (s.outputGainDb));
        wetAmount.setTargetValue (s.bypass ? 0.0f : 1.0f);

        eq.setSettings (s.eq);
        eqGate.setTargets (s.eqOn, s.eqMix);

        compressor.setSettings (s.comp);
        compGate.setTargets (s.compOn, s.compMix);

        deesser.setSettings (s.deess);
        // Listen mode must output the band alone, so it forces 100 % wet
        deessGate.setTargets (s.deessOn, 1.0f);

        saturation.setSettings (s.sat);

        auto delaySettings = s.delay;
        delaySettings.timeMs = s.delayTimeMs;
        bool synced = false;

        if (s.delaySync)
        {
            const float t = DelayModule::timeForTempo (s.hostBpm, s.delayDivision);
            if (t > 0.0f)
            {
                delaySettings.timeMs = t;
                synced = true;
            }
        }

        meters.tempoSynced.store (synced, std::memory_order_relaxed);
        meters.effectiveDelayMs.store (delaySettings.timeMs, std::memory_order_relaxed);
        delay.setSettings (delaySettings);
        delayReturn.setTargetValue (s.delayOn ? juce::jlimit (0.0f, 1.0f, s.delayMix) : 0.0f);

        reverb.setSettings (s.reverb);
        reverbReturn.setTargetValue (s.reverbOn ? juce::jlimit (0.0f, 1.0f, s.reverbMix) : 0.0f);
        delayIntoReverb.setTargetValue (juce::jlimit (0.0f, 1.0f, s.delayIntoReverb));

        if (s.routing.isValidPermutation())
            pendingRouting = s.routing;

        if (adoptRoutingImmediately)
        {
            activeRouting = pendingRouting;
            adoptRoutingImmediately = false;
        }
    }

    void VocalChain::process (juce::AudioBuffer<float>& buffer, int numChannels, const ChainSettings& s) noexcept
    {
        jassert (prepared);
        if (! prepared)
            return;

        const int ioChannels = juce::jlimit (0, maxChannels, std::min (numChannels, buffer.getNumChannels()));
        const int numSamples = buffer.getNumSamples();
        if (ioChannels == 0 || numSamples == 0)
            return;

        sanitize (buffer.getArrayOfWritePointers(), ioChannels, numSamples);
        applySettings (s);

        for (int start = 0; start < numSamples; start += internalBlockSize)
        {
            const int n = std::min (internalBlockSize, numSamples - start);

            // The chain always runs in stereo; a mono input is duplicated
            for (int ch = 0; ch < maxChannels; ++ch)
                work.copyFrom (ch, 0, buffer, std::min (ch, ioChannels - 1), start, n);

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
            reset(); // something produced invalid values: start from a clean state

        for (int ch = 0; ch < ioChannels; ++ch)
            meters.output[(size_t) ch].push (buffer.getMagnitude (ch, 0, numSamples));
        if (ioChannels == 1)
            meters.output[1].push (buffer.getMagnitude (0, 0, numSamples));
    }

    void VocalChain::processChunk (float* const* io, int numChannels, int n, const ChainSettings& s) noexcept
    {
        // 1) Untouched input, delayed by the chain latency, for the global bypass
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

        // A pending routing change starts with a short fade-out of the old routing
        if (pendingRouting != activeRouting)
            routeGain.setTargetValue (0.0f);

        // 3) Serial modules in routing order
        for (auto id : activeRouting.order)
            processSerialModule (id, io, numChannels, n);

        // 4) Parallel sends, fed from the end of the serial chain. They return
        //    wet only, so the dry signal is present exactly once.
        processSends (io, numChannels, n, s);

        applyRoutingFade (io, numChannels, n);

        // 5) Output gain, clip detection, safety clipper
        bool clipped = false;
        for (int i = 0; i < n; ++i)
        {
            const float g = outputGain.getNextValue();
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

        // 6) Global bypass crossfade against the latency-aligned input
        if (wetAmount.isSmoothing() || wetAmount.getCurrentValue() < 1.0f)
        {
            for (int i = 0; i < n; ++i)
            {
                const float w = wetAmount.getNextValue();
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const float d = dryForBypass.getSample (ch, i);
                    io[ch][i] = d + (io[ch][i] - d) * w;
                }
            }
        }
    }

    //==========================================================================
    bool VocalChain::wakeIfNeeded (juce::SmoothedValue<float>& ret, bool& active, ModuleId id) noexcept
    {
        const bool needed = ret.getTargetValue() > 0.0f || ret.isSmoothing();
        if (needed && ! active)
        {
            if (id == ModuleId::delay)
                delay.reset();
            else
                reverb.reset();
        }
        active = needed;
        return needed;
    }

    void VocalChain::processSerialModule (ModuleId id, float* const* io, int numChannels, int n) noexcept
    {
        switch (id)
        {
            case ModuleId::eq:      runGated (eqGate, eq, io, numChannels, n); break;
            case ModuleId::comp:    runGated (compGate, compressor, io, numChannels, n); break;
            case ModuleId::deess:   runGated (deessGate, deesser, io, numChannels, n); break;
            case ModuleId::sat:     saturation.process (io, numChannels, n); break; // own on/off + mix, constant latency

            case ModuleId::doubler: // not implemented yet
            case ModuleId::pitch:   // not implemented yet
                break;

            case ModuleId::delay:
            case ModuleId::reverb:
            {
                if (! activeRouting.isInsert (id))
                    break;

                const bool isDelay = id == ModuleId::delay;
                auto& ret = isDelay ? delayReturn : reverbReturn;
                auto& wet = isDelay ? delayWet : reverbWet;

                if (! wakeIfNeeded (ret, isDelay ? delayActive : reverbActive, id))
                {
                    ret.skip (n);
                    break;
                }

                if (isDelay)
                    delay.process (io, wet.getArrayOfWritePointers(), numChannels, n);
                else
                    reverb.process (io, wet.getArrayOfWritePointers(), numChannels, n);

                // Insert: out = in + wet * mix (same law as the send)
                for (int i = 0; i < n; ++i)
                {
                    const float g = ret.getNextValue();
                    for (int ch = 0; ch < numChannels; ++ch)
                        io[ch][i] += g * wet.getSample (ch, i);
                }
                break;
            }
        }
    }

    void VocalChain::processSends (float* const* io, int numChannels, int n, const ChainSettings&) noexcept
    {
        const bool delaySend = activeRouting.isSend (ModuleId::delay);
        const bool reverbSend = activeRouting.isSend (ModuleId::reverb);

        const bool delayNeeded = delaySend && wakeIfNeeded (delayReturn, delayActive, ModuleId::delay);
        const bool reverbNeeded = reverbSend && wakeIfNeeded (reverbReturn, reverbActive, ModuleId::reverb);

        if (delayNeeded)
            delay.process (io, delayWet.getArrayOfWritePointers(), numChannels, n);

        if (reverbNeeded)
        {
            const float* const* reverbSource = io;

            // Delay -> reverb: the delay return (at its mix level) also feeds the reverb
            const bool coupled = delayNeeded && (delayIntoReverb.getTargetValue() > 0.0f || delayIntoReverb.isSmoothing());
            if (coupled)
            {
                const float delayLevel = delayReturn.getCurrentValue();
                for (int i = 0; i < n; ++i)
                {
                    const float c = delayIntoReverb.getNextValue() * delayLevel;
                    for (int ch = 0; ch < numChannels; ++ch)
                        reverbIn.setSample (ch, i, io[ch][i] + c * delayWet.getSample (ch, i));
                }
                reverbSource = reverbIn.getArrayOfReadPointers();
            }
            else
            {
                delayIntoReverb.skip (n);
            }

            reverb.process (reverbSource, reverbWet.getArrayOfWritePointers(), numChannels, n);
        }

        if (delayNeeded || reverbNeeded)
        {
            for (int i = 0; i < n; ++i)
            {
                const float dg = delayNeeded ? delayReturn.getNextValue() : 0.0f;
                const float rg = reverbNeeded ? reverbReturn.getNextValue() : 0.0f;

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    float add = 0.0f;
                    if (delayNeeded)
                        add += dg * delayWet.getSample (ch, i);
                    if (reverbNeeded)
                        add += rg * reverbWet.getSample (ch, i);
                    io[ch][i] += add;
                }
            }
        }

        if (delaySend && ! delayNeeded)
            delayReturn.skip (n);
        if (reverbSend && ! reverbNeeded)
            reverbReturn.skip (n);
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
            activeRouting = pendingRouting;
            routeGain.setTargetValue (1.0f);
        }
    }
}
