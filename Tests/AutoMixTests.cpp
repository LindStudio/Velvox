// Automatic mix: bounded capture, analysis, planning rules, verification by
// rendering through the real chain, the processor workflow (state, prompt,
// compare, restore) and the editor flow. Everything runs offline with
// deterministic synthetic vocals (Tests/SyntheticVocal.h).

#include "TestHelpers.h"
#include "SyntheticVocal.h"
#include "AutoMix/AutoMixEngine.h"
#include "PluginEditor.h"
#include "StateKeys.h"
#include "UI/UserSettings.h"
#include <thread>

using namespace velvox;
using namespace velvox::test;
using namespace velvox::automix;
using dsp::ModuleId;

namespace
{
    std::vector<const float*> ptrs (const CapturedAudio& a)
    {
        std::vector<const float*> p;
        for (auto& c : a.vocal)
            p.push_back (c.data());
        return p;
    }

    // Pushes a captured signal through a CaptureBuffer in host-sized blocks
    void feed (CaptureBuffer& cb, const CapturedAudio& a, int block = 512, bool playing = true)
    {
        const auto p = ptrs (a);
        for (int start = 0; start < a.numSamples(); start += block)
        {
            const int n = std::min (block, a.numSamples() - start);
            std::array<const float*, 2> ch { p[0] + start, p[std::min<size_t> (1, p.size() - 1)] + start };
            cb.push (ch.data(), a.channels, nullptr, 0, n, playing);
        }
    }

    std::map<juce::String, float> defaults()
    {
        std::map<juce::String, float> v;
        for (auto& s : getParameterSpecs())
            v[s.id] = s.defaultValue;
        return v;
    }

    VelvoxAudioProcessorEditor& asVelvox (juce::AudioProcessorEditor& e) { return dynamic_cast<VelvoxAudioProcessorEditor&> (e); }

    // Button clicks are delivered asynchronously: click and let the message loop run
    // long enough that a busy loop (editor animations, first paints) still delivers it
    void click (juce::Button& b)
    {
        b.triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (150);
    }

    // Feeds the processor with audio until the capture is complete, then waits for the worker
    bool runWorkflow (Harness& h, const CapturedAudio& a, double timeoutSeconds = 90.0, const CapturedAudio* beat = nullptr)
    {
        auto& am = h.proc.getAutoMix();
        const int mainCh = h.proc.getMainBusNumInputChannels();
        const int totalCh = std::max (h.proc.getTotalNumInputChannels(), h.proc.getTotalNumOutputChannels());
        juce::AudioBuffer<float> buffer (totalCh, h.blockSize);
        juce::MidiBuffer midi;
        for (int start = 0; start < a.numSamples() && am.getPhase() != AutoMixController::Phase::analysing; start += h.blockSize)
        {
            const int n = std::min (h.blockSize, a.numSamples() - start);
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), totalCh, n);
            view.clear();
            for (int ch = 0; ch < mainCh; ++ch)
                view.copyFrom (ch, 0, a.vocal[(size_t) std::min (ch, a.channels - 1)].data() + start, n);
            if (beat != nullptr)
                for (int ch = mainCh; ch < totalCh; ++ch)
                    view.copyFrom (ch, 0, beat->vocal[(size_t) std::min (ch - mainCh, beat->channels - 1)].data() + start, n);
            h.proc.processBlock (view, midi);
            if ((start / h.blockSize) % 32 == 0)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
        }
        const auto until = juce::Time::getMillisecondCounterHiRes() + timeoutSeconds * 1000.0;
        while (juce::Time::getMillisecondCounterHiRes() < until)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
            // audio keeps running during the analysis
            buffer.clear();
            h.proc.processBlock (buffer, midi);
            const auto ph = am.getPhase();
            if (ph == AutoMixController::Phase::done || ph == AutoMixController::Phase::failed)
                return ph == AutoMixController::Phase::done;
        }
        return false;
    }
}

class AutoMixTests final : public juce::UnitTest
{
public:
    AutoMixTests() : juce::UnitTest ("Velvox automatic mix", "Velvox") {}

    void runTest() override
    {
        captureTests();
        profilerTests();
        plannerTests();
        engineTests();
        optimizerTests();
        workflowTests();
        editorTests();
    }

    //==========================================================================
    void captureTests()
    {
        beginTest ("Capture: waits for signal, ignores silence, stops by itself at the target");
        {
            CaptureBuffer cb;
            CaptureConfig cfg;
            cfg.targetVoicedSeconds = 3.0f;
            cfg.maxSeconds = 8.0f;
            cfg.minVoicedSeconds = 1.0f;
            cb.arm (48000.0, 1, cfg);
            expect (cb.getState() == CaptureBuffer::State::waiting);

            CapturedAudio silence;
            silence.sampleRate = 48000.0;
            silence.channels = 1;
            silence.vocal.push_back (std::vector<float> (48000 * 2, 0.0f));
            feed (cb, silence);
            expect (cb.getState() == CaptureBuffer::State::waiting, "silence does not start the capture");
            expectEquals (cb.getProgress().capturedSeconds, 0.0f);

            VocalSpec s; s.seconds = 8.0;
            const auto voice = makeVocal (s);
            feed (cb, voice);
            const auto p = cb.getProgress();
            expect (cb.getState() == CaptureBuffer::State::complete, "stops when enough voice was heard");
            expect (p.voicedSeconds >= 3.0f && p.voicedSeconds < 3.2f, "voiced " + juce::String (p.voicedSeconds));
            expect (p.capturedSeconds <= 8.0f);

            // Further audio changes nothing: the buffer never grows
            const float before = cb.getProgress().capturedSeconds;
            feed (cb, voice);
            expectEquals (cb.getProgress().capturedSeconds, before);
            const auto out = cb.copyOut();
            expectEquals (out.channels, 1);
            expect (out.numSamples() <= cb.capacitySamples());
        }

        beginTest ("Capture: fixed capacity at every sample rate, also above 96 kHz");
        {
            for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
            {
                const int cap = CaptureBuffer::capacityFor (fs, 24.0f);
                expect (cap <= (int) (96000.0 * 24.0) + 1, "bounded at " + juce::String (fs));
                expect (cap >= (int) (fs * 11.9), "at least ~12 s at " + juce::String (fs));
            }
        }

        beginTest ("Capture: digital silence (stopped transport) is skipped, the stop is reported");
        {
            CaptureBuffer cb;
            CaptureConfig cfg;
            cfg.targetVoicedSeconds = 6.0f;
            cfg.minVoicedSeconds = 2.0f;
            cb.arm (48000.0, 2, cfg);
            VocalSpec s; s.seconds = 3.0; s.channels = 2;
            const auto voice = makeVocal (s);
            feed (cb, voice, 512, true);
            const float captured = cb.getProgress().capturedSeconds;

            CapturedAudio silence;
            silence.sampleRate = 48000.0;
            silence.channels = 2;
            silence.vocal.assign (2, std::vector<float> (48000 * 3, 0.0f));
            feed (cb, silence, 512, false);
            const auto p = cb.getProgress();
            expectEquals (p.capturedSeconds, captured, "silence is not stored");
            expect (p.transportStopped, "stop reported");
            expect (cb.getState() == CaptureBuffer::State::capturing, "not complete with too little voice");

            // The user analyses what is there (above the minimum)
            expect (p.enough());
            expect (cb.finishNow());
            const auto out = cb.copyOut();
            expectEquals (out.channels, 2);
            expect (out.stoppedEarly);
        }

        beginTest ("Capture: stereo channels stay separate, clipping is counted");
        {
            CaptureBuffer cb;
            CaptureConfig cfg;
            cfg.targetVoicedSeconds = 2.0f;
            cfg.minVoicedSeconds = 1.0f;
            cb.arm (48000.0, 2, cfg);
            VocalSpec s; s.seconds = 4.0; s.channels = 2; s.stereoDifference = 0.5f; s.clipGain = 3.0f;
            const auto voice = makeVocal (s);
            feed (cb, voice);
            const auto out = cb.copyOut();
            double diff = 0.0;
            for (int i = 0; i < out.numSamples(); ++i)
                diff += std::abs (out.vocal[0][(size_t) i] - out.vocal[1][(size_t) i]);
            expect (diff > 1.0, "left and right differ (not summed)");
            expect (cb.getProgress().clipSamples > 0, "clipping counted");
        }

        beginTest ("Capture: re-arming and cancelling while the audio thread writes is safe");
        {
            CaptureBuffer cb;
            CaptureConfig cfg;
            cfg.targetVoicedSeconds = 30.0f;
            cb.arm (48000.0, 2, cfg);
            std::atomic<bool> run { true };
            std::vector<float> noise (512);
            juce::Random r (5);
            for (auto& x : noise) x = 0.3f * (r.nextFloat() * 2.0f - 1.0f);
            std::thread audio ([&]
            {
                const float* ch[2] { noise.data(), noise.data() };
                while (run.load())
                    cb.push (ch, 2, nullptr, 0, 512, true);
            });
            for (int i = 0; i < 200; ++i)
            {
                cb.arm (i % 2 == 0 ? 48000.0 : 44100.0, 2, cfg);
                if (i % 3 == 0)
                    cb.cancel();
            }
            run.store (false);
            audio.join();
            expect (cb.getProgress().capturedSeconds <= cfg.maxSeconds + 0.1f);
        }
    }

