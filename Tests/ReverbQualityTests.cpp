// Reverb quality 0.6 (spec 5.12, QUALITY_BASELINE #4): decay accuracy for every room type, no modal ringing,
// the decay display feed and the "Raumtiefe" depth path. Metrics: Tests/ReverbMetrics.h (= tools/refhost/qmetrics.py).

#include <juce_core/juce_core.h>
#include "DSP/ReverbModule.h"
#include "DSP/ReverbDepth.h"
#include "ReverbMetrics.h"

using namespace velvox::dsp;

namespace
{
    juce::AudioBuffer<float> renderImpulse (const ReverbSettings& s, double seconds, double fs = 48000.0, int block = 512)
    {
        ReverbModule rv;
        rv.setSettings (s);
        rv.prepare (fs, block);
        juce::AudioBuffer<float> in (2, (int) (seconds * fs)), out (2, (int) (seconds * fs));
        in.clear();
        out.clear();
        in.setSample (0, 0, 0.5f);
        in.setSample (1, 0, 0.5f);
        for (int start = 0; start < in.getNumSamples(); start += block)
        {
            const int n = std::min (block, in.getNumSamples() - start);
            const float* ip[2] { in.getReadPointer (0, start), in.getReadPointer (1, start) };
            float* op[2] { out.getWritePointer (0, start), out.getWritePointer (1, start) };
            rv.process (ip, op, 2, n);
        }
        return out;
    }

    // The bench's settings (tools/refhost/modules.py, reverb): low cut 150, high cut 12 k, mod 30 %, diffusion 70 %
    ReverbSettings benchSettings (int type, float decay, float size, float damping)
    {
        ReverbSettings s;
        s.type = type;
        s.decaySeconds = decay;
        s.size = size;
        s.dampingHz = damping;
        s.lowCutHz = 150.0f;
        s.highCutHz = 12000.0f;
        s.preDelayMs = 20.0f;
        s.modulation = 0.3f;
        s.diffusion = 0.7f;
        s.early = 0.5f;
        return s;
    }

    juce::String typeName (int type) { return ReverbModule::voicing (type).name; }
}

class ReverbQualityTests final : public juce::UnitTest
{
public:
    ReverbQualityTests() : juce::UnitTest ("Headliner reverb quality", "Headliner") {}

