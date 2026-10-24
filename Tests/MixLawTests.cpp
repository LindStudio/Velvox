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
        baselineTools();
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
