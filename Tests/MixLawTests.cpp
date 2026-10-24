// Mix law (0.6, spec 2.9 / Q1 / Q2): true dry/wet for fresh module instances, the additive
// law of 0.1-0.5 for old projects, Delay/Reverb as inserts in a fresh plugin, and the
// dry/wet audit of all 15 modules.

#include "TestHelpers.h"
#include "SyntheticVocal.h"
#include "MixLawFixtures.h"
#include "StateKeys.h"
#include "DSP/MixLaw.h"

using namespace velvox;
using namespace velvox::test;
using dsp::ModuleId;

namespace
{
    // 0.5 s silence, a noise burst, 0.5 s silence: the burst window is where only the dry can be
    struct Burst
    {
        juce::AudioBuffer<float> signal;
        int start = 0, length = 0;
    };

    Burst makeBurst (double fs, int channels, double burstMs, int seed = 7)
    {
        Burst b;
        b.start = (int) (0.5 * fs);
        b.length = (int) (burstMs * 0.001 * fs);
        b.signal.setSize (channels, b.start + b.length + (int) (0.5 * fs));
        b.signal.clear();
        juce::Random r (seed);
        for (int i = 0; i < b.length; ++i)
        {
            const float v = 0.25f * (r.nextFloat() * 2.0f - 1.0f);
            for (int ch = 0; ch < channels; ++ch)
                b.signal.setSample (ch, b.start + i, v);
        }
        return b;
    }

    // Level of the output inside the burst window relative to the burst (dB, all channels)
    double burstResidualDb (const juce::AudioBuffer<float>& out, const Burst& b, int latency = 0)
    {
        double eo = 0.0, ei = 0.0;
        for (int ch = 0; ch < out.getNumChannels(); ++ch)
            for (int i = 0; i < b.length; ++i)
            {
                const double o = out.getSample (ch, b.start + latency + i);
                const double x = b.signal.getSample (std::min (ch, b.signal.getNumChannels() - 1), b.start + i);
                eo += o * o;
                ei += x * x;
            }
        return 10.0 * std::log10 (std::max (1.0e-30, eo) / std::max (1.0e-30, ei));
    }

