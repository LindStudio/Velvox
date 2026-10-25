// 0.6.1 audition (items 1, 5, 6): EQ band solo (Pro-Q-style headphone), Saturation band solo in
// multiband mode, and the Saturation Mix in the panel's main row. Monitoring states: never a
// parameter, never saved, never an undo step.

#include "EqTestHelpers.h"
#include "SyntheticVocal.h"
#include "DSP/Audition.h"
#include "DSP/SaturationModule.h"
#include "Parameters/EqBands.h"
#include "Parameters/ParameterIDs.h"

using namespace velvox;
using namespace velvox::test;
using namespace velvox::test::eq;
using dsp::EqualizerModule;

namespace
{
    constexpr double fsA = 48000.0;

    juce::AudioBuffer<float> sines (std::initializer_list<double> freqs, double dbfs, int n, int channels = 2)
    {
        juce::AudioBuffer<float> b (channels, n);
        b.clear();
        const float a = (float) std::pow (10.0, dbfs / 20.0);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < n; ++i)
            {
                double v = 0.0;
                for (double f : freqs)
                    v += std::sin (2.0 * pi * f * i / fsA);
                b.setSample (ch, i, a * (float) v);
            }
        return b;
    }

    double rmsOf (const juce::AudioBuffer<float>& b, int from, int len)
    {
        double s = 0.0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = from; i < from + len; ++i)
                s += (double) b.getSample (ch, i) * b.getSample (ch, i);
        return std::sqrt (s / std::max (1, len * b.getNumChannels()));
    }

    // EQ module + its audition path as the chain runs them (module input captured per block)
    void runEq (EqualizerModule& eq, juce::AudioBuffer<float>& buf, int block = 256,
                std::function<void (int sample)> before = {})
    {
        juce::AudioBuffer<float> in (buf.getNumChannels(), block);
        for (int start = 0; start < buf.getNumSamples(); start += block)
        {
            if (before)
                before (start);
            const int n = std::min (block, buf.getNumSamples() - start);
            std::array<float*, 2> p { buf.getWritePointer (0, start), buf.getWritePointer (std::min (1, buf.getNumChannels() - 1), start) };
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                in.copyFrom (ch, 0, buf, ch, start, n);
            eq.process (p.data(), buf.getNumChannels(), n);
            eq.processAudition (in.getArrayOfReadPointers(), p.data(), buf.getNumChannels(), n);
        }
    }

    dsp::EqSettings oneBand (const EqBand& b, int slot = 0)
    {
        dsp::EqSettings s;
        s.bands[(size_t) slot] = b;
        return s;
    }

    std::unique_ptr<EqualizerModule> makeEq (const dsp::EqSettings& s)
    {
        auto eq = std::make_unique<EqualizerModule>();
        eq->setSettings (s);
        eq->prepare (fsA, 256);
        return eq;
    }

    // Largest second difference inside [from, to) relative to a reference stretch (clicks = spikes)
    double secondDiff (const juce::AudioBuffer<float>& b, int from, int to)
    {
        double m = 0.0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = std::max (2, from); i < std::min (to, b.getNumSamples()); ++i)
                m = std::max (m, (double) std::abs (b.getSample (ch, i) - 2.0f * b.getSample (ch, i - 1) + b.getSample (ch, i - 2)));
        return m;
    }
}

class AuditionEqTests final : public juce::UnitTest
{
public:
    AuditionEqTests() : juce::UnitTest ("Velvox audition: EQ band solo", "Velvox") {}