    //==========================================================================
    void profilerTests()
    {
        beginTest ("Analysis: loudness (BS.1770 gating) and loudness range are measured correctly");
        {
            CapturedAudio a;
            a.sampleRate = 48000.0;
            a.channels = 1;
            std::vector<float> sine (48000 * 10);
            for (size_t i = 0; i < sine.size(); ++i)
                sine[i] = 0.1f * (float) std::sin (2.0 * pi * 997.0 * (double) i / 48000.0);
            a.vocal.push_back (sine);
            const float l1 = VocalProfiler::integratedLufs (ptrs (a), a.numSamples(), 48000.0);
            for (auto& x : a.vocal[0]) x *= 2.0f;
            const float l2 = VocalProfiler::integratedLufs (ptrs (a), a.numSamples(), 48000.0);
            expectWithinAbsoluteError (l2 - l1, 6.02f, 0.05f);

            // Adding silence does not change the gated loudness
            a.vocal[0].insert (a.vocal[0].end(), (size_t) 48000 * 5, 0.0f);
            const float l3 = VocalProfiler::integratedLufs (ptrs (a), a.numSamples(), 48000.0);
            expectWithinAbsoluteError (l3, l2, 0.1f);

            // Two levels 20 dB apart -> loudness range close to 20 LU
            CapturedAudio b;
            b.sampleRate = 48000.0;
            b.channels = 1;
            std::vector<float> two;
            for (int seg = 0; seg < 6; ++seg)
                for (int i = 0; i < 48000 * 5; ++i)
                    two.push_back ((seg % 2 == 0 ? 0.3f : 0.03f) * (float) std::sin (2.0 * pi * 500.0 * i / 48000.0));
            b.vocal.push_back (two);
            const auto lra = VocalProfiler::loudnessRange (ptrs (b), b.numSamples(), 48000.0);
            expect (lra.has_value());
            expectWithinAbsoluteError (*lra, 20.0f, 2.0f);
        }

        beginTest ("Analysis: pitch of low and high voices");
        {
            for (float f0 : { 110.0f, 330.0f })
            {
                VocalSpec s; s.f0 = f0; s.seconds = 12.0;
                const auto p = VocalProfiler::analyse (makeVocal (s));
                expect (p.pitchValid, "pitch found at " + juce::String (f0));
                expect (std::abs (p.f0MedianHz / f0 - 1.0f) < 0.08f, "median " + juce::String (p.f0MedianHz) + " for " + juce::String (f0));
                expect (p.f0LowHz < p.f0MedianHz && p.f0HighHz > p.f0MedianHz);
            }
        }

        beginTest ("Analysis: problems are detected only when present");
        {
            VocalSpec clean; clean.seconds = 14.0;
            const auto c = VocalProfiler::analyse (makeVocal (clean));
            expect (c.enoughMaterial);
            expect (! c.hasIssue ("rumble") && ! c.hasIssue ("clipping") && ! c.hasIssue ("noise") && ! c.reverberant,
                    "clean recording: no rumble, clipping, noise or room");

            VocalSpec rumble = clean; rumble.rumbleDb = -24.0f;
            expect (VocalProfiler::analyse (makeVocal (rumble)).hasIssue ("rumble"), "rumble");

            VocalSpec sib = clean; sib.sibilance = 1.6f;
            const auto ps = VocalProfiler::analyse (makeVocal (sib));
            expect (ps.sibilanceValid && ps.hasIssue ("sibilance"), "strong S sounds: " + juce::String (ps.sibilanceVsVowelDb));
            expect (ps.sibilanceHz > 4000.0f, "S centre above 4 kHz");
            VocalSpec soft = clean; soft.sibilance = 0.05f;
            expect (! VocalProfiler::analyse (makeVocal (soft)).hasIssue ("sibilance"), "soft S sounds are not flagged");

            VocalSpec dark = clean; dark.tiltDb = -5.0f;
            VocalSpec bright = clean; bright.tiltDb = 4.0f;
            const auto pd = VocalProfiler::analyse (makeVocal (dark)), pb = VocalProfiler::analyse (makeVocal (bright));
            expect (pd.topDb < pb.topDb - 6.0f, "dark has less top end than bright");
            expect (pd.hasIssue ("dull"), "dark voice flagged dull: " + juce::String (pd.topDb));
            expect (! pb.hasIssue ("dull"), "bright voice never dull");

            VocalSpec res = clean; res.resonanceHz = 2500.0f; res.resonanceDb = 16.0f;
            const auto pr = VocalProfiler::analyse (makeVocal (res));
            bool found = false;
            for (auto& r : pr.resonances)
                found = found || std::abs (std::log2 (r.hz / 2500.0f)) < 0.17f;
            expect (found, "resonance at 2.5 kHz found");

            VocalSpec room = clean; room.roomSeconds = 2.0f; room.roomMix = 0.6f;
            const auto pm = VocalProfiler::analyse (makeVocal (room));
            expect (pm.reverberant, "room detected, tail " + juce::String (pm.tailSeconds));

            VocalSpec noisy = clean; noisy.noiseDb = -46.0f;
            const auto pn = VocalProfiler::analyse (makeVocal (noisy));
            expect (pn.snrValid && pn.hasIssue ("noise"), "noise floor detected, snr " + juce::String (pn.snrDb));

            VocalSpec clip = clean; clip.clipGain = 6.0f;
            const auto pc = VocalProfiler::analyse (makeVocal (clip));
            expect (pc.clipRuns > 0 && pc.hasIssue ("clipping"), "clipping");

            VocalSpec comp = clean; comp.compress = 1.0f; comp.phraseRangeDb = 0.0f;
            expect (VocalProfiler::analyse (makeVocal (comp)).heavilyCompressed, "heavily compressed");

            VocalSpec quiet = clean; quiet.levelDb = -46.0f;
            expect (VocalProfiler::analyse (makeVocal (quiet)).hasIssue ("quiet"), "very quiet");

            VocalSpec st = clean; st.channels = 2; st.stereoDifference = 0.6f;
            const auto pst = VocalProfiler::analyse (makeVocal (st));
            expect (pst.stereoContent && pst.correlation < 0.95f, "stereo content");
            VocalSpec mono2 = clean; mono2.channels = 2;
            expect (! VocalProfiler::analyse (makeVocal (mono2)).stereoContent, "identical channels are not stereo");

            VocalSpec shortOne = clean; shortOne.seconds = 3.0;
            expect (! VocalProfiler::analyse (makeVocal (shortOne)).enoughMaterial, "3 s is not enough");
        }
    }

