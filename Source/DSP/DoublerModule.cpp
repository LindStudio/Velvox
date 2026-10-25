#include "DoublerModule.h"

namespace velvox::dsp
{
    namespace
    {
        constexpr double twoPi = 6.283185307179586;
        constexpr double pi = 3.141592653589793;
        // Wobble speed of each voice relative to Mod Rate (voices 1/2 = the 0.2 rates 0.31 / 0.47 Hz)
        constexpr double rateFactors[DoublerModule::maxVoices] { 1.0, 1.516, 0.83, 1.27 };
        constexpr double slowRatio = 0.364;    // second, slower walk (0.2: 0.113 / 0.171 Hz)
        constexpr double slowDepth = 0.35;     // ... with 35 % of the depth
        constexpr int controlInterval = 16;
    }

    double DoublerModule::depthForCents (double cents, double rateHz, double sampleRate) noexcept
    {
        // Delay D(t) = D0 + A sin(2 pi f t)  ->  pitch ratio 1 - dD/dt, peak |dD/dt| = A 2 pi f / fs
        const double ratio = std::pow (2.0, cents / 1200.0);
        return (ratio - 1.0) * sampleRate / (twoPi * rateHz);
    }

    DoublerVoices DoublerModule::resolveVoices (const DoublerSettings& s) noexcept
    {
        DoublerVoices r;
        const float w = juce::jlimit (0.0f, 1.0f, s.width);
        if (s.mode == DoublerMode::classic)
        {
            // The 0.2 doubler: left voice at Delay, right voice 1.37x later, Detune = wobble depth
            const float d = juce::jlimit (1.0f, maxDelayMs, s.delayMs);
            r.count = 2;
            r.v[0] = { true, d, 0.0f, 1.0f, -w };
            r.v[1] = { true, d * 1.37f, 0.0f, 1.0f, +w };
            r.modDepthCents = s.detuneCents;
            r.normalisation = 1.0f;
            return r;
        }
        r.count = s.mode == DoublerMode::four ? 4 : 2;
        for (int i = 0; i < r.count; ++i)
        {
            const auto& vs = s.voices[(size_t) i];
            r.v[(size_t) i] = { vs.on, vs.delayMs, vs.detuneCents, vs.on ? juce::Decibels::decibelsToGain (vs.gainDb) : 0.0f,
                                juce::jlimit (-1.0f, 1.0f, vs.pan * w) };
        }
        r.modDepthCents = s.modDepthCents;
        r.normalisation = std::sqrt (2.0f / (float) r.count);
        return r;
    }

    //==============================================================================
    double DoublerModule::Walk::value() const noexcept
    {
        return from + (to - from) * (0.5 - 0.5 * std::cos (pi * pos / len));
    }

    void DoublerModule::Walk::advance (double samples, double meanLength) noexcept
    {
        pos += samples;
        while (pos >= len)
        {
            // Next move: to the other side, random amplitude and duration (no periodicity)
            pos -= len;
            from = to;
            // A short move is also a small one: no move is steeper than the sine of the same rate
            const double stretch = 0.5 + rng.nextDouble();
            const double magnitude = std::max (0.1, std::min (0.5 + 0.5 * rng.nextDouble(), 2.0 * stretch - std::abs (from)));
            to = from > 0.0 ? -magnitude : magnitude;
            len = std::max (1.0, meanLength * stretch);
        }
    }

