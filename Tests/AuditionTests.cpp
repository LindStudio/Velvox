// 0.6.1 audition (items 1, 5, 6): EQ band solo (Pro-Q-style headphone), Saturation band solo in
// multiband mode, and the Saturation Mix in the panel's main row. Monitoring states: never a
// parameter, never saved, never an undo step.

#include "EqTestHelpers.h"
#include "SyntheticVocal.h"
#include "DSP/Audition.h"
#include "DSP/SaturationModule.h"
#include "Parameters/EqBands.h"
#include "Parameters/ParameterIDs.h"
#include "PluginEditor.h"
#include "UI/EqPanel.h"
#include "UI/SatPanel.h"
#include "UI/AuditionButton.h"
#include <juce_audio_formats/juce_audio_formats.h>

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

    // Largest amplitude of a tone in [from, to) of channel 0 (narrow band pass, Q 20)
    double toneAmplitude (const juce::AudioBuffer<float>& b, int from, int to, double freq, double fs = fsA)
    {
        juce::IIRFilter f;
        f.setCoefficients (juce::IIRCoefficients::makeBandPass (fs, freq, 20.0));
        double m = 0.0;
        for (int i = 0; i < std::min (to, b.getNumSamples()); ++i)
        {
            const double y = f.processSingleSampleRaw (b.getSample (0, i));
            if (i >= from)
                m = std::max (m, std::abs (y));
        }
        return m;
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

            // Sibilance through a high region: matched in RMS, but its peaks stay below the input's
            VocalSpec sib;
            sib.seconds = 4.0;
            sib.channels = 2;
            sib.sibilance = 1.0f;
            sib.levelDb = -20.0f;   // input peaks below -1 dBFS
            const auto sv = makeVocal (sib);
            juce::AudioBuffer<float> sb (2, sv.numSamples());
            for (int ch = 0; ch < 2; ++ch)
                sb.copyFrom (ch, 0, sv.vocal[(size_t) ch].data(), sv.numSamples());
            const float inPeak = std::max (sb.getMagnitude (0, 0, sb.getNumSamples()), sb.getMagnitude (1, 0, sb.getNumSamples()));
            auto eq3 = makeEq (oneBand (makeBand (EqBandType::highShelf, 9000.0f, 3.0f, 0.7f)));
            eq3->setAuditionBand (0);
            runEq (*eq3, sb);
            const float outPeak = std::max (sb.getMagnitude (0, 0, sb.getNumSamples()), sb.getMagnitude (1, 0, sb.getNumSamples()));
            logMessage ("high region peak vs input peak: " + juce::String (dbOf (outPeak / inPeak), 2) + " dB, out "
                        + juce::String (dbOf (outPeak), 2) + " dBFS");
            const float allowed = std::max (inPeak, std::min (2.0f * inPeak, 0.891f));
            expect (outPeak <= allowed * 1.06f && outPeak < 0.95f, "peaks capped: " + juce::String (dbOf (outPeak), 2) + " dBFS");
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

        beginTest ("From one band to another: a crossfade between the regions, never a blip of the full signal");
        {
            dsp::EqSettings s;
            s.bands[0] = makeBand (EqBandType::bell, 300.0f, 0.0f, 8.0f);
            s.bands[1] = makeBand (EqBandType::bell, 5000.0f, 0.0f, 8.0f);
            auto eq = makeEq (s);
            eq->setAuditionBand (0);
            const int total = (int) fsA * 2, at = 256 * 188;   // ~1 s, block aligned
            auto buf = sines ({ 300.0, 1200.0, 5000.0 }, -20.0, total);
            runEq (*eq, buf, 256, [&] (int start) { if (start == at) eq->setAuditionBand (1); });
            const double outside = toneAmplitude (buf, at - (int) (0.05 * fsA), at + (int) (0.06 * fsA), 1200.0);
            const double tone = std::pow (10.0, -20.0 / 20.0);
            logMessage ("1.2 kHz (in neither region) around the hop: " + juce::String (dbOf (outside / tone), 1) + " dB");
            expect (dbOf (outside / tone) < -15.0, juce::String (dbOf (outside / tone), 1) + " dB");
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

//==============================================================================
namespace
{
    dsp::SatSettings multiband (bool mb = true, float mix = 1.0f)
    {
        dsp::SatSettings s;
        s.enabled = true;
        s.multiband = mb;
        s.mix = mix;
        s.driveDb = 9.0f;
        s.character = 2;
        s.xoverLowHz = 250.0f;
        s.xoverHighHz = 4000.0f;
        s.bands = { dsp::SatBand { true, 9.0f, 0 }, dsp::SatBand { true, 12.0f, 2 }, dsp::SatBand { true, 6.0f, 1 } };
        return s;
    }

    juce::AudioBuffer<float> renderSat (const dsp::SatSettings& s, const juce::AudioBuffer<float>& input, int solo,
                                        std::function<void (dsp::SaturationModule&, int)> before = {})
    {
        auto sat = std::make_unique<dsp::SaturationModule>();
        sat->setSettings (s);
        sat->prepare (fsA, 512);
        if (solo != -100)
            sat->setSoloBand (solo);
        auto buf = input;
        for (int start = 0; start < buf.getNumSamples(); start += 512)
        {
            if (before)
                before (*sat, start);
            const int n = std::min (512, buf.getNumSamples() - start);
            std::array<float*, 2> p { buf.getWritePointer (0, start), buf.getWritePointer (1, start) };
            sat->process (p.data(), 2, n);
        }
        return buf;
    }
}

class AuditionSatTests final : public juce::UnitTest
{
public:
    AuditionSatTests() : juce::UnitTest ("Velvox audition: Saturation band solo", "Velvox") {}

    void runTest() override
    {
        const int n = (int) (fsA * 1.0), from = (int) (fsA * 0.3), len = (int) (fsA * 0.5);
        const auto in = sines ({ 60.0, 1000.0, 15500.0 }, -20.0, n);   // two octaves inside the bands, no tone a harmonic of another

        beginTest ("Solo low / mid / high: only that band is heard");
        {
            const double freqs[3] { 60.0, 1000.0, 15500.0 };
            for (int b = 0; b < 3; ++b)
            {
                const auto out = renderSat (multiband(), in, b);
                const double own = toneLevel (out, from, len, freqs[b], fsA);
                for (int o = 0; o < 3; ++o)
                    if (o != b)
                        expect (dbOf (own / toneLevel (out, from, len, freqs[o], fsA)) > 30.0,
                                "band " + juce::String (b) + " vs tone of band " + juce::String (o));
                // the band's own level is what the normal output has there (processed, not raw)
                const auto normal = renderSat (multiband(), in, -100);
                expect (std::abs (dbOf (own / toneLevel (normal, from, len, freqs[b], fsA))) < 1.0, "same level as in the mix");
            }
        }

        beginTest ("The three solos add up to the normal output (only that band's processed output)");
        {
            for (float mix : { 1.0f, 0.4f })
            {
                const auto normal = renderSat (multiband (true, mix), in, -100);
                auto sum = renderSat (multiband (true, mix), in, 0);
                const auto mid = renderSat (multiband (true, mix), in, 1);
                const auto high = renderSat (multiband (true, mix), in, 2);
                for (int ch = 0; ch < 2; ++ch)
                {
                    sum.addFrom (ch, 0, mid, ch, 0, n);
                    sum.addFrom (ch, 0, high, ch, 0, n);
                }
                const double d = diffDb (normal, sum, (int) (fsA * 0.05));
                logMessage ("sum of solos vs normal, mix " + juce::String (mix) + ": " + juce::String (d, 1) + " dB");
                expect (d < -70.0, juce::String (d, 1) + " dB");
            }
        }

        beginTest ("Multiband off: a solo request has no effect (bit-identical)");
        {
            const auto a = renderSat (multiband (false), in, -100);
            const auto b = renderSat (multiband (false), in, 1);
            expectEquals (maxAbsDiff (a, b), 0.0);
        }

        beginTest ("Released: bit-identical to the normal output after the fade");
        {
            const auto a = renderSat (multiband(), in, -100);
            const auto b = renderSat (multiband(), in, -100, [] (dsp::SaturationModule& s, int start)
            {
                if (start == 4096) s.setSoloBand (2);
                if (start == 4096 * 3) s.setSoloBand (-1);
            });
            expect (maxAbsDiff (a, b, 4096 * 3 + 512 + (int) (fsA * 0.03)) == 0.0, "identical after the release");
            expect (maxAbsDiff (a, b, 4096) > 1.0e-3, "the solo was audible");
            expectEquals (dsp::SaturationModule().getSoloBand(), -1);
        }

        beginTest ("From one band to another: band to band, never a blip of the full signal");
        {
            const int at = 512 * 47;   // ~0.5 s, block aligned
            const auto out = renderSat (multiband(), in, 0, [at] (dsp::SaturationModule& s, int start) { if (start == at) s.setSoloBand (2); });
            const auto normal = renderSat (multiband(), in, -100);
            const double mid = toneAmplitude (out, at - (int) (0.05 * fsA), at + (int) (0.08 * fsA), 1000.0);
            const double full = toneAmplitude (normal, at - (int) (0.05 * fsA), at + (int) (0.08 * fsA), 1000.0);
            expect (full > 0.02 && at + (int) (0.08 * fsA) < out.getNumSamples(), "the window holds the tone");
            logMessage ("1 kHz (Mid) around the hop Low -> High: " + juce::String (dbOf (mid / full), 1) + " dB");
            expect (dbOf (mid / full) < -20.0, juce::String (dbOf (mid / full), 1) + " dB");
        }

        beginTest ("Click-free: on, to another band, multiband off, off");
        {
            const int total = (int) fsA * 2;
            const auto tones = sines ({ 180.0, 1200.0 }, -14.0, total);
            const auto out = renderSat (multiband(), tones, -100, [] (dsp::SaturationModule& s, int start)
            {
                const int t = start / 512;
                if (t == 20) s.setSoloBand (0);
                if (t == 60) s.setSoloBand (1);
                if (t == 100) s.setSoloBand (0);
                if (t == 140) { auto x = multiband (false); s.setSettings (x); }
                if (t == 160) s.setSoloBand (-1);
            });
            const double peak = out.getMagnitude (0, total);
            // Saturated tones carry harmonics: the steady reference is the normal render's own worst
            const auto ref = renderSat (multiband(), tones, -100);
            const double steady = std::max (secondDiff (ref, 4096, total), peak * std::pow (2.0 * pi * 1200.0 / fsA, 2.0));
            const double worst = secondDiff (out, 4096, total);
            logMessage ("sat solo switching: worst " + juce::String (worst / steady, 2) + " x steady");
            expect (worst < 1.5 * steady, juce::String (worst / steady, 2) + " x steady");
        }
    }
};

static AuditionSatTests auditionSatTests;

//==============================================================================
// UI: the headphones of the EQ panel and the Saturation strip, Mix in the Saturation main row
namespace
{
    juce::MouseEvent mouseOn (juce::Component& c, juce::Point<float> pos, juce::ModifierKeys mods = {})
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, mods,
                                 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, pos, now, 1, false);
    }

    void press (juce::Component& c, juce::Point<float> at) { c.mouseDown (mouseOn (c, at)); }
    void release (juce::Component& c, juce::Point<float> at) { c.mouseUp (mouseOn (c, at)); }
    void clickAt (juce::Component& c, juce::Point<float> at) { press (c, at); release (c, at); }
    juce::Point<float> centreOf (juce::Component& c) { return c.getLocalBounds().getCentre().toFloat(); }

    std::unique_ptr<ui::EqPanel> openEq (Harness& h, int instance)
    {
        auto r = h.proc.getRouting();
        if (! r.contains (dsp::ModuleId::eq, instance))
            r.insertAt (r.count, dsp::ModuleId::eq, instance, true);
        h.proc.setRouting (r);
        h.proc.getUndo().clear();
        auto p = ui::createDetailPanel (h.proc, dsp::selectionCode (dsp::ModuleId::eq, instance));
        p->setBounds (VelvoxAudioProcessorEditor::panelArea().withZeroOrigin());
        auto* eq = dynamic_cast<ui::EqPanel*> (p.get());
        if (eq != nullptr)
            p.release();
        return std::unique_ptr<ui::EqPanel> (eq);
    }

    // Every parameter value (normalised) of the processor
    std::vector<float> allValues (Harness& h)
    {
        std::vector<float> v;
        for (auto* p : h.proc.getParameters())
            v.push_back (p->getValue());
        return v;
    }
}

