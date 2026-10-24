#include "SaturationModule.h"

namespace velvox::dsp
{
    namespace
    {
        constexpr float bias = 0.2f;     // adds even harmonics
        constexpr float tubeBias = 0.5f;
        constexpr int updateInterval = 16;
        constexpr double adaaEpsilon = 1.0e-6;
        constexpr double hfFix = 0.133;

        // log (cosh (v)) without overflow
        inline double logCosh (double v) noexcept
        {
            const double a = std::abs (v);
            return a + std::log1p (std::exp (-2.0 * a)) - 0.69314718055994531;
        }

        inline float meterDb (double sumSq, int count) noexcept
        {
            return count > 0 ? (float) (10.0 * std::log10 (sumSq / count + 1.0e-20)) : -200.0f;
        }
    }

    float SaturationModule::shape (float x, float drive, int character) noexcept
    {
        const float u = drive * x;
        switch (character)
        {
            case 1: // Tape: symmetric and gentle (odd harmonics, soft knee)
                return 0.63661977f * std::atan (1.5707963f * u);

            case 2: // Tube: stronger asymmetry (more even harmonics)
            {
                static const float tanhTubeBias = std::tanh (tubeBias);
                return std::tanh (u + tubeBias) - tanhTubeBias;
            }

            case 3: // Hard: cubic soft clipper with a firm ceiling
            {
                const float c = juce::jlimit (-1.0f, 1.0f, u);
                return 1.5f * (c - c * c * c / 3.0f);
            }

            default: // Warm: mildly asymmetric tanh (v0.1 curve)
            {
                static const float tanhBias = std::tanh (bias);
                return std::tanh (u + bias) - tanhBias;
            }
        }
    }

    double SaturationModule::antiderivative (double x, double drive, int character) noexcept
    {
        const double u = drive * x;
        switch (character)
        {
            case 1:
            {
                constexpr double k = 0.63661977, b = 1.5707963;
                const double a = b * u;
                return k * (a * std::atan (a) - 0.5 * std::log1p (a * a)) / (b * drive);
            }
            case 2:
            case 0:
            {
                const double beta = character == 2 ? (double) tubeBias : (double) bias;
                // The float curve subtracts tanh of the float bias; same constant here
                const double t = (double) std::tanh (character == 2 ? tubeBias : bias);
                return logCosh (u + beta) / drive - t * x;
            }
            default:
            {
                const double a = std::abs (u);
                if (a <= 1.0)
                    return 1.5 * (0.5 * u * u - u * u * u * u / 12.0) / drive;
                return (0.625 + (a - 1.0)) / drive;
            }
        }
    }

    // Keeps perceived level roughly constant: matches the RMS of a -12 dBFS sine
    // before and after the curve.
    float SaturationModule::compensationFor (float drive, int character) noexcept
    {
        constexpr int points = 48;
        constexpr float amp = 0.25f;
        double inSum = 0.0, outSum = 0.0, outMean = 0.0;
        float out[points];

        for (int i = 0; i < points; ++i)
        {
            const float x = amp * std::sin (6.2831853f * (float) i / (float) points);
            out[i] = shape (x, drive, character);
            outMean += out[i];
            inSum += (double) x * x;
        }

        outMean /= points;
        for (int i = 0; i < points; ++i)
            outSum += (out[i] - outMean) * (out[i] - outMean);

        return outSum > 1.0e-12 ? (float) std::sqrt (inSum / outSum) : 1.0f;
    }

    void SaturationModule::transferCurve (int character, float driveDb, const float* x, float* y, int n) noexcept
    {
        character = juce::jlimit (0, numCharacters - 1, character);
        const float d = dbToGain (driveDb);
        const float c = compensationFor (d, character);
        for (int i = 0; i < n; ++i)
            y[i] = shape (x[i], d, character) * c;
    }