    //==============================================================================
    void DoublerModule::prepare (double sampleRate, int)
    {
        fs = sampleRate;
        fadeLength = juce::jmax (16, (int) std::lround (0.030 * fs));
        shiftLimit = 0.012 * fs;
        minLanding = 0.002 * fs;
        minDelaySamples = 0.001 * fs;
        maxModSamples = 0.010 * fs;
        corrLength = juce::jmax (64, (int) std::lround (0.016 * fs));

        // Longest read: delay + wobble (2 walks, 0..2A each) + shifter range, plus the
        // correlation window behind a splice candidate
        const double longest = maxDelayMs * 0.001 * fs + 4.0 * maxModSamples + 3.0 * shiftLimit + corrLength + 16.0;
        int size = 1;
        while (size < (int) longest)
            size <<= 1;
        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;

        for (auto* sv : { &amount, &monoSafe, &dryGain })
        {
            sv->reset (fs, 0.05);
            sv->setCurrentAndTargetValue (sv->getTargetValue());
        }
        for (int v = 0; v < maxVoices; ++v)
        {
            auto& vo = voices[(size_t) v];
            vo.rateFactor = rateFactors[v];
            vo.delayMs.reset (fs, 0.2);
            vo.gain.reset (fs, 0.05);
            vo.pan.reset (fs, 0.05);
            for (auto* sv : { &vo.delayMs, &vo.gain, &vo.pan })
                sv->setCurrentAndTargetValue (sv->getTargetValue());
        }
        toneTarget = std::min (toneTarget, (float) (0.45 * fs));
        lowCutNow = lowCutTarget;
        toneNow = toneTarget;
        modDepthNow = modDepth;
        modRateNow = modRate;

        updateFilters();
        reset();
        snapNext = true;
    }