    // Runs the automatic mix in a processor on a fixed synthetic vocal (capture -> analysis -> apply)
    bool runAutoMix (Harness& h, const automix::CapturedAudio& a, double timeoutSeconds = 120.0)
    {
        using automix::AutoMixController;
        auto& am = h.proc.getAutoMix();
        am.start();
        const int totalCh = std::max (h.proc.getTotalNumInputChannels(), h.proc.getTotalNumOutputChannels());
        const int mainCh = h.proc.getMainBusNumInputChannels();
        juce::AudioBuffer<float> buffer (totalCh, h.blockSize);
        juce::MidiBuffer midi;
        for (int start = 0; start < a.numSamples() && am.getPhase() != AutoMixController::Phase::analysing; start += h.blockSize)
        {
            const int n = std::min (h.blockSize, a.numSamples() - start);
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), totalCh, n);
            view.clear();
            for (int ch = 0; ch < mainCh; ++ch)
                view.copyFrom (ch, 0, a.vocal[(size_t) std::min (ch, a.channels - 1)].data() + start, n);
            h.proc.processBlock (view, midi);
            if ((start / h.blockSize) % 32 == 0)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
        }
        const auto until = juce::Time::getMillisecondCounterHiRes() + timeoutSeconds * 1000.0;
        while (juce::Time::getMillisecondCounterHiRes() < until)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
            buffer.clear();
            h.proc.processBlock (buffer, midi);
            const auto ph = am.getPhase();
            if (ph == AutoMixController::Phase::done || ph == AutoMixController::Phase::failed)
                return ph == AutoMixController::Phase::done;
        }
        return false;
    }

    //==========================================================================
    // The bare chain (no processor): one module, every other stage neutral
    struct ChainRig
    {
        double fs = 48000.0;
        dsp::ChainSettings s;

        ChainRig (const char* routing)
        {
            s.eqOn = s.compOn = s.deessOn = false;
            s.safetyClip = false;
            s.routing = dsp::Routing::fromString (routing);
        }

        void law (dsp::MixLaw l) { s.mixLaws = l == dsp::MixLaw::legacy ? dsp::MixLaws::allLegacy() : dsp::MixLaws::allTrue(); }

        juce::AudioBuffer<float> run (const juce::AudioBuffer<float>& in, int block = 512) const
        {
            auto chain = std::make_unique<dsp::VocalChain>();
            chain->prepare (fs, block);
            juce::AudioBuffer<float> out (in);
            for (int start = 0; start < out.getNumSamples(); start += block)
            {
                const int n = std::min (block, out.getNumSamples() - start);
                juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), out.getNumChannels(), start, n);
                chain->process (view, out.getNumChannels(), s);
            }
            return out;
        }

        int latency() const
        {
            dsp::VocalChain c;
            c.prepare (fs, 512);
            return c.getLatencyFor (s.routing, s.pitch.enabled);
        }
    };

    float maxAbsDiff (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        float d = 0.0f;
        for (int ch = 0; ch < a.getNumChannels(); ++ch)
            for (int i = 0; i < a.getNumSamples(); ++i)
                d = std::max (d, std::abs (a.getSample (ch, i) - b.getSample (ch, i)));
        return d;
    }

    // Additive modules, configured at Mix `m` with a wet path that starts late
    enum class Additive { delay, reverb, doubler, smash };

    ChainRig additiveRig (Additive which, float m, bool asSend = false)
    {
        switch (which)
        {
            case Additive::delay:
            {
                ChainRig r (asSend ? "v2,delay:send" : "v2,delay:insert");
                r.s.delayOn = true; r.s.delayMix = m; r.s.delay.feedback = 0.0f; r.s.delayTimeMs = 300.0f;
                return r;
            }
            case Additive::reverb:
            {
                ChainRig r (asSend ? "v2,reverb:send" : "v2,reverb:insert");
                r.s.reverbOn = true; r.s.reverbMix = m; r.s.reverb.preDelayMs = 100.0f;
                return r;
            }
            case Additive::doubler:
            {
                ChainRig r ("v2,doubler");
                r.s.doubler.enabled = true; r.s.doubler.mix = m; r.s.doubler.delayMs = 14.0f; r.s.doubler.monoSafe = 0.0f;
                return r;
            }
            case Additive::smash:
            {
                ChainRig r ("v2,smash");
                r.s.smash.enabled = true; r.s.smash.blend = m;
                return r;
            }
        }
        return ChainRig ("v2");
    }

    const char* additiveName (Additive a)
    {
        switch (a)
        {
            case Additive::delay:   return "Delay insert";
            case Additive::reverb:  return "Reverb insert";
            case Additive::doubler: return "Doubler";
            case Additive::smash:   return "Smash";
        }
        return "?";
    }

    // Routing and every parameter value, one line each (to compare two mixes)
    juce::String digest (Harness& h)
    {
        juce::StringArray lines;
        lines.add ("routing " + h.proc.getRouting().toString());
        for (auto& s : getParameterSpecs())
            lines.add (juce::String (s.id) + " " + juce::String (h.get (s.id), 5));
        return lines.joinIntoString ("\n");
    }

    automix::CapturedAudio autoMixVocal()
    {
        VocalSpec s;
        s.seconds = 20.0;
        s.channels = 2;
        s.f0 = 160.0f;
        return makeVocal (s);
    }

    // The processor's output for `in` after a reload of its state into a fresh processor
    juce::AudioBuffer<float> renderReloaded (Harness& h, const juce::AudioBuffer<float>& in)
    {
        juce::MemoryBlock blob;
        h.proc.getStateInformation (blob);
        Harness r;
        r.proc.setStateInformation (blob.getData(), (int) blob.getSize());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
        return r.run (in);
    }
}

class MixLawTests final : public juce::UnitTest
{
public:
    MixLawTests() : juce::UnitTest ("Velvox mix law", "Velvox") {}

    void runTest() override
    {
        bugTests();
        lawTypeTests();
        dspTests();
        baselineTools();
    }

