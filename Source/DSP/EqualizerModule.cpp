#include "EqualizerModule.h"

namespace velvox::dsp
{
    int EqSettings::findRole (EqRole role) const noexcept
    {
        if (role == EqRole::none)
            return -1;
        for (int i = 0; i < maxEqBands; ++i)
            if (bands[(size_t) i].bandRole() == role)
                return i;
        return -1;
    }

    int EqSettings::findFree() const noexcept
    {
        for (int i = 0; i < maxEqBands; ++i)
            if (bands[(size_t) i].isFree())
                return i;
        return -1;
    }

    namespace
    {
        float clampedFreq (const EqBand& b) noexcept { return juce::jlimit (eqdesign::minFreq, eqdesign::maxFreq, b.freq); }
        float clampedQ (const EqBand& b) noexcept    { return juce::jlimit (eqdesign::minQ, eqdesign::maxQ, b.q); }
        float clampedGain (const EqBand& b) noexcept { return juce::jlimit (-eqdesign::maxGainDb, eqdesign::maxGainDb, b.gain); }
    }

    void EqualizerModule::prepare (double sampleRate, int)
    {
        fs = sampleRate;
        for (auto& b : bands)
        {
            b.freq.reset (sampleRate, 0.05);
            b.q.reset (sampleRate, 0.05);
            b.gain.reset (sampleRate, 0.05);
            b.amount.reset (sampleRate, 0.03);
        }
        for (auto& v : auditionVoices)
        {
            v.path.prepare (sampleRate);
            v.band = -1;
        }
        reset();
    }

    void EqualizerModule::reset()
    {
        for (auto& b : bands)
            for (auto& f : b.filters)
                f.reset();
        for (auto& v : auditionVoices)
            v.path.reset();
        snapNext = true;
    }

    void EqualizerModule::processAudition (const float* const* moduleInput, float* const* io, int numChannels, int numSamples) noexcept
    {
        int want = auditionRequest.load (std::memory_order_relaxed);
        if (want < 0 || want >= maxEqBands || bands[(size_t) want].target.isFree())
            want = -1;
        const auto region = auditionRegionFor (bands[(size_t) (want >= 0 ? want : 0)].target);

        // The path that plays the wanted band keeps playing it (and follows a dragged band); every
        // other path fades out. A new band (or a new shape of the band) starts on a silent path while
        // the old one fades out: a crossfade between the two regions.
        bool playing = false;
        for (auto& v : auditionVoices)
        {
            if (v.path.isSilent())
                v.band = -1;
            if (want >= 0 && ! playing && v.band == want && v.path.getShape() == region.shape)
            {
                v.path.follow (region);
                v.path.setOn (true);
                playing = true;
            }
            else
            {
                v.path.setOn (false);
            }
        }
        if (want >= 0 && ! playing)
            for (auto& v : auditionVoices)
                if (v.band < 0)
                {
                    v.band = want;
                    v.path.start (region);
                    v.path.setOn (true);
                    break;
                }   // both paths still fading: the new band starts at the next block

        const bool a = ! auditionVoices[0].path.isSilent(), b = ! auditionVoices[1].path.isSilent();
        if (! a && ! b)
            return;
        if (a != b)
        {
            (a ? auditionVoices[0] : auditionVoices[1]).path.process (moduleInput, io, io, numChannels, numSamples);
            return;
        }

        // Both audible: each path needs the normal output (and the input, when it is io) unchanged
        numChannels = std::min (numChannels, maxChannels);
        const bool inputIsIo = moduleInput[0] == io[0];
        for (int start = 0; start < numSamples; start += auditionBlock)
        {
            const int n = std::min (auditionBlock, numSamples - start);
            std::array<const float*, maxChannels> normal {}, input {};
            std::array<float*, maxChannels> out {};
            for (int ch = 0; ch < numChannels; ++ch)
            {
                std::copy (io[ch] + start, io[ch] + start + n, auditionNormal[(size_t) ch].begin());
                normal[(size_t) ch] = auditionNormal[(size_t) ch].data();
                input[(size_t) ch] = inputIsIo ? normal[(size_t) ch] : moduleInput[ch] + start;
                out[(size_t) ch] = io[ch] + start;
            }
            for (auto& v : auditionVoices)
                v.path.process (input.data(), normal.data(), out.data(), numChannels, n);
        }
    }