    void DoublerModule::reset()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
        lowCut.reset();
        highCut.reset();
        for (int v = 0; v < maxVoices; ++v)
        {
            auto& vo = voices[(size_t) v];
            vo.shift = vo.fadeShift = 0.0;
            vo.fadeLeft = 0;
            vo.turn = 0.0;
            // Fixed seeds: a render is reproducible; every voice and walk differs
            vo.walk1.rng.setSeed (0x5d0b1e + 7919 * v);
            vo.walk2.rng.setSeed (0x2c4a31 + 104729 * v);
            for (auto* w : { &vo.walk1, &vo.walk2 })
            {
                w->from = 0.0;
                w->to = ((v + (w == &vo.walk2 ? 1 : 0)) % 2 == 0 ? 1.0 : -1.0) * (0.5 + 0.5 * w->rng.nextDouble());
                w->pos = 0.0;
                w->len = 1.0;
            }
            vo.mod = vo.modStep = 0.0;
            vo.started = false;
        }
    }

    void DoublerModule::setSettings (const DoublerSettings& s) noexcept
    {
        const auto r = resolveVoices (s);
        count = r.count;
        modDepth = juce::jlimit (0.0f, maxModCents, r.modDepthCents);
        modRate = juce::jlimit (0.05f, 3.0f, s.modRateHz);
        amount.setTargetValue (s.enabled ? juce::jlimit (0.0f, 1.0f, s.mix) : 0.0f);
        monoSafe.setTargetValue (juce::jlimit (0.0f, 1.0f, s.monoSafe));
        toneTarget = juce::jlimit (1000.0f, std::min (20000.0f, (float) (0.45 * fs)), s.toneHz);
        lowCutTarget = juce::jlimit (20.0f, 1000.0f, s.lowCutHz);

        for (int v = 0; v < maxVoices; ++v)
        {
            auto& vo = voices[(size_t) v];
            const auto& rv = r.v[(size_t) v];
            const bool on = v < r.count && rv.on;
            vo.delayMs.setTargetValue (juce::jlimit (1.0f, maxDelayMs, rv.delayMs));
            vo.gain.setTargetValue (on ? rv.gain * r.normalisation : 0.0f);
            vo.pan.setTargetValue (juce::jlimit (-1.0f, 1.0f, rv.pan));
            vo.detune = on ? juce::jlimit (-maxDetuneCents, maxDetuneCents, rv.detuneCents) : 0.0f;
        }

        law = s.mixLaw;
        // True law: the original follows the user's Mix (macros only add voices); off = unity
        const float dry = s.dryDb <= -60.0f ? 0.0f : dbToGain (juce::jlimit (-60.0f, 6.0f, s.dryDb));
        dryGain.setTargetValue (s.enabled ? additiveDryGain (s.lawMix >= 0.0f ? s.lawMix : s.mix, law) * dry : 1.0f);

        if (snapNext)
        {
            // First settings after prepare(): start there, no glide from the defaults
            snapNext = false;
            for (auto* sv : { &amount, &monoSafe, &dryGain })
                sv->setCurrentAndTargetValue (sv->getTargetValue());
            for (auto& vo : voices)
                for (auto* sv : { &vo.delayMs, &vo.gain, &vo.pan })
                    sv->setCurrentAndTargetValue (sv->getTargetValue());
            lowCutNow = lowCutTarget;
            toneNow = toneTarget;
            modDepthNow = modDepth;
            modRateNow = modRate;
            updateFilters();
        }
    }

    void DoublerModule::updateFilters() noexcept
    {
        lowCut.setCoeffs (design::highPass (fs, lowCutNow, 0.70710678));
        highCut.setCoeffs (design::lowPass (fs, toneNow, 0.70710678));
    }

    void DoublerModule::updateModulation (Voice& vo, int samples) noexcept
    {
        // Wobble: two smoothed random walks; the delay offset A1 (1 + x1) + A2 (1 + x2) never goes
        // below the voice's own delay. Interpolated linearly inside the control interval.
        const double f1 = modRateNow * vo.rateFactor, f2 = f1 * slowRatio;
        const double a1 = std::min (maxModSamples, depthForCents (modDepthNow, f1, fs));
        const double a2 = std::min (maxModSamples, depthForCents (modDepthNow * slowDepth, f2, fs));
        if (! vo.started)
        {
            vo.walk1.len = 0.5 * fs / f1;
            vo.walk2.len = 0.5 * fs / f2;
        }
        vo.walk1.advance (samples, 0.5 * fs / f1);
        vo.walk2.advance (samples, 0.5 * fs / f2);
        const double target = a1 * (1.0 + vo.walk1.value()) + a2 * (1.0 + vo.walk2.value());
        if (! vo.started)
        {
            vo.mod = target;
            vo.started = true;
        }
        vo.modStep = (target - vo.mod) / samples;
    }

    float DoublerModule::tap (double delay) const noexcept
    {
        // 6-point (5th-order) Lagrange around the read position; the newest sample is at writePos
        const double d = juce::jlimit (3.0, (double) buffer.size() - 8.0, delay);
        const double pos = (double) writePos - d + (double) buffer.size();
        const int i = (int) pos;
        const double t = pos - (double) i;
        const double a = t + 2.0, b = t + 1.0, c = t, e = t - 1.0, f = t - 2.0, g = t - 3.0;
        const double ab = a * b, fg = f * g;
        const float* x = buffer.data();
        const double y = - (b * c * e * fg) / 120.0 * x[(i - 2) & mask]
                         + (a * c * e * fg) / 24.0 * x[(i - 1) & mask]
                         - (ab * e * fg) / 12.0 * x[i & mask]
                         + (ab * c * fg) / 12.0 * x[(i + 1) & mask]
                         - (ab * c * e * g) / 24.0 * x[(i + 2) & mask]
                         + (ab * c * e * f) / 120.0 * x[(i + 3) & mask];
        return (float) y;
    }

    double DoublerModule::findJump (double delayA, double direction, double minJ, double maxJ) const noexcept
    {
        // The splice target is the candidate whose recent past looks most like the playing tap's
        // (normalised cross-correlation of the last ~16 ms): on a voiced note that is a whole
        // number of pitch periods, so the crossfade joins two copies in phase.
        const int step = juce::jmax (1, (int) std::lround (4.0 * fs / 48000.0));
        const int ia = writePos - (int) std::lround (delayA);
        const auto at = [this] (int i) { return (double) buffer[(size_t) (i & mask)]; };

        const auto score = [&] (int jump, int kStep, double& energyA)
        {
            const int ib = ia - (int) direction * jump;
            double c = 0.0, eb = 0.0, ea = 0.0;
            for (int k = 0; k < corrLength; k += kStep)
            {
                const double xa = at (ia - k), xb = at (ib - k);
                c += xa * xb;
                eb += xb * xb;
                ea += xa * xa;
            }
            energyA = ea;
            return c / std::sqrt (ea * eb + 1.0e-24);
        };

        const int lo = juce::jmax (1, (int) std::ceil (minJ)), hi = juce::jmax (lo, (int) std::floor (maxJ));
        double energy = 0.0, best = -2.0;
        int bestJ = (lo + hi) / 2;
        for (int j = lo; j <= hi; j += step)
        {
            const double sc = score (j, step, energy);
            if (sc > best) { best = sc; bestJ = j; }
        }
        if (energy < 1.0e-12)
            return (double) ((lo + hi) / 2);   // silence: any jump is inaudible

        int fine = bestJ;
        best = -2.0;
        for (int j = juce::jmax (lo, bestJ - step + 1); j <= juce::jmin (hi, bestJ + step - 1); ++j)
        {
            const double sc = score (j, 1, energy);
            if (sc > best) { best = sc; fine = j; }
        }
        return (double) fine;
    }

    void DoublerModule::process (float* const* ch, int numChannels, int numSamples) noexcept
    {
        const bool active = amount.getTargetValue() > 0.0f || amount.isSmoothing()
                         || dryGain.getTargetValue() != 1.0f || dryGain.isSmoothing();
        voiceCount.store (count, std::memory_order_relaxed);
        if (! active)
        {
            wasActive = false;
            dryGain.setCurrentAndTargetValue (dryGain.getTargetValue());
            for (int v = 0; v < maxVoices; ++v)
            {
                auto& vo = voices[(size_t) v];
                for (auto* sv : { &vo.delayMs, &vo.gain, &vo.pan })
                    sv->setCurrentAndTargetValue (sv->getTargetValue());
                voicePan[(size_t) v].store (vo.pan.getTargetValue(), std::memory_order_relaxed);
                voiceGainDb[(size_t) v].store (-100.0f, std::memory_order_relaxed);
                voiceCents[(size_t) v].store (vo.detune, std::memory_order_relaxed);
                voiceDelayMs[(size_t) v].store (vo.delayMs.getTargetValue(), std::memory_order_relaxed);
            }
            return;
        }
        if (! wasActive)
        {
            reset();
            wasActive = true;
        }

        const double msToSamples = 0.001 * fs;

        for (int start = 0; start < numSamples; start += controlInterval)
        {
            const int n = std::min (controlInterval, numSamples - start);

            // Control rate: filters, wobble depth / speed (smoothed), per-voice modulation
            const double k = 1.0 - std::exp (-(double) n / (0.05 * fs));
            if (std::abs (lowCutNow / lowCutTarget - 1.0f) > 1.0e-4f || std::abs (toneNow / toneTarget - 1.0f) > 1.0e-4f)
            {
                lowCutNow = (float) (lowCutNow * std::pow ((double) lowCutTarget / lowCutNow, k));
                toneNow = (float) (toneNow * std::pow ((double) toneTarget / toneNow, k));
                updateFilters();
            }
            modDepthNow += (float) k * (modDepth - modDepthNow);
            modRateNow += (float) k * (modRate - modRateNow);

            double drift[maxVoices];
            for (int v = 0; v < maxVoices; ++v)
            {
                updateModulation (voices[(size_t) v], n);
                // Splice shifter: the tap drifts by (1 - ratio) samples per sample
                drift[v] = 1.0 - std::pow (2.0, (double) voices[(size_t) v].detune / 1200.0);
            }

            for (int i = start; i < start + n; ++i)
            {
                const float in = numChannels > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
                buffer[(size_t) writePos] = highCut.process (lowCut.process (in, 0), 0);

                float wetL = 0.0f, wetR = 0.0f;
                for (int v = 0; v < maxVoices; ++v)
                {
                    auto& vo = voices[(size_t) v];
                    const float g = vo.gain.getNextValue();
                    const float p = vo.pan.getNextValue();
                    const double base = vo.delayMs.getNextValue() * msToSamples + vo.mod;
                    vo.mod += vo.modStep;
                    if (g == 0.0f && ! vo.gain.isSmoothing())
                        continue;

                    const double dr = drift[v];
                    if (dr != 0.0 || vo.fadeLeft > 0)
                    {
                        vo.shift += dr;
                        if (vo.fadeLeft > 0)
                            vo.fadeShift += dr;
                        else
                        {
                            // Symmetric sawtooth: the tap sweeps from its landing point +L to -L (or -L to +L
                            // for a lower pitch) and is spliced there to a new landing in [2, 12] ms, chosen
                            // by correlation - so the voice's delay averages its set value over every cycle.
                            const double floorShift = minDelaySamples - base + fadeLength * std::abs (dr) + 4.0;
                            if (dr < 0.0 && vo.shift <= std::max (-vo.turn, floorShift))
                            {
                                const double jLo = std::max (1.0, minLanding - vo.shift), jHi = std::max (jLo, shiftLimit - vo.shift);
                                vo.fadeShift = vo.shift;
                                vo.shift += findJump (base + vo.shift, 1.0, jLo, jHi);
                                vo.turn = vo.shift;
                                vo.fadeLeft = fadeLength;
                            }
                            else if (dr > 0.0 && vo.shift >= vo.turn)
                            {
                                // the landing (shift - J) must stay above the shortest delay
                                const double room = vo.shift - floorShift;
                                const double jHi = std::max (1.0, std::min (vo.shift + shiftLimit, room));
                                const double jLo = std::min (jHi, std::max (1.0, vo.shift + minLanding));
                                vo.fadeShift = vo.shift;
                                vo.shift -= findJump (base + vo.shift, -1.0, jLo, jHi);
                                vo.turn = -vo.shift;
                                vo.fadeLeft = fadeLength;
                            }
                        }
                    }

                    float y = tap (base + vo.shift);
                    if (vo.fadeLeft > 0)
                    {
                        const double t = 1.0 - (double) vo.fadeLeft / (double) fadeLength;
                        const float gNew = (float) (0.5 - 0.5 * std::cos (pi * t));
                        y = gNew * y + (1.0f - gNew) * tap (base + vo.fadeShift);
                        --vo.fadeLeft;
                    }
                    y *= g;
                    wetL += y * 0.5f * (1.0f - p);
                    wetR += y * 0.5f * (1.0f + p);
                }

                writePos = (writePos + 1) & mask;

                // Limit how much of the voices reaches the mono sum
                const float mid = 0.5f * (wetL + wetR) * (1.0f - monoSafe.getNextValue());
                const float side = 0.5f * (wetL - wetR);
                const float a = amount.getNextValue();
                const float dg = dryGain.getNextValue();   // exactly 1 on the legacy law at 0 dB dry
                if (numChannels > 1)
                {
                    ch[0][i] = ch[0][i] * dg + a * (mid + side);
                    ch[1][i] = ch[1][i] * dg + a * (mid - side);
                }
                else
                {
                    ch[0][i] = ch[0][i] * dg + a * mid;
                }
            }

            // UI feed
            if (start + n >= numSamples)
            {
                const float a = amount.getCurrentValue();
                for (int v = 0; v < maxVoices; ++v)
                {
                    const auto& vo = voices[(size_t) v];
                    const float g = vo.gain.getCurrentValue() * a;
                    const double ratio = 1.0 - vo.modStep - drift[v];
                    voicePan[(size_t) v].store (vo.pan.getCurrentValue(), std::memory_order_relaxed);
                    voiceGainDb[(size_t) v].store (g > 1.0e-5f ? gainToDb (g) : -100.0f, std::memory_order_relaxed);
                    voiceCents[(size_t) v].store ((float) (1200.0 * std::log2 (std::max (ratio, 1.0e-3))), std::memory_order_relaxed);
                    voiceDelayMs[(size_t) v].store ((float) ((vo.delayMs.getCurrentValue() * msToSamples + vo.mod + vo.shift) / msToSamples),
                                                    std::memory_order_relaxed);
                }
            }
        }

        if (! lowCut.isStateFinite() || ! highCut.isStateFinite())
            reset();
    }
}
