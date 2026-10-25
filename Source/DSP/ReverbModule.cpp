#include "ReverbModule.h"
#include <complex>

namespace velvox::dsp
{
    namespace
    {
        constexpr double twoPi = 6.283185307179586;
        constexpr float minScale = 0.45f, maxScale = 1.6f;
        constexpr float maxEarlySpanMs = 110.0f;
        constexpr float bassCrossoverHz = 220.0f;

        // Per character voicing. Line lengths are spread geometrically between min and max;
        // the long characters use long lines (sparser modes, no "small box" ring) and more,
        // slower modulation; the short ones are dense and fast.
        //                                        name          lineMin lineMax early  eLvl  diff  dens  modHz modMs hfBias gain
        constexpr ReverbModule::Voicing voicings[ReverbModule::numTypes] {
            {                                     "Room",        16.0f,  56.0f,  40.0f, 0.90f, 0.62f, 0.8f, 1.50f, 1.10f, 1.00f, 1.23f },
            {                                     "Plate",        6.0f,  30.0f,   0.0f, 0.00f, 0.76f, 1.6f, 1.70f, 0.70f, 1.35f, 1.58f },
            {                                     "Hall",        22.0f,  85.0f,  70.0f, 0.55f, 0.70f, 2.1f, 0.90f, 0.90f, 0.90f, 1.58f },
            {                                     "Large Hall",  30.0f, 125.0f,  95.0f, 0.45f, 0.72f, 2.6f, 0.76f, 1.20f, 0.85f, 1.80f },
            {                                     "Ambience",     9.0f,  31.0f,  22.0f, 1.00f, 0.55f, 0.6f, 1.90f, 0.90f, 1.10f, 1.27f },
            {                                     "Atmosphere",  40.0f, 170.0f,  60.0f, 0.25f, 0.76f, 3.2f, 0.60f, 1.60f, 0.95f, 1.69f },
            {                                     "Cinematic",   35.0f, 150.0f, 110.0f, 0.40f, 0.74f, 2.8f, 0.50f, 1.40f, 0.55f, 1.72f },
            {                                     "Ethereal",    28.0f, 140.0f,  40.0f, 0.20f, 0.78f, 3.0f, 0.90f, 1.80f, 1.60f, 1.52f },
        };

        constexpr float diffuserMs[maxChannels][ReverbModule::numDiffusers] {
            { 4.77f, 3.59f, 12.73f, 9.31f, 1.73f, 7.13f },
            { 5.21f, 3.17f, 11.87f, 8.63f, 2.03f, 6.71f },
        };

        // Rows of a 16x16 Hadamard matrix as output tap signs: orthogonal, so left and
        // right are decorrelated while each still sums every line
        inline float hadamardSign (int row, int column) noexcept
        {
            int bits = row & column, parity = 0;
            while (bits != 0)
            {
                parity ^= 1;
                bits &= bits - 1;
            }
            return parity != 0 ? -1.0f : 1.0f;
        }

        inline void hadamard16 (std::array<float, ReverbModule::numLines>& v) noexcept
        {
            for (int len = 1; len < ReverbModule::numLines; len <<= 1)
                for (int i = 0; i < ReverbModule::numLines; i += len << 1)
                    for (int j = i; j < i + len; ++j)
                    {
                        const float a = v[(size_t) j], b = v[(size_t) (j + len)];
                        v[(size_t) j] = a + b;
                        v[(size_t) (j + len)] = a - b;
                    }
            for (auto& x : v)
                x *= 0.25f; // 1 / sqrt(16): orthogonal, lossless
        }

        inline int nextPowerOfTwo (int v) noexcept
        {
            int p = 1;
            while (p < v)
                p <<= 1;
            return p;
        }

        // Late line l (0..15) and its in-loop density all-pass, in ms at size scale 1
        inline float lineMsAt (const ReverbModule::Voicing& v, int l) noexcept
        {
            const float t = (float) l / (float) (ReverbModule::numLines - 1);
            return v.lineMinMs * std::pow (v.lineMaxMs / v.lineMinMs, t) * (1.0f + 0.031f * std::sin ((float) l * 3.7f));
        }