    void EqualizerModule::setSettings (const EqSettings& s)
    {
        legacyTarget = s.legacyResponse;
        for (size_t i = 0; i < bands.size(); ++i)
        {
            auto& b = bands[i];
            b.target = s.bands[i];
            if (snapNext)
                continue;   // adopted as a whole by the next process()

            const bool topologyChange = b.target.type != b.type || legacyTarget != b.legacy
                                     || (b.target.isCut() && b.target.slope != b.slope);
            if (topologyChange)
            {
                // fade out, switch, fade in (process() completes it)
                b.switching = true;
                b.amount.setTargetValue (0.0f);
                continue;
            }
            if (b.switching)
                continue;

            b.freq.setTargetValue (clampedFreq (b.target));
            b.q.setTargetValue (clampedQ (b.target));
            b.gain.setTargetValue (clampedGain (b.target));
            b.amount.setTargetValue (b.target.on ? 1.0f : 0.0f);
        }
    }

    void EqualizerModule::adopt (BandState& b) noexcept
    {
        b.type = b.target.type;
        b.slope = b.target.slope;
        b.legacy = legacyTarget;
        b.switching = false;
        b.freq.setCurrentAndTargetValue (clampedFreq (b.target));
        b.q.setCurrentAndTargetValue (clampedQ (b.target));
        b.gain.setCurrentAndTargetValue (clampedGain (b.target));
        for (auto& f : b.filters)
            f.reset();
        design (b);
    }

    void EqualizerModule::design (BandState& b) noexcept
    {
        EqBand cur = b.target;
        cur.type = b.type;
        cur.slope = b.slope;
        cur.freq = b.freq.getCurrentValue();
        cur.q = b.q.getCurrentValue();
        cur.gain = b.gain.getCurrentValue();

        const auto d = eqdesign::design (cur, b.legacy, fs);
        for (int k = 0; k < d.numSections; ++k)
            b.filters[(size_t) k].setCoeffs (d.sections[(size_t) k]);
        b.numSections = d.numSections;
        b.isCut = cur.isCut();
        b.needsDesign = false;
    }