    void runTest() override
    {
        const int n = (int) (fsA * 1.5);
        const int from = (int) (fsA * 0.5), len = (int) (fsA * 0.5);

        beginTest ("Region of a bell: a band pass at its frequency and Q");
        {
            auto eq = makeEq (oneBand (makeBand (EqBandType::bell, 1000.0f, 0.0f, 4.0f)));
            eq->setAuditionBand (0);
            expectEquals (eq->getAuditionBand(), 0);
            auto buf = sines ({ 200.0, 1000.0, 5000.0 }, -20.0, n);
            runEq (*eq, buf);
            const double c = toneLevel (buf, from, len, 1000.0, fsA);
            expect (dbOf (c / toneLevel (buf, from, len, 200.0, fsA)) > 20.0, "200 Hz well below the band");
            expect (dbOf (c / toneLevel (buf, from, len, 5000.0, fsA)) > 20.0, "5 kHz well below the band");
        }

        beginTest ("The band's gain is not applied (no gain, same region at +12 and 0 dB)");
        {
            auto a = makeEq (oneBand (makeBand (EqBandType::bell, 1000.0f, 0.0f, 2.0f)));
            auto b = makeEq (oneBand (makeBand (EqBandType::bell, 1000.0f, 12.0f, 2.0f)));
            a->setAuditionBand (0);
            b->setAuditionBand (0);
            auto x = noiseBuffer (2, n, 0.2f, 5), y = x;
            runEq (*a, x);
            runEq (*b, y);
            expect (diffDb (x, y, from) < -100.0, "identical: " + juce::String (diffDb (x, y, from), 1) + " dB");
        }

        beginTest ("Shelves and cuts: the region they act on (low = below, high = above)");
        {
            struct Case { EqBandType type; bool low; };
            for (const auto c : { Case { EqBandType::lowShelf, true }, Case { EqBandType::lowCut, true },
                                  Case { EqBandType::highShelf, false }, Case { EqBandType::highCut, false } })
            {
                const float f = c.low ? 400.0f : 4000.0f;
                auto eq = makeEq (oneBand (makeBand (c.type, f, c.low ? 4.0f : -4.0f, 0.7f)));
                eq->setAuditionBand (0);
                auto buf = sines ({ 100.0, 1000.0, 12000.0 }, -20.0, n);
                runEq (*eq, buf);
                const double lo = toneLevel (buf, from, len, 100.0, fsA), hi = toneLevel (buf, from, len, 12000.0, fsA);
                const double mid = toneLevel (buf, from, len, 1000.0, fsA);
                if (c.low)
                    expect (dbOf (lo / hi) > 30.0 && dbOf (lo / mid) > 12.0, eqedit_name (c.type) + ": below passes");
                else
                    expect (dbOf (hi / lo) > 30.0 && dbOf (hi / mid) > 12.0, eqedit_name (c.type) + ": above passes");
            }
        }

        beginTest ("Level-matched: the region plays at about the input level (noise and vocal)");
        {
            auto eq = makeEq (oneBand (makeBand (EqBandType::bell, 2500.0f, -6.0f, 3.0f)));
            eq->setAuditionBand (0);
            auto buf = noiseBuffer (2, (int) fsA * 2, 0.2f, 9);
            const auto in = buf;
            runEq (*eq, buf);
            const double d = dbOf (rmsOf (buf, (int) fsA, (int) fsA) / rmsOf (in, (int) fsA, (int) fsA));
            logMessage ("level match, noise: " + juce::String (d, 2) + " dB");
            expect (std::abs (d) < 3.0, "noise: " + juce::String (d, 2) + " dB");

            VocalSpec s;
            s.seconds = 3.0;
            s.channels = 2;
            const auto v = makeVocal (s);
            juce::AudioBuffer<float> vb (2, v.numSamples());
            for (int ch = 0; ch < 2; ++ch)
                vb.copyFrom (ch, 0, v.vocal[(size_t) ch].data(), v.numSamples());
            const auto vin = vb;
            auto eq2 = makeEq (oneBand (makeBand (EqBandType::bell, 3000.0f, 0.0f, 6.0f)));
            eq2->setAuditionBand (0);
            runEq (*eq2, vb);
            const double dv = dbOf (rmsOf (vb, (int) fsA, (int) fsA * 2) / rmsOf (vin, (int) fsA, (int) fsA * 2));
            logMessage ("level match, vocal: " + juce::String (dv, 2) + " dB");
            expect (std::abs (dv) < 3.0, "vocal: " + juce::String (dv, 2) + " dB");
        }

        beginTest ("Never requested: bit-identical to the EQ alone; released: back to bit-identical");
        {
            const auto s = oneBand (makeBand (EqBandType::bell, 1000.0f, 6.0f, 1.0f));
            auto ref = makeEq (s);
            auto eq = makeEq (s);
            const auto in = noiseBuffer (2, n, 0.3f, 11);
            auto a = in, b = in;
            runModule (*ref, a);
            runEq (*eq, b);
            expectEquals (maxAbsDiff (a, b), 0.0);

            auto r2 = makeEq (s);
            auto e2 = makeEq (s);
            auto c = in, d = in;
            runModule (*r2, c);
            runEq (*e2, d, 256, [&] (int start)
            {
                if (start == 4096) e2->setAuditionBand (0);
                if (start == 4096 * 4) e2->setAuditionBand (-1);
            });
            expect (maxAbsDiff (c, d, 4096 * 4 + (int) (fsA * 0.05)) == 0.0, "after the release fade: bit-identical");
            expect (maxAbsDiff (c, d, 4096) > 1.0e-3, "the solo was audible");
        }

        beginTest ("A free slot or an index out of range: no audition");
        {
            auto eq = makeEq (oneBand (makeBand (EqBandType::bell, 1000.0f, 6.0f, 1.0f)));
            auto ref = makeEq (oneBand (makeBand (EqBandType::bell, 1000.0f, 6.0f, 1.0f)));
            for (int band : { 5, 16, 99, -7 })
            {
                eq->setAuditionBand (band);
                const auto in = noiseBuffer (2, 8192, 0.3f, band + 40);
                auto a = in, b = in;
                runModule (*ref, a);
                runEq (*eq, b);
                expectEquals (maxAbsDiff (a, b), 0.0);
            }
            expectEquals (eq->getAuditionBand(), -1);   // out of range is stored as none
        }

        beginTest ("Click-free: on, off, to another band, type change and a dragged band");
        {
            dsp::EqSettings s;
            s.bands[0] = makeBand (EqBandType::bell, 300.0f, 4.0f, 2.0f);
            s.bands[1] = makeBand (EqBandType::bell, 2000.0f, -3.0f, 4.0f);
            auto eq = makeEq (s);
            // Two tones that both sit in the bands: every state plays something
            const int total = (int) fsA * 3;
            auto buf = sines ({ 300.0, 2000.0 }, -14.0, total);
            runEq (*eq, buf, 256, [&] (int start)
            {
                const int t = start / 256;
                if (t == 40) eq->setAuditionBand (0);
                if (t == 120) eq->setAuditionBand (1);
                if (t == 200) { auto s2 = s; s2.bands[1].freq = 2600.0f; eq->setSettings (s2); }
                if (t == 260) { auto s2 = s; s2.bands[1].type = (int) EqBandType::highShelf; eq->setSettings (s2); }
                if (t == 380) eq->setAuditionBand (-1);
            });
            // Reference: the steepest steady second difference of the two tones at full scale of the result
            const double peak = buf.getMagnitude (0, total);
            const double steady = peak * std::pow (2.0 * pi * 2600.0 / fsA, 2.0);
            const double worst = secondDiff (buf, 256, total);
            logMessage ("switching: worst second difference " + juce::String (worst / steady, 2) + " x steady");
            expect (worst < 1.5 * steady, "worst " + juce::String (worst / steady, 2) + " x a steady tone at the peak");
            expect (rangeIsFinite (buf));
        }

        chainTests();
    }