    void dspTests()
    {
        using dsp::MixLaw;
        const double fs = 48000.0;

        beginTest ("True law: Delay / Reverb inserts and Doubler at Mix 100 % leave no dry (>= 60 dB down)");
        {
            for (auto a : { Additive::delay, Additive::reverb, Additive::doubler })
            {
                // The burst ends before the first wet sample can arrive (300 ms, 100 ms pre-delay, 14 ms voices)
                const auto b = makeBurst (fs, 2, a == Additive::doubler ? 10.0 : 20.0);
                auto rig = additiveRig (a, 1.0f);
                rig.law (MixLaw::trueDryWet);
                const double r = burstResidualDb (rig.run (b.signal), b, rig.latency());
                rig.law (MixLaw::legacy);
                const double legacy = burstResidualDb (rig.run (b.signal), b, rig.latency());
                logMessage (juce::String (additiveName (a)) + " 100 %: dry residual true law " + juce::String (r, 1)
                            + " dB, legacy " + juce::String (legacy, 1) + " dB");
                expectLessThan (r, -60.0, additiveName (a));
                expectWithinAbsoluteError (legacy, 0.0, 0.1, "legacy keeps the dry at unity");
            }
        }

        beginTest ("Legacy law: an insert behaves exactly like 0.5 (out = in + wet * mix, same as the send at the chain end)");
        {
            const auto in = makeBurst (fs, 2, 200.0).signal;
            for (auto a : { Additive::delay, Additive::reverb })
                for (float m : { 0.3f, 0.8f, 1.0f })
                {
                    auto ins = additiveRig (a, m);
                    auto send = additiveRig (a, m, true);
                    ins.law (MixLaw::legacy);
                    send.law (MixLaw::trueDryWet);   // a send never depends on the law
                    const float d = maxAbsDiff (ins.run (in), send.run (in));
                    expectLessThan (d, 1.0e-6f, juce::String (additiveName (a)) + " " + juce::String (m));
                }
        }

        beginTest ("True law up to 50 % is bit-identical to the legacy law (auto-mix, macros and presets keep their sound)");
        {
            juce::AudioBuffer<float> in (2, (int) fs);
            juce::Random r (3);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < in.getNumSamples(); ++i)
                    in.setSample (ch, i, 0.3f * (r.nextFloat() * 2.0f - 1.0f) * (float) std::sin (pi * i / fs * 3.0));
            for (auto a : { Additive::delay, Additive::reverb, Additive::doubler, Additive::smash })
                for (float m : { 0.0f, 0.2f, 0.35f, 0.5f })
                {
                    auto t = additiveRig (a, m), l = additiveRig (a, m);
                    t.law (MixLaw::trueDryWet);
                    l.law (MixLaw::legacy);
                    expectEquals (maxAbsDiff (t.run (in), l.run (in)), 0.0f, juce::String (additiveName (a)) + " " + juce::String (m));
                }
        }