    //==========================================================================
    void plannerTests()
    {
        const auto planFor = [] (const VocalSpec& s) { return AutoMixPlanner::plan (VocalProfiler::analyse (makeVocal (s)), defaults()); };

        beginTest ("Planner: every value is legal and creative effects stay off");
        {
            VocalSpec s; s.seconds = 14.0;
            const auto plan = planFor (s);
            for (auto& [id, v] : plan.values)
            {
                const auto* spec = findParameterSpec (id);
                expect (spec != nullptr, "known id " + id);
                if (spec != nullptr)
                    expect (v >= spec->minValue - 1.0e-4f && v <= spec->maxValue + 1.0e-4f, id + " in range");
            }
            for (auto* id : { pid::pitchOn, pid::chopOn, pid::pulseOn, pid::choirOn, pid::macroPresence, pid::macroSpace })
                expectEquals (plan.get (id, -1.0f), 0.0f, id);
            expect (plan.routing.isValid());

            // Every decision belongs to a module the plan really switches on
            const auto active = plan.activeModules();
            for (auto& d : plan.decisions)
                expect (d.outputStage || active.count (d.module) > 0, "decision for an active module: " + d.title);
        }

        beginTest ("Planner: the low cut follows the voice, not a fixed frequency");
        {
            VocalSpec low; low.f0 = 105.0f; low.seconds = 14.0;
            VocalSpec high; high.f0 = 320.0f; high.seconds = 14.0;
            const auto pl = planFor (low), ph = planFor (high);
            expect (pl.get (pid::hpfFreq) < ph.get (pid::hpfFreq) - 40.0f,
                    "male " + juce::String (pl.get (pid::hpfFreq)) + " Hz vs female " + juce::String (ph.get (pid::hpfFreq)) + " Hz");
            expect (pl.get (pid::hpfFreq) < 0.85f * 105.0f * std::pow (2.0f, -5.0f / 12.0f) + 5.0f, "below the lowest sung notes");
        }

        beginTest ("Planner: tone decisions depend on the material");
        {
            VocalSpec dark; dark.tiltDb = -5.0f; dark.seconds = 14.0;
            VocalSpec bright; bright.tiltDb = 4.0f; bright.seconds = 14.0;
            const auto pd = planFor (dark), pb = planFor (bright);
            expect (pd.get (pid::highGain) > 1.0f, "dark voice gets more highs");
            expectEquals (pb.get (pid::highGain), 0.0f, "bright voice gets no extra highs");

            VocalSpec sib; sib.sibilance = 1.6f; sib.seconds = 14.0;
            VocalSpec soft; soft.sibilance = 0.05f; soft.seconds = 14.0;
            expect (planFor (sib).on (pid::deessOn), "S sounds -> de-esser");
            expect (! planFor (soft).on (pid::deessOn), "no S problem -> no de-esser");

            VocalSpec room; room.roomSeconds = 2.0f; room.roomMix = 0.6f; room.seconds = 14.0;
            VocalSpec dry; dry.seconds = 14.0;
            const auto pr = planFor (room), pdry = planFor (dry);
            expect (pr.get (pid::reverbMix) * pr.get (pid::reverbOn) < pdry.get (pid::reverbMix) * pdry.get (pid::reverbOn),
                    "roomy recording gets less (or no) reverb");

            VocalSpec comp; comp.compress = 1.0f; comp.phraseRangeDb = 0.0f; comp.seconds = 14.0;
            const auto pc = planFor (comp);
            expect (! pc.on (pid::smashOn) && ! pc.on (pid::rideOn), "no parallel / ride on compressed material");
            expect (pc.get (pid::compRatio) < 2.5f, "gentle compression only");

            VocalSpec clip; clip.clipGain = 6.0f; clip.seconds = 14.0;
            expect (! planFor (clip).on (pid::satOn), "no saturation on clipped material");

            VocalSpec noisy; noisy.noiseDb = -46.0f; noisy.seconds = 14.0; noisy.phraseRangeDb = 14.0f;
            const auto pn = planFor (noisy);
            if (pn.on (pid::rideOn))
                expect (pn.get (pid::rideMaxBoost) <= 3.0f, "ride boost limited with noise");
            expect (pn.get (pid::highGain) <= 2.0f, "high boost limited with noise");

            VocalSpec res; res.resonanceHz = 3000.0f; res.resonanceDb = 16.0f; res.seconds = 14.0;
            expect (planFor (res).on (pid::smoothOn), "upper resonance -> SMOOTH");

            VocalSpec shortOne; shortOne.seconds = 6.0;
            const auto ps = planFor (shortOne);
            expect (ps.conservative, "little material -> conservative");
            expect (! ps.on (pid::delayOn) && ! ps.on (pid::doublerOn), "no extras on little material");

            // Reverb character follows the style: rap stays in a short room, sung lines get a
            // plate, long sustained melodies the Large Hall of the 0.4 reverb engine
            {
                VocalSpec styled; styled.seconds = 14.0;
                auto prof = VocalProfiler::analyse (makeVocal (styled));
                prof.reverberant = false;
                const auto typeFor = [&] (float pitchedShare, float spread)
                {
                    auto p = prof;
                    p.pitchedShare = pitchedShare;
                    p.pitchSpreadSemitones = spread;
                    const auto plan = AutoMixPlanner::plan (p, defaults());
                    expect (plan.on (pid::reverbOn), "reverb planned");
                    return (int) plan.get (pid::reverbType);
                };
                expectEquals (typeFor (0.30f, 1.0f), 0, "spoken -> Room");
                expectEquals (typeFor (0.60f, 3.0f), 1, "sung -> Plate");
                expectEquals (typeFor (0.90f, 5.0f), 3, "sustained -> Large Hall");
            }

            // Level Ride also on a wet, steady hook (LRA 1 LU): in the blind listening test of
            // 2026-10-24 the ridden version was the more intelligible one. The ride lifts quiet
            // syllables into the next onset, so the compressor behind it has to be fast.
            {
                VocalSpec swings; swings.seconds = 14.0; swings.phraseRangeDb = 14.0f;
                auto prof = VocalProfiler::analyse (makeVocal (swings));
                expect (AutoMixPlanner::plan (prof, defaults()).on (pid::rideOn), "real phrase swings -> level ride");
                prof.loudnessRangeValid = true;
                prof.loudnessRangeDb = 1.0f;
                const auto wet = AutoMixPlanner::plan (prof, defaults());
                expect (wet.on (pid::rideOn), "steady loudness (LRA 1 LU) keeps the level ride");
                expect (wet.get (pid::compAttack) <= 5.0f, "ride -> fast compressor attack: " + juce::String (wet.get (pid::compAttack), 1) + " ms");
            }

            // Different recordings -> different configurations
            int differences = 0;
            for (auto& [id, v] : pd.values)
                if (std::abs (v - pb.get (id, v)) > 1.0e-3f)
                    ++differences;
            expect (differences >= 3, "dark and bright plans differ in " + juce::String (differences) + " values");
        }
    }