    void EqualizerModule::processBand (BandState& b, float* const* channels, int numChannels, int start, int n) noexcept
    {
        std::array<float, updateInterval> amounts;
        const bool ramp = b.amount.isSmoothing();
        const float steady = b.amount.getCurrentValue();
        if (ramp)
            for (int i = 0; i < n; ++i)
                amounts[(size_t) i] = b.amount.getNextValue();

        // Cuts always blend (x + (y - x) * amount), other types are applied directly at full
        // amount: exactly the arithmetic of the 0.5 module (bit-identical legacy states)
        const bool direct = ! b.isCut && ! ramp && steady >= 1.0f;
        const int sections = b.numSections;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* d = channels[ch] + start;
            for (int i = 0; i < n; ++i)
            {
                float x = d[i];
                float y = x;
                for (int k = 0; k < sections; ++k)
                    y = b.filters[(size_t) k].process (y, ch);
                if (direct)
                    x = y;
                else
                    x += (y - x) * (ramp ? amounts[(size_t) i] : steady);
                d[i] = x;
            }
        }
    }

    void EqualizerModule::process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        numChannels = std::min (numChannels, maxChannels);

        if (snapNext)
        {
            snapNext = false;
            for (auto& b : bands)
            {
                adopt (b);
                b.amount.setCurrentAndTargetValue (b.target.on ? 1.0f : 0.0f);
            }
        }

        for (int start = 0; start < numSamples; start += updateInterval)
        {
            const int n = std::min (updateInterval, numSamples - start);

            for (auto& b : bands)
            {
                // A pending topology change takes place once the band has faded out
                if (b.switching && b.amount.getCurrentValue() <= 0.0f && ! b.amount.isSmoothing())
                {
                    adopt (b);
                    b.amount.setTargetValue (b.target.on ? 1.0f : 0.0f);
                }

                // Silent band: nothing to process; follows its settings without a sweep
                if (b.amount.getCurrentValue() <= 0.0f && ! b.amount.isSmoothing())
                {
                    if (b.freq.getTargetValue() != clampedFreq (b.target) || b.q.getTargetValue() != clampedQ (b.target)
                        || b.gain.getTargetValue() != clampedGain (b.target) || b.freq.isSmoothing() || b.q.isSmoothing() || b.gain.isSmoothing())
                    {
                        b.freq.setCurrentAndTargetValue (clampedFreq (b.target));
                        b.q.setCurrentAndTargetValue (clampedQ (b.target));
                        b.gain.setCurrentAndTargetValue (clampedGain (b.target));
                        b.needsDesign = true;
                    }
                    for (auto& f : b.filters)
                        f.reset();
                    continue;
                }

                if (b.freq.isSmoothing() || b.q.isSmoothing() || b.gain.isSmoothing())
                {
                    b.freq.skip (n);
                    b.q.skip (n);
                    b.gain.skip (n);
                    b.needsDesign = true;
                }
                if (b.needsDesign)
                    design (b);

                processBand (b, channels, numChannels, start, n);
            }
        }

        for (auto& b : bands)
            for (int k = 0; k < b.numSections; ++k)
                if (! b.filters[(size_t) k].isStateFinite())
                {
                    for (auto& band : bands)
                        for (auto& f : band.filters)
                            f.reset();
                    return;
                }
    }

    //==========================================================================
    double EqualizerModule::bandResponseDb (const EqBand& band, bool legacyResponse, double freq, double sampleRate)
    {
        const auto d = eqdesign::design (band, legacyResponse, sampleRate);
        return 20.0 * std::log10 (std::max (eqdesign::magnitude (d, freq, sampleRate), 1.0e-9));
    }

    double EqualizerModule::responseDb (const EqSettings& s, double freq, double sampleRate)
    {
        double mag = 1.0;
        for (const auto& b : s.bands)
            if (b.on)
                mag *= eqdesign::magnitude (eqdesign::design (b, s.legacyResponse, sampleRate), freq, sampleRate);
        return 20.0 * std::log10 (std::max (mag, 1.0e-9));
    }

    void EqualizerModule::responseCurveDb (const EqSettings& s, double sampleRate, const float* freqs, float* outDb, int count, int band)
    {
        std::array<EqBandDesign, maxEqBands> designs;
        std::array<bool, maxEqBands> used {};
        for (int i = 0; i < maxEqBands; ++i)
        {
            const auto& b = s.bands[(size_t) i];
            used[(size_t) i] = band < 0 ? b.on : i == band;
            if (used[(size_t) i])
                designs[(size_t) i] = eqdesign::design (b, s.legacyResponse, sampleRate);
        }

        for (int k = 0; k < count; ++k)
        {
            double mag = 1.0;
            for (int i = 0; i < maxEqBands; ++i)
                if (used[(size_t) i])
                    mag *= eqdesign::magnitude (designs[(size_t) i], freqs[k], sampleRate);
            outDb[k] = (float) (20.0 * std::log10 (std::max (mag, 1.0e-9)));
        }
    }

    std::array<EqBand, 5> EqualizerModule::legacyBands (const EqSettings& s) noexcept
    {
        const auto slopeOf = [] (int legacySlope)
        {
            constexpr int map[3] { 1, 3, 5 };   // 12 / 24 / 48 dB/oct
            return map[juce::jlimit (0, 2, legacySlope)];
        };

        std::array<EqBand, 5> b;
        b[0] = { s.hpfOn, (int) EqBandType::lowCut, s.hpfFreq, 0.0f, 0.7071f, slopeOf (s.hpfSlope), (int) EqRole::lowCut };
        b[1] = { true, (int) (s.lowType == 1 ? EqBandType::bell : EqBandType::lowShelf), s.lowFreq, s.lowGain, s.lowQ, 3, (int) EqRole::warmth };
        b[2] = { true, (int) EqBandType::bell, s.midFreq, s.midGain, s.midQ, 3, (int) EqRole::autoMix };
        b[3] = { true, (int) (s.highType == 1 ? EqBandType::bell : EqBandType::highShelf), s.highFreq, s.highGain, s.highQ, 3, (int) EqRole::air };
        b[4] = { s.lpfOn, (int) EqBandType::highCut, s.lpfFreq, 0.0f, 0.7071f, slopeOf (s.lpfSlope), (int) EqRole::highCut };
        return b;
    }
}