class AuditionUiTests final : public juce::UnitTest
{
public:
    AuditionUiTests() : juce::UnitTest ("Headliner audition panels", "Headliner") {}

    void runTest() override
    {
        beginTest ("AuditionButton: click latches, second click ends, press-and-hold is momentary");
        {
            ui::AuditionButton b;
            b.setBounds (0, 0, 28, 28);
            juce::uint32 now = 1000;
            b.clock = [&] { return now; };
            std::vector<bool> calls;
            b.onChange = [&] (bool on) { calls.push_back (on); };
            clickAt (b, centreOf (b));
            expect (b.isActive(), "latched");
            now += 50;
            clickAt (b, centreOf (b));
            expect (! b.isActive(), "second click ends it");
            press (b, centreOf (b));
            expect (b.isActive(), "on while held");
            now += ui::AuditionPress::holdMs + 10;
            release (b, centreOf (b));
            expect (! b.isActive(), "momentary: off at release");
            expect (calls == std::vector<bool> { true, false, true, false });
        }

        beginTest ("EQ panel: the row headphone solos the selected band - no parameter, no undo step, not saved");
        {
            Harness h;
            auto panel = openEq (h, 0);
            const int a = ui::eqedit::addBand (h.proc, 0, 1000.0f, 3.0f);
            const int b = ui::eqedit::addBand (h.proc, 0, 4000.0f, -2.0f);
            panel->refresh();
            panel->selectBand (b);
            h.proc.getUndo().clear();
            const auto values = allValues (h);
            juce::MemoryBlock before;
            h.proc.getStateInformation (before);

            auto& eq = h.proc.getChain().getEqualizer (0);
            auto& solo = panel->getAuditionButton();
            expect (solo.isVisible(), "shown with a selected band");
            expect (! panel->getMainRowArea().isEmpty() && panel->getMainRowArea().contains (solo.getBounds().getCentre()),
                    "in the selected-band row");
            clickAt (solo, centreOf (solo));
            expectEquals (eq.getAuditionBand(), b);
            panel->refresh();
            expect (solo.isActive());
            expect (allValues (h) == values, "no parameter changed");
            expectEquals (h.proc.getUndo().getNumSteps(), 0, "no undo step");
            juce::MemoryBlock during;
            h.proc.getStateInformation (during);
            expect (before == during, "not in the saved state");

            panel->selectBand (a);
            panel->refresh();
            expect (! solo.isActive(), "the row shows the selected band's state");
            expectEquals (eq.getAuditionBand(), b, "the solo stays with its band");
            clickAt (solo, centreOf (solo));
            expectEquals (eq.getAuditionBand(), a, "one solo at a time: now band a");
            clickAt (solo, centreOf (solo));
            expectEquals (eq.getAuditionBand(), -1);

            panel->selectBand (-1);
            panel->refresh();
            expect (! solo.isVisible(), "hidden without a selection");
        }

        beginTest ("EQ graph: hot spot beside the hovered point - click latches, hold is momentary; deleting the band ends it");
        {
            Harness h;
            auto panel = openEq (h, 0);
            const int a = ui::eqedit::addBand (h.proc, 0, 800.0f, 4.0f);
            panel->refresh();
            auto* g = panel->getGraph();
            expect (g != nullptr);
            if (g == nullptr)
                return;
            g->update (*std::make_unique<ui::LiveData>());
            auto& eq = h.proc.getChain().getEqualizer (0);
            juce::uint32 now = 5000;
            g->clock = [&] { return now; };

            expect (! g->isAuditionIconShown (a), "hidden until the point is hovered");
            g->mouseMove (mouseOn (*g, g->pointFor (a)));
            expect (g->isAuditionIconShown (a), "shown on hover");
            const auto icon = g->auditionIconArea (a).getCentre();
            expect (g->bandAt (icon) < 0, "the hot spot is beside the point, not on it");
            g->mouseMove (mouseOn (*g, icon));
            expect (g->isAuditionIconShown (a), "stays while the mouse moves onto it");

            const int stepsBefore = h.proc.getUndo().getNumSteps();
            const auto freqBefore = ui::eqedit::parameterBand (h.proc, 0, a).freq;
            press (*g, icon);
            expectEquals (eq.getAuditionBand(), a, "on at the press");
            g->mouseDrag (mouseOn (*g, icon.translated (40.0f, -30.0f)));
            now += ui::AuditionPress::holdMs + 50;
            release (*g, icon);
            expectEquals (eq.getAuditionBand(), -1, "held: ends at release");
            expectEquals (ui::eqedit::parameterBand (h.proc, 0, a).freq, freqBefore, "pressing the hot spot never moves the band");
            expectEquals (h.proc.getUndo().getNumSteps(), stepsBefore, "no undo step");

            clickAt (*g, icon);
            expectEquals (eq.getAuditionBand(), a, "click: latched");
            g->mouseMove (mouseOn (*g, { 5.0f, 5.0f }));
            expect (g->isAuditionIconShown (a), "a soloed band keeps its headphone");
            g->performMenu (a, ui::EqGraph::menuDelete);
            g->update (*std::make_unique<ui::LiveData>());
            expectEquals (eq.getAuditionBand(), -1, "deleted band: solo ends");
        }

        beginTest ("EQ: closing the panel ends the solo");
        {
            Harness h;
            auto panel = openEq (h, 0);
            const int a = ui::eqedit::addBand (h.proc, 0, 1000.0f, 0.0f);
            panel->selectBand (a);
            clickAt (panel->getAuditionButton(), centreOf (panel->getAuditionButton()));
            expectEquals (h.proc.getChain().getEqualizer (0).getAuditionBand(), a);
            panel.reset();
            expectEquals (h.proc.getChain().getEqualizer (0).getAuditionBand(), -1);
        }

        satTests();
    }