        inline int densitySamplesAt (const ReverbModule::Voicing& v, int l, double fs) noexcept
        {
            const float t = (float) l / (float) (ReverbModule::numLines - 1);
            const float ms = v.densityMs * (0.35f + 0.65f * t) * (1.0f + 0.07f * std::sin ((float) l * 2.3f));
            return std::max (1, (int) std::lround (ms * 0.001 * fs));
        }

        inline float sizeScaleFor (float size) noexcept
        {
            return minScale + (maxScale - minScale) * juce::jlimit (0.0f, 1.0f, size);
        }

        // Jot absorption low-pass H = (1 - a) / (1 - a z^-1): unity at DC, one extra factor g at fDamp
        inline double jotCoeff (double g, double cosW) noexcept
        {
            const double r = g * g;
            if (1.0 - r <= 1.0e-9)
                return 0.0;
            const double p = 1.0 - r * cosW;
            const double disc = std::max (0.0, p * p - (1.0 - r) * (1.0 - r));
            return juce::jlimit (0.0, 0.999, (p - std::sqrt (disc)) / (1.0 - r));
        }

        // First-order shelf H = k (1 - b z^-1) / (1 - a z^-1) with |H| = 1 at DC, hd at the damping
        // frequency (cosW = its cosine) and hn at Nyquist. Falls back to the Jot low-pass when the
        // three points cannot be met by one section (then a = jotCoeff, b = 0).
        inline void designShelf (double hd, double hn, double cosW, double& a, double& b, double& k) noexcept
        {
            const auto response = [&] (double pole, double& zero, double& gain)
            {
                const double q = hn * (1.0 + pole) / (1.0 - pole);
                zero = (q - 1.0) / (q + 1.0);
                gain = (1.0 - pole) / (1.0 - zero);
                return gain * gain * (1.0 - 2.0 * zero * cosW + zero * zero) / (1.0 - 2.0 * pole * cosW + pole * pole) - hd * hd;
            };
            double z = 0.0, g = 1.0, lo = 0.0, hi = 0.9999;
            const double s0 = response (lo, z, g), s1 = response (hi, z, g);
            if (! (s0 > 0.0 && s1 < 0.0))
            {
                a = jotCoeff (hd, cosW);
                b = 0.0;
                k = 1.0 - a;
                return;
            }
            for (int it = 0; it < 26; ++it)
            {
                const double mid = 0.5 * (lo + hi);
                if (response (mid, z, g) > 0.0)
                    lo = mid;
                else
                    hi = mid;
            }
            a = 0.5 * (lo + hi);
            response (a, b, k);
        }

        // Analytic loop model: RT60 at `hz` relative to the loop's mid RT60. Every loop follows the
        // longest loop's Jot law (see updateDecayFilters), so the longest loop is the model.
        struct LoopModel
        {
            double m = 1.0, meanLoop = 1.0, fs = 48000.0, gMid = 1.0, a = 0.0, k = 0.0, b = 0.0;

            LoopModel (const ReverbSettings& s, double sampleRate) noexcept : fs (sampleRate)
            {
                const auto& v = ReverbModule::voicing (s.type);
                const double scale = sizeScaleFor (s.size);
                double sum = 0.0;
                m = 0.0;
                for (int l = 0; l < ReverbModule::numLines; ++l)
                {
                    const double len = lineMsAt (v, l) * scale * 0.001 * fs + densitySamplesAt (v, l, fs);
                    sum += len;
                    m = std::max (m, len);
                }
                meanLoop = sum / ReverbModule::numLines;
                constexpr double tRef = 2.0;
                const double bassMult = juce::jlimit (0.5, 2.0, (double) s.bassMultiplier);
                gMid = std::pow (10.0, -3.0 * m / (tRef * fs));
                k = std::pow (10.0, -3.0 * m / (tRef * bassMult * fs)) / gMid - 1.0;
                const double fDamp = juce::jlimit (500.0, 0.45 * fs, (double) juce::jlimit (500.0f, 20000.0f, s.dampingHz) * v.hfBias);
                a = jotCoeff (gMid, std::cos (twoPi * fDamp / fs));
                b = 1.0 - std::exp (-twoPi * bassCrossoverHz / fs);
            }

