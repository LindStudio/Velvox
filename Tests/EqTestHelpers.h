#pragma once

// Shared helpers of the EQ 0.6 tests (EqTests.cpp, EqModuleTests.cpp, EqModelTests.cpp)

#include "TestHelpers.h"
#include "LegacyEqV05.h"
#include "DSP/EqDesign.h"
#include "DSP/EqualizerModule.h"

namespace velvox::test::eq
{
    using dsp::EqBand;
    using dsp::EqBandType;

    inline EqBand makeBand (EqBandType type, float freq, float gain, float q, int slope = 3)
    {
        EqBand b;
        b.on = true;
        b.type = (int) type;
        b.freq = freq;
        b.gain = gain;
        b.q = q;
        b.slope = slope;
        return b;
    }

    inline double dbOf (double lin) { return 20.0 * std::log10 (std::max (lin, 1.0e-12)); }

    // The 0.5 fixed bands of `o` in the 0.6 EqSettings (legacy fields)
    inline dsp::EqSettings legacyFieldsOf (const legacy::v05::EqSettings& o)
    {
        dsp::EqSettings s;
        s.hpfOn = o.hpfOn; s.hpfFreq = o.hpfFreq; s.hpfSlope = o.hpfSlope;
        s.lowType = o.lowType; s.lowFreq = o.lowFreq; s.lowGain = o.lowGain; s.lowQ = o.lowQ;
        s.midFreq = o.midFreq; s.midGain = o.midGain; s.midQ = o.midQ;
        s.highType = o.highType; s.highFreq = o.highFreq; s.highGain = o.highGain; s.highQ = o.highQ;
        s.lpfOn = o.lpfOn; s.lpfFreq = o.lpfFreq; s.lpfSlope = o.lpfSlope;
        return s;
    }

    // What a migrated 0.5 state processes: bands 1-5 from the fixed bands, legacy response
    inline dsp::EqSettings migrated (const legacy::v05::EqSettings& o)
    {
        auto s = legacyFieldsOf (o);
        const auto five = dsp::EqualizerModule::legacyBands (s);
        for (size_t i = 0; i < five.size(); ++i)
            s.bands[i] = five[i];
        s.legacyResponse = true;
        return s;
    }

    inline juce::AudioBuffer<float> noiseBuffer (int channels, int n, float amp, int seed)
    {
        juce::Random r (seed);
        juce::AudioBuffer<float> b (channels, n);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < n; ++i)
                b.setSample (ch, i, amp * (r.nextFloat() * 2.0f - 1.0f));
        return b;
    }

    // Runs `module` over `buf` in blocks of `block` (the chain uses up to 256)
    template <typename Module>
    void runModule (Module& m, juce::AudioBuffer<float>& buf, int block = 256)
    {
        for (int start = 0; start < buf.getNumSamples(); start += block)
        {
            const int n = std::min (block, buf.getNumSamples() - start);
            std::array<float*, 2> p { buf.getWritePointer (0, start), buf.getWritePointer (std::min (1, buf.getNumChannels() - 1), start) };
            m.process (p.data(), buf.getNumChannels(), n);
        }
    }

    inline double maxAbsDiff (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int from = 0)
    {
        double d = 0.0;
        for (int ch = 0; ch < std::min (a.getNumChannels(), b.getNumChannels()); ++ch)
            for (int i = from; i < std::min (a.getNumSamples(), b.getNumSamples()); ++i)
                d = std::max (d, (double) std::abs (a.getSample (ch, i) - b.getSample (ch, i)));
        return d;
    }

    inline double diffDb (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int from = 0)
    {
        return juce::Decibels::gainToDecibels (maxAbsDiff (a, b, from), -400.0);
    }

    // Largest second difference |x[n] - 2x[n-1] + x[n-2]| (clicks show up as spikes)
    inline double maxSecondDifference (const juce::AudioBuffer<float>& b, int from)
    {
        double m = 0.0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = std::max (2, from); i < b.getNumSamples(); ++i)
                m = std::max (m, (double) std::abs (b.getSample (ch, i) - 2.0f * b.getSample (ch, i - 1) + b.getSample (ch, i - 2)));
        return m;
    }

    // A few 0.5 configurations (defaults, typical, bells + steep cuts, extremes)
    inline std::vector<legacy::v05::EqSettings> legacyCases()
    {
        std::vector<legacy::v05::EqSettings> v;
        v.push_back ({});   // 0.5 defaults: 80 Hz low cut, flat bands
        legacy::v05::EqSettings a;
        a.hpfFreq = 120.0f; a.hpfSlope = 0; a.lowFreq = 200.0f; a.lowGain = -3.0f; a.midFreq = 2500.0f; a.midGain = 4.0f; a.midQ = 2.0f;
        a.highFreq = 9000.0f; a.highGain = 5.0f; a.lpfOn = true; a.lpfFreq = 16000.0f; a.lpfSlope = 1;
        v.push_back (a);
        legacy::v05::EqSettings b;
        b.hpfOn = false; b.lowType = 1; b.lowFreq = 300.0f; b.lowGain = 6.0f; b.lowQ = 3.0f; b.midFreq = 700.0f; b.midGain = -9.0f; b.midQ = 6.0f;
        b.highType = 1; b.highFreq = 4000.0f; b.highGain = -4.5f; b.highQ = 0.5f; b.lpfOn = true; b.lpfFreq = 6000.0f; b.lpfSlope = 2;
        v.push_back (b);
        legacy::v05::EqSettings c;
        c.hpfFreq = 400.0f; c.hpfSlope = 2; c.lowGain = 18.0f; c.highGain = -18.0f; c.highFreq = 18000.0f; c.midGain = 12.0f; c.midFreq = 8000.0f;
        v.push_back (c);
        return v;
    }
}