    void runTest() override
    {
        constexpr double fs = 48000.0;

        beginTest ("Decay: broadband T30 within 10 % of the set decay, every type, default settings");
        for (int type = 0; type < ReverbModule::numTypes; ++type)
            for (float decay : { 0.5f, 1.0f, 1.8f, 2.6f, 6.0f })
            {
                ReverbSettings s;
                s.type = type;
                s.decaySeconds = decay;
                const double t = reverbmetrics::broadbandT30 (renderImpulse (s, decay * 2.5 + 1.0), fs);
                const double err = std::abs (t - decay) / decay;
                logMessage ("  " + typeName (type) + " " + juce::String (decay, 1) + " s -> T30 " + juce::String (t, 2) + " s");
                expect (err <= 0.10, typeName (type) + " decay " + juce::String (decay, 1) + " s: T30 " + juce::String (t, 2) + " s");
            }

        beginTest ("Decay: broadband T30 within 10 % at the bench's plate / hall settings and dark damping");
        for (int type = 0; type < ReverbModule::numTypes; ++type)
            for (auto [decay, size, damping] : { std::tuple { 1.8f, 0.6f, 9000.0f }, std::tuple { 2.6f, 0.7f, 6500.0f },
                                                 std::tuple { 3.5f, 1.0f, 3000.0f }, std::tuple { 1.2f, 0.2f, 15000.0f } })
            {
                const auto s = benchSettings (type, decay, size, damping);
                const double t = reverbmetrics::broadbandT30 (renderImpulse (s, decay * 2.5 + 1.0), fs);
                logMessage ("  " + typeName (type) + " " + juce::String (decay, 1) + " s damping " + juce::String (damping, 0) + " -> T30 "
                            + juce::String (t, 2) + " s");
                expect (std::abs (t - decay) / decay <= 0.10, typeName (type) + " decay " + juce::String (decay, 1) + " s, damping "
                                                                + juce::String (damping, 0) + ": T30 " + juce::String (t, 2) + " s");
            }

        beginTest ("Decay: T30 holds at 44.1 and 96 kHz");
        for (double rate : { 44100.0, 96000.0 })
        {
            ReverbSettings s;
            s.type = 2;
            s.decaySeconds = 2.6f;
            const double t = reverbmetrics::broadbandT30 (renderImpulse (s, 7.5, rate), rate);
            expect (std::abs (t - 2.6) / 2.6 <= 0.10, juce::String (rate, 0) + " Hz: T30 " + juce::String (t, 2) + " s");
        }

        beginTest ("Decay display: predictedRt60 tracks the measured octave-band T30");
        for (int type : { 1, 2, 6 })
        {
            const auto s = benchSettings (type, 2.6f, 0.7f, 6500.0f);
            const auto ir = renderImpulse (s, 7.5);
            for (double fc : { 500.0, 1000.0, 4000.0 })
            {
                const double measured = reverbmetrics::octaveT30 (ir, fs, fc);
                const double predicted = ReverbModule::predictedRt60 (s, (float) fc, fs);
                logMessage ("  " + typeName (type) + " " + juce::String (fc, 0) + " Hz: measured " + juce::String (measured, 2)
                            + " s, predicted " + juce::String (predicted, 2) + " s");
                expect (std::abs (predicted - measured) / measured <= 0.15, typeName (type) + " " + juce::String (fc, 0) + " Hz: measured "
                                                                              + juce::String (measured, 2) + " predicted " + juce::String (predicted, 2));
            }
        }

        beginTest ("Ringing: no metallic modes in the late tail, every type (bench settings)");
        for (int type = 0; type < ReverbModule::numTypes; ++type)
            for (auto [decay, size, damping] : { std::tuple { 1.8f, 0.6f, 9000.0f }, std::tuple { 2.6f, 0.7f, 6500.0f },
                                                 std::tuple { 1.0f, 0.3f, 7000.0f } })
            {
                const auto s = benchSettings (type, decay, size, damping);
                const double ring = reverbmetrics::modalRingingDb (renderImpulse (s, decay * 2.5 + 1.0), fs);
                logMessage ("  " + typeName (type) + " " + juce::String (decay, 1) + " s size " + juce::String (size, 1)
                            + ": ringing " + juce::String (ring, 1) + " dB");
                // the bench's cases (1.8 / 2.6 s) against the gate with margin; the short, small room gets 1 dB more
                expect (ring <= (decay < 1.5f ? 10.0 : 9.0), typeName (type) + " " + juce::String (decay, 1) + " s: ringing " + juce::String (ring, 1) + " dB");
            }

        beginTest ("Ringing: no band outlives its neighbours (octave T30 250 Hz - 2 kHz within 12 % of 1 kHz)");
        for (int type = 0; type < ReverbModule::numTypes; ++type)
        {
            auto s = benchSettings (type, 2.6f, 0.6f, 20000.0f);
            s.lowCutHz = 20.0f;
            s.highCutHz = 20000.0f;
            const auto ir = renderImpulse (s, 7.5);
            const double ref = reverbmetrics::octaveT30 (ir, fs, 1000.0);
            for (double fc : { 250.0, 500.0, 2000.0 })
            {
                const double t = reverbmetrics::octaveT30 (ir, fs, fc);
                expect (std::abs (t - ref) / ref <= 0.12, typeName (type) + " " + juce::String (fc, 0) + " Hz: " + juce::String (t, 2)
                                                           + " s vs " + juce::String (ref, 2) + " s at 1 kHz");
            }
        }

        beginTest ("Decay: the compensation stays bounded and every loop stays below unity gain");
        for (int type = 0; type < ReverbModule::numTypes; ++type)
        {
            ReverbSettings s;
            s.type = type;
            s.decaySeconds = ReverbModule::maxDecaySeconds;
            s.bassMultiplier = 2.0f;
            s.dampingHz = 1000.0f;
            s.lowCutHz = 20.0f;
            s.highCutHz = 20000.0f;
            ReverbModule rv;
            rv.setSettings (s);
            rv.prepare (fs, 256);
            expect (rv.decayCompensation() >= 1.0f && rv.decayCompensation() <= 1.6f, "comp " + juce::String (rv.decayCompensation(), 3));
            for (int l = 0; l < ReverbModule::numLines; ++l)
                expect (rv.loopGainAtDc (l) < 1.0f);
        }
    }
};

static ReverbQualityTests reverbQualityTests;