            // RT60 at hz / RT60 of the loop in the mids
            double ratioAt (double hz) const noexcept
            {
                const std::complex<double> zi = std::polar (1.0, -twoPi * juce::jlimit (1.0, 0.499 * fs, hz) / fs);
                const double jot = (1.0 - a) / std::abs (1.0 - a * zi);
                const double shelf = std::abs (1.0 + k * b / (1.0 - (1.0 - b) * zi));
                const double g = gMid * jot * shelf;
                if (g >= 1.0)
                    return 50.0;
                return (-3.0 * m / (fs * std::log10 (g))) / 2.0;
            }
        };

        // Small deterministic generator for the early reflection patterns
        struct Lcg
        {
            std::uint32_t state;
            float next() noexcept
            {
                state = state * 1664525u + 1013904223u;
                return (float) ((state >> 8) & 0xFFFFFF) / 16777216.0f;
            }
        };
    }

    const ReverbModule::Voicing& ReverbModule::voicing (int type) noexcept
    {
        return voicings[juce::jlimit (0, numTypes - 1, type)];
    }

    //==========================================================================
    float ReverbModule::decayCompensationFor (const ReverbSettings& s, double sampleRate) noexcept
    {
        // A white impulse through the input low cut and the output high cut, every frequency decaying
        // with its own RT60: the Schroeder curve is a sum of exponentials; its -5..-35 dB slope is the T30
        const LoopModel model (s, sampleRate);
        const auto hp = design::highPass (sampleRate, juce::jlimit (20.0f, 2000.0f, s.lowCutHz), 0.70710678);
        const auto lp = design::lowPass (sampleRate, std::min ((double) juce::jlimit (1000.0f, 20000.0f, s.highCutHz), 0.45 * sampleRate),
                                         0.70710678);
        constexpr int numPoints = 40;
        std::array<double, numPoints> w {}, r {};
        const double f0 = 20.0, f1 = std::min (20000.0, 0.49 * sampleRate);
        double total = 0.0;
        for (int i = 0; i < numPoints; ++i)
        {
            const double f = f0 * std::pow (f1 / f0, (i + 0.5) / numPoints);
            const double hpm = design::magnitude (hp, f, sampleRate), lpm = design::magnitude (lp, f, sampleRate);
            w[(size_t) i] = f * hpm * hpm * lpm * lpm;   // white: the energy per log step grows with f
            r[(size_t) i] = model.ratioAt (f);
            total += w[(size_t) i];
        }
        const auto edcDb = [&] (double t)
        {
            double e = 0.0;
            for (int i = 0; i < numPoints; ++i)
                e += w[(size_t) i] * std::exp (-13.815510558 * t / r[(size_t) i]);
            return 10.0 * std::log10 (std::max (e, 1.0e-300) / total);
        };
        const auto timeAt = [&] (double db)
        {
            double lo = 0.0, hi = 60.0;
            for (int it = 0; it < 40; ++it)
            {
                const double mid = 0.5 * (lo + hi);
                if (edcDb (mid) > db)
                    lo = mid;
                else
                    hi = mid;
            }
            return 0.5 * (lo + hi);
        };
        const double t30 = 2.0 * (timeAt (-35.0) - timeAt (-5.0));   // in units of the loop's mid RT60

        // Calibrated against the rendered tails (all 8 types, 0.5..6 s, damping 3..15 kHz): the dense
        // network's highs live a little longer than the single-loop model says (exponent < 1), and long
        // lines need a few passes to build the diffuse tail, which shortens the fitted slope of short decays
        const double damped = std::pow (t30 > 0.0 ? 1.0 / t30 : 1.0, dampingExponent);
        const double decaySeconds = juce::jlimit (0.1f, maxDecaySeconds, s.decaySeconds);
        const double buildUp = 1.0 + buildUpWeight * (model.meanLoop / sampleRate) / decaySeconds;
        return (float) juce::jlimit (1.0, (double) maxCompensation, damped * buildUp);
    }