    std::array<float, 8> SaturationModule::harmonicProfile (int character, float driveDb, float levelDb) noexcept
    {
        character = juce::jlimit (0, numCharacters - 1, character);
        constexpr int points = 64;
        const float d = dbToGain (driveDb);
        const float c = compensationFor (d, character);
        const float a = dbToGain (levelDb);
        float y[points];
        for (int i = 0; i < points; ++i)
            y[i] = shape (a * std::sin (juce::MathConstants<float>::twoPi * (float) i / (float) points), d, character) * c;

        std::array<double, 8> mag {};
        for (int k = 1; k <= 8; ++k)
        {
            double re = 0.0, im = 0.0;
            for (int i = 0; i < points; ++i)
            {
                const double w = juce::MathConstants<double>::twoPi * k * i / points;
                re += y[i] * std::cos (w);
                im += y[i] * std::sin (w);
            }
            mag[(size_t) k - 1] = 2.0 * std::sqrt (re * re + im * im) / points;
        }

        std::array<float, 8> out {};
        out[0] = (float) (20.0 * std::log10 (mag[0] / std::max (1.0e-12, (double) a) + 1.0e-12));
        for (int k = 1; k < 8; ++k)
            out[(size_t) k] = (float) (20.0 * std::log10 (mag[(size_t) k] / std::max (mag[0], 1.0e-12) + 1.0e-12));
        return out;
    }