    //==========================================================================
    void engineTests()
    {
        struct Case { const char* name; VocalSpec spec; };
        std::vector<Case> cases;
        { VocalSpec s; s.seconds = 12.0; cases.push_back ({ "clean dry", s }); }
        { VocalSpec s; s.seconds = 12.0; s.tiltDb = -5.0f; s.f0 = 120.0f; cases.push_back ({ "dark low", s }); }
        { VocalSpec s; s.seconds = 12.0; s.tiltDb = 4.0f; s.sibilance = 1.6f; s.f0 = 300.0f; cases.push_back ({ "bright sibilant", s }); }
        { VocalSpec s; s.seconds = 12.0; s.levelDb = -40.0f; cases.push_back ({ "very quiet", s }); }
        { VocalSpec s; s.seconds = 12.0; s.levelDb = -9.0f; s.phraseRangeDb = 14.0f; cases.push_back ({ "loud with high peaks", s }); }
        { VocalSpec s; s.seconds = 12.0; s.roomSeconds = 2.0f; s.roomMix = 0.6f; cases.push_back ({ "reverberant", s }); }
        { VocalSpec s; s.seconds = 12.0; s.channels = 2; s.stereoDifference = 0.5f; s.rumbleDb = -26.0f; cases.push_back ({ "stereo with rumble", s }); }
        { VocalSpec s; s.seconds = 12.0; s.compress = 1.0f; s.phraseRangeDb = 0.0f; cases.push_back ({ "heavily compressed", s }); }

        for (auto& c : cases)
        {
            beginTest (juce::String ("Engine (render + verify): ") + c.name);
            const auto audio = makeVocal (c.spec);
            const auto r = AutoMixEngine::run (audio, defaults());
            expect (r.ok, r.error);
            if (! r.ok)
                continue;

            const auto& out = r.processed;
            logMessage (juce::String ("    ") + c.name + ": in " + juce::String (r.original.lufs, 1) + " LUFS -> out " + juce::String (out.lufs, 1)
                        + " LUFS, peak " + juce::String (out.peakDb, 1) + " dBFS, comp GR " + juce::String (out.compGrMedianDb, 1)
                        + " dB, top " + juce::String (r.original.topDb, 1) + " -> " + juce::String (out.topDb, 1)
                        + " dB, rounds " + juce::String (r.iterations) + ", modules " + juce::String ((int) r.plan.activeModules().size()));

            expect (out.peakDb <= -0.9f, "headroom: peak " + juce::String (out.peakDb));
            const float target = AutoMixEngine::targetLufs (r.original.lufs);
            expect (out.lufs <= target + 1.0f, "not louder than the target (fair compare): " + juce::String (out.lufs));
            expect (out.lufs >= target - 6.0f, "close to the target: " + juce::String (out.lufs));
            expect (out.compGrMedianDb <= 7.0f, "no over-compression: " + juce::String (out.compGrMedianDb));
            expect (out.topDb <= r.original.topDb + 6.0f, "no excessive top-end boost");
            expect (out.bodyDb >= r.original.bodyDb - 9.0f, "no unnatural loss of body");
            // S sounds: never pushed above a natural level (about -8 dB under the vowels),
            // and never more than slightly above where they were
            if (r.profile.sibilanceValid && out.sibilanceValid)
                expect (out.sibilanceVsVowelDb <= std::max (r.profile.sibilanceVsVowelDb + 2.5f, -8.0f),
                        "S sounds controlled: " + juce::String (r.profile.sibilanceVsVowelDb, 1) + " -> " + juce::String (out.sibilanceVsVowelDb, 1));
            expect (out.monoLossDb > -2.0f, "mono compatible: " + juce::String (out.monoLossDb));
            expect (r.plan.decisions.size() >= 3, "a real plan");
            if (juce::String (c.name) == "reverberant")
                expect (! r.plan.on (pid::delayOn), "no delay on a reverberant recording");
        }

        beginTest ("Engine: too little voice is refused, not mixed");
        {
            CapturedAudio a;
            a.sampleRate = 48000.0;
            a.channels = 1;
            a.vocal.push_back (std::vector<float> (48000 * 4, 0.0f));
            const auto r = AutoMixEngine::run (a, defaults());
            expect (! r.ok && r.error.isNotEmpty());
        }

        beginTest ("Engine: the offline render uses exactly the plugin's parameter mapping");
        {
            Harness h (48000.0, 512, 2);
            VocalSpec s; s.seconds = 3.0; s.channels = 2;
            const auto audio = makeVocal (s);
            auto values = defaults();
            values[pid::compRatio] = 6.0f;
            values[pid::satOn] = 1.0f;
            values[pid::reverbOn] = 0.0f;
            values[pid::safetyClip] = 0.0f;
            for (auto& [id, v] : values)
                h.set (id.toRawUTF8(), v);
            CapturedAudio rendered;
            AutoMixEngine::render (audio, values, {}, h.proc.getRouting(), &rendered);

            juce::AudioBuffer<float> in (2, audio.numSamples());
            for (int ch = 0; ch < 2; ++ch)
                in.copyFrom (ch, 0, audio.vocal[(size_t) ch].data(), audio.numSamples());
            const auto out = h.run (in);
            float maxDiff = 0.0f;
            for (int i = 0; i < out.getNumSamples(); ++i)
                maxDiff = std::max (maxDiff, std::abs (out.getSample (0, i) - rendered.vocal[0][(size_t) i]));
            expect (maxDiff < 1.0e-4f, "plugin and offline render agree (max diff " + juce::String (maxDiff) + ")");
        }
    }

