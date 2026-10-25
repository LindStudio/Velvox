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
        processorTests();
        auditTests();
        macroAndPresetTests();
        autoMixTests();
        baselineTools();
    }

    // Every instance of every module on the legacy law (a 0.5-like instance)
    static void makeAllLegacy (Harness& h)
    {
        for (int m = 0; m < dsp::numModules; ++m)
            for (int k = 0; k < dsp::maxInstances ((ModuleId) m); ++k)
                h.proc.setLegacyMixLaw ((ModuleId) m, k, true);
    }

    void macroAndPresetTests()
    {
        beginTest ("Macros add wet but never take the dry away: WIDTH / SPACE sound the same on both laws");
        {
            struct Case { const char* name; std::function<void (Harness&)> set; };
            const std::vector<Case> cases {
                { "WIDTH 100 % on a Doubler at 50 %", [] (Harness& h) {
                      h.set (pid::doublerOn, 1.0f); h.set (pid::doublerMix, 50.0f); h.set (pid::macroWidth, 100.0f); } },
                // Both effects switched on by the user (SPACE only adds to them, nothing becomes a send)
                { "SPACE 100 % on a Reverb insert at 40 % and a Delay insert at 45 %", [] (Harness& h) {
                      h.set (pid::reverbOn, 1.0f); h.set (pid::reverbMix, 40.0f);
                      h.set (pid::delayOn, 1.0f); h.set (pid::delayMix, 45.0f); h.set (pid::macroSpace, 100.0f); } },
            };
            for (const auto& c : cases)
            {
                Harness t, l;
                for (auto* h : { &t, &l })
                {
                    h->neutral();
                    h->proc.setRouting (dsp::Routing::fromString ("v2,doubler,delay:insert,reverb:insert"));
                }
                makeAllLegacy (l);
                for (auto* h : { &t, &l })
                {
                    c.set (*h);
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (2);
                }
                // 0.6.1: the slider writes the Mix itself; an instance it pushes over 50 % keeps the dry via the additive law
                expect (l.proc.getMixLaws() == dsp::MixLaws::allLegacy());
                expect (t.proc.getMixLaws() != dsp::MixLaws::allTrue(), t.proc.getMixLaws().toString());
                expect (t.proc.getRouting() == l.proc.getRouting(), c.name);
                const auto in = fixedSignal (t);
                expectEquals (maxAbsDiff (t.run (in), l.run (in)), 0.0f, c.name);
            }
        }

        beginTest ("Presets: the law travels with snapshots; presets without it keep their sound (legacy only where Mix > 50 %)");
        {
            Harness h;
            load (h, state05());
            h.proc.switchToTrueMixLaw (ModuleId::reverb, 0);
            const auto snap = ParameterSnapshot::capture (h.proc.getState(), "x");
            expectEquals (snap.mixLaw, h.proc.getMixLaws().toString());
            const auto restored = ParameterSnapshot::fromValueTree (snap.toValueTree());
            expectEquals (restored.mixLaw, snap.mixLaw);

            Harness other;
            restored.applyTo (other.proc.getState());
            expect (other.proc.getMixLaws() == h.proc.getMixLaws(), other.proc.getMixLaws().toString());

            // A preset without a law (factory, 0.5 user preset): legacy exactly where it changes the sound
            auto old = restored;
            old.mixLaw = {};
            old.values[pid::doublerMix] = 70.0f;
            old.values[pid::smashBlend] = 30.0f;
            old.values[pid::delayMix] = 100.0f;
            old.values[pid::reverbMix] = 50.0f;
            Harness fresh;
            old.applyTo (fresh.proc.getState());
            expect (fresh.proc.usesLegacyMixLaw (ModuleId::doubler, 0), "doubler 70 %");
            expect (fresh.proc.usesLegacyMixLaw (ModuleId::delay, 0), "delay 100 %");
            expect (! fresh.proc.usesLegacyMixLaw (ModuleId::smash, 0), "smash 30 % sounds the same on the true law");
            expect (! fresh.proc.usesLegacyMixLaw (ModuleId::reverb, 0), "reverb 50 % sounds the same on the true law");
        }

        beginTest ("Factory presets are not converted: each sounds the same on the true and the legacy law");
        {
            juce::StringArray differing;
            for (const auto& preset : getFactoryPresets())
            {
                Harness t, l;
                makeAllLegacy (l);
                preset.applyTo (t.proc.getState());
                preset.applyTo (l.proc.getState());
                makeAllLegacy (l);
                expect (t.proc.getRouting() == l.proc.getRouting(), preset.name);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (2);
                const auto in = fixedSignal (t);
                if (maxAbsDiff (t.run (in), l.run (in)) > 0.0f)
                    differing.add (preset.name);
            }
            logMessage ("factory presets that sound different on a fresh instance than in 0.5: "
                        + (differing.isEmpty() ? juce::String ("none") : differing.joinIntoString (", ")));
            expect (differing.isEmpty(), differing.joinIntoString (", "));
        }
    }

    void autoMixTests()
    {
        beginTest ("Auto mix: same result in a fresh (true law, insert defaults) and a 0.5-like instance; additive mixes <= 50 %, sends kept");
        {
            Harness fresh, old;
            old.proc.setRouting (dsp::Routing::legacyDefaults());
            for (int m = 0; m < dsp::numModules; ++m)
                for (int k = 0; k < dsp::maxInstances ((ModuleId) m); ++k)
                    old.proc.setLegacyMixLaw ((ModuleId) m, k, true);

            const auto vocal = autoMixVocal();
            expect (runAutoMix (fresh, vocal), "fresh: mix finished");
            expect (runAutoMix (old, vocal), "0.5-like: mix finished");
            const auto d = digest (fresh);
            expectEquals (d, digest (old), "identical parameters and routing");

            const auto r = fresh.proc.getRouting();
            for (auto id : { ModuleId::delay, ModuleId::reverb })
                expect (! r.contains (id) || r.isSend (id), juce::String ("planner keeps the send: ") + r.toString());
            expectLessOrEqual (fresh.get (pid::doublerMix), 50.0f);
            expectLessOrEqual (fresh.get (pid::smashBlend), 50.0f);

            const auto in = fixedSignal (fresh);
            const auto outFresh = renderReloaded (fresh, in);
            expectEquals (maxAbsDiff (outFresh, renderReloaded (old, in)), 0.0f, "identical sound");

            if (const auto ref = juce::SystemStats::getEnvironmentVariable ("VELVOX_MIXLAW_BASELINE", {}); ref.isNotEmpty())
            {
                const auto base = juce::File (ref).getChildFile ("automix.txt").loadFileAsString().replace ("\r\n", "\n");
                expectEquals (d, base, "same mix as the 0.5 build");
                juce::MemoryBlock raw;
                juce::File (ref).getChildFile ("automix.raw").loadFileAsData (raw);
                const auto* f = static_cast<const float*> (raw.getData());
                float diff = 0.0f;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < outFresh.getNumSamples(); ++i)
                        diff = std::max (diff, std::abs (outFresh.getSample (ch, i) - f[(size_t) ch * (size_t) outFresh.getNumSamples() + (size_t) i]));
                logMessage ("auto mix vs the 0.5 build: max difference " + juce::String (diff, 9));
                expectEquals (diff, 0.0f, "bit-exact with 0.5");
            }
        }
    }

    // Magnitude of `freq` in channel 0 over [start, start + len) (Hann window, normalised to a sine's amplitude)
    static double toneMagnitude (const juce::AudioBuffer<float>& b, int start, int len, double freq, double fs)
    {
        double re = 0.0, im = 0.0, wsum = 0.0;
        for (int i = 0; i < len; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * pi * i / (len - 1));
            const double x = b.getSample (0, start + i) * w;
            re += x * std::cos (2.0 * pi * freq * i / fs);
            im -= x * std::sin (2.0 * pi * freq * i / fs);
            wsum += w;
        }
        return 2.0 * std::sqrt (re * re + im * im) / wsum;
    }

    // Lowest / highest short-term level over a steady input (dB): how far a gate module closes
    static std::pair<double, double> gateRangeDb (const juce::AudioBuffer<float>& out, int start, int len, int window)
    {
        double lo = 1.0e9, hi = -1.0e9;
        for (int s = start; s + window <= start + len; s += window / 2)
        {
            const double db = juce::Decibels::gainToDecibels ((double) out.getRMSLevel (0, s, window), -300.0);
            lo = std::min (lo, db);
            hi = std::max (hi, db);
        }
        return { lo, hi };
    }

    void auditTests()
    {
        using dsp::MixLaw;
        const double fs = 48000.0;
        juce::StringArray table;

        beginTest ("Audit: Choir at Mix 100 % leaves no dry (voices start >= 6 ms late)");
        {
            const auto b = makeBurst (fs, 2, 4.0);
            ChainRig rig ("v2,choir");
            rig.s.choir.enabled = true; rig.s.choir.mix = 1.0f; rig.s.choir.humanize = 0.0f;
            const double r = burstResidualDb (rig.run (b.signal), b, rig.latency());
            table.add ("choir | choirMix | true crossfade (both laws) | " + juce::String (r, 1) + " dB");
            expectLessThan (r, -60.0);
        }

        beginTest ("Audit: Pitch at Mix 100 % (+7 st) leaves no dry at the input frequency");
        {
            ChainRig rig ("v2,pitch");
            rig.s.pitch.enabled = true; rig.s.pitch.semitones = 7.0f; rig.s.pitch.mix = 1.0f; rig.s.pitch.preserveFormants = false;
            juce::AudioBuffer<float> in (2, (int) (3.0 * fs));
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < in.getNumSamples(); ++i)
                    in.setSample (ch, i, 0.25f * (float) std::sin (2.0 * pi * 440.0 * i / fs));
            const auto out = rig.run (in);
            const int start = (int) (1.5 * fs) + rig.latency(), len = (int) fs;
            const double dry = toneMagnitude (out, start, len, 440.0, fs) / toneMagnitude (in, (int) (1.5 * fs), len, 440.0, fs);
            const double wet = toneMagnitude (out, start, len, 440.0 * std::pow (2.0, 7.0 / 12.0), fs) / 0.25;
            const double r = juce::Decibels::gainToDecibels (dry, -300.0);
            logMessage ("pitch +7 st: shifted tone " + juce::String (juce::Decibels::gainToDecibels (wet), 1) + " dB, output "
                        + juce::String (rmsDb (out, start, len) - rmsDb (in, start, len), 1) + " dB re input, residual at the input frequency " + juce::String (r, 1) + " dB");
            expectGreaterThan (rmsDb (out, start, len), rmsDb (in, start, len) - 12.0f, "the shifted voice is there");
            table.add ("pitch | pitchMix | true crossfade, dry latency aligned (both laws) | " + juce::String (r, 1) + " dB");
            expectLessThan (r, -60.0);
        }

        beginTest ("Audit: Mix 0 % passes the (latency-aligned) dry bit-exactly; 100 % is the processed signal");
        {
            struct Case { const char* name; const char* routing; std::function<void (dsp::ChainSettings&, float)> set; };
            const std::vector<Case> cases {
                { "eq",      "v2,eq",      [] (auto& s, float m) { s.eqOn = true; s.eqMix = m; s.eq.bands[0].on = true; s.eq.bands[0].gain = 9.0f; } },
                { "comp",    "v2,comp",    [] (auto& s, float m) { s.compOn = true; s.compMix = m; s.comp.thresholdDb = -40.0f; s.comp.ratio = 8.0f; } },
                { "sat",     "v2,sat",     [] (auto& s, float m) { s.sat.enabled = true; s.sat.mix = m; s.sat.driveDb = 18.0f; } },
                { "pitch",   "v2,pitch",   [] (auto& s, float m) { s.pitch.enabled = true; s.pitch.mix = m; s.pitch.semitones = 5.0f; } },
                { "choir",   "v2,choir",   [] (auto& s, float m) { s.choir.enabled = true; s.choir.mix = m; } },
                { "pulse",   "v2,pulse",   [] (auto& s, float m) { s.pulse.enabled = true; s.pulse.mix = m; s.pulse.depth = 1.0f; } },
                { "chop",    "v2,chop",    [] (auto& s, float m) { s.chop.enabled = true; s.chop.depth = m; } },
                { "delay",   "v2,delay:insert",  [] (auto& s, float m) { s.delayOn = true; s.delayMix = m; } },
                { "reverb",  "v2,reverb:insert", [] (auto& s, float m) { s.reverbOn = true; s.reverbMix = m; } },
                { "doubler", "v2,doubler", [] (auto& s, float m) { s.doubler.enabled = true; s.doubler.mix = m; } },
                { "smash",   "v2,smash",   [] (auto& s, float m) { s.smash.enabled = true; s.smash.blend = m; } },
            };
            juce::AudioBuffer<float> in (2, (int) fs);
            juce::Random rnd (5);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < in.getNumSamples(); ++i)
                    in.setSample (ch, i, 0.2f * (rnd.nextFloat() * 2.0f - 1.0f) + 0.1f * (float) std::sin (2.0 * pi * 300.0 * i / fs));
            for (const auto& c : cases)
                for (auto law : { MixLaw::legacy, MixLaw::trueDryWet })
                {
                    ChainRig zero (c.routing), full (c.routing);
                    c.set (zero.s, 0.0f);
                    c.set (full.s, 1.0f);
                    zero.law (law);
                    full.law (law);
                    const int L = zero.latency();
                    const auto y0 = zero.run (in);
                    float d = 0.0f;
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i + L < in.getNumSamples(); ++i)
                            d = std::max (d, std::abs (y0.getSample (ch, i + L) - in.getSample (ch, i)));
                    const juce::String what = juce::String (c.name) + (law == MixLaw::legacy ? " (legacy)" : " (true)");
                    expectEquals (d, 0.0f, what + ": 0 % = dry, bit-exact");

                    const auto y1 = full.run (in);
                    float p = 0.0f;
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i + L < in.getNumSamples(); ++i)
                            p = std::max (p, std::abs (y1.getSample (ch, i + L) - in.getSample (ch, i)));
                    expectGreaterThan (p, 0.01f, what + ": 100 % is processed");
                }
        }

        beginTest ("Audit: Pulse and Chop at 100 % close fully (the wet is the gated dry by design)");
        {
            juce::AudioBuffer<float> in (2, (int) (4.0 * fs));
            juce::Random rnd (9);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < in.getNumSamples(); ++i)
                    in.setSample (ch, i, 0.2f * (rnd.nextFloat() * 2.0f - 1.0f));

            ChainRig pulse ("v2,pulse");
            pulse.s.pulse.enabled = true; pulse.s.pulse.mix = 1.0f; pulse.s.pulse.depth = 1.0f;
            pulse.s.pulse.y = { 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f };   // a closed segment (the default only touches 0)
            const auto [plo, phi] = gateRangeDb (pulse.run (in), (int) fs, (int) (2.0 * fs), 240);
            ChainRig chop ("v2,chop");
            chop.s.chop.enabled = true; chop.s.chop.depth = 1.0f;
            const auto [clo, chi] = gateRangeDb (chop.run (in), (int) fs, (int) (2.0 * fs), 240);
            logMessage ("pulse depth/mix 100 %: closed " + juce::String (plo - phi, 1) + " dB below open; chop depth 100 %: "
                        + juce::String (clo - chi, 1) + " dB");
            table.add ("pulse | pulseMix | true crossfade (both laws); wet = gain-modulated dry | closed phase " + juce::String (plo - phi, 1) + " dB");
            table.add ("chop | chopDepth (no Mix) | depth crossfade (both laws); wet = gated / repeated dry | muted step " + juce::String (clo - chi, 1) + " dB");
            expectLessThan (plo - phi, -60.0, "pulse closes");
            expectLessThan (clo - chi, -60.0, "chop mutes");
        }

        beginTest ("Audit: Vocal Check is read-only (bit-exact pass-through)");
        {
            ChainRig rig ("v2,check");
            rig.s.checkOn = true;
            const auto in = makeBurst (fs, 2, 300.0).signal;
            expectEquals (maxAbsDiff (rig.run (in), in), 0.0f);
        }

        logMessage ("AUDIT module | mix param | law | dry residual at 100 %");
        for (auto& line : table)
            logMessage ("AUDIT " + line);
    }

    static juce::MemoryBlock state05()
    {
        juce::MemoryOutputStream z;
        juce::Base64::convertFromBase64 (z, mixlaw::state05Base64);
        juce::MemoryInputStream zin (z.getData(), z.getDataSize(), false);
        juce::GZIPDecompressorInputStream gz (zin);
        juce::MemoryBlock blob;
        gz.readIntoMemoryBlock (blob);
        return blob;
    }

    static void load (Harness& h, const juce::MemoryBlock& blob)
    {
        h.proc.setStateInformation (blob.getData(), (int) blob.getSize());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    }

    // What the browser does: fresh parameters, off, joins the chain, then switched on
    static void addLikeBrowser (Harness& h, ModuleId id, int k, const char* onParam, int index = -1)
    {
        h.set (dsp::instanceParamId (onParam, k).toRawUTF8(), 0.0f);
        auto r = h.proc.getRouting();
        r.insertAt (index < 0 ? r.size() : index, id, k, true);
        h.proc.setRouting (r);
        h.set (dsp::instanceParamId (onParam, k).toRawUTF8(), 1.0f);
    }

    void processorTests()
    {
        using dsp::MixLaws;
        using dsp::Routing;

        beginTest ("Fresh plugin: every instance on the true law; Delay / Reverb are inserts in the default chain");
        {
            Harness h;
            expect (h.proc.getMixLaws() == MixLaws::allTrue());
            for (auto id : { ModuleId::delay, ModuleId::reverb, ModuleId::doubler, ModuleId::smash })
                expect (! h.proc.usesLegacyMixLaw (id, 0));
            const auto r = h.proc.getRouting();
            expect (r == Routing::defaults());
            expectEquals (r.toString(), juce::String ("v2,eq,comp,deess,delay:insert,reverb:insert"));
            expectEquals (Routing::legacyDefaults().toString(), juce::String ("v2,eq,comp,deess,delay:send,reverb:send"));
        }

        beginTest ("0.5 state (no flag): every instance legacy; the switch flips exactly one instance; the flag survives save / load");
        {
            Harness h;
            load (h, state05());
            expect (h.proc.getMixLaws() == MixLaws::allLegacy(), h.proc.getMixLaws().toString());
            for (auto id : { ModuleId::delay, ModuleId::reverb, ModuleId::doubler, ModuleId::smash })
                expect (h.proc.usesLegacyMixLaw (id, 0));
            expect (! h.proc.usesLegacyMixLaw (ModuleId::eq, 0), "only additive-mix modules report the legacy law");
            expectEquals (h.proc.getRouting().toString(), juce::String ("v2,eq,comp,deess,doubler,smash,delay:insert,reverb:insert"));

            const auto in = fixedSignal (h);
            const auto legacyOut = renderReloaded (h, in);
            if (const auto ref = juce::SystemStats::getEnvironmentVariable ("VELVOX_MIXLAW_BASELINE", {}); ref.isNotEmpty())
            {
                juce::MemoryBlock raw;
                juce::File (ref).getChildFile ("state05.raw").loadFileAsData (raw);
                const auto* f = static_cast<const float*> (raw.getData());
                float d = 0.0f;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < legacyOut.getNumSamples(); ++i)
                        d = std::max (d, std::abs (legacyOut.getSample (ch, i) - f[(size_t) ch * (size_t) legacyOut.getNumSamples() + (size_t) i]));
                logMessage ("0.5 state vs the 0.5 build: max difference " + juce::String (d, 9));
                expectEquals (d, 0.0f, "bit-exact with 0.5");
            }

            h.proc.switchToTrueMixLaw (ModuleId::delay, 0);
            expect (! h.proc.usesLegacyMixLaw (ModuleId::delay, 0));
            expect (h.proc.usesLegacyMixLaw (ModuleId::reverb, 0) && h.proc.usesLegacyMixLaw (ModuleId::delay, 1));
            const auto switched = renderReloaded (h, in);
            expectGreaterThan (maxAbsDiff (switched, legacyOut), 0.01f, "Delay at 100 % sounds different on the true law");

            juce::MemoryBlock saved;
            h.proc.getStateInformation (saved);
            Harness h2;
            load (h2, saved);
            expect (h2.proc.getMixLaws() == h.proc.getMixLaws(), h2.proc.getMixLaws().toString());

            h.proc.setLegacyMixLaw (ModuleId::delay, 0, true);
            expect (h.proc.getMixLaws() == MixLaws::allLegacy());
        }

        beginTest ("0.1 and 0.3 states load on the legacy law (also instances that are off)");
        {
            for (int version : { 1, 3 })
            {
                juce::MemoryBlock old;
                {
                    Harness src;
                    auto state = src.proc.getState().copyState();
                    state.removeProperty (statekeys::mixLaw, nullptr);
                    if (version == 1)
                        state.removeProperty (statekeys::routing, nullptr);
                    else
                        state.setProperty (statekeys::routing, "eq,ride,comp,smash,deess,smooth,sat,doubler,choir,pitch,delay:send,reverb:insert,chop,pulse,check", nullptr);
                    state.setProperty (statekeys::stateVersion, version, nullptr);
                    juce::AudioProcessor::copyXmlToBinary (*state.createXml(), old);
                }
                Harness h;
                load (h, old);
                expect (h.proc.getMixLaws() == MixLaws::allLegacy(), "version " + juce::String (version));
            }
        }

        beginTest ("Old project: a module added through the browser is fresh (true law, insert); automation keeps the legacy law");
        {
            Harness h;
            load (h, state05());
            addLikeBrowser (h, ModuleId::delay, 1, pid::delayOn);
            expect (h.proc.getRouting().isInsert (ModuleId::delay, 1));
            expect (! h.proc.usesLegacyMixLaw (ModuleId::delay, 1), "added delay #2 is fresh");
            expect (h.proc.usesLegacyMixLaw (ModuleId::delay, 0), "the project's delay stays legacy");

            // Automation switches on an instance that is not in the chain: it joins on, keeps its law
            h.set ("smashOn_2", 1.0f);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
            expect (h.proc.getRouting().contains (ModuleId::smash, 1), "engaged by automation");
            expect (h.proc.usesLegacyMixLaw (ModuleId::smash, 1), "automation keeps the old sound");

            // Removing and re-adding an instance through the browser makes it fresh
            auto r = h.proc.getRouting();
            r.remove (ModuleId::doubler, 0);
            h.proc.setRouting (r);
            addLikeBrowser (h, ModuleId::doubler, 0, pid::doublerOn);
            expect (! h.proc.usesLegacyMixLaw (ModuleId::doubler, 0));
        }

        beginTest ("SPACE on a fresh instance: the delay / reverb it switches on become sends and sound like 0.5");
        {
            Harness fresh, old;
            fresh.neutral();
            old.neutral();
            old.proc.setRouting (Routing::legacyDefaults());
            for (auto id : { ModuleId::delay, ModuleId::reverb, ModuleId::doubler, ModuleId::smash })
                for (int k = 0; k < dsp::maxInstances (id); ++k)
                    old.proc.setLegacyMixLaw (id, k, true);
            for (auto* h : { &fresh, &old })
            {
                h->set (pid::macroSpace, 100.0f);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
            }
            expect (fresh.proc.getRouting().isSend (ModuleId::delay, 0) && fresh.proc.getRouting().isSend (ModuleId::reverb, 0),
                    fresh.proc.getRouting().toString());
            expect (fresh.proc.getRouting() == old.proc.getRouting(), fresh.proc.getRouting().toString());
            const auto in = makeBurst (fresh.fs, 2, 300.0).signal;
            expectLessThan (maxAbsDiff (fresh.run (in), old.run (in)), 1.0e-6f, "same sound as the 0.5 send setup");

            // A delay the user switched on keeps its insert; SPACE only adds to its level
            Harness user;
            user.neutral();
            user.set (pid::delayOn, 1.0f);
            user.set (pid::macroSpace, 100.0f);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
            expect (user.proc.getRouting().isInsert (ModuleId::delay, 0));
            expect (user.proc.getRouting().isSend (ModuleId::reverb, 0), "the reverb was switched on by SPACE");
        }
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

        beginTest ("\"Alte Mix-Kurve - umstellen\" is one undo step; undo / redo restore the law");
        {
            Harness h;
            h.proc.setLegacyMixLaw (ModuleId::delay, 0, true);   // as loaded from an old project
            h.proc.getUndo().clear();
            h.proc.switchToTrueMixLaw (ModuleId::delay, 0);
            expect (! h.proc.usesLegacyMixLaw (ModuleId::delay, 0));
            auto& undo = h.proc.getUndo();
            expectEquals (undo.getNumSteps(), 1, "one step");
            expect (undo.undo());
            expect (h.proc.usesLegacyMixLaw (ModuleId::delay, 0), "undo: legacy law again");
            expect (undo.redo());
            expect (! h.proc.usesLegacyMixLaw (ModuleId::delay, 0), "redo: true law");

            // A transaction that changes only a law is a step as well (nothing else compares it)
            undo.clear();
            undo.perform ("law", [&] { h.proc.setLegacyMixLaw (ModuleId::reverb, 0, true); });
            expectEquals (undo.getNumSteps(), 1);
            undo.undo();
            expect (! h.proc.usesLegacyMixLaw (ModuleId::reverb, 0));
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