    float ReverbModule::predictedRt60 (const ReverbSettings& s, float hz, double sampleRate) noexcept
    {
        const LoopModel model (s, sampleRate);
        const double decaySeconds = juce::jlimit (0.1f, maxDecaySeconds, s.decaySeconds);
        return (float) (decaySeconds * decayCompensationFor (s, sampleRate) * model.ratioAt (hz));
    }

    void ReverbModule::updateCompensationTarget() noexcept
    {
        const std::array<float, 7> key { (float) juce::jlimit (0, numTypes - 1, settings.type), settings.dampingHz,
                                         settings.bassMultiplier, settings.lowCutHz, settings.highCutHz, settings.size,
                                         settings.decaySeconds };
        if (key == compKey)
            return;
        compKey = key;
        compensation.setTargetValue (decayCompensationFor (settings, fs));
    }

    //==========================================================================
    void ReverbModule::DelayLine::allocate (int minSize, int stagger)
    {
        const int size = nextPowerOfTwo (std::max (8, minSize));
        constexpr int slack = 1024;   // floats: room to reach any of the 64 cache-set offsets
        storage.assign ((size_t) (size + slack), 0.0f);

        // Start where the address is 64-byte aligned and sits in cache set (stagger * 5) mod 64
        const auto base = reinterpret_cast<std::uintptr_t> (storage.data());
        const std::uintptr_t wantedSet = (std::uintptr_t) ((stagger * 5) & 63);
        int offset = 0;
        for (int k = 0; k < slack; ++k)
        {
            const auto addr = base + (std::uintptr_t) k * sizeof (float);
            if (addr % 64 == 0 && (addr / 64) % 64 == wantedSet)
            {
                offset = k;
                break;
            }
        }
        buffer = storage.data() + offset;
        mask = size - 1;
        writePos = 0;
    }

    void ReverbModule::DelayLine::clear() noexcept
    {
        std::fill (storage.begin(), storage.end(), 0.0f);
        writePos = 0;
    }

    float ReverbModule::DelayLine::readLinear (float delay) const noexcept
    {
        const float d = juce::jlimit (0.0f, (float) (mask - 2), delay);
        const int i = (int) d;
        const float f = d - (float) i;
        const float a = buffer[(writePos - 1 - i) & mask];
        const float b = buffer[(writePos - 2 - i) & mask];
        return a + f * (b - a);
    }