    //==========================================================================
    // The candidate search of 0.4 (docs/AUTOMIX_OBJECTIVE.md)
    void optimizerTests()
    {
        const auto profileFor = [] (const VocalSpec& s) { return VocalProfiler::analyse (makeVocal (s)); };

        beginTest ("Optimizer: every candidate is a legal, bounded and deterministic chain");
        {
            std::vector<VocalSpec> specs;
            { VocalSpec s; s.seconds = 12.0; specs.push_back (s); }
            { VocalSpec s; s.seconds = 12.0; s.tiltDb = -5.0f; s.f0 = 120.0f; specs.push_back (s); }
            { VocalSpec s; s.seconds = 12.0; s.tiltDb = 4.0f; s.sibilance = 1.6f; s.f0 = 300.0f; specs.push_back (s); }
            { VocalSpec s; s.seconds = 12.0; s.compress = 1.0f; s.phraseRangeDb = 0.0f; specs.push_back (s); }

            for (auto& s : specs)
            {
                const auto profile = profileFor (s);
                const auto base = AutoMixPlanner::plan (profile, defaults());
                const auto cands = AutoMixEngine::candidatesFor (base, profile);

                expect (! cands.empty() && cands.front().second.values == base.values, "index 0 is the base plan");
                expect (cands.size() <= 9, "bounded: " + juce::String ((int) cands.size()) + " candidates");

                juce::StringArray names;
                for (auto& [name, plan] : cands)
                {
                    expect (! names.contains (name), "unique name " + name);
                    names.add (name);
                    expect (plan.routing.isValid(), "valid routing: " + name);
                    for (auto& [id, v] : plan.values)
                    {
                        const auto* spec = findParameterSpec (dsp::splitInstanceParamId (id).first);
                        expect (spec != nullptr, "known id " + id);
                        if (spec != nullptr)
                            expect (v >= spec->minValue - 1.0e-4f && v <= spec->maxValue + 1.0e-4f, name + ": " + id + " in range");
                    }
                    // Creative effects stay off in every variant, as in the base plan
                    for (auto* id : { pid::pitchOn, pid::chopOn, pid::pulseOn, pid::choirOn })
                        expectEquals (plan.get (id, 0.0f), 0.0f, name + ": " + id);
                }

                const auto again = AutoMixEngine::candidatesFor (base, profile);
                bool same = again.size() == cands.size();
                for (size_t i = 0; same && i < cands.size(); ++i)
                    same = again[i].first == cands[i].first && again[i].second.values == cands[i].second.values
                        && again[i].second.routing == cands[i].second.routing;
                expect (same, "deterministic");
            }
        }

        beginTest ("Optimizer: a variant has to earn the change; problems cost");
        {
            VocalSpec s; s.seconds = 12.0;
            const auto profile = profileFor (s);
            RenderMetrics m;
            m.valid = true;
            m.lufs = -18.0f; m.peakDb = -6.0f;
            m.bodyDb = -4.0f; m.lowMidDb = -3.0f; m.topDb = -11.0f; m.airDb = -24.0f; m.presenceDb = -8.0f;
            m.dynamicsDb = 6.0f; m.dynamicsValid = true; m.crestDb = 12.0f;
            m.correlation = 0.95f; m.monoLossDb = 0.0f;
            const auto original = m;

            const auto base = evaluate (m, original, profile, -18.0f, true, false);
            const auto variant = evaluate (m, original, profile, -18.0f, false, false);
            expect (base.total() < 0.01f, "a balanced render costs nothing: " + base.describe());
            expect (variant.total() > base.total(), "the same result is not worth a change");

            auto squashed = m;
            squashed.crestDb = 4.0f;
            expect (evaluate (squashed, original, profile, -18.0f, true, false).dynamics > 1.0f, "over-compression costs");

            auto harsh = m;
            harsh.presenceDb = 0.0f;
            expect (evaluate (harsh, original, profile, -18.0f, true, false).harshness > 1.0f, "harshness costs");

            // The render is float and runs before the output stage, which always turns the mix
            // down to -1 dBFS peak: samples over full scale in the render never clip in the
            // result. Their real cost is the loudness lost, which peak growth covers.
            auto hot = m;
            hot.lufs = -10.0f; hot.peakDb = 2.0f; hot.clipRuns = 12;   // same peak-to-loudness as m
            expect (evaluate (hot, original, profile, -18.0f, true, false).artifacts < 0.01f, "float overs before the output stage are free");
            auto hotAtTarget = hot;
            hotAtTarget.lufs = -18.0f; hotAtTarget.peakDb = 4.0f;   // +4 dBFS even at the target loudness
            const float hotCost = evaluate (hotAtTarget, original, profile, -18.0f, true, false).artifacts;
            expect (hotCost > 0.5f && hotCost < 10.0f, "overs at the target loudness cost as lost loudness, not as clipping: " + juce::String (hotCost, 2));

            // Headroom: a dynamic take (peak 15.6 dB over its loudness) is only turned down a
            // little by the output stage - that is not an artifact. A de-esser that takes the
            // S energy out (loudness -0.8 dB, vowel peaks unchanged) must not be punished.
            auto dynamicTake = m;
            dynamicTake.lufs = -14.0f; dynamicTake.peakDb = 1.6f;
            const auto dynOriginal = dynamicTake;
            const float headroomCost = evaluate (dynamicTake, dynOriginal, profile, -14.0f, true, false).artifacts;
            expect (headroomCost < 1.0f, "a dynamic take's headroom is a small cost: " + juce::String (headroomCost, 2));
            auto deEssed = dynamicTake;
            deEssed.lufs -= 0.8f;
            expect (evaluate (deEssed, dynOriginal, profile, -14.0f, true, false).artifacts - headroomCost < 0.5f,
                    "removing S energy is not punished as an artifact");

            // ... a chain that makes the peaks stick out further than in the original costs - mildly,
            // since at matched loudness the listening test did not hear onset peaks
            auto spiky = dynamicTake;
            spiky.peakDb += 4.0f;
            const float spikyCost = evaluate (spiky, dynOriginal, profile, -14.0f, true, false).artifacts;
            expect (spikyCost > headroomCost + 1.0f && spikyCost < 6.0f, "peaks sticking out cost a little: " + juce::String (spikyCost, 2));

            // Listening test 2026-10-24: dense and upfront beat loose - a render with 4 dB phrase
            // variation and 7 dB crest must not score worse than one with 8 dB and 12 dB
            auto dense = m, loose = m;
            dense.dynamicsDb = 4.0f; dense.crestDb = 7.0f;
            loose.dynamicsDb = 8.0f; loose.crestDb = 12.0f;
            expect (evaluate (dense, original, profile, -18.0f, true, false).total()
                        < evaluate (loose, original, profile, -18.0f, true, false).total(), "dense beats loose");

            // A high voice (lowest notes above 250 Hz) has no fundamentals in the 100-300 Hz body
            // band: "no body" there is physics, not a mixing problem (VocalSet male9: -42 dB)
            auto highVoice = profile;
            highVoice.pitchValid = true;
            highVoice.f0LowHz = 360.0f;
            auto thinBody = m;
            thinBody.bodyDb = -40.0f;
            expect (evaluate (thinBody, original, highVoice, -18.0f, true, false).tonal < 0.01f, "high voice: empty body band costs nothing");
            auto lowVoice = highVoice;
            lowVoice.f0LowHz = 110.0f;
            expect (evaluate (thinBody, original, lowVoice, -18.0f, true, false).tonal > 1.0f, "low voice: empty body band costs");

            auto louder = m;
            louder.lufs = -12.0f;
            louder.peakDb = -6.0f + 6.0f;
            expect (std::abs (evaluate (louder, original, profile, -18.0f, true, false).total()
                              - evaluate (m, original, profile, -18.0f, true, false).total()) < 1.0e-4f,
                    "loudness alone is never rewarded");

            RenderMetrics invalid;
            expect (evaluate (invalid, original, profile, -18.0f, true, false).total() > 1000.0f, "a failed render never wins");
        }

        beginTest ("Optimizer: a bright take gets a softer-highs variant, a dark one never");
        {
            const auto softer = [] (const std::vector<std::pair<juce::String, MixPlan>>& cands)
            {
                return std::find_if (cands.begin(), cands.end(), [] (auto& c) { return c.first.startsWith (juce::String::fromUTF8 ("Höhen sanfter")); });
            };

            VocalSpec b; b.seconds = 12.0; b.tiltDb = 4.0f; b.sibilance = 1.6f; b.f0 = 300.0f;
            const auto bright = profileFor (b);
            logMessage ("    bright take: top " + juce::String (bright.topDb, 1) + " dB, presence " + juce::String (bright.presenceDb, 1) + " dB");
            const auto baseB = AutoMixPlanner::plan (bright, defaults());
            const auto candsB = AutoMixEngine::candidatesFor (baseB, bright);
            const auto it = softer (candsB);
            expect (bright.topDb > -9.0f, "the test take is bright");
            expect (it != candsB.end(), "bright take -> softer-highs variant");
            if (it != candsB.end())
            {
                const auto& c = it->second;
                expect (c.on (pid::eqOn) && c.routing.contains (ModuleId::eq), "EQ engaged");
                expectEquals (c.get (pid::highType), 0.0f, "shelf");
                expect (c.get (pid::highGain) <= -1.0f && c.get (pid::highGain) >= -4.0f, "gentle cut: " + juce::String (c.get (pid::highGain), 1));
                expect (c.get (pid::highFreq) <= 6000.0f, "shelf reaches into the upper mids");
            }

            VocalSpec d; d.seconds = 12.0; d.tiltDb = -5.0f; d.f0 = 120.0f;
            const auto dark = profileFor (d);
            const auto candsD = AutoMixEngine::candidatesFor (AutoMixPlanner::plan (dark, defaults()), dark);
            expect (softer (candsD) == candsD.end(), "dark take -> no softer-highs variant");
        }

        beginTest ("Optimizer: the EQ 2 variant moves the highs behind the dynamics");
        {
            VocalSpec s; s.seconds = 12.0;
            const auto profile = profileFor (s);
            auto base = AutoMixPlanner::plan (profile, defaults());
            base.set (pid::eqOn, 1.0f);
            base.set (pid::compOn, 1.0f);
            base.set (pid::highGain, 3.0f);
            base.set (pid::highFreq, 9000.0f);
            if (! base.routing.contains (ModuleId::comp))
                base.routing.insertAt (base.routing.size(), ModuleId::comp, 0);

            const auto cands = AutoMixEngine::candidatesFor (base, profile);
            const auto it = std::find_if (cands.begin(), cands.end(), [] (auto& c) { return c.second.routing.contains (ModuleId::eq, 1); });
            expect (it != cands.end(), "an EQ 2 variant exists when the plan boosts the highs");
            if (it != cands.end())
            {
                const auto& c = it->second;
                const auto k2 = [] (const char* id) { return dsp::instanceParamId (id, 1); };
                expect (c.on (k2 (pid::eqOn).toRawUTF8()), "EQ 2 is switched on");
                expectWithinAbsoluteError (c.get (k2 (pid::highGain)), 3.0f, 1.0e-4f, "the boost moved to EQ 2");
                expectWithinAbsoluteError (c.get (k2 (pid::highFreq)), 9000.0f, 1.0e-2f, "at the same frequency");
                expectWithinAbsoluteError (c.get (pid::highGain), 0.0f, 1.0e-4f, "and left EQ 1");
                for (auto m : { ModuleId::comp, ModuleId::smash, ModuleId::deess, ModuleId::smooth })
                    if (c.routing.contains (m))
                        expect (c.routing.indexOf (ModuleId::eq, 1) > c.routing.indexOf (m), "EQ 2 after the dynamics");
            }

            // No highs boosted, no presence boost: no EQ 2 variant
            auto flat = base;
            flat.set (pid::highGain, 0.0f);
            flat.set (pid::midGain, 0.0f);
            const auto none = AutoMixEngine::candidatesFor (flat, profile);
            expect (std::none_of (none.begin(), none.end(), [] (auto& c) { return c.second.routing.contains (ModuleId::eq, 1); }),
                    "no EQ 2 without a boost to move");
        }

        beginTest ("Optimizer: the run reports every candidate and never picks a worse one");
        {
            VocalSpec s; s.seconds = 12.0; s.tiltDb = 4.0f; s.sibilance = 1.6f;
            const auto r = AutoMixEngine::run (makeVocal (s), defaults());
            expect (r.ok, r.error);
            if (r.ok)
            {
                expect (r.candidates.size() >= 2, "candidates were compared: " + juce::String ((int) r.candidates.size()));
                expect (r.chosenCandidate >= 0 && r.chosenCandidate < (int) r.candidates.size());
                for (auto& c : r.candidates)
                    logMessage ("    " + c.name + ": " + juce::String (c.score.total(), 2) + " (" + c.score.describe() + ")");
                if (! r.candidates.empty())
                {
                    const auto& chosen = r.candidates[(size_t) r.chosenCandidate];
                    for (auto& c : r.candidates)
                        expect (chosen.score.total() <= c.score.total() + 1.0e-4f, "chosen is the best: " + c.name);
                    expect (r.plan.routing.isValid());
                    if (r.chosenCandidate > 0)
                        expect (std::any_of (r.adjustments.begin(), r.adjustments.end(), [&] (auto& a) { return a.contains (chosen.name); }),
                                "the report names the chosen variant");
                }
            }
        }
    }