    void SaturationModule::Svf::set (double fc, double fs) noexcept
    {
        const double g = std::tan (juce::MathConstants<double>::pi * fc / fs);
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    void SaturationModule::prepare (double sampleRate, int maxBlockSize)
    {
        oversampler.prepare (sampleRate, maxBlockSize);
        highRate = sampleRate * oversampler.getFactor();
        // The ADAA (and the matching dry average) delay the high-rate signal by half a sample,
        // the roll-off correction by one more
        latency = (int) std::lround (oversampler.getDelayBaseSamples (1.5));

        dry.setSize (maxChannels, maxBlockSize, false, true, false);
        dryDelay.prepare (std::max (1, latency));
        dryDelay.setDelay (latency);

        for (auto& b : bandParams)
        {
            b.driveDb.reset (sampleRate, 0.05);
            b.onAmount.reset (sampleRate, 0.02);
            b.onAmount.setCurrentAndTargetValue (b.onAmount.getTargetValue());
        }
        outGain.reset (sampleRate, 0.05);
        amount.reset (sampleRate, 0.03);
        amount.setCurrentAndTargetValue (amountTarget);
        weight.reset (sampleRate, 0.02);
        weight.setCurrentAndTargetValue (amountTarget > 0.0f ? 1.0f : 0.0f);
        xoverLow.reset (sampleRate, 0.05);
        xoverHigh.reset (sampleRate, 0.05);
        activeMultiband = requestedMultiband;

        dcR = 1.0 - 2.0 * juce::MathConstants<double>::pi * 10.0 / highRate;
        warmupLength = (int) std::lround (0.005 * sampleRate);
        for (int b = 0; b < numBands; ++b)
            updateBandParams (b, 0, true);
        updateCrossovers (0, true);
        reset();
    }

    void SaturationModule::resetState() noexcept
    {
        oversampler.reset();
        for (auto& band : shapers)
            for (auto& s : band)
                s = {};
        for (auto& ch : svfState)
            for (auto& s : ch)
                s = {};
        dryPrev.fill (0.0);
        dcX.fill (0.0);
        dcY.fill (0.0);
        for (auto& b : bandPrev)
            b.fill (0.0);
        for (auto& f : firHist)
            f = { 0.0, 0.0 };
        needsReset = false;
    }

    void SaturationModule::reset()
    {
        dryDelay.reset();
        resetState();
    }

    void SaturationModule::setSettings (const SatSettings& s) noexcept
    {
        requestedMultiband = s.multiband;
        const bool mb = activeMultiband;   // the band targets follow the mode that is playing
        for (int b = 0; b < numBands; ++b)
        {
            auto& p = bandParams[(size_t) b];
            const SatBand band = mb ? s.bands[(size_t) b] : SatBand { true, s.driveDb, s.character };
            p.onAmount.setTargetValue (band.on ? 1.0f : 0.0f);
            p.character = juce::jlimit (0, numCharacters - 1, band.character);
            p.driveDb.setTargetValue (juce::jlimit (0.0f, 36.0f, band.driveDb));
        }
        pending = s;
        const float lo = juce::jlimit (20.0f, 2000.0f, s.xoverLowHz);
        xoverLow.setTargetValue (lo);
        xoverHigh.setTargetValue (juce::jlimit (lo * 1.5f, 20000.0f, s.xoverHighHz));
        outGain.setTargetValue (dbToGain (s.outputDb));
        amountTarget = s.enabled ? juce::jlimit (0.0f, 1.0f, s.mix) : 0.0f;
        amount.setTargetValue (amountTarget);
        weight.setTargetValue (amountTarget > 0.0f && requestedMultiband == activeMultiband ? 1.0f : 0.0f);
    }

    void SaturationModule::updateBandParams (int band, int skip, bool force) noexcept
    {
        auto& p = bandParams[(size_t) band];
        const bool smoothing = p.driveDb.isSmoothing();
        if (! (force || smoothing || p.character != p.preparedCharacter))
            return;

        if (smoothing)
            p.driveDb.skip (skip);
        p.driveLin = (double) dbToGain (p.driveDb.getCurrentValue());
        p.compensation = compensationFor ((float) p.driveLin, p.character);
        p.preparedCharacter = p.character;

        // The ADAA difference needs F (x[n-1]) of the new curve
        for (auto& s : shapers[(size_t) band])
            s.fp = antiderivative (s.xp, p.driveLin, p.character);
    }

    void SaturationModule::updateCrossovers (int skip, bool force) noexcept
    {
        if (! (force || xoverLow.isSmoothing() || xoverHigh.isSmoothing()))
            return;
        xoverLow.skip (skip);
        xoverHigh.skip (skip);
        const double lo = xoverLow.getCurrentValue();
        const double hi = juce::jlimit (lo * 1.5, 0.45 * highRate / oversampler.getFactor(), (double) xoverHigh.getCurrentValue());
        for (int i : { 0, 1, 2 })
            svf[(size_t) i].set (lo, highRate);
        for (int i : { 3, 4, 5, 6 })
            svf[(size_t) i].set (hi, highRate);
    }

    double SaturationModule::shapeAdaa (Shaper& s, const BandParams& p, double x) noexcept
    {
        const double f = antiderivative (x, p.driveLin, p.character);
        const double dx = x - s.xp;
        double y;
        if (std::abs (dx) > adaaEpsilon)
            y = (f - s.fp) / dx;
        else
            y = (double) shape ((float) (0.5 * (x + s.xp)), (float) p.driveLin, p.character);
        s.xp = x;
        s.fp = f;
        return y * p.compensation;
    }

    void SaturationModule::process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        // Aligned dry copy
        for (int ch = 0; ch < numChannels; ++ch)
            dry.copyFrom (ch, 0, channels[ch], numSamples);
        dryDelay.process (dry.getArrayOfWritePointers(), numChannels, numSamples);

        // A pending switch between single and multiband happens while the output is the dry
        if (requestedMultiband != activeMultiband && weight.getCurrentValue() <= 0.0f && ! weight.isSmoothing())
        {
            activeMultiband = requestedMultiband;
            setSettings (pending);
            for (auto& p : bandParams)
            {
                p.driveDb.setCurrentAndTargetValue (p.driveDb.getTargetValue());
                p.onAmount.setCurrentAndTargetValue (p.onAmount.getTargetValue());
            }
            for (int b = 0; b < numBands; ++b)
                updateBandParams (b, 0, true);
            needsReset = true;
        }

        if (amountTarget <= 0.0f && ! amount.isSmoothing() && weight.getCurrentValue() <= 0.0f && ! weight.isSmoothing())
        {
            for (int ch = 0; ch < numChannels; ++ch)
                juce::FloatVectorOperations::copy (channels[ch], dry.getReadPointer (ch), numSamples);
            needsReset = true;
            for (int b = 0; b < numBands; ++b)
            {
                meters.inDb[(size_t) b].store (-200.0f, std::memory_order_relaxed);
                meters.outDb[(size_t) b].store (-200.0f, std::memory_order_relaxed);
            }
            return;
        }

        if (needsReset)
        {
            // The filters start from silence while the input is running: their start-up
            // transient must settle before the crossfade lets them be heard
            resetState();
            warmup = warmupLength;
        }

        const bool mb = activeMultiband;
        const int factor = oversampler.getFactor();
        float* const* up = oversampler.upsample (channels, numChannels, numSamples);
        std::array<double, numBands> inSum {}, outSum {};

        for (int start = 0; start < numSamples; start += updateInterval)
        {
            const int n = std::min (updateInterval, numSamples - start);
            for (int b = 0; b < (mb ? numBands : 1); ++b)
                updateBandParams (b, n, false);
            if (mb)
                updateCrossovers (n, false);

            float aBuf[updateInterval], gBuf[updateInterval], onBuf[numBands][updateInterval];
            for (int i = 0; i < n; ++i)
            {
                aBuf[i] = amount.getNextValue();
                gBuf[i] = outGain.getNextValue();
                for (int b = 0; b < numBands; ++b)
                    onBuf[b][i] = bandParams[(size_t) b].onAmount.getNextValue();
            }

            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* s = up[ch];
                auto& st = svfState[(size_t) ch];
                for (int j = start * factor; j < (start + n) * factor; ++j)
                {
                    const int i = j / factor - start;
                    const double u = s[j];
                    double wet = 0.0, sum = u;

                    if (! mb)
                    {
                        auto& p = bandParams[0];
                        wet = shapeAdaa (shapers[0][(size_t) ch], p, u);
                        inSum[0] += u * u;
                        outSum[0] += wet * wet;
                    }
                    else
                    {
                        // LR4 split (two Butterworth sections each): low, then all-passed at the
                        // high crossover so it stays in phase with mid + high; mid; high
                        double bp;
                        const double l1 = st[0].tick (svf[0], u, bp);
                        const double h1 = u - svf[0].k * bp - l1;
                        const double low4 = st[1].tick (svf[1], l1, bp);
                        const double l2 = st[2].tick (svf[2], h1, bp);
                        const double high4 = h1 - svf[2].k * bp - l2;
                        st[3].tick (svf[3], low4, bp);
                        const double lowAp = low4 - 2.0 * svf[3].k * bp;
                        const double l3 = st[4].tick (svf[4], high4, bp);
                        const double h3 = high4 - svf[4].k * bp - l3;
                        const double mid = st[5].tick (svf[5], l3, bp);
                        const double l4 = st[6].tick (svf[6], h3, bp);
                        const double high = h3 - svf[6].k * bp - l4;

                        const double x[numBands] { lowAp, mid, high };
                        sum = lowAp + mid + high;
                        auto& prev = bandPrev[(size_t) ch];
                        for (int b = 0; b < numBands; ++b)
                        {
                            const double xb = x[b];
                            const double lin = 0.5 * (xb + prev[(size_t) b]);   // what ADAA does to a straight line
                            prev[(size_t) b] = xb;
                            const auto& p = bandParams[(size_t) b];
                            auto& sh = shapers[(size_t) b][(size_t) ch];
                            const float on = onBuf[b][i];
                            double y = lin;
                            if (on > 0.0f)
                            {
                                if (sh.stale)
                                {
                                    sh.fp = antiderivative (sh.xp, p.driveLin, p.character);
                                    sh.stale = false;
                                }
                                y = lin + (double) on * (shapeAdaa (sh, p, xb) - lin);
                            }
                            else
                            {
                                sh.xp = xb;
                                sh.stale = true;
                            }
                            wet += y;
                            inSum[(size_t) b] += xb * xb;
                            outSum[(size_t) b] += y * y;
                        }
                    }

                    const double dryOs = 0.5 * (sum + dryPrev[(size_t) ch]);
                    dryPrev[(size_t) ch] = sum;

                    // DC blocker on the added distortion only (the asymmetric curves make DC)
                    const double diff = wet - dryOs;
                    const double hpDiff = diff - dcX[(size_t) ch] + dcR * dcY[(size_t) ch];
                    dcX[(size_t) ch] = diff;
                    dcY[(size_t) ch] = hpDiff;

                    const double a = aBuf[i];
                    const double v = dryOs + a * ((double) gBuf[i] * (dryOs + hpDiff) - dryOs);

                    // Undo the cos (w / 2) roll-off of the half-sample average (ADAA and dry):
                    // symmetric 3-tap FIR 1 + 2c (1 - cos w), within 0.02 dB up to 20 kHz
                    auto& fh = firHist[(size_t) ch];
                    s[j] = (float) (-hfFix * fh[1] + (1.0 + 2.0 * hfFix) * fh[0] - hfFix * v);
                    fh[1] = fh[0];
                    fh[0] = v;
                }
            }
        }

        oversampler.downsample (channels, numChannels, numSamples);

        // Crossfade against the delayed dry (on/off, mode switches)
        for (int i = 0; i < numSamples; ++i)
        {
            float w = 0.0f;
            if (warmup > 0)
                --warmup;
            else
                w = weight.getNextValue();
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float d = dry.getSample (ch, i);
                channels[ch][i] = d + (channels[ch][i] - d) * w;
            }
        }

        const int count = numSamples * factor * std::max (1, numChannels);
        for (int b = 0; b < numBands; ++b)
        {
            meters.inDb[(size_t) b].store (meterDb (inSum[(size_t) b], count), std::memory_order_relaxed);
            meters.outDb[(size_t) b].store (meterDb (outSum[(size_t) b], count), std::memory_order_relaxed);
        }

        for (auto& v : dcY)
            if (! std::isfinite (v))
                reset();
    }
}
