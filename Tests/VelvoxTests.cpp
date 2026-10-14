// Offline verification of the DSP chain, state handling and presets.
// Runs without a DAW: builds the real VelvoxAudioProcessor and feeds it signals.

#include "TestHelpers.h"
#include "Presets/FactoryPresets.h"
#include "ProductInfo.h"

using namespace velvox;

using namespace velvox::test;

//==============================================================================
class ChainTests final : public juce::UnitTest
{
public:
    ChainTests() : juce::UnitTest ("Velvox chain", "Velvox") {}

    void runTest() override
    {
        beginTest ("Neutral settings are a pure (latency-delayed) pass-through");
        {
            Harness h;
            h.neutral();
            const auto in = h.noise (0.3f, 1.0);
            const auto out = h.run (in);
            const int L = h.proc.getLatencySamples();
            logMessage ("Reported latency: " + juce::String (L) + " samples");
            expect (L >= 0 && L < 64, "latency should be small");

            double maxErr = 0.0;
            for (int i = 4800 + L; i < in.getNumSamples(); ++i)
                maxErr = std::max (maxErr, (double) std::abs (out.getSample (0, i) - in.getSample (0, i - L)));
            expectLessThan (maxErr, 1.0e-5, "pass-through error");
        }

        beginTest ("Global bypass returns the latency-aligned input");
        {
            Harness h;
            for (auto& p : getFactoryPresets())
                if (p.name == "Hypertechno Lead")
                    p.applyTo (h.proc.getState());
            h.set (pid::bypass, 1.0f);
            const auto in = h.noise (0.3f, 1.0, 7);
            const auto out = h.run (in);
            const int L = h.proc.getLatencySamples();
            double maxErr = 0.0;
            for (int i = 4800 + L; i < in.getNumSamples(); ++i)
                maxErr = std::max (maxErr, (double) std::abs (out.getSample (1, i) - in.getSample (1, i - L)));
            expectLessThan (maxErr, 1.0e-5, "bypass error");
        }

        beginTest ("Input / output gain");
        {
            Harness h;
            h.neutral();
            h.set (pid::inputGain, 6.0f);
            h.set (pid::outputGain, -12.0f);
            const auto out = h.run (h.sine (1000.0, 0.1f, 1.0));
            const float gain = rmsDb (out, 24000, 24000) - juce::Decibels::gainToDecibels (0.1f / std::sqrt (2.0f));
            expectWithinAbsoluteError (gain, -6.0f, 0.1f);
        }

        beginTest ("EQ: +12 dB mid bell at 1 kHz");
        {
            Harness h;
            h.neutral();
            h.set (pid::eqOn, 1.0f);
            h.set (pid::eqMix, 100.0f);
            h.set (pid::lowGain, 0.0f);
            h.set (pid::highGain, 0.0f);
            h.set (pid::midFreq, 1000.0f);
            h.set (pid::midQ, 1.0f);
            h.set (pid::midGain, 12.0f);
            const auto in = h.sine (1000.0, 0.05f, 1.0);
            const auto out = h.run (in);
            const float diff = rmsDb (out, 24000, 24000) - rmsDb (in, 24000, 24000);
            logMessage ("Bell gain measured: " + juce::String (diff, 2) + " dB");
            expectWithinAbsoluteError (diff, 12.0f, 0.3f);
        }

        beginTest ("EQ: 24 dB/oct low cut at 200 Hz removes 50 Hz rumble");
        {
            Harness h;
            h.neutral();
            h.set (pid::eqOn, 1.0f);
            h.set (pid::hpfOn, 1.0f);
            h.set (pid::hpfFreq, 200.0f);
            h.set (pid::hpfSlope, 1.0f);
            h.set (pid::midGain, 0.0f);
            const auto in = h.sine (50.0, 0.5f, 1.0);
            const auto out = h.run (in);
            const float diff = rmsDb (out, 24000, 24000) - rmsDb (in, 24000, 24000);
            logMessage ("50 Hz attenuation: " + juce::String (diff, 1) + " dB");
            expectLessThan (diff, -30.0f);
        }

        beginTest ("Compressor: static curve (thr -30 dB, 4:1)");
        {
            Harness h;
            h.neutral();
            h.set (pid::compOn, 1.0f);
            h.set (pid::compMix, 100.0f);
            h.set (pid::compThreshold, -30.0f);
            h.set (pid::compRatio, 4.0f);
            h.set (pid::compKnee, 0.0f);
            h.set (pid::compAttack, 1.0f);
            h.set (pid::compRelease, 200.0f);
            h.set (pid::compMakeup, 0.0f);
            const auto out = h.run (h.sine (1000.0, 0.5f, 1.5));
            const float outPeakDb = juce::Decibels::gainToDecibels (peakOf (out, 48000, 24000));
            // -6 dBFS in, 24 dB over threshold -> -30 + 24/4 = -24 dBFS
            logMessage ("Compressed peak: " + juce::String (outPeakDb, 2) + " dBFS (expected about -24)");
            expectWithinAbsoluteError (outPeakDb, -24.0f, 1.5f);
            const float gr = h.proc.getChain().getCompressor().gainReductionDb.pull();
            expectGreaterThan (gr, 15.0f, "gain reduction meter");
        }

        beginTest ("Compressor: signal below threshold untouched");
        {
            Harness h;
            h.neutral();
            h.set (pid::compOn, 1.0f);
            h.set (pid::compThreshold, -10.0f);
            h.set (pid::compKnee, 0.0f);
            h.set (pid::compMakeup, 0.0f);
            const auto in = h.sine (1000.0, 0.05f, 1.0);
            const auto out = h.run (in);
            expectWithinAbsoluteError (rmsDb (out, 24000, 24000) - rmsDb (in, 24000, 24000), 0.0f, 0.05f);
        }

        beginTest ("De-esser reduces sibilance band only");
        {
            const auto measure = [] (double freq)
            {
                Harness h;
                h.neutral();
                h.set (pid::deessOn, 1.0f);
                h.set (pid::deessFreq, 5000.0f);
                h.set (pid::deessThreshold, -40.0f);
                h.set (pid::deessRange, 12.0f);
                const auto in = h.sine (freq, 0.3f, 1.0);
                const auto out = h.run (in);
                return rmsDb (out, 24000, 24000) - rmsDb (in, 24000, 24000);
            };

            const float hi = measure (8000.0);
            const float lo = measure (200.0);
            logMessage ("8 kHz change: " + juce::String (hi, 2) + " dB, 200 Hz change: " + juce::String (lo, 2) + " dB");
            expectLessThan (hi, -8.0f, "sibilant tone must be reduced");
            expectWithinAbsoluteError (lo, 0.0f, 0.3f, "low tone must stay untouched");
        }

        beginTest ("Saturation adds harmonics");
        {
            Harness h;
            h.neutral();
            h.set (pid::satOn, 1.0f);
            h.set (pid::satDrive, 18.0f);
            h.set (pid::satMix, 100.0f);
            const auto out = h.run (h.sine (1000.0, 0.5f, 1.0));
            const double f1 = toneLevel (out, 24000, 24000, 1000.0, h.fs);
            const double h2 = toneLevel (out, 24000, 24000, 2000.0, h.fs);
            const double h3 = toneLevel (out, 24000, 24000, 3000.0, h.fs);
            const double thdDb = 20.0 * std::log10 (std::sqrt (h2 * h2 + h3 * h3) / f1);
            logMessage ("H2+H3 relative to fundamental: " + juce::String (thdDb, 1) + " dB");
            expectGreaterThan (thdDb, -30.0);
            expect (allFinite (out));
        }

        beginTest ("Delay: echo at the set time, dry signal present exactly once");
        {
            Harness h;
            h.neutral();
            h.set (pid::delayOn, 1.0f);
            h.set (pid::delayMix, 100.0f);
            h.set (pid::delayFeedback, 0.0f);
            h.set (pid::delayTime, 100.0f);
            h.set (pid::delayLowCut, 20.0f);
            h.set (pid::delayHighCut, 20000.0f);
            h.run (h.silence (0.5)); // let smoothing settle

            auto in = h.silence (0.5);
            for (int ch = 0; ch < in.getNumChannels(); ++ch)
                in.setSample (ch, 0, 0.5f);
            const auto out = h.run (in);
            const int L = h.proc.getLatencySamples();

            expectWithinAbsoluteError (out.getSample (0, L), 0.5f, 1.0e-4f, "dry impulse must not be doubled");

            const int expected = L + 4800;
            const float echo = peakOf (out, expected - 3, 7);
            const float elsewhere = peakOf (out, L + 10, 4700);
            logMessage ("Echo peak: " + juce::String (echo, 3) + ", stray energy: " + juce::String (elsewhere, 5));
            expectGreaterThan (echo, 0.3f);
            expectLessThan (elsewhere, 0.01f);
        }

        beginTest ("Delay and reverb at 0 % mix leave the dry signal unchanged");
        {
            Harness h;
            h.neutral();
            h.set (pid::delayOn, 1.0f);
            h.set (pid::delayMix, 0.0f);
            h.set (pid::reverbOn, 1.0f);
            h.set (pid::reverbMix, 0.0f);
            const auto in = h.noise (0.3f, 1.0, 3);
            const auto out = h.run (in);
            const int L = h.proc.getLatencySamples();
            double maxErr = 0.0;
            for (int i = 4800 + L; i < in.getNumSamples(); ++i)
                maxErr = std::max (maxErr, (double) std::abs (out.getSample (0, i) - in.getSample (0, i - L)));
            expectLessThan (maxErr, 1.0e-5);
        }

        beginTest ("Delay: maximum feedback stays bounded");
        {
            Harness h;
            h.neutral();
            h.set (pid::delayOn, 1.0f);
            h.set (pid::delayMix, 100.0f);
            h.set (pid::delayFeedback, 90.0f);
            h.set (pid::delayTime, 5.0f);
            h.set (pid::delayLowCut, 20.0f);
            h.set (pid::delayHighCut, 20000.0f);
            h.run (h.noise (1.0f, 1.0, 5));
            const auto out = h.run (h.silence (8.0));
            expect (allFinite (out));
            const float tail = peakOf (out, out.getNumSamples() - 4800, 4800);
            logMessage ("Delay tail after 8 s: " + juce::String (tail, 6));
            expectLessThan (tail, 0.01f, "repeats must die out");
        }

        beginTest ("Delay: tempo sync falls back to ms without host tempo");
        {
            Harness h;
            h.neutral();
            h.set (pid::delayOn, 1.0f);
            h.set (pid::delaySync, 1.0f);
            h.set (pid::delayTime, 123.0f);
            h.run (h.silence (0.1));
            expect (! h.proc.getChain().meters.tempoSynced.load());
            expectWithinAbsoluteError (h.proc.getChain().meters.effectiveDelayMs.load(), 123.0f, 0.01f);
            expectWithinAbsoluteError (dsp::DelayModule::timeForTempo (120.0, 9), 500.0f, 0.01f);   // 1/4 @ 120
            expectWithinAbsoluteError (dsp::DelayModule::timeForTempo (150.0, 7), 300.0f, 0.01f);   // 1/8D @ 150
        }

        beginTest ("Reverb: tail decays at roughly the set RT60");
        {
            Harness h;
            h.neutral();
            h.set (pid::reverbOn, 1.0f);
            h.set (pid::reverbMix, 100.0f);
            h.set (pid::reverbDecay, 2.0f);
            h.set (pid::reverbPreDelay, 0.0f);
            h.set (pid::reverbLowCut, 20.0f);
            h.set (pid::reverbDamping, 20000.0f);
            h.run (h.silence (0.5));

            auto in = h.noise (0.5f, 0.05, 11);
            juce::AudioBuffer<float> sig (2, (int) (3.0 * h.fs));
            sig.clear();
            for (int ch = 0; ch < 2; ++ch)
                sig.copyFrom (ch, 0, in, ch, 0, in.getNumSamples());
            const auto out = h.run (sig);

            const int w = 4800;
            const float e1 = rmsDb (out, (int) (0.2 * h.fs), w);
            const float e2 = rmsDb (out, (int) (1.2 * h.fs), w);
            logMessage ("Decay over 1 s: " + juce::String (e1 - e2, 1) + " dB (ideal 30 dB for RT60 = 2 s)");
            expectGreaterThan (e1 - e2, 18.0f);
            expectLessThan (e1 - e2, 45.0f);
            expect (std::abs (out.getSample (0, 30000) - out.getSample (1, 30000)) > 1.0e-6f, "stereo decorrelation");
        }

        beginTest ("Reverb: extreme settings stay bounded");
        {
            Harness h;
            h.neutral();
            h.set (pid::reverbOn, 1.0f);
            h.set (pid::reverbMix, 100.0f);
            h.set (pid::reverbDecay, 10.0f);
            h.set (pid::reverbSize, 100.0f);
            h.set (pid::reverbDamping, 20000.0f);
            const auto out = h.run (h.noise (1.0f, 5.0, 13));
            expect (allFinite (out));
            const float p = peakOf (out, 0, out.getNumSamples());
            logMessage ("Reverb peak with full-scale noise: " + juce::String (p, 3));
            expectLessThan (p, 4.0f);
        }

        beginTest ("Safety clip keeps output below 0 dBFS");
        {
            Harness h;
            h.neutral();
            h.set (pid::safetyClip, 1.0f);
            h.set (pid::outputGain, 12.0f);
            const auto out = h.run (h.sine (200.0, 0.9f, 0.5));
            expectLessOrEqual (peakOf (out, 0, out.getNumSamples()), 1.0f);
            expect (h.proc.getChain().meters.clipped.load(), "clip indicator should trigger");
            expectWithinAbsoluteError (dsp::VocalChain::safetyClip (0.5f), 0.5f, 1.0e-7f, "transparent below -1 dBFS");
        }

        beginTest ("NaN / Inf input is sanitised");
        {
            Harness h;
            for (auto& p : getFactoryPresets())
                if (p.name == "Wide Vocal")
                    p.applyTo (h.proc.getState());
            auto in = h.noise (0.3f, 0.5, 17);
            in.setSample (0, 100, std::numeric_limits<float>::quiet_NaN());
            in.setSample (1, 2000, std::numeric_limits<float>::infinity());
            in.setSample (0, 9000, -std::numeric_limits<float>::infinity());
            const auto out = h.run (in);
            expect (allFinite (out));
            const auto after = h.run (h.noise (0.3f, 0.5, 18));
            expect (allFinite (after));
            expectGreaterThan (peakOf (after, 0, after.getNumSamples()), 0.01f, "audio continues after bad input");
        }

        beginTest ("Sample rates and block sizes (incl. blocks larger than announced)");
        {
            for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
            {
                for (int bs : { 1, 32, 441, 512, 4096 })
                {
                    Harness h (fs, 512);
                    for (auto& p : getFactoryPresets())
                        if (p.name == "Hypertechno Lead")
                            p.applyTo (h.proc.getState());
                    const auto out = h.run (h.noise (0.3f, 0.25, 19), bs);
                    expect (allFinite (out), "fs " + juce::String (fs) + " bs " + juce::String (bs));
                    expectGreaterThan (peakOf (out, 0, out.getNumSamples()), 0.01f);
                }
            }
        }

        beginTest ("Mono layout");
        {
            Harness h (48000.0, 256, 1);
            for (auto& p : getFactoryPresets())
                if (p.name == "Wide Vocal")
                    p.applyTo (h.proc.getState());
            const auto out = h.run (h.noise (0.3f, 1.0, 23));
            expectEquals (out.getNumChannels(), 1);
            expect (allFinite (out));
            expectGreaterThan (peakOf (out, 0, out.getNumSamples()), 0.01f);
        }

        beginTest ("Every factory preset processes audio and differs from bypass");
        {
            for (const auto& preset : getFactoryPresets())
            {
                if (preset.name == "Init")
                    continue;
                Harness h;
                preset.applyTo (h.proc.getState());
                const auto in = h.noise (0.2f, 1.0, 29);
                const auto out = h.run (in);
                expect (allFinite (out), preset.name);
                const int L = h.proc.getLatencySamples();
                double diff = 0.0;
                for (int i = 24000; i < in.getNumSamples(); ++i)
                    diff += std::abs (out.getSample (0, i) - in.getSample (0, i - L));
                expectGreaterThan (diff, 1.0, preset.name + " should change the signal");
            }
        }
    }
};