    //==========================================================================
    void workflowTests()
    {
        beginTest ("Workflow: capture during playback, analysis in the background, applied to the real parameters");
        {
            Harness h;
            auto& am = h.proc.getAutoMix();
            expect (am.shouldShowPrompt(), "a new instance asks for an analysis");
            const float hpfBefore = h.get (pid::hpfFreq);

            am.start();
            expect (am.getPhase() == AutoMixController::Phase::listening);
            VocalSpec s; s.seconds = 20.0; s.channels = 2; s.f0 = 140.0f;
            expect (runWorkflow (h, makeVocal (s)), "mix finished");
            expect (am.getPhase() == AutoMixController::Phase::done);
            expect (! am.shouldShowPrompt(), "prompt done after the first mix");
            expect (am.hasMix() && am.getReport().valid);
            expect (am.getReport().steps.size() >= 3);
            for (auto& st : am.getReport().steps)
            {
                logMessage ("    step: " + st.title + " | " + st.reason);
                expect (! st.title.contains ("Hall-Hall"), "reverb title reads naturally: " + st.title);
            }
            expect (am.getReport().steps.size() > 0 && am.getReport().steps[0].title.startsWith ("Low Cut"), "low cut decided by the analysis");
            expect (h.get (pid::hpfOn) > 0.5f, "low cut on");
            expect (h.get (pid::levelMatchMode) > 0.5f, "fair compare enabled");
            expect (h.proc.getRouting().isValid());

            // Compare with the previous state (real parameter switch) and back
            const float mixHpf = h.get (pid::hpfFreq);
            am.showPrevious (true);
            expectWithinAbsoluteError (h.get (pid::hpfFreq), hpfBefore, 0.01f);
            am.showPrevious (false);
            expectWithinAbsoluteError (h.get (pid::hpfFreq), mixHpf, 0.01f);

            // State round trip: report, prompt and previous state survive save / load
            juce::MemoryBlock state;
            h.proc.getStateInformation (state);
            Harness h2;
            h2.proc.setStateInformation (state.getData(), (int) state.getSize());
            auto& am2 = h2.proc.getAutoMix();
            expect (am2.hasMix() && am2.hasPrevious(), "mix and previous state restored");
            expect (! am2.shouldShowPrompt(), "no prompt when reopening the project");
            expectEquals ((int) am2.getReport().steps.size(), (int) am.getReport().steps.size());
            expectWithinAbsoluteError (h2.get (pid::hpfFreq), mixHpf, 0.01f);

            // Restore the previous state completely
            am.restorePrevious();
            expectWithinAbsoluteError (h.get (pid::hpfFreq), hpfBefore, 0.01f);
            expect (! am.hasMix());
        }

        beginTest ("Workflow: mono track at 44.1 kHz with an odd host block size");
        {
            Harness h (44100.0, 333, 1);
            auto& am = h.proc.getAutoMix();
            am.start();
            VocalSpec s; s.seconds = 20.0; s.sampleRate = 44100.0; s.channels = 1;
            expect (runWorkflow (h, makeVocal (s)), "mono mix finished");
            expect (am.hasMix());
            expect (am.getReport().voicedSeconds >= 5.0f);
            const auto out = h.run (h.noise (0.1f, 0.5, 4));
            expect (allFinite (out));
        }

        beginTest ("Workflow: 96 kHz stereo");
        {
            Harness h (96000.0, 1024, 2);
            auto& am = h.proc.getAutoMix();
            am.start();
            VocalSpec s; s.seconds = 26.0; s.sampleRate = 96000.0; s.channels = 2;
            const bool ok96 = runWorkflow (h, makeVocal (s));
            expect (ok96, "96 kHz mix finished: phase " + juce::String ((int) am.getPhase()) + ", " + am.getStatus().error
                          + ", voiced " + juce::String (am.getStatus().capture.voicedSeconds));
            expect (am.hasMix());
        }

        beginTest ("Workflow: the beat on the sidechain is analysed but never heard");
        {
            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses.add (juce::AudioChannelSet::stereo());
            layout.inputBuses.add (juce::AudioChannelSet::stereo());
            layout.outputBuses.add (juce::AudioChannelSet::stereo());

            // A loud "beat" with strong presence-range energy (noise bursts, high-passed)
            CapturedAudio beat;
            beat.sampleRate = 48000.0;
            beat.channels = 2;
            {
                juce::Random r (11);
                std::vector<float> b ((size_t) (48000 * 20));
                dsp::Biquad hp;
                hp.setCoeffs (dsp::design::highPass (48000.0, 1500.0, 0.7071));
                for (size_t i = 0; i < b.size(); ++i)
                    b[i] = 0.5f * hp.process (r.nextFloat() * 2.0f - 1.0f, 0) * ((i % 12000) < 3000 ? 1.0f : 0.3f);
                beat.vocal = { b, b };
            }

            Harness h;
            expect (h.proc.setBusesLayout (layout), "sidechain layout accepted");
            h.proc.prepareToPlay (48000.0, 512);
            auto& am = h.proc.getAutoMix();
            am.start();
            VocalSpec s; s.seconds = 20.0; s.channels = 2;
            expect (runWorkflow (h, makeVocal (s), 90.0, &beat), "mix with beat finished");
            expect (am.getReport().beatAnalysed, "beat was analysed");

            // The sidechain never reaches the output (ducking source = vocal)
            h.set (pid::duckSource, 0.0f);
            juce::AudioBuffer<float> a (4, 48000), b (4, 48000);
            juce::Random r (2);
            for (int i = 0; i < 48000; ++i)
            {
                const float v = 0.1f * (r.nextFloat() * 2.0f - 1.0f);
                for (int ch = 0; ch < 2; ++ch) { a.setSample (ch, i, v); b.setSample (ch, i, v); }
                for (int ch = 2; ch < 4; ++ch) { a.setSample (ch, i, 0.0f); b.setSample (ch, i, beat.vocal[0][(size_t) i]); }
            }
            Harness h2;
            h2.proc.setBusesLayout (layout);
            h2.proc.prepareToPlay (48000.0, 512);
            juce::MemoryBlock st;
            h.proc.getStateInformation (st);
            h2.proc.setStateInformation (st.getData(), (int) st.getSize());
            h2.set (pid::duckSource, 0.0f);
            juce::MidiBuffer midi;
            for (int start = 0; start < 48000; start += 512)
            {
                juce::AudioBuffer<float> va (a.getArrayOfWritePointers(), 4, start, 512), vb (b.getArrayOfWritePointers(), 4, start, 512);
                h.proc.processBlock (va, midi);
                h2.proc.processBlock (vb, midi);
            }
            // The two processors are not sample-identical (reverb/doubler state), so compare
            // levels: a beat 20 dB louder than the vocal would dominate if it leaked through
            const float la = rmsDb (a, 24000, 24000), lb = rmsDb (b, 24000, 24000);
            expectWithinAbsoluteError (lb, la, 1.0f, "output level independent of the sidechain beat");
        }

        beginTest ("Workflow: a project from an older version is not asked to analyse; presets never start one");
        {
            Harness h;
            juce::MemoryBlock state;
            h.proc.getStateInformation (state);
            auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
            auto tree = juce::ValueTree::fromXml (*xml);
            tree.removeChild (tree.getChildWithName (AutoMixController::treeType), nullptr);
            tree.setProperty (statekeys::stateVersion, 2, nullptr);
            juce::MemoryBlock old;
            juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), old);