    float ReverbModule::DelayLine::readCubic (float delay) const noexcept
    {
        const float d = juce::jlimit (1.0f, (float) (mask - 3), delay);
        const int i = (int) d;
        const float f = d - (float) i;
        const int newest = writePos - 1;
        const float ym1 = buffer[(newest - i + 1) & mask];
        const float y0  = buffer[(newest - i) & mask];
        const float y1  = buffer[(newest - i - 1) & mask];
        const float y2  = buffer[(newest - i - 2) & mask];
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * f + c2) * f + c1) * f + y0;
    }

    void ReverbModule::Allpass::allocate (int maxLength)
    {
        buffer.assign ((size_t) std::max (1, maxLength), 0.0f);
        length = (int) buffer.size();
        pos = 0;
    }

    void ReverbModule::Allpass::clear() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        pos = 0;
    }

    //==========================================================================
    void ReverbModule::prepare (double sampleRate, int)
    {
        fs = sampleRate;
        const auto msToSamples = [sampleRate] (double ms) { return (int) std::ceil (ms * 0.001 * sampleRate); };

        float longestLine = 0.0f, longestDensity = 0.0f, longestModMs = 0.0f;
        for (const auto& v : voicings)
        {
            longestLine = std::max (longestLine, v.lineMaxMs);
            longestDensity = std::max (longestDensity, v.densityMs);
            longestModMs = std::max (longestModMs, v.modDepthMs);
        }

        for (int ch = 0; ch < maxChannels; ++ch)
            preDelay[(size_t) ch].allocate (msToSamples (maxPreDelayMs + maxEarlySpanMs * maxScale) + 8, numLines + ch);
        for (int l = 0; l < numLines; ++l)
            lines[(size_t) l].allocate (msToSamples (longestLine * maxScale + 2.0f * longestModMs) + 8, l);
        for (auto& d : density)
            d.allocate (msToSamples (longestDensity) + 2);
        for (int ch = 0; ch < maxChannels; ++ch)
            for (int k = 0; k < numDiffusers; ++k)
                diffusers[(size_t) ch][(size_t) k].allocate (std::max (1, msToSamples (diffuserMs[ch][k])));
        for (int ch = 0; ch < maxChannels; ++ch)
            for (int k = 0; k < 2; ++k)
            {
                auto& ap = earlySmear[(size_t) ch][(size_t) k];
                ap.allocate (std::max (1, msToSamples ((k == 0 ? 1.3 : 2.9) + 0.37 * ch)));
                ap.g = 0.5f;
            }

        bassCoeff = (float) (1.0 - std::exp (-twoPi * bassCrossoverHz / sampleRate));

        sizeScale.reset (sampleRate, 0.25);
        preDelaySamples.reset (sampleRate, 0.1);
        width.reset (sampleRate, 0.05);
        early.reset (sampleRate, 0.05);
        topologyGain.reset (sampleRate, 0.02);
        diffusion.reset (sampleRate, 0.05);
        modulation.reset (sampleRate, 0.1);
        decay.reset (sampleRate, 0.1);
        damping.reset (sampleRate, 0.05);
        lowCut.reset (sampleRate, 0.05);
        highCut.reset (sampleRate, 0.05);
        bass.reset (sampleRate, 0.1);
        compensation.reset (sampleRate, 0.1);
        compKey.fill (-1.0f);

        setSettings (settings);
        for (auto* s : { &sizeScale, &preDelaySamples, &width, &early, &diffusion, &modulation })
            s->setCurrentAndTargetValue (s->getTargetValue());
        for (auto* s : { &decay, &damping, &lowCut, &highCut, &bass, &compensation })
            s->setCurrentAndTargetValue (s->getTargetValue());
        topologyGain.setCurrentAndTargetValue (1.0f);

        activeType = juce::jlimit (0, numTypes - 1, settings.type);
        configureTopology();
        reset();
    }

    void ReverbModule::reset()
    {
        for (auto& p : preDelay)
            p.clear();
        for (auto& l : lines)
            l.clear();
        for (auto& d : density)
            d.clear();
        for (auto& side : diffusers)
            for (auto& d : side)
                d.clear();
        for (auto& side : earlySmear)
            for (auto& d : side)
                d.clear();

        dampZ.fill (0.0f);
        dampX.fill (0.0f);
        bassZ.fill (0.0f);
        modNow.fill (0.0f);
        modStep.fill (0.0f);
        for (int l = 0; l < numLines; ++l)
            lfoPhase[(size_t) l] = std::fmod ((double) l * 0.381966, 1.0);   // staggered start, deterministic
        inputLowCut.reset();
        outputHighCut.reset();
    }

    void ReverbModule::setSettings (const ReverbSettings& s) noexcept
    {
        settings = s;
        sizeScale.setTargetValue (sizeScaleFor (s.size));
        preDelaySamples.setTargetValue ((float) (juce::jlimit (0.0f, maxPreDelayMs, s.preDelayMs) * 0.001 * fs));
        width.setTargetValue (juce::jlimit (0.0f, 1.0f, s.width));
        early.setTargetValue (juce::jlimit (0.0f, 1.0f, s.early));
        diffusion.setTargetValue (juce::jlimit (0.0f, 1.0f, s.diffusion));
        modulation.setTargetValue (juce::jlimit (0.0f, 1.0f, s.modulation));
        decay.setTargetValue (juce::jlimit (0.1f, maxDecaySeconds, s.decaySeconds));
        damping.setTargetValue (juce::jlimit (500.0f, 20000.0f, s.dampingHz));
        lowCut.setTargetValue (juce::jlimit (20.0f, 2000.0f, s.lowCutHz));
        highCut.setTargetValue (juce::jlimit (1000.0f, 20000.0f, s.highCutHz));
        bass.setTargetValue (juce::jlimit (0.5f, 2.0f, s.bassMultiplier));
        updateCompensationTarget();
    }

    void ReverbModule::configureTopology() noexcept
    {
        const auto& v = voicing (activeType);

        // Density all-passes and early reflection pattern depend on the character only
        for (int l = 0; l < numLines; ++l)
        {
            density[(size_t) l].setLength (densitySamplesAt (v, l, fs));
            // Different, incommensurate rates per line (0.35..1.6 x the character's rate, golden-ratio
            // spread): every mode drifts on its own schedule, no two lines line up into a stable mode
            const double spread = 0.35 * std::pow (1.6 / 0.35, std::fmod ((double) l * 0.618034 + 0.13, 1.0));
            lfoInc[(size_t) l] = v.modRateHz * spread / fs;
        }

        numTaps = v.earlyMs > 0.0f ? maxEarlyTaps : 0;
        Lcg rnd { 0x9E3779B9u + (std::uint32_t) activeType * 7919u };
        float energy = 0.0f;
        for (int k = 0; k < numTaps; ++k)
        {
            auto& tap = taps[(size_t) k];
            const float base = std::pow ((float) (k + 1) / (float) numTaps, 1.35f);
            tap.position = juce::jlimit (0.02f, 1.0f, base + (rnd.next() - 0.5f) * 0.6f / (float) numTaps);
            const float level = (1.0f - 0.65f * tap.position) * (0.6f + 0.4f * rnd.next());
            const float pan = rnd.next() * 2.0f - 1.0f;
            const float sign = rnd.next() < 0.2f ? -1.0f : 1.0f;
            tap.gainL = sign * level * std::sqrt (0.5f * (1.0f + pan));
            tap.gainR = sign * level * std::sqrt (0.5f * (1.0f - pan));
            energy += tap.gainL * tap.gainL + tap.gainR * tap.gainR;
        }
        const float norm = energy > 0.0f ? std::sqrt (2.0f / energy) : 0.0f;
        for (int k = 0; k < numTaps; ++k)
        {
            taps[(size_t) k].gainL *= norm;
            taps[(size_t) k].gainR *= norm;
        }

        appliedScale = -1.0f;   // forces line lengths and decay filters
        updateDecayFilters();
    }

    void ReverbModule::updateDecayFilters() noexcept
    {
        const auto& v = voicing (activeType);
        const float scale = sizeScale.getCurrentValue();
        const double t60 = (double) decay.getCurrentValue() * compensation.getCurrentValue();
        const double bassT60 = t60 * bass.getCurrentValue();
        const double fDamp = juce::jlimit (500.0, 0.45 * fs, (double) damping.getCurrentValue() * v.hfBias);
        const double cosW = std::cos (twoPi * fDamp / fs);

        // Effective loop length: line + density all-pass (its mean group delay = its length)
        std::array<double, numLines> loop {};
        int longest = 0;
        for (int l = 0; l < numLines; ++l)
        {
            baseDelay[(size_t) l] = lineMsAt (v, l) * scale * 0.001f * (float) fs;
            loop[(size_t) l] = baseDelay[(size_t) l] + density[(size_t) l].length;
            if (loop[(size_t) l] > loop[(size_t) longest])
                longest = l;
        }

        // The frequency law of the decay comes from the longest loop's Jot low-pass (unity at DC, one
        // extra factor gMid at fDamp: RT60 there = half the mid RT60). Every other loop gets a
        // first-order shelf (pole + zero) matching the same per-second loss at DC, fDamp and Nyquist.
        // A plain Jot low-pass per line would saturate differently for long and short lines, so with
        // short decays and dark damping the long lines' highs outlived the rest (metallic HF modes).
        const double mL = loop[(size_t) longest];
        const double gL = std::pow (10.0, -3.0 * mL / (t60 * fs));
        const double aL = jotCoeff (gL, cosW);
        const double nyquistLossPerSample = std::log10 ((1.0 + aL) / std::max (1.0e-12, 1.0 - aL)) / mL;   // extra, log10 gain

        for (int l = 0; l < numLines; ++l)
        {
            const double m = loop[(size_t) l];
            const double gMid = std::pow (10.0, -3.0 * m / (t60 * fs));
            const double gBass = std::pow (10.0, -3.0 * m / (bassT60 * fs));
            gainMid[(size_t) l] = (float) gMid;
            bassK[(size_t) l] = (float) (gBass / gMid - 1.0);

            double a = aL, b = 0.0, k = 1.0 - aL;
            if (l != longest)
                designShelf (gMid, std::pow (10.0, -nyquistLossPerSample * m), cosW, a, b, k);
            dampA[(size_t) l] = (float) a;
            dampB[(size_t) l] = (float) k;
            dampC[(size_t) l] = (float) (-k * b);
        }

        appliedScale = scale;
    }

    void ReverbModule::process (const float* const* input, float* const* wetOut, int numChannels, int numSamples) noexcept
    {
        juce::ScopedNoDenormals noDenormals;

        const auto& v = voicing (activeType);
        const int requestedType = juce::jlimit (0, numTypes - 1, settings.type);
        const int lastIn = numChannels > 1 ? 1 : 0;

        constexpr int left = 5, right = 10;   // Hadamard rows used as output taps
        static const auto signs = []
        {
            std::array<std::array<float, numLines>, 2> s {};
            for (int l = 0; l < numLines; ++l)
            {
                s[0][(size_t) l] = hadamardSign (left, l);
                s[1][(size_t) l] = hadamardSign (right, l);
            }
            return s;
        }();

        for (int start = 0; start < numSamples; start += updateInterval)
        {
            const int n = std::min (updateInterval, numSamples - start);

            // Changing the character: fade the return out, swap the topology on silence, fade in
            if (requestedType != activeType)
            {
                topologyGain.setTargetValue (0.0f);
                if (! topologyGain.isSmoothing() && topologyGain.getCurrentValue() <= 0.0f)
                {
                    activeType = requestedType;
                    reset();
                    configureTopology();
                    topologyGain.setTargetValue (1.0f);
                }
            }

            const bool sizeMoving = sizeScale.isSmoothing();
            if (sizeMoving || decay.isSmoothing() || damping.isSmoothing() || bass.isSmoothing() || compensation.isSmoothing()
                || appliedScale < 0.0f)
            {
                compensation.skip (n);
                sizeScale.skip (n);
                decay.skip (n);
                damping.skip (n);
                bass.skip (n);
                updateDecayFilters();
            }
            if (lowCut.isSmoothing() || highCut.isSmoothing() || start == 0)
            {
                lowCut.skip (lowCut.isSmoothing() ? n : 0);
                highCut.skip (highCut.isSmoothing() ? n : 0);
                inputLowCut.setCoeffs (design::highPass (fs, lowCut.getCurrentValue(), 0.70710678));
                outputHighCut.setCoeffs (design::lowPass (fs, std::min ((double) highCut.getCurrentValue(), 0.45 * fs), 0.70710678));
            }

            diffusion.skip (n);
            const float diff = diffusion.getCurrentValue();
            const float inputG = v.diffusion * (0.25f + 0.75f * diff);
            for (auto& side : diffusers)
                for (auto& d : side)
                    d.g = inputG;
            const float densityG = 0.25f + 0.4f * diff;
            for (auto& d : density)
                d.g = densityG;

            // Modulation: depth from the character and the control, ramped across the sub-block
            modulation.skip (n);
            const float depth = v.modDepthMs * modulation.getCurrentValue() * 0.001f * (float) fs;
            for (int l = 0; l < numLines; ++l)
            {
                const size_t li = (size_t) l;
                lfoPhase[li] += lfoInc[li] * n;
                if (lfoPhase[li] >= 1.0)
                    lfoPhase[li] -= 1.0;
                const float target = depth * (1.0f + (float) std::sin (twoPi * lfoPhase[li]));
                modStep[li] = (target - modNow[li]) / (float) n;
            }

            const float scale = sizeScale.getCurrentValue();
            const float spanSamples = v.earlyMs * scale * 0.001f * (float) fs;
            const float outScale = 0.33f * v.outputGain;

            for (int i = start; i < start + n; ++i)
            {
                const float inL = input[0][i];
                const float inR = input[lastIn][i];

                // Low cut, then pre-delay (the early reflections read the same buffer)
                preDelay[0].write (inputLowCut.process (inL, 0));
                preDelay[1].write (inputLowCut.process (inR, 1));
                const float pd = preDelaySamples.getNextValue();
                float xL = preDelay[0].readLinear (pd);
                float xR = preDelay[1].readLinear (pd);

                // Early reflections
                float eL = 0.0f, eR = 0.0f;
                for (int k = 0; k < numTaps; ++k)
                {
                    const auto& tap = taps[(size_t) k];
                    const float d = pd + tap.position * spanSamples;
                    const float s = 0.5f * (preDelay[0].readLinear (d) + preDelay[1].readLinear (d));
                    eL += s * tap.gainL;
                    eR += s * tap.gainR;
                }
                for (auto& ap : earlySmear[0])
                    eL = ap.process (eL);
                for (auto& ap : earlySmear[1])
                    eR = ap.process (eR);
                const float eg = early.getNextValue() * 2.0f * v.earlyLevel;

                // Input diffusion (decorrelated per side)
                for (auto& d : diffusers[0])
                    xL = d.process (xL);
                for (auto& d : diffusers[1])
                    xR = d.process (xR);

                // Late network
                std::array<float, numLines> fb {};
                float lateL = 0.0f, lateR = 0.0f;
                for (int l = 0; l < numLines; ++l)
                {
                    const size_t li = (size_t) l;
                    modNow[li] += modStep[li];
                    float o = lines[li].readCubic (baseDelay[li] + modNow[li] + 1.0f);
                    o = density[li].process (o);

                    lateL += signs[0][li] * o;
                    lateR += signs[1][li] * o;

                    bassZ[li] += bassCoeff * (o - bassZ[li]);
                    o += bassK[li] * bassZ[li];
                    dampZ[li] = dampB[li] * o + dampC[li] * dampX[li] + dampA[li] * dampZ[li];
                    dampX[li] = o;
                    fb[li] = dampZ[li] * gainMid[li];
                }

                hadamard16 (fb);

                for (int l = 0; l < numLines; ++l)
                {
                    const float in = (l & 1) ? xR : xL;
                    const float sign = (l & 2) ? -1.0f : 1.0f;
                    lines[(size_t) l].write (fb[(size_t) l] + 0.5f * sign * in);
                }

                float wl = (eL * eg + lateL * 0.25f) * outScale;
                float wr = (eR * eg + lateR * 0.25f) * outScale;
                wl = outputHighCut.process (wl, 0);
                wr = outputHighCut.process (wr, 1);

                const float w = width.getNextValue();
                const float g = topologyGain.getNextValue();
                const float mid = 0.5f * (wl + wr);
                const float side = 0.5f * (wl - wr) * w;

                if (numChannels > 1)
                {
                    wetOut[0][i] = (mid + side) * g;
                    wetOut[1][i] = (mid - side) * g;
                }
                else
                {
                    wetOut[0][i] = mid * g;
                }
            }
        }

        for (int l = 0; l < numLines; ++l)
            if (! std::isfinite (dampZ[(size_t) l]) || ! std::isfinite (bassZ[(size_t) l]))
            {
                reset();
                break;
            }
    }
}