        beginTest ("True law: Smash at Blend 100 % is the squashed path alone (the legacy output minus exactly the dry)");
        {
            const auto in = makeBurst (fs, 2, 300.0).signal;
            auto t = additiveRig (Additive::smash, 1.0f), l = additiveRig (Additive::smash, 1.0f);
            t.law (MixLaw::trueDryWet);
            l.law (MixLaw::legacy);
            const auto yt = t.run (in), yl = l.run (in);
            // legacy at blend 1: (dry + wet) / 1.6  ->  wet = 1.6 * legacy - dry
            double err = 0.0, ref = 0.0;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < in.getNumSamples(); ++i)
                {
                    const double wet = 1.6 * yl.getSample (ch, i) - in.getSample (ch, i);
                    err += juce::square (yt.getSample (ch, i) - wet);
                    ref += juce::square ((double) in.getSample (ch, i));
                }
            const double db = 10.0 * std::log10 (std::max (1.0e-30, err) / ref);
            logMessage ("Smash 100 %: deviation from the squashed path alone " + juce::String (db, 1) + " dB");
            expectLessThan (db, -100.0);
        }

        beginTest ("True law: moving Mix 0 -> 100 % while playing does not click");
        {
            for (auto a : { Additive::delay, Additive::reverb, Additive::doubler, Additive::smash })
            {
                // Starts slightly on, so the module is running (its own wake-up is not what is tested)
                auto rig = additiveRig (a, 0.01f);
                rig.s.delayTimeMs = 302.5f;   // the echo is out of phase with the dry
                rig.law (MixLaw::trueDryWet);
                const int block = 256, n = 188 * block;
                juce::AudioBuffer<float> io (2, n);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < n; ++i)
                        io.setSample (ch, i, 0.4f * (float) std::sin (2.0 * pi * 200.0 * i / fs));
                const float naturalStep = 0.4f * (float) (2.0 * pi * 200.0 / fs);
                auto chain = std::make_unique<dsp::VocalChain>();
                chain->prepare (fs, block);
                float worst = 0.0f, prev = 0.0f;
                for (int start = 0; start < n; start += block)
                {
                    if (start >= n / 2)
                    {
                        rig.s.delayMix = rig.s.reverbMix = rig.s.doubler.mix = rig.s.smash.blend = 1.0f;
                    }
                    juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), 2, start, block);
                    chain->process (view, 2, rig.s);
                    for (int i = 0; i < block; ++i)
                    {
                        const float y = io.getSample (0, start + i);
                        if (start + i > n / 2 - block && start + i < n / 2 + (int) (0.1 * fs))
                            worst = std::max (worst, std::abs (y - prev));
                        prev = y;
                    }
                }
                logMessage (juce::String (additiveName (a)) + ": largest step during the Mix jump " + juce::String (worst / naturalStep, 2) + " x the sine's own");
                expectLessThan (worst, 3.0f * naturalStep, additiveName (a));
            }
        }
    }

    void lawTypeTests()
    {
        using dsp::MixLaw;
        using dsp::MixLaws;

        beginTest ("MixLaws: fresh = all true, unknown text = all legacy, string round trip");
        {
            const auto fresh = MixLaws::allTrue();
            const auto old = MixLaws::allLegacy();
            for (int m = 0; m < dsp::numModules; ++m)
                for (int k = 0; k < dsp::maxInstances ((ModuleId) m); ++k)
                {
                    expect (! fresh.isLegacy ((ModuleId) m, k));
                    expect (old.isLegacy ((ModuleId) m, k));
                }
            expectEquals (fresh.toString(), juce::String ("v1"));
            expect (MixLaws::fromString ({}) == old, "absent flag = every instance legacy");
            expect (MixLaws::fromString ("garbage") == old);
            expect (MixLaws::fromString (old.toString()) == old);
            expect (MixLaws::fromString ("v1") == fresh);

            auto mixed = fresh;
            mixed.setLegacy (ModuleId::delay, 1, true);
            mixed.setLegacy (ModuleId::doubler, 0, true);
            expectEquals (mixed.toString(), juce::String ("v1,doubler,delay#2"));
            expect (MixLaws::fromString (mixed.toString()) == mixed);
            expect (MixLaws::fromString ("v1, delay#2 ,nonsense,eq#9,doubler") == mixed, "tolerant parser");
            expect (mixed.lawOf (ModuleId::delay, 1) == MixLaw::legacy && mixed.lawOf (ModuleId::delay, 0) == MixLaw::trueDryWet);
        }

        beginTest ("MixLaws: dry gain of an additive Mix - legacy unity, true law holds the dry up to 50 % and reaches 0 at 100 %");
        {
            for (float m : { 0.0f, 0.2f, 0.5f, 0.8f, 1.0f })
                expectEquals (dsp::additiveDryGain (m, MixLaw::legacy), 1.0f);
            expectEquals (dsp::additiveDryGain (0.0f, MixLaw::trueDryWet), 1.0f);
            expectEquals (dsp::additiveDryGain (0.35f, MixLaw::trueDryWet), 1.0f);
            expectEquals (dsp::additiveDryGain (0.5f, MixLaw::trueDryWet), 1.0f);
            expectWithinAbsoluteError (dsp::additiveDryGain (0.75f, MixLaw::trueDryWet), 0.5f, 1.0e-6f);
            expectEquals (dsp::additiveDryGain (1.0f, MixLaw::trueDryWet), 0.0f);
            for (auto id : { ModuleId::delay, ModuleId::reverb, ModuleId::doubler, ModuleId::smash })
                expect (dsp::hasAdditiveMix (id));
            for (auto id : { ModuleId::eq, ModuleId::comp, ModuleId::sat, ModuleId::choir, ModuleId::pitch, ModuleId::pulse })
                expect (! dsp::hasAdditiveMix (id));
        }
    }

    void bugTests()
    {
        beginTest ("Bug: Delay at 100 % in a fresh instance passes no dry vocal");
        {
            Harness h;
            h.neutral();
            h.set (pid::delayOn, 1.0f);
            h.set (pid::delayMix, 100.0f);
            h.set (pid::delayFeedback, 0.0f);
            h.set (pid::delayTime, 300.0f);
            const auto b = makeBurst (h.fs, 2, 20.0);
            const auto out = h.run (b.signal);
            const double r = burstResidualDb (out, b, h.proc.getLatencySamples());
            logMessage ("delay 100 %: dry residual " + juce::String (r, 1) + " dB");
            expectLessThan (r, -60.0, "dry >= 60 dB down");
        }
    }

    // One-off tools for the 0.5 baseline (run with VELVOX_MIXLAW_DUMP=<dir>)
    void baselineTools()
    {
        const auto dumpDir = juce::SystemStats::getEnvironmentVariable ("VELVOX_MIXLAW_DUMP", {});
        if (dumpDir.isEmpty())
            return;

        beginTest ("Baseline dump: 0.5 state with additive mixes above 50 %");
        juce::MemoryBlock blob;
        {
            Harness h;
            configureState05 (h);
            h.proc.getStateInformation (blob);
        }
        juce::MemoryOutputStream z;
        {
            juce::GZIPCompressorOutputStream gz (z, 9);
            gz.write (blob.getData(), blob.getSize());
        }
        juce::File (dumpDir).getChildFile ("state05.b64").replaceWithText (juce::Base64::toBase64 (z.getData(), z.getDataSize()));

        Harness h;
        h.proc.setStateInformation (blob.getData(), (int) blob.getSize());
        const auto out = h.run (fixedSignal (h));
        writeRaw (juce::File (dumpDir).getChildFile ("state05.raw"), out);

        beginTest ("Baseline dump: automatic mix of a fixed vocal in a fresh instance");
        Harness am;
        expect (runAutoMix (am, autoMixVocal()), "mix finished");
        juce::File (dumpDir).getChildFile ("automix.txt").replaceWithText (digest (am));
        writeRaw (juce::File (dumpDir).getChildFile ("automix.raw"), renderReloaded (am, fixedSignal (am)));
        logMessage ("dumped to " + dumpDir);
    }

