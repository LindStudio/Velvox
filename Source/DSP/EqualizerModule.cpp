#include "EqualizerModule.h"

namespace velvox::dsp
{
    namespace
    {
        constexpr double shelfQ = 0.7071;
        // Butterworth Q values: 2nd order, and the two sections of a 4th order
        constexpr double bw2Q = 0.70710678;
        constexpr double bw4Q1 = 0.54119610;
        constexpr double bw4Q2 = 1.30656296;
    }

    void EqualizerModule::prepare (double sampleRate, int)
    {
        fs = sampleRate;

        for (auto* s : { &hpfFreq, &lowFreq, &midFreq, &highFreq, &midQ })
            s->reset (sampleRate, 0.05);
        for (auto* s : { &lowGain, &midGain, &highGain })
            s->reset (sampleRate, 0.05);
        hpfAmount.reset (sampleRate, 0.03);

        setSettings (target);
        hpfAmount.setCurrentAndTargetValue (target.hpfOn ? 1.0f : 0.0f);
        updateCoefficients (true);
        reset();
    }

    void EqualizerModule::reset()
    {
        for (auto& f : filters)
            f.reset();
    }

    void EqualizerModule::setSettings (const EqSettings& s)
    {
        target = s;
        hpfFreq.setTargetValue (s.hpfFreq);
        lowFreq.setTargetValue (s.lowFreq);
        midFreq.setTargetValue (s.midFreq);
        highFreq.setTargetValue (s.highFreq);
        midQ.setTargetValue (s.midQ);
        lowGain.setTargetValue (s.lowGain);
        midGain.setTargetValue (s.midGain);
        highGain.setTargetValue (s.highGain);
        hpfAmount.setTargetValue (s.hpfOn ? 1.0f : 0.0f);
    }

    EqualizerModule::Sections EqualizerModule::designSections (const EqSettings& s, double sampleRate)
    {
        Sections out;

        if (s.hpfSlope == 0)
        {
            out[hp1] = design::highPass (sampleRate, s.hpfFreq, bw2Q);
            out[hp2] = BiquadCoeffs {}; // identity
        }
        else
        {
            out[hp1] = design::highPass (sampleRate, s.hpfFreq, bw4Q1);
            out[hp2] = design::highPass (sampleRate, s.hpfFreq, bw4Q2);
        }

        out[lowShelf]  = design::lowShelf (sampleRate, s.lowFreq, shelfQ, s.lowGain);
        out[midBell]   = design::peak (sampleRate, s.midFreq, s.midQ, s.midGain);
        out[highShelf] = design::highShelf (sampleRate, s.highFreq, shelfQ, s.highGain);
        return out;
    }

    double EqualizerModule::responseDb (const EqSettings& s, double freq, double sampleRate)
    {
        const auto sections = designSections (s, sampleRate);
        double mag = 1.0;

        for (int i = 0; i < numSections; ++i)
        {
            if ((i == hp1 || i == hp2) && ! s.hpfOn)
                continue;
            mag *= design::magnitude (sections[(size_t) i], freq, sampleRate);
        }

        return 20.0 * std::log10 (std::max (mag, 1.0e-9));
    }

    void EqualizerModule::updateCoefficients (bool snap) noexcept
    {
        if (snap)
        {
            for (auto* s : { &hpfFreq, &lowFreq, &midFreq, &highFreq, &midQ })
                s->setCurrentAndTargetValue (s->getTargetValue());
            for (auto* s : { &lowGain, &midGain, &highGain })
                s->setCurrentAndTargetValue (s->getTargetValue());
        }

        EqSettings cur = target;
        cur.hpfFreq  = hpfFreq.getCurrentValue();
        cur.lowFreq  = lowFreq.getCurrentValue();
        cur.midFreq  = midFreq.getCurrentValue();
        cur.highFreq = highFreq.getCurrentValue();
        cur.midQ     = midQ.getCurrentValue();
        cur.lowGain  = lowGain.getCurrentValue();
        cur.midGain  = midGain.getCurrentValue();
        cur.highGain = highGain.getCurrentValue();

        if (cur.hpfSlope != currentSlope)
        {
            // Changing the filter topology: clear the HP state to avoid bursts
            filters[hp1].reset();
            filters[hp2].reset();
            currentSlope = cur.hpfSlope;
        }

        const auto sections = designSections (cur, fs);
        for (size_t i = 0; i < sections.size(); ++i)
            filters[i].setCoeffs (sections[i]);
    }

    void EqualizerModule::process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        int start = 0;

        while (start < numSamples)
        {
            const int n = std::min (updateInterval, numSamples - start);

            const bool smoothing = hpfFreq.isSmoothing() || lowFreq.isSmoothing() || midFreq.isSmoothing()
                                || highFreq.isSmoothing() || midQ.isSmoothing() || lowGain.isSmoothing()
                                || midGain.isSmoothing() || highGain.isSmoothing() || target.hpfSlope != currentSlope;

            if (smoothing)
            {
                for (auto* s : { &hpfFreq, &lowFreq, &midFreq, &highFreq, &midQ })
                    s->skip (n);
                for (auto* s : { &lowGain, &midGain, &highGain })
                    s->skip (n);
                updateCoefficients (false);
            }

            const bool hpfRamping = hpfAmount.isSmoothing();
            const float hpfStatic = hpfAmount.getCurrentValue();

            for (int i = start; i < start + n; ++i)
            {
                const float amt = hpfRamping ? hpfAmount.getNextValue() : hpfStatic;

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    float x = channels[ch][i];

                    if (amt > 0.0f)
                    {
                        const float hp = filters[hp2].process (filters[hp1].process (x, ch), ch);
                        x += (hp - x) * amt;
                    }

                    x = filters[lowShelf].process (x, ch);
                    x = filters[midBell].process (x, ch);
                    x = filters[highShelf].process (x, ch);
                    channels[ch][i] = x;
                }
            }

            start += n;
        }

        for (auto& f : filters)
            if (! f.isStateFinite())
                f.reset();
    }
}
