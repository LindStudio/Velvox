// EQ 0.6: band processing (16 bands, smoothing, legacy null, clicks, stability, CPU)

#include "EqTestHelpers.h"

using namespace velvox;
using namespace velvox::test;
using namespace velvox::test::eq;

class EqModuleTests final : public juce::UnitTest
{
public:
    EqModuleTests() : juce::UnitTest ("Velvox EQ module", "Velvox") {}

    void runTest() override
    {
        using M = dsp::EqualizerModule;

        beginTest ("All bands off: bit-exact pass-through");
        {
            M eq;
            dsp::EqSettings s;   // 16 bands, all off
            eq.setSettings (s);
            eq.prepare (48000.0, 256);
            const auto in = noiseBuffer (2, 48000, 0.5f, 3);
            auto out = in;
            runModule (eq, out);
            expectEquals (maxAbsDiff (in, out), 0.0);
        }

        beginTest ("Legacy response: migrated bands 1-5 null against the 0.5 EQ (bit-exact)");
        for (double fs : { 44100.0, 48000.0, 96000.0 })
        {
            int index = 0;
            for (const auto& o : legacyCases())
            {
                legacy::v05::EqualizerModule ref;
                ref.setSettings (o);
                ref.prepare (fs, 256);
                M eq;
                eq.setSettings (migrated (o));
                eq.prepare (fs, 256);

                const auto in = noiseBuffer (2, (int) fs, 0.25f, 7 + index);
                auto a = in, b = in;
                runModule (ref, a);
                runModule (eq, b);
                const double d = maxAbsDiff (a, b);
                expect (d == 0.0, "case " + juce::String (index) + " @" + juce::String (fs) + ": max diff " + juce::String (diffDb (a, b), 1) + " dB");
                ++index;
            }
        }

        beginTest ("Legacy response: moving gain / frequency / Q smooths exactly like 0.5 (<= -120 dB)");
        {
            const auto cases = legacyCases();
            legacy::v05::EqualizerModule ref;
            ref.setSettings (cases[1]);
            ref.prepare (48000.0, 256);
            M eq;
            eq.setSettings (migrated (cases[1]));
            eq.prepare (48000.0, 256);
            const auto in = noiseBuffer (2, 48000 * 2, 0.25f, 11);
            auto a = in, b = in;
            for (int start = 0, blockIndex = 0; start < in.getNumSamples(); start += 256, ++blockIndex)
            {
                auto o = cases[1];
                const float t = (float) start / 48000.0f;
                o.midFreq = 2500.0f * std::pow (2.0f, std::sin (6.0f * t));   // fast automation
                o.midGain = 4.0f + 8.0f * std::sin (11.0f * t);
                o.midQ = 2.0f + 1.5f * std::sin (3.0f * t);
                o.highGain = blockIndex % 40 < 20 ? 5.0f : -5.0f;            // steps
                o.hpfFreq = blockIndex % 60 < 30 ? 120.0f : 300.0f;
                ref.setSettings (o);
                eq.setSettings (migrated (o));
                const int n = std::min (256, in.getNumSamples() - start);
                std::array<float*, 2> pa { a.getWritePointer (0, start), a.getWritePointer (1, start) };
                std::array<float*, 2> pb { b.getWritePointer (0, start), b.getWritePointer (1, start) };
                ref.process (pa.data(), 2, n);
                eq.process (pb.data(), 2, n);
            }
            expect (diffDb (a, b) <= -120.0, "max diff " + juce::String (diffDb (a, b), 1) + " dB");
        }

        beginTest ("Processed sines follow responseDb (16 matched bands)");
        {
            dsp::EqSettings s;
            const EqBandType types[] { EqBandType::lowCut, EqBandType::lowShelf, EqBandType::bell, EqBandType::bell, EqBandType::notch, EqBandType::bell,
                                       EqBandType::bell, EqBandType::highShelf, EqBandType::bell, EqBandType::tilt, EqBandType::bell, EqBandType::bell,
                                       EqBandType::bell, EqBandType::bell, EqBandType::bandPass, EqBandType::highCut };
            for (int i = 0; i < 16; ++i)
                s.bands[(size_t) i] = makeBand (types[i], 30.0f * std::pow (2.0f, (float) i * 0.62f), i % 2 == 0 ? 4.0f : -3.0f, 0.8f + 0.1f * (float) i, i % 6);
            s.bands[14].q = 0.2f;   // wide band pass: keeps the level measurable
            for (double f : { 100.0, 440.0, 1000.0, 3000.0, 7000.0, 12000.0 })
            {
                M eq;
                eq.setSettings (s);
                eq.prepare (48000.0, 256);
                juce::AudioBuffer<float> b (1, 48000);
                for (int i = 0; i < b.getNumSamples(); ++i)
                    b.setSample (0, i, 0.1f * (float) std::sin (2.0 * pi * f * i / 48000.0));
                runModule (eq, b);
                const double measured = juce::Decibels::gainToDecibels (toneLevel (b, 24000, 24000, f, 48000.0) / 0.1);
                const double expected = M::responseDb (s, f, 48000.0);
                expectWithinAbsoluteError (measured, expected, 0.05, "at " + juce::String (f) + " Hz");
            }

            // the curve helper agrees with the point evaluation, in total and per band
            std::vector<float> freqs { 20.0f, 150.0f, 1234.0f, 9000.0f, 19000.0f }, out (freqs.size());
            M::responseCurveDb (s, 48000.0, freqs.data(), out.data(), (int) freqs.size());
            for (size_t i = 0; i < freqs.size(); ++i)
                expectWithinAbsoluteError ((double) out[i], M::responseDb (s, freqs[i], 48000.0), 1.0e-3);
            M::responseCurveDb (s, 48000.0, freqs.data(), out.data(), (int) freqs.size(), 7);
            for (size_t i = 0; i < freqs.size(); ++i)
                expectWithinAbsoluteError ((double) out[i], M::bandResponseDb (s.bands[7], false, freqs[i], 48000.0), 1.0e-3);
            // an off band does not count in the total, but its own curve can still be drawn
            auto off = s;
            off.bands[7].on = false;
            expect (std::abs (M::responseDb (off, 9000.0, 48000.0) - M::responseDb (s, 9000.0, 48000.0)) > 0.5);
            M::responseCurveDb (off, 48000.0, freqs.data(), out.data(), (int) freqs.size(), 7);
            expectWithinAbsoluteError ((double) out[3], M::bandResponseDb (s.bands[7], false, 9000.0, 48000.0), 1.0e-3);
        }

        beginTest ("No clicks or zipper noise when sweeping, stepping, switching types, slopes and bands");
        {
            const double fs = 48000.0;
            const int n = (int) (fs * 4.0);
            juce::AudioBuffer<float> in (2, n);
            for (int i = 0; i < n; ++i)
            {
                const float x = 0.3f * (float) std::sin (2.0 * pi * 200.0 * i / fs) + 0.1f * (float) std::sin (2.0 * pi * 3000.0 * i / fs);
                in.setSample (0, i, x);
                in.setSample (1, i, x);
            }
            const auto heavy = [] (dsp::EqSettings& s, float t)
            {
                s.bands[0] = makeBand (EqBandType::bell, 300.0f * std::pow (20.0f, 0.5f + 0.5f * std::sin (t)), 12.0f * std::sin (1.7f * t), 1.0f + 0.9f * std::sin (2.3f * t));
                s.bands[1] = makeBand (EqBandType::highShelf, 4000.0f, 9.0f, 0.7f);
                s.bands[2] = makeBand (EqBandType::lowCut, 60.0f, 0.0f, 0.7f, 3);
                s.bands[3] = makeBand (EqBandType::bell, 1500.0f, 15.0f, 0.5f);
            };
            const auto steps = [] (dsp::EqSettings& s, int phase)
            {
                // steps every 0.25 s: frequency jumps, type and slope changes, bands on / off, design switch
                s.bands[4] = makeBand (phase % 2 == 0 ? EqBandType::bell : EqBandType::notch, phase < 2 ? 500.0f : 5000.0f, phase == 1 ? -15.0f : 10.0f, 2.0f);
                s.bands[5] = makeBand (EqBandType::highCut, phase < 2 ? 8000.0f : 2500.0f, 0.0f, 0.7f, phase + 1);
                s.bands[3].on = phase != 2;
                s.legacyResponse = phase == 3;
            };
            // The scale of a "smooth" output: the largest second difference of the steady signal through
            // any static EQ on the trajectory (sampled), so the gain alone never counts as a click
            double scale = 0.0;
            for (int phase = 0; phase < 4; ++phase)
                for (int k = 0; k < 24; ++k)
                {
                    dsp::EqSettings ref;
                    heavy (ref, 0.27f * (float) k);
                    steps (ref, phase);
                    M still;
                    still.setSettings (ref);
                    still.prepare (fs, 256);
                    juce::AudioBuffer<float> calm (2, 7200);
                    for (int ch = 0; ch < 2; ++ch)
                        calm.copyFrom (ch, 0, in, ch, 0, 7200);
                    runModule (still, calm);
                    scale = std::max (scale, maxSecondDifference (calm, 4800));
                }

            M eq;
            dsp::EqSettings s;
            heavy (s, 0.0f);
            steps (s, 0);
            eq.setSettings (s);
            eq.prepare (fs, 256);
            auto out = in;
            for (int start = 0; start < n; start += 128)
            {
                const float t = (float) start / (float) fs * 6.5f;   // fast sweeps: about one octave every 50 ms
                heavy (s, t);
                steps (s, (start / 12000) % 4);
                eq.setSettings (s);
                const int len = std::min (128, n - start);
                std::array<float*, 2> p { out.getWritePointer (0, start), out.getWritePointer (1, start) };
                eq.process (p.data(), 2, len);
            }
            const double worst = maxSecondDifference (out, 4800);
            logMessage ("    second difference: static " + juce::String (scale, 5) + ", modulated " + juce::String (worst, 5));
            expect (allFinite (out));
            expect (worst < 3.0 * scale, "no clicks: " + juce::String (worst / scale, 2) + " x the static EQ");
        }

        beginTest ("Extreme settings stay stable at every sample rate");
        for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            M eq;
            dsp::EqSettings s;
            juce::Random r (5);
            eq.setSettings (s);
            eq.prepare (fs, 256);
            auto buf = noiseBuffer (2, (int) fs * 2, 0.5f, 13);
            for (int start = 0; start < buf.getNumSamples(); start += 64)
            {
                if ((start / 64) % 20 == 0)
                    for (auto& b : s.bands)
                    {
                        b = makeBand ((EqBandType) r.nextInt (dsp::numEqBandTypes), r.nextBool() ? 10.0f : 30000.0f, r.nextBool() ? 30.0f : -30.0f,
                                      r.nextBool() ? 40.0f : 0.025f, r.nextInt (dsp::numEqSlopes));
                        if (r.nextInt (4) == 0)
                            b.freq = 20.0f * std::pow (1000.0f, r.nextFloat());
                    }
                s.legacyResponse = (start / 64) % 200 > 100;
                eq.setSettings (s);
                const int len = std::min (64, buf.getNumSamples() - start);
                std::array<float*, 2> p { buf.getWritePointer (0, start), buf.getWritePointer (1, start) };
                eq.process (p.data(), 2, len);
            }
            expect (allFinite (buf), "finite at " + juce::String (fs));
            const float peak = peakOf (buf, 0, buf.getNumSamples());
            expect (peak < 1.0e6f, "bounded at " + juce::String (fs) + ": " + juce::String (peak));
        }

