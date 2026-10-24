// EQ 0.6: band designs (analog-matched / legacy bilinear), band processing, parameters,
// migration of 0.5 states, role bands for macros and the automatic mix.

#include "TestHelpers.h"
#include "LegacyEqV05.h"
#include "DSP/EqDesign.h"

using namespace velvox;
using namespace velvox::test;
using dsp::EqBand;
using dsp::EqBandType;

namespace
{
    EqBand makeBand (EqBandType type, float freq, float gain, float q, int slope = 3)
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

    double dbOf (double lin) { return 20.0 * std::log10 (std::max (lin, 1.0e-12)); }

    // Largest deviation (dB) of the digital design from its analog prototype on a log grid up to fMax,
    // ignoring points where the prototype is below `floorDb` (deep stop bands)
    double maxDeviationDb (const EqBand& b, bool legacy, double fs, double fMax, double floorDb = -40.0)
    {
        const auto d = dsp::eqdesign::design (b, legacy, fs);
        double worst = 0.0;
        for (int i = 0; i <= 400; ++i)
        {
            const double f = 20.0 * std::pow (fMax / 20.0, i / 400.0);
            const double ana = dbOf (dsp::eqdesign::analogMagnitude (b, f));
            if (ana < floorDb)
                continue;
            worst = std::max (worst, std::abs (dbOf (dsp::eqdesign::magnitude (d, f, fs)) - ana));
        }
        return worst;
    }

    // Jury: both roots of z^2 + c1 z + c2 strictly / weakly inside the unit circle
    bool rootsInside (double c1, double c2, double margin) { return std::abs (c2) < 1.0 - margin && std::abs (c1) < 1.0 + c2 - margin; }
}

class EqDesignTests final : public juce::UnitTest
{
public:
    EqDesignTests() : juce::UnitTest ("Velvox EQ designs", "Velvox") {}

