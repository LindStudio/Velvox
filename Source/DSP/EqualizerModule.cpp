#include "EqualizerModule.h"

namespace velvox::dsp
{
    namespace
    {
        // Butterworth section Q values for 2nd, 4th and 8th order
        constexpr double bwQ[3][4] {
            { 0.70710678, 0, 0, 0 },
            { 0.54119610, 1.30656296, 0, 0 },
            { 0.50979558, 0.60134489, 0.89997622, 2.56291545 },
        };
        constexpr int sectionsForSlope[3] { 1, 2, 4 };

        template <typename Fn>
        void designCut (EqualizerModule::Sections& out, int first, int slope, Fn&& design)
        {
            slope = juce::jlimit (0, 2, slope);
            for (int k = 0; k < EqualizerModule::maxCutSections; ++k)
                out[(size_t) (first + k)] = k < sectionsForSlope[slope] ? design (bwQ[slope][k]) : BiquadCoeffs {};
        }

        template <typename SV>
        bool anySmoothing (std::initializer_list<SV*> values)
        {
            for (auto* v : values)
                if (v->isSmoothing())
                    return true;
            return false;
        }
    }

    void EqualizerModule::prepare (double sampleRate, int)
    {
        fs = sampleRate;

        for (auto* s : { &hpfFreq, &lowFreq, &midFreq, &highFreq, &lpfFreq, &midQ, &lowQ, &highQ })
            s->reset (sampleRate, 0.05);
        for (auto* s : { &lowGain, &midGain, &highGain })
            s->reset (sampleRate, 0.05);
        hpfAmount.reset (sampleRate, 0.03);
        lpfAmount.reset (sampleRate, 0.03);

        setSettings (target);
        hpfAmount.setCurrentAndTargetValue (target.hpfOn ? 1.0f : 0.0f);
        lpfAmount.setCurrentAndTargetValue (target.lpfOn ? 1.0f : 0.0f);
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
        lpfFreq.setTargetValue (s.lpfFreq);
        midQ.setTargetValue (s.midQ);
        lowQ.setTargetValue (s.lowQ);
        highQ.setTargetValue (s.highQ);
        lowGain.setTargetValue (s.lowGain);
        midGain.setTargetValue (s.midGain);
        highGain.setTargetValue (s.highGain);
        hpfAmount.setTargetValue (s.hpfOn ? 1.0f : 0.0f);
        lpfAmount.setTargetValue (s.lpfOn ? 1.0f : 0.0f);
    }

    EqualizerModule::Sections EqualizerModule::designSections (const EqSettings& s, double sr)
    {
        Sections out;

        designCut (out, hp1, s.hpfSlope, [&] (double q) { return design::highPass (sr, s.hpfFreq, q); });
        designCut (out, lp1, s.lpfSlope, [&] (double q) { return design::lowPass (sr, s.lpfFreq, q); });

        out[lowBand]  = s.lowType == 1 ? design::peak (sr, s.lowFreq, s.lowQ, s.lowGain)
                                       : design::lowShelf (sr, s.lowFreq, s.lowQ, s.lowGain);
        out[midBell]  = design::peak (sr, s.midFreq, s.midQ, s.midGain);
        out[highBand] = s.highType == 1 ? design::peak (sr, s.highFreq, s.highQ, s.highGain)
                                        : design::highShelf (sr, s.highFreq, s.highQ, s.highGain);
        return out;
    }

    double EqualizerModule::responseDb (const EqSettings& s, double freq, double sampleRate)
    {
        const auto sections = designSections (s, sampleRate);
        double mag = 1.0;

        for (int i = 0; i < numSections; ++i)
        {
            if (i >= hp1 && i <= hp4 && ! s.hpfOn)
                continue;
            if (i >= lp1 && i <= lp4 && ! s.lpfOn)
                continue;
            mag *= design::magnitude (sections[(size_t) i], freq, sampleRate);
        }

        return 20.0 * std::log10 (std::max (mag, 1.0e-9));
    }