    void satTests()
    {
        beginTest ("Saturation strip: a headphone per band in multiband only; one solo at a time; multiband off ends it");
        {
            Harness h;
            h.set (pid::satOn, 1.0f);
            auto& sat = h.proc.getChain().getSaturation (0);
            auto view = std::make_unique<ui::SatView> (h.proc, 0);
            view->setBounds (0, 0, 760, 300);
            auto live = std::make_unique<ui::LiveData>();
            live->settings = h.proc.readEffectiveSettings();
            view->update (*live);
            for (int b = 0; b < 3; ++b)
                expect (! view->getSoloButton (b).isVisible(), "single band: no headphones");

            h.set (pid::satMultiband, 1.0f);
            live->settings = h.proc.readEffectiveSettings();
            view->update (*live);
            for (int b = 0; b < 3; ++b)
            {
                auto& s = view->getSoloButton (b);
                expect (s.isVisible(), "multiband: headphone " + juce::String (b));
                expect (view->getBandBounds (b).contains (s.getBounds().toFloat().getCentre()), "inside its band");
                expect (! s.getBounds().intersects (view->getBandSwitch (b).getBounds()), "beside the band's on switch");
            }

            const auto values = allValues (h);
            h.proc.getUndo().clear();
            clickAt (view->getSoloButton (0), centreOf (view->getSoloButton (0)));
            expectEquals (sat.getSoloBand(), 0);
            clickAt (view->getSoloButton (2), centreOf (view->getSoloButton (2)));
            expectEquals (sat.getSoloBand(), 2, "the other band takes over");
            view->update (*live);
            expect (view->getSoloButton (2).isActive() && ! view->getSoloButton (0).isActive(), "buttons show it");
            expect (allValues (h) == values, "no parameter");
            expectEquals (h.proc.getUndo().getNumSteps(), 0, "no undo step");
            clickAt (view->getSoloButton (2), centreOf (view->getSoloButton (2)));
            expectEquals (sat.getSoloBand(), -1, "second click ends it");

            clickAt (view->getSoloButton (1), centreOf (view->getSoloButton (1)));
            h.set (pid::satMultiband, 0.0f);
            live->settings = h.proc.readEffectiveSettings();
            view->update (*live);
            expectEquals (sat.getSoloBand(), -1, "multiband off: solo ends");

            h.set (pid::satMultiband, 1.0f);
            live->settings = h.proc.readEffectiveSettings();
            view->update (*live);
            clickAt (view->getSoloButton (1), centreOf (view->getSoloButton (1)));
            expectEquals (sat.getSoloBand(), 1);
            view.reset();
            expectEquals (sat.getSoloBand(), -1, "closing the panel ends it");
        }
    }
};