            Harness h2;
            h2.proc.setStateInformation (old.getData(), (int) old.getSize());
            expect (! h2.proc.getAutoMix().shouldShowPrompt(), "older project: no analysis prompt");

            Harness h3;
            auto& pm = h3.proc.getPresetManager();
            pm.loadPreset (1);
            expect (h3.proc.getAutoMix().getPhase() == AutoMixController::Phase::idle, "a preset never starts an analysis");
            expect (h3.proc.getAutoMix().shouldShowPrompt(), "and does not consume the first-open prompt");

            // A new instance restored from its own (unanalysed) state still offers the analysis
            juce::MemoryBlock fresh;
            h3.proc.getStateInformation (fresh);
            Harness h4;
            h4.proc.setStateInformation (fresh.getData(), (int) fresh.getSize());
            expect (h4.proc.getAutoMix().shouldShowPrompt(), "unanalysed instance still pending after save/load");
        }

        beginTest ("Workflow: cancelling changes nothing; the audio never stops");
        {
            Harness h;
            auto& am = h.proc.getAutoMix();
            const float hpf = h.get (pid::hpfFreq);
            am.start();
            const auto out = h.run (h.noise (0.2f, 1.0, 3));
            juce::MessageManager::getInstance()->runDispatchLoopUntil (150);
            expect (allFinite (out));
            expect (am.getPhase() == AutoMixController::Phase::capturing, "capturing after signal");
            am.cancel();
            expect (am.getPhase() == AutoMixController::Phase::idle);
            expectEquals (h.get (pid::hpfFreq), hpf);
            expect (! am.analyseNow(), "nothing captured anymore");
        }
    }

    //==========================================================================
    void editorTests()
    {
        ui::settings::set (ui::settings::tutorialDone, true);

        beginTest ("Editor: the analysis popup appears on the first open only");
        {
            Harness h;
            {
                std::unique_ptr<juce::AudioProcessorEditor> e (h.proc.createEditor());
                auto& ov = asVelvox (*e).getAnalysisOverlay();
                expect (ov.isOpen(), "popup on the first open of a new instance");
                expect (ov.getView() == ui::AnalysisOverlay::View::intro);
                expectEquals (ov.getPrimaryButton().getButtonText(), juce::String ("Audio analysieren"));
                click (ov.getSecondaryButton());   // "Später"
                expect (! ov.isOpen());
            }
            std::unique_ptr<juce::AudioProcessorEditor> again (h.proc.createEditor());
            expect (! asVelvox (*again).getAnalysisOverlay().isOpen(), "not again after Später");

            // Simple mode (opened by default) has "Vocal analysieren" under the spectrum; it opens the progress card
            expect (asVelvox (*again).getSimpleView().isVisible() && asVelvox (*again).getSimpleView().getAnalyseButton().isVisible(), "analyse visible in Simple mode");
            click (asVelvox (*again).getSimpleView().getAnalyseButton());
            auto& ov = asVelvox (*again).getAnalysisOverlay();
            expect (ov.isOpen() && ov.getView() == ui::AnalysisOverlay::View::listening);
            click (ov.getSecondaryButton());   // Abbrechen
            expect (h.proc.getAutoMix().getPhase() == AutoMixController::Phase::idle);
        }

        beginTest ("Editor: the overview lists exactly the applied steps; a step opens its module");
        {
            Harness h;
            h.proc.getAutoMix().dismissPrompt();
            VocalSpec s; s.seconds = 14.0;
            const auto result = h.proc.getAutoMix().runSynchronously (makeVocal (s));
            expect (result.ok, "ok");
            h.proc.getAutoMix().applyResult (result);

            std::unique_ptr<juce::AudioProcessorEditor> e (h.proc.createEditor());
            auto& ed = asVelvox (*e);
            expect (! ed.getAnalysisOverlay().isOpen(), "overlay closed");
            expect (! ed.isAdvanced(), "Simple mode");
            const auto& report = h.proc.getAutoMix().getReport();
            expectEquals (ed.getAutoMixView().getNumStepRows(),
                          (int) (report.steps.size() + (size_t) report.notes.size() + (size_t) report.adjustments.size()));
            expect (ed.getSimpleView().getBeforeButton().isVisible(), "Vorher / Nachher available");
            for (auto& key : report.changedModules)
                if (auto m = dsp::moduleFromKey (key))
                    expect (ed.getChainView().isAutoMixModule (*m), "auto badge on " + key);

            // Vorher / Nachher switch the real parameters
            click (ed.getSimpleView().getBeforeButton());
            expect (h.proc.getAutoMix().isShowingPrevious(), "previous");
            click (ed.getSimpleView().getAfterButton());
            expect (! h.proc.getAutoMix().isShowingPrevious(), "mix");

            // A step of the report opens its module in Advanced mode
            ed.showMixDetails();
            expect (ed.isMixDetailsOpen());
            ed.getAutoMixView().onOpenModule ((int) ModuleId::comp);
            expect (ed.isAdvanced() && ed.getDetail() != nullptr && ! ed.isMixDetailsOpen(), "a step opens the module");
        }
    }
};

static AutoMixTests autoMixTests;