    void EqualizerModule::updateCoefficients (bool snap) noexcept
    {
        if (snap)
        {
            for (auto* s : { &hpfFreq, &lowFreq, &midFreq, &highFreq, &lpfFreq, &midQ, &lowQ, &highQ })
                s->setCurrentAndTargetValue (s->getTargetValue());
            for (auto* s : { &lowGain, &midGain, &highGain })
                s->setCurrentAndTargetValue (s->getTargetValue());
        }

        EqSettings cur = target;
        cur.hpfFreq  = hpfFreq.getCurrentValue();
        cur.lowFreq  = lowFreq.getCurrentValue();
        cur.midFreq  = midFreq.getCurrentValue();
        cur.highFreq = highFreq.getCurrentValue();
        cur.lpfFreq  = lpfFreq.getCurrentValue();
        cur.midQ     = midQ.getCurrentValue();
        cur.lowQ     = lowQ.getCurrentValue();
        cur.highQ    = highQ.getCurrentValue();
        cur.lowGain  = lowGain.getCurrentValue();
        cur.midGain  = midGain.getCurrentValue();
        cur.highGain = highGain.getCurrentValue();

        // Changing the filter topology: clear the affected state to avoid bursts
        if (cur.hpfSlope != currentHpSlope)
        {
            for (int i = hp1; i <= hp4; ++i)
                filters[(size_t) i].reset();
            currentHpSlope = cur.hpfSlope;
        }
        if (cur.lpfSlope != currentLpSlope)
        {
            for (int i = lp1; i <= lp4; ++i)
                filters[(size_t) i].reset();
            currentLpSlope = cur.lpfSlope;
        }

        const auto sections = designSections (cur, fs);
        for (size_t i = 0; i < sections.size(); ++i)
            filters[i].setCoeffs (sections[i]);
    }

    void EqualizerModule::process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        const int hpSections = sectionsForSlope[juce::jlimit (0, 2, target.hpfSlope)];
        const int lpSections = sectionsForSlope[juce::jlimit (0, 2, target.lpfSlope)];

        for (int start = 0; start < numSamples; start += updateInterval)
        {
            const int n = std::min (updateInterval, numSamples - start);

            const bool smoothing = anySmoothing<Mult> ({ &hpfFreq, &lowFreq, &midFreq, &highFreq, &lpfFreq, &midQ, &lowQ, &highQ })
                                || anySmoothing<juce::SmoothedValue<float>> ({ &lowGain, &midGain, &highGain })
                                || target.hpfSlope != currentHpSlope || target.lpfSlope != currentLpSlope;

            if (smoothing)
            {
                for (auto* s : { &hpfFreq, &lowFreq, &midFreq, &highFreq, &lpfFreq, &midQ, &lowQ, &highQ })
                    s->skip (n);
                for (auto* s : { &lowGain, &midGain, &highGain })
                    s->skip (n);
                updateCoefficients (false);
            }

            const bool hpRamping = hpfAmount.isSmoothing(), lpRamping = lpfAmount.isSmoothing();
            const float hpStatic = hpfAmount.getCurrentValue(), lpStatic = lpfAmount.getCurrentValue();

            for (int i = start; i < start + n; ++i)
            {
                const float hpAmt = hpRamping ? hpfAmount.getNextValue() : hpStatic;
                const float lpAmt = lpRamping ? lpfAmount.getNextValue() : lpStatic;

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    float x = channels[ch][i];

                    if (hpAmt > 0.0f)
                    {
                        float y = x;
                        for (int k = 0; k < hpSections; ++k)
                            y = filters[(size_t) (hp1 + k)].process (y, ch);
                        x += (y - x) * hpAmt;
                    }

                    x = filters[lowBand].process (x, ch);
                    x = filters[midBell].process (x, ch);
                    x = filters[highBand].process (x, ch);

                    if (lpAmt > 0.0f)
                    {
                        float y = x;
                        for (int k = 0; k < lpSections; ++k)
                            y = filters[(size_t) (lp1 + k)].process (y, ch);
                        x += (y - x) * lpAmt;
                    }

                    channels[ch][i] = x;
                }
            }
        }

        for (auto& f : filters)
            if (! f.isStateFinite())
            {
                reset();
                break;
            }
    }
}