//==============================================================================
class StateTests final : public juce::UnitTest
{
public:
    StateTests() : juce::UnitTest ("Velvox state & presets", "Velvox") {}

    void runTest() override
    {
        beginTest ("Factory presets are valid and complete");
        {
            const auto& presets = getFactoryPresets();
            expectGreaterOrEqual ((int) presets.size(), 6);
            for (const char* required : { "Clean Vocal", "Modern Pop", "Hypertechno Lead", "Aggressive Vocal", "Wide Vocal" })
            {
                bool found = false;
                for (auto& p : presets)
                    found = found || p.name == required;
                expect (found, juce::String ("missing preset ") + required);
            }
            for (auto& p : presets)
            {
                const auto problems = p.validate();
                expect (problems.isEmpty(), problems.joinIntoString ("; "));
            }
        }

        beginTest ("Loading a preset changes the processing parameters");
        {
            Harness h;
            auto& pm = h.proc.getPresetManager();
            int idx = -1;
            for (int i = 0; i < pm.getEntries().size(); ++i)
                if (pm.getEntries()[i].name == "Hypertechno Lead")
                    idx = i;
            expect (idx >= 0);
            expect (pm.loadPreset (idx));
            expectWithinAbsoluteError (h.get (pid::satDrive), 12.0f, 0.01f);
            expectWithinAbsoluteError (h.get (pid::hpfFreq), 150.0f, 0.01f);
            expectEquals ((int) h.get (pid::delayPingPong), 1);
            expectEquals (pm.getCurrentPresetName(), juce::String ("Hypertechno Lead"));

            expect (pm.loadPreset (0)); // Init resets to defaults
            expectWithinAbsoluteError (h.get (pid::satDrive), 6.0f, 0.01f);
            expectEquals ((int) h.get (pid::satOn), 0);
        }

        beginTest ("User preset save / load / delete round trip");
        {
            Harness h;
            const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                 .getChildFile ("VelvoxTestPresets_" + juce::String (juce::Random::getSystemRandom().nextInt (1000000)));
            PresetManager pm (h.proc.getState(), dir);

            h.set (pid::compThreshold, -42.0f);
            h.set (pid::reverbType, 0.0f);
            h.set (pid::delayDivision, 7.0f);
            expect (pm.saveUserPreset ("My Test"));
            expect (dir.getChildFile ("My Test" + PresetManager::fileExtension).existsAsFile());

            h.set (pid::compThreshold, -5.0f);
            h.set (pid::reverbType, 1.0f);
            h.set (pid::delayDivision, 2.0f);

            int idx = -1;
            for (int i = 0; i < pm.getEntries().size(); ++i)
                if (pm.getEntries()[i].name == "My Test" && ! pm.getEntries()[i].isFactory)
                    idx = i;
            expect (idx >= 0);
            expect (pm.loadPreset (idx));
            expectWithinAbsoluteError (h.get (pid::compThreshold), -42.0f, 0.01f);
            expectEquals ((int) h.get (pid::reverbType), 0);
            expectEquals ((int) h.get (pid::delayDivision), 7);

            expect (pm.deleteUserPreset (idx));
            expect (! dir.getChildFile ("My Test" + PresetManager::fileExtension).exists());
            dir.deleteRecursively();
        }

        beginTest ("Plugin state save / restore (DAW project)");
        {
            juce::MemoryBlock block;
            {
                Harness h;
                h.set (pid::midGain, 7.5f);
                h.set (pid::delaySync, 1.0f);
                h.set (pid::reverbDecay, 4.2f);
                h.set (pid::hpfSlope, 0.0f);
                h.proc.getState().state.setProperty (PresetManager::presetNameProperty, "Something", nullptr);
                h.proc.getStateInformation (block);
            }
            Harness h2;
            h2.proc.setStateInformation (block.getData(), (int) block.getSize());
            expectWithinAbsoluteError (h2.get (pid::midGain), 7.5f, 0.01f);
            expectEquals ((int) h2.get (pid::delaySync), 1);
            expectWithinAbsoluteError (h2.get (pid::reverbDecay), 4.2f, 0.01f);
            expectEquals ((int) h2.get (pid::hpfSlope), 0);
            expectEquals (h2.proc.getPresetManager().getCurrentPresetName(), juce::String ("Something"));

            // Garbage must not crash or change anything
            const char junk[] = "not a valid state";
            h2.proc.setStateInformation (junk, (int) sizeof (junk));
            expectWithinAbsoluteError (h2.get (pid::midGain), 7.5f, 0.01f);
        }

        beginTest ("A/B comparison");
        {
            Harness h;
            h.set (pid::compRatio, 2.0f);
            h.proc.selectSlot (1);              // B starts as a copy of A
            expectWithinAbsoluteError (h.get (pid::compRatio), 2.0f, 0.01f);
            h.set (pid::compRatio, 10.0f);      // edit B
            h.proc.selectSlot (0);
            expectWithinAbsoluteError (h.get (pid::compRatio), 2.0f, 0.01f);
            h.proc.selectSlot (1);
            expectWithinAbsoluteError (h.get (pid::compRatio), 10.0f, 0.01f);

            juce::MemoryBlock block;
            h.proc.getStateInformation (block);
            Harness h2;
            h2.proc.setStateInformation (block.getData(), (int) block.getSize());
            expectEquals (h2.proc.getActiveSlot(), 1);
            h2.proc.selectSlot (0);
            expectWithinAbsoluteError (h2.get (pid::compRatio), 2.0f, 0.01f);
        }

        beginTest ("Parameter schema for future mixing engine is valid JSON");
        {
            const auto json = juce::JSON::toString (createParameterSchema());
            const auto parsed = juce::JSON::parse (json);
            expect (parsed.isArray());
            expectEquals (parsed.size(), (int) getParameterSpecs().size());
        }

        beginTest ("All parameters are automatable and have unique IDs");
        {
            Harness h;
            juce::StringArray ids;
            for (auto* p : h.proc.getParameters())
            {
                expect (p->isAutomatable());
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                {
                    expect (! ids.contains (rp->getParameterID()), "duplicate " + rp->getParameterID());
                    ids.add (rp->getParameterID());
                }
            }
            expectEquals (ids.size(), (int) getParameterSpecs().size());
        }
    }
};

static ChainTests chainTests;
static StateTests stateTests;