public:
    // The 0.5 project of the fixture: Delay / Reverb inserts, Doubler and Smash above 50 %
    static void configureState05 (Harness& h)
    {
        h.neutral();
        h.set (pid::delayOn, 1.0f);   h.set (pid::delayMix, 100.0f);  h.set (pid::delayFeedback, 30.0f); h.set (pid::delayTime, 250.0f);
        h.set (pid::reverbOn, 1.0f);  h.set (pid::reverbMix, 80.0f);
        h.set (pid::doublerOn, 1.0f); h.set (pid::doublerMix, 80.0f);
        h.set (pid::smashOn, 1.0f);   h.set (pid::smashBlend, 90.0f);
        h.proc.setRouting (dsp::Routing::fromString ("v2,eq,comp,deess,doubler,smash,delay:insert,reverb:insert"));
    }

    static juce::AudioBuffer<float> fixedSignal (const Harness& h)
    {
        auto s = h.noise (0.1f, 2.0, 11);
        for (int ch = 0; ch < s.getNumChannels(); ++ch)
            for (int i = 0; i < s.getNumSamples(); ++i)
            {
                const double t = i / h.fs;
                const float env = (float) (0.5 + 0.5 * std::sin (2.0 * pi * 1.5 * t));
                s.setSample (ch, i, env * (s.getSample (ch, i) + 0.3f * (float) std::sin (2.0 * pi * 220.0 * t)));
            }
        return s;
    }

    static void writeRaw (const juce::File& f, const juce::AudioBuffer<float>& b)
    {
        f.deleteFile();
        juce::FileOutputStream os (f);
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            os.write (b.getReadPointer (ch), sizeof (float) * (size_t) b.getNumSamples());
    }
};

static MixLawTests mixLawTests;