        beginTest ("CPU: 16 active bands");
        {
            const double fs = 48000.0;
            const int n = (int) fs * 10;
            auto buf = noiseBuffer (2, n, 0.25f, 17);
            dsp::EqSettings s;
            for (int i = 0; i < 16; ++i)
                s.bands[(size_t) i] = makeBand (EqBandType::bell, 50.0f * std::pow (1.4f, (float) i), 3.0f, 1.0f);
            M eq;
            eq.setSettings (s);
            eq.prepare (fs, 256);
            legacy::v05::EqualizerModule old;
            old.prepare (fs, 256);
            auto b2 = buf;
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            runModule (eq, buf);
            const auto t1 = juce::Time::getMillisecondCounterHiRes();
            runModule (old, b2);
            const auto t2 = juce::Time::getMillisecondCounterHiRes();
            const double newShare = (t1 - t0) / 10000.0 * 100.0, oldShare = (t2 - t1) / 10000.0 * 100.0;
            logMessage ("    16 bands: " + juce::String (newShare, 3) + " % of real time (stereo 48 kHz), 0.5 EQ (5 bands): " + juce::String (oldShare, 3) + " %");
            expect (newShare < 1.5, "16 bands below 1.5 % of one core: " + juce::String (newShare, 3) + " %");
        }
    }
};

static EqModuleTests eqModuleTests;