    void chainTests()
    {
        beginTest ("In the chain: only the region also at EQ Mix 50 % and with the EQ switched off; no parameter, not saved");
        {
            Harness h;
            h.neutral();
            h.set (pid::eqOn, 1.0f);
            h.set (pid::eqMix, 50.0f);
            const auto a = eqbands::accessFor (h.proc.getState());
            const int slot = eqbands::findFreeBand (a, 0);
            eqbands::writeBand (a, 0, slot, makeBand (EqBandType::bell, 1000.0f, 9.0f, 4.0f));

            juce::MemoryBlock before;
            h.proc.getStateInformation (before);

            auto& eq = h.proc.getChain().getEqualizer (0);
            eq.setAuditionBand (slot);
            const int n = (int) (fsA * 1.2), from = (int) (fsA * 0.6), len = (int) (fsA * 0.5);
            for (float on : { 1.0f, 0.0f })
            {
                h.set (pid::eqOn, on);
                const auto out = h.run (sines ({ 200.0, 1000.0, 5000.0 }, -20.0, n));
                const double c = toneLevel (out, from, len, 1000.0, fsA);
                expect (dbOf (c / toneLevel (out, from, len, 200.0, fsA)) > 20.0, "EQ on " + juce::String (on) + ": 200 Hz out");
                expect (dbOf (c / toneLevel (out, from, len, 5000.0, fsA)) > 20.0, "EQ on " + juce::String (on) + ": 5 kHz out");
            }

            h.set (pid::eqOn, 1.0f);
            juce::MemoryBlock during;
            h.proc.getStateInformation (during);
            expect (before == during, "the solo is not in the saved state");
            eq.setAuditionBand (-1);
        }
    }

    static juce::String eqedit_name (EqBandType t) { return juce::String ((int) t); }
    static bool rangeIsFinite (const juce::AudioBuffer<float>& b)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (b.getSample (ch, i)))
                    return false;
        return true;
    }
};

static AuditionEqTests auditionEqTests;