static AuditionUiTests auditionUiTests;

//==============================================================================
// Listen renders of dry vocals (only with VELVOX_AUDITION_RENDER=<out dir>; inputs from
// VELVOX_AUDITION_INPUTS=<dir> or the bench's EQ inputs): EQ / Saturation normal, each solo, and a
// render that switches the solo every second. Writes WAVs and summary.txt (levels, clicks).
class AuditionRenders final : public juce::UnitTest
{
public:
    AuditionRenders() : juce::UnitTest ("Velvox audition renders (env VELVOX_AUDITION_RENDER)", "Velvox") {}

    void runTest() override
    {
        beginTest ("Renders");
        const auto outPath = juce::SystemStats::getEnvironmentVariable ("VELVOX_AUDITION_RENDER", {});
        if (outPath.isEmpty())
        {
            logMessage ("skipped (VELVOX_AUDITION_RENDER not set)");
            return;
        }
        const juce::File out (outPath);
        out.createDirectory();
        const juce::File inputs (juce::SystemStats::getEnvironmentVariable ("VELVOX_AUDITION_INPUTS",
                                                                            "C:\\Users\\jakob\\dev\\velvox-bench\\2026-10-25\\modules\\eq\\_inputs"));
        juce::String summary = "file | render | rms dBFS | peak dBFS | vs dry dB | switch click ratio (<= 1: none)\n";

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        for (auto name : { "mylk_chorus", "f8_belt", "m8_caro", "m9_breathy" })
        {
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (inputs.getChildFile (juce::String (name) + ".wav")));
            if (reader == nullptr)
            {
                logMessage (juce::String ("missing input ") + name);
                continue;
            }
            const double fs = reader->sampleRate;
            const int n = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (fs * 30.0));
            juce::AudioBuffer<float> dry (2, n);
            reader->read (&dry, 0, n, 0, true, true);
            write (out.getChildFile (juce::String (name) + "__dry.wav"), dry, fs);