// "Raumtiefe" (spec 5.12): one depth value moves Mix, Pre-delay and Size together along a curated path
class ReverbDepthTests final : public juce::UnitTest
{
public:
    ReverbDepthTests() : juce::UnitTest ("Headliner reverb depth path", "Headliner") {}

    void runTest() override
    {
        beginTest ("Depth path: mix rises, pre-delay falls, size grows - every type");
        for (int type = 0; type < ReverbModule::numTypes; ++type)
        {
            auto prev = reverbDepthPath (type, 0.0f);
            bool mixUp = true, preDown = true, sizeUp = true;
            for (int i = 1; i <= 100; ++i)
            {
                const auto p = reverbDepthPath (type, (float) i / 100.0f);
                mixUp = mixUp && p.mixPercent > prev.mixPercent;
                preDown = preDown && p.preDelayMs <= prev.preDelayMs;
                sizeUp = sizeUp && p.sizePercent >= prev.sizePercent;
                prev = p;
            }
            const auto name = juce::String (ReverbModule::voicing (type).name);
            expect (mixUp, name + ": mix strictly increasing");
            expect (preDown, name + ": pre-delay never increasing");
            expect (sizeUp, name + ": size never decreasing");
        }

        beginTest ("Depth path: sensible end points inside the parameter ranges");
        for (int type = 0; type < ReverbModule::numTypes; ++type)
        {
            const auto name = juce::String (ReverbModule::voicing (type).name);
            const auto front = reverbDepthPath (type, 0.0f), back = reverbDepthPath (type, 1.0f);
            expect (front.mixPercent > 0.0f && front.mixPercent <= 6.0f, name + " front mix " + juce::String (front.mixPercent));
            expect (front.preDelayMs >= 25.0f, name + " front pre-delay " + juce::String (front.preDelayMs));
            expect (back.mixPercent >= 35.0f && back.mixPercent <= 70.0f, name + " back mix " + juce::String (back.mixPercent));
            expect (back.sizePercent >= 80.0f, name + " back size " + juce::String (back.sizePercent));
            expect (back.preDelayMs < front.preDelayMs && back.sizePercent > front.sizePercent);
            for (float d : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                const auto p = reverbDepthPath (type, d);
                expect (p.mixPercent >= 0.0f && p.mixPercent <= 100.0f);
                expect (p.preDelayMs >= 0.0f && p.preDelayMs <= 250.0f);
                expect (p.sizePercent >= 0.0f && p.sizePercent <= 100.0f);
            }
        }

        beginTest ("Depth path: out-of-range depth and type are clamped, never NaN");
        {
            const auto a = reverbDepthPath (2, -3.0f), b = reverbDepthPath (2, 0.0f);
            expectEquals (a.mixPercent, b.mixPercent);
            const auto c = reverbDepthPath (2, 7.0f), d = reverbDepthPath (2, 1.0f);
            expectEquals (c.sizePercent, d.sizePercent);
            const auto e = reverbDepthPath (99, 0.5f), f = reverbDepthPath (ReverbModule::numTypes - 1, 0.5f);
            expectEquals (e.preDelayMs, f.preDelayMs);
            const auto g = reverbDepthPath (1, std::numeric_limits<float>::quiet_NaN());
            expect (std::isfinite (g.mixPercent) && std::isfinite (g.preDelayMs) && std::isfinite (g.sizePercent));
        }

        beginTest ("Depth path: settings on the path map back to their depth (places the dot)");
        for (int type = 0; type < ReverbModule::numTypes; ++type)
            for (int i = 0; i <= 20; ++i)
            {
                const float d = (float) i / 20.0f;
                const auto p = reverbDepthPath (type, d);
                const float back = reverbDepthFromSettings (type, p.mixPercent, p.preDelayMs, p.sizePercent);
                expect (std::abs (back - d) <= 0.02f, juce::String (ReverbModule::voicing (type).name) + " depth " + juce::String (d, 2)
                                                        + " -> " + juce::String (back, 3));
            }
        {
            // off the path: the mix decides (the dot follows what is heard most)
            const float lo = reverbDepthFromSettings (2, 2.0f, 250.0f, 0.0f), hi = reverbDepthFromSettings (2, 100.0f, 0.0f, 100.0f);
            expect (lo <= 0.05f && hi >= 0.95f, juce::String (lo, 3) + " / " + juce::String (hi, 3));
        }
    }
};

static ReverbDepthTests reverbDepthTests;