    void runTest() override
    {
        beginTest ("Matched bell at 16 kHz / 48 kHz follows the analog curve within 0.5 dB up to 20 kHz");
        for (float g : { -18.0f, -12.0f, -6.0f, 6.0f, 12.0f, 18.0f })
            for (float q : { 0.5f, 1.0f, 2.0f, 4.0f })
            {
                const double dev = maxDeviationDb (makeBand (EqBandType::bell, 16000.0f, g, q), false, 48000.0, 20000.0);
                expect (dev <= 0.5, "bell 16 kHz " + juce::String (g) + " dB Q " + juce::String (q) + ": " + juce::String (dev, 3) + " dB");
            }

        beginTest ("The bilinear (0.5) bell is cramped there - the reason for the matched design");
        {
            const double dev = maxDeviationDb (makeBand (EqBandType::bell, 16000.0f, 12.0f, 1.0f), true, 48000.0, 20000.0);
            expect (dev > 2.0, "legacy bell deviates " + juce::String (dev, 2) + " dB");
        }

        beginTest ("Bells and shelves stay within 1.5 dB of analog over the audio band (Q 0.3-4, +-18 dB, f0 <= 0.4 fs)");
        for (double fs : { 44100.0, 48000.0, 96000.0 })
            for (auto type : { EqBandType::bell, EqBandType::lowShelf, EqBandType::highShelf })
            {
                double worst = 0.0;
                juce::String where;
                for (float f : { 30.0f, 100.0f, 400.0f, 1000.0f, 3000.0f, 8000.0f, 12000.0f, 16000.0f })
                    for (float q : { 0.3f, 0.7071f, 1.5f, 4.0f })
                        for (float g : { -18.0f, -9.0f, -3.0f, 3.0f, 9.0f, 18.0f })
                        {
                            if (f > 0.4 * fs)
                                continue;
                            const double dev = maxDeviationDb (makeBand (type, f, g, q), false, fs, std::min (20000.0, 0.45 * fs));
                            if (dev > worst)
                            {
                                worst = dev;
                                where = juce::String (f) + " Hz Q " + juce::String (q) + " " + juce::String (g) + " dB";
                            }
                        }
                logMessage ("    " + juce::String (fs) + " type " + juce::String ((int) type) + " worst " + juce::String (worst, 3) + " dB at " + where);
                expect (worst <= 1.5, "type " + juce::String ((int) type) + " at " + juce::String (fs) + ": " + juce::String (worst, 3) + " dB at " + where);
            }

        beginTest ("Cut filters: Butterworth slopes 6-48 dB/oct match the analog prototype");
        for (double fs : { 44100.0, 48000.0, 96000.0 })
            for (auto type : { EqBandType::lowCut, EqBandType::highCut })
                for (int slope = 0; slope < dsp::numEqSlopes; ++slope)
                    for (float f : { 30.0f, 120.0f, 1000.0f, 5000.0f, 12000.0f, 16000.0f })
                    {
                        if (f > 0.4 * fs)
                            continue;
                        const auto b = makeBand (type, f, 0.0f, 0.7071f, slope);
                        // within 1 dB through the pass band and the knee, within 2.5 dB down to -20 dB
                        const double dev = maxDeviationDb (b, false, fs, std::min (20000.0, 0.45 * fs), -6.0);
                        const double devStop = maxDeviationDb (b, false, fs, std::min (20000.0, 0.45 * fs), -20.0);
                        expect (devStop <= 2.5, "stop band " + juce::String (devStop, 2) + " dB");
                        expect (dev <= 1.0, juce::String (type == EqBandType::lowCut ? "low" : "high") + " cut " + juce::String (dsp::eqSlopeDbPerOct (slope))
                                              + " dB/oct " + juce::String (f) + " Hz @" + juce::String (fs) + ": " + juce::String (dev, 3) + " dB");
                        // the nominal slope: one octave into the stop band the level drops by about the slope
                        const double fStop = type == EqBandType::lowCut ? f / 8.0 : f * 8.0;
                        if (fStop > 15.0 && fStop < 0.3 * fs)
                        {
                            const auto d = dsp::eqdesign::design (b, false, fs);
                            const double drop = dbOf (dsp::eqdesign::magnitude (d, fStop, fs));
                            const double expected = -3.0 * dsp::eqSlopeDbPerOct (slope);
                            expect (std::abs (drop - expected) < 2.0, "slope " + juce::String (drop, 1) + " vs " + juce::String (expected, 1));
                        }
                    }

        beginTest ("Notch, band pass and tilt");
        {
            const auto notch = dsp::eqdesign::design (makeBand (EqBandType::notch, 1000.0f, 0.0f, 4.0f), false, 48000.0);
            expect (dbOf (dsp::eqdesign::magnitude (notch, 1000.0, 48000.0)) < -60.0, "notch removes its frequency");
            expectWithinAbsoluteError (dbOf (dsp::eqdesign::magnitude (notch, 100.0, 48000.0)), 0.0, 0.1);
            const auto bp = dsp::eqdesign::design (makeBand (EqBandType::bandPass, 2000.0f, 0.0f, 2.0f), false, 48000.0);
            expectWithinAbsoluteError (dbOf (dsp::eqdesign::magnitude (bp, 2000.0, 48000.0)), 0.0, 0.1, "band pass: 0 dB at its centre");
            expect (dbOf (dsp::eqdesign::magnitude (bp, 100.0, 48000.0)) < -20.0, "band pass rejects far away");
            for (float g : { -12.0f, 6.0f })
            {
                const auto b = makeBand (EqBandType::tilt, 1000.0f, g, 1.0f);
                const auto t = dsp::eqdesign::design (b, false, 48000.0);
                expectWithinAbsoluteError (dbOf (dsp::eqdesign::magnitude (t, 1000.0, 48000.0)), 0.0, 0.2, "tilt pivots at its frequency");
                expectWithinAbsoluteError (dbOf (dsp::eqdesign::magnitude (t, 20.0, 48000.0)), -0.5 * g, 0.5, "lows at -gain/2");
                expectWithinAbsoluteError (dbOf (dsp::eqdesign::magnitude (t, 16000.0, 48000.0)), 0.5 * g, 0.6, "highs at +gain/2");
                expect (maxDeviationDb (b, false, 48000.0, 20000.0) < 0.5);
            }
            for (auto type : { EqBandType::notch, EqBandType::bandPass })
                expect (maxDeviationDb (makeBand (type, 3000.0f, 0.0f, 1.0f), false, 48000.0, 20000.0, -12.0) < 1.0);
        }

        beginTest ("Every design is stable and minimum phase (all types, slopes, extremes, 44.1-192 kHz, both modes)");
        {
            int checked = 0, failures = 0;
            for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
                for (int type = 0; type < dsp::numEqBandTypes; ++type)
                    for (float f : { 10.0f, 20.0f, 63.0f, 500.0f, 4000.0f, 15000.0f, 20000.0f, 24000.0f, 30000.0f })
                        for (float q : { 0.025f, 0.1f, 0.7071f, 3.0f, 12.0f, 40.0f })
                            for (float g : { -30.0f, -12.0f, -0.1f, 0.0f, 0.1f, 12.0f, 30.0f })
                                for (int slope : { 0, 2, 3, 5 })
                                    for (bool legacy : { false, true })
                                    {
                                        auto b = makeBand ((EqBandType) type, f, g, q, slope);
                                        const auto d = dsp::eqdesign::design (b, legacy, fs);
                                        bool ok = d.numSections >= 1 && d.numSections <= 4;
                                        for (int s = 0; s < d.numSections; ++s)
                                        {
                                            const auto& c = d.sections[(size_t) s];
                                            ok = ok && std::isfinite (c.b0) && std::isfinite (c.b1) && std::isfinite (c.b2)
                                                    && std::isfinite (c.a1) && std::isfinite (c.a2);
                                            ok = ok && rootsInside (c.a1, c.a2, 1.0e-12);
                                            // zeros: b0 z^2 + b1 z + b2 (on the circle allowed: notch, cuts)
                                            ok = ok && std::abs (c.b0) > 0.0 && rootsInside (c.b1 / c.b0, c.b2 / c.b0, -1.0e-6);
                                        }
                                        ++checked;
                                        if (! ok && ++failures < 10)
                                            logMessage ("    unstable/non-min-phase: type " + juce::String (type) + " f " + juce::String (f) + " q " + juce::String (q)
                                                        + " g " + juce::String (g) + " slope " + juce::String (slope) + " legacy " + juce::String ((int) legacy) + " fs " + juce::String (fs));
                                    }
            expectEquals (failures, 0, "of " + juce::String (checked));
        }

        beginTest ("Legacy mode reproduces the 0.5 coefficients exactly");
        {
            const double fs = 48000.0;
            const auto same = [this] (const dsp::BiquadCoeffs& a, const legacy::v05::BiquadCoeffs& b)
            {
                expect (a.b0 == b.b0 && a.b1 == b.b1 && a.b2 == b.b2 && a.a1 == b.a1 && a.a2 == b.a2);
            };
            same (dsp::eqdesign::design (makeBand (EqBandType::bell, 2500.0f, 4.0f, 2.0f), true, fs).sections[0], legacy::v05::design::peak (fs, 2500.0f, 2.0f, 4.0f));
            same (dsp::eqdesign::design (makeBand (EqBandType::lowShelf, 200.0f, -3.0f, 0.7071f), true, fs).sections[0], legacy::v05::design::lowShelf (fs, 200.0f, 0.7071f, -3.0f));
            same (dsp::eqdesign::design (makeBand (EqBandType::highShelf, 9000.0f, 5.0f, 0.7071f), true, fs).sections[0], legacy::v05::design::highShelf (fs, 9000.0f, 0.7071f, 5.0f));
            const auto hp = dsp::eqdesign::design (makeBand (EqBandType::lowCut, 120.0f, 0.0f, 1.0f, 3), true, fs);
            expectEquals (hp.numSections, 2);
            same (hp.sections[0], legacy::v05::design::highPass (fs, 120.0f, 0.54119610));
            same (hp.sections[1], legacy::v05::design::highPass (fs, 120.0f, 1.30656296));
            const auto lp = dsp::eqdesign::design (makeBand (EqBandType::highCut, 16000.0f, 0.0f, 1.0f, 5), true, fs);
            expectEquals (lp.numSections, 4);
            same (lp.sections[3], legacy::v05::design::lowPass (fs, 16000.0f, 2.56291545));
        }
    }
};

static EqDesignTests eqDesignTests;