            // EQ: a typical vocal curve (low cut 100 Hz, mud cut, presence bell, air shelf)
            dsp::EqSettings eqs;
            eqs.bands[0] = makeBand (EqBandType::lowCut, 100.0f, 0.0f, 0.7f);
            eqs.bands[1] = makeBand (EqBandType::bell, 320.0f, -3.0f, 1.4f);
            eqs.bands[2] = makeBand (EqBandType::bell, 3200.0f, 2.5f, 4.0f);
            eqs.bands[3] = makeBand (EqBandType::highShelf, 9000.0f, 3.0f, 0.7f);
            const auto eqNormal = renderEq (eqs, dry, fs, [] (EqualizerModule&, int) {});
            record (summary, name, "eq normal", eqNormal, dry, eqNormal, out, fs);
            const char* bandNames[4] { "eq solo lowcut100", "eq solo bell320", "eq solo bell3k2 q4", "eq solo highshelf9k" };
            for (int b = 0; b < 4; ++b)
                record (summary, name, bandNames[b], renderEq (eqs, dry, fs, [b] (EqualizerModule& m, int) { m.setAuditionBand (b); }), dry, eqNormal, out, fs);
            const auto eqSwitch = renderEq (eqs, dry, fs, [fs] (EqualizerModule& m, int start)
            {
                const int second = (int) (start / fs);
                m.setAuditionBand (second % 5 == 4 ? -1 : second % 5);
            });
            record (summary, name, "eq switching", eqSwitch, dry, eqNormal, out, fs, 256);

            // Saturation multiband: Warm low, Tube mid, Tape high
            dsp::SatSettings sat = multiband();
            const auto satNormal = renderSatFile (sat, dry, fs, [] (dsp::SaturationModule&, int) {});
            record (summary, name, "sat normal", satNormal, dry, satNormal, out, fs);
            const char* satNames[3] { "sat solo low", "sat solo mid", "sat solo high" };
            for (int b = 0; b < 3; ++b)
                record (summary, name, satNames[b], renderSatFile (sat, dry, fs, [b] (dsp::SaturationModule& m, int) { m.setSoloBand (b); }), dry, satNormal, out, fs);
            const auto satSwitch = renderSatFile (sat, dry, fs, [fs] (dsp::SaturationModule& m, int start)
            {
                const int second = (int) (start / fs);
                m.setSoloBand (second % 4 == 3 ? -1 : second % 4);
            });
            record (summary, name, "sat switching", satSwitch, dry, satNormal, out, fs, 512);
        }
        out.getChildFile ("summary.txt").replaceWithText (summary);
        logMessage (summary);
    }

private:
    template <typename Fn>
    static juce::AudioBuffer<float> renderEq (const dsp::EqSettings& s, const juce::AudioBuffer<float>& in, double fs, Fn&& before)
    {
        auto eq = std::make_unique<EqualizerModule>();
        eq->setSettings (s);
        eq->prepare (fs, 256);
        auto buf = in;
        juce::AudioBuffer<float> blockIn (2, 256);
        for (int start = 0; start < buf.getNumSamples(); start += 256)
        {
            before (*eq, start);
            const int n = std::min (256, buf.getNumSamples() - start);
            std::array<float*, 2> p { buf.getWritePointer (0, start), buf.getWritePointer (1, start) };
            for (int ch = 0; ch < 2; ++ch)
                blockIn.copyFrom (ch, 0, buf, ch, start, n);
            eq->process (p.data(), 2, n);
            eq->processAudition (blockIn.getArrayOfReadPointers(), p.data(), 2, n);
        }
        return buf;
    }

    template <typename Fn>
    static juce::AudioBuffer<float> renderSatFile (const dsp::SatSettings& s, const juce::AudioBuffer<float>& in, double fs, Fn&& before)
    {
        auto sat = std::make_unique<dsp::SaturationModule>();
        sat->setSettings (s);
        sat->prepare (fs, 512);
        auto buf = in;
        for (int start = 0; start < buf.getNumSamples(); start += 512)
        {
            before (*sat, start);
            const int n = std::min (512, buf.getNumSamples() - start);
            std::array<float*, 2> p { buf.getWritePointer (0, start), buf.getWritePointer (1, start) };
            sat->process (p.data(), 2, n);
        }
        return buf;
    }

    static double levelDb (const juce::AudioBuffer<float>& b) { return dbOf (rmsOf (b, 0, b.getNumSamples())); }

    // switchBlock > 0: the solo changed every second at the first block of `switchBlock` samples
    // after the full second; reports the largest second difference in the 30 ms after a switch
    // relative to the 50 - 300 ms before and after it (a click shows up as > 1)
    void record (juce::String& summary, const char* file, const char* render, const juce::AudioBuffer<float>& b,
                 const juce::AudioBuffer<float>& dry, const juce::AudioBuffer<float>& normal, const juce::File& out, double fs,
                 int switchBlock = 0)
    {
        juce::ignoreUnused (normal);
        const double peak = dbOf (std::max (b.getMagnitude (0, 0, b.getNumSamples()), b.getMagnitude (1, 0, b.getNumSamples())));
        double click = 0.0;
        if (switchBlock > 0)
            for (int second = 1; second * fs + 0.35 * fs < b.getNumSamples(); ++second)
            {
                const int s = (int) std::ceil (second * fs / switchBlock) * switchBlock;
                const double around = std::max (secondDiff (b, s - (int) (0.3 * fs), s - (int) (0.05 * fs)),
                                                secondDiff (b, s + (int) (0.05 * fs), s + (int) (0.3 * fs)));
                if (around > 1.0e-4)
                    click = std::max (click, secondDiff (b, s - 64, s + (int) (0.03 * fs)) / around);
            }
        summary << file << " | " << render << " | " << juce::String (levelDb (b), 1) << " | " << juce::String (peak, 1) << " | "
                << juce::String (levelDb (b) - levelDb (dry), 1) << " | " << (switchBlock > 0 ? juce::String (click, 2) : juce::String ("-")) << "\n";
        if (switchBlock > 0)
            expect (click < 1.5, juce::String (file) + " " + render + ": switching click ratio " + juce::String (click, 2));
        bool finite = true;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                finite = finite && std::isfinite (b.getSample (ch, i));
        expect (finite, juce::String (file) + " " + render + ": finite");
        expect (peak < 0.0, juce::String (file) + " " + render + ": no overs");
        write (out.getChildFile (juce::String (file) + "__" + juce::String (render).replaceCharacter (' ', '_') + ".wav"), b, fs);
    }

    static void write (const juce::File& f, const juce::AudioBuffer<float>& audio, double fs)
    {
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (f.createOutputStream().release());
        if (stream != nullptr)
            if (auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (fs)
                                                                                         .withNumChannels (audio.getNumChannels()).withBitsPerSample (24)))
                writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    }
};

static AuditionRenders auditionRenders;
