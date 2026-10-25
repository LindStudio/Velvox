#pragma once

// Reproduction tool for clicks / pops (VelvoxTests --popscan ...).
//
// Runs the real processor in (approximately) real time with FL-Studio-like
// conditions: variable host block sizes, a playhead that reports a stopped
// transport, optionally the editor open with its timers running, and an
// automatic-mix configuration. The output is scanned for
//   - any output during digital silence (after the effect tails), and
//   - discontinuities (second difference far above the signal's own).
// Every event is printed with its time stamp so periodic behaviour shows up.

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SyntheticVocal.h"

namespace velvox::test
{
    struct ScanPlayHead final : juce::AudioPlayHead
    {
        bool playing = false;
        double bpm = 150.0, ppq = 0.0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo p;
            p.setBpm (bpm);
            p.setIsPlaying (playing);
            p.setPpqPosition (ppq);
            p.setTimeSignature (TimeSignature { 4, 4 });
            return p;
        }
    };

    struct PopScanResult
    {
        int silentEvents = 0, discontinuities = 0;
        float maxSilentDb = -200.0f;
        juce::StringArray log;
    };

    // signal: "silence", "floor" (-80 dBFS noise), "tone" (-18 dBFS 440 Hz), "vocal"
    inline PopScanResult runPopScan (const juce::String& signal, bool automix, bool withEditor, double seconds,
                                     double fs, bool realTime, unsigned seed = 7,
                                     juce::String overrides = juce::SystemStats::getEnvironmentVariable ("VELVOX_POPSCAN_SET", {}))
    {
        PopScanResult res;
        auto proc = std::make_unique<VelvoxAudioProcessor>();
        ScanPlayHead head;
        proc->setPlayHead (&head);
        proc->setRateAndBufferSizeDetails (fs, 1024);
        proc->prepareToPlay (fs, 1024);
        auto& am = proc->getAutoMix();
        am.dismissPrompt();

        if (automix)
        {
            VocalSpec s;
            s.seconds = 14.0;
            s.channels = 2;
            s.sampleRate = fs;
            const auto r = am.runSynchronously (makeVocal (s));
            am.applyResult (r);
        }

        // Bisecting: VELVOX_POPSCAN_SET="reverbOn=0,delayOn=0" overrides parameters after the mix
        {
            juce::StringArray pairs;
            pairs.addTokens (overrides, ",", {});
            for (auto& kv : pairs)
                if (auto* prm = proc->getState().getParameter (kv.upToFirstOccurrenceOf ("=", false, false).trim()))
                    prm->setValueNotifyingHost (prm->convertTo0to1 (kv.fromFirstOccurrenceOf ("=", false, false).getFloatValue()));
        }

        std::unique_ptr<juce::AudioProcessorEditor> editor;
        if (withEditor)
            editor.reset (proc->createEditor());

        VocalSpec vs;
        vs.seconds = 12.0;
        vs.channels = 2;
        vs.sampleRate = fs;
        const auto vocal = makeVocal (vs);

        juce::Random rnd (seed);
        juce::AudioBuffer<float> buffer (2, 1024);
        juce::MidiBuffer midi;
        const int total = (int) (seconds * fs);
        const int settle = (int) (std::min (20.0, seconds * 0.25) * fs);   // tails of a previous signal
        double phase = 0.0;
        float prev1[2] {}, prev2[2] {};
        float inPrev1 = 0.0f, inPrev2 = 0.0f, oPrev1[2] {}, oPrev2[2] {};
        std::vector<float> inputD2, outputD2;
        std::vector<std::pair<int, double>> blockTimes;   // (position, microseconds)
        std::vector<int> blockSizes;
        std::vector<float> out0, in0;
        inputD2.reserve ((size_t) total);
        outputD2.reserve ((size_t) total);

        for (int pos = 0; pos < total;)
        {
            // FL hands over variable block sizes (e.g. 1..1024, often odd)
            const int n = std::min (total - pos, rnd.nextInt (100) < 70 ? 512 : 1 + rnd.nextInt (1024));
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, n);
            for (int i = 0; i < n; ++i)
            {
                float x = 0.0f;
                if (signal == "floor")
                    x = 1.0e-4f * (rnd.nextFloat() * 2.0f - 1.0f);
                else if (signal == "tone")
                {
                    x = 0.125f * (float) std::sin (phase);
                    phase += 2.0 * juce::MathConstants<double>::pi * 440.0 / fs;
                }
                else if (signal == "vocal")
                    x = vocal.vocal[0][(size_t) ((pos + i) % vocal.numSamples())];
                view.setSample (0, i, x);
                view.setSample (1, i, x);
            }

            // Keep the input's own curvature (2nd difference) for input-referenced detection
            for (int i = 0; i < n; ++i)
            {
                const float x = view.getSample (0, i);
                inputD2.push_back (std::abs (x - 2.0f * inPrev1 + inPrev2));
                in0.push_back (x);
                inPrev2 = inPrev1;
                inPrev1 = x;
            }

            const auto t0 = juce::Time::getHighResolutionTicks();
            proc->processBlock (view, midi);
            const double us = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0) * 1.0e6;
            blockTimes.push_back ({ pos, us });
            blockSizes.push_back (n);
            head.ppq += 0.0;   // transport stopped: position does not move

            // Detector self-check: VELVOX_POPSCAN_INJECT=1 adds a single-sample 0.02 spike at t = 30 s
            static const bool inject = juce::SystemStats::getEnvironmentVariable ("VELVOX_POPSCAN_INJECT", {}) == "1";
            const int at = (int) (30.0 * fs);
            if (inject && at >= pos && at < pos + n)
                for (int ch = 0; ch < 2; ++ch)
                    view.setSample (ch, at - pos, view.getSample (ch, at - pos) + 0.02f);

            for (int i = 0; i < n; ++i)
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float y = view.getSample (ch, i);
                    const float d2 = y - 2.0f * prev1[ch] + prev2[ch];
                    prev2[ch] = prev1[ch];
                    prev1[ch] = y;
                    const int s = pos + i;
                    if (s < settle)
                        continue;
                    if (signal == "silence" && std::abs (y) > 1.0e-5f)
                    {
                        const float db = juce::Decibels::gainToDecibels (std::abs (y));
                        res.maxSilentDb = std::max (res.maxSilentDb, db);
                        if (res.silentEvents++ < 40)
                            res.log.add ("  t=" + juce::String (s / fs, 3) + " s  output " + juce::String (db, 1) + " dBFS during silence");
                    }
                    juce::ignoreUnused (d2);
                }
            for (int i = 0; i < n; ++i)
            {
                // Curvature of the output (both channels)
                float m = 0.0f;
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float y = view.getSample (ch, i);
                    m = std::max (m, std::abs (y - 2.0f * oPrev1[ch] + oPrev2[ch]));
                    oPrev2[ch] = oPrev1[ch];
                    oPrev1[ch] = y;
                }
                outputD2.push_back (m);
                out0.push_back (view.getSample (0, i));
            }

            pos += n;
            if (withEditor || realTime)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (realTime ? std::max (1, (int) (n * 1000.0 / fs)) : 0);
        }

        // A pop: output curvature far above the output's local median (+-10 ms) AND far above
        // anything in the input nearby (+-3 ms). Processing (EQ, saturation, delays) scales
        // curvature by a bounded factor; a discontinuity creates a single-sample spike.
        // Echoes repeat the input's own curvature later: when the delay is on, the input
        // is also referenced around every repeat (k * delay time), otherwise an echo of a
        // consonant in a pause would count as a discontinuity.
        const int localHalf = (int) (0.01 * fs), inHalf = (int) (0.003 * fs) + 4;
        const bool delayHeard = proc->readEffectiveSettings().delayOn;
        const int echoLag = delayHeard ? (int) std::lround (proc->getChain().meters.effectiveDelayMs.load() * 0.001 * fs) : 0;
        const int echoRepeats = delayHeard ? 8 : 0;
        // The output is late by the reported latency (e.g. LEVEL RIDE's lookahead): reference the input there
        const int latency = proc->getLatencySamples();
        int lastEvent = -1000000;
        std::vector<float> win;
        for (int s = std::max (settle, localHalf); s + localHalf < (int) outputD2.size(); ++s)
        {
            const float d = outputD2[(size_t) s];
            if (d < 2.0e-3f)
                continue;
            float inMax = 0.0f;
            for (int rep = 0; rep <= echoRepeats; ++rep)
            {
                const int c = s - latency - rep * echoLag;
                for (int k = std::max (0, c - inHalf); k <= c + inHalf && k < (int) inputD2.size(); ++k)
                    inMax = std::max (inMax, inputD2[(size_t) k]);
            }
            if (d < 10.0f * inMax)
                continue;
            win.assign (outputD2.begin() + (s - localHalf), outputD2.begin() + (s + localHalf));
            std::nth_element (win.begin(), win.begin() + (long) win.size() / 2, win.end());
            const float median = win[win.size() / 2];
            if (d < 15.0f * median)
                continue;
            if (s - lastEvent > (int) (0.05 * fs))
            {
                if (res.discontinuities < 40)
                    res.log.add ("  t=" + juce::String (s / fs, 3) + " s  discontinuity: curvature " + juce::String (d, 4)
                                 + " = " + juce::String (d / std::max (1.0e-9f, median), 0) + "x local median, "
                                 + juce::String (d / std::max (1.0e-9f, inMax), 0) + "x input");
                ++res.discontinuities;
            }
            lastEvent = s;
        }

        // VELVOX_POPSCAN_DUMP=<seconds>: print output and input (also 300 ms earlier) around that time
        if (const auto dump = juce::SystemStats::getEnvironmentVariable ("VELVOX_POPSCAN_DUMP", {}); dump.isNotEmpty())
        {
            const int c = (int) (dump.getDoubleValue() * fs);
            const int echo = (int) (0.3 * fs);
            for (int k = c - 12; k <= c + 12 && k < (int) out0.size(); ++k)
                if (k - echo >= 0)
                    res.log.add ("    " + juce::String (k) + "  out " + juce::String (out0[(size_t) k], 6) + "   in " + juce::String (in0[(size_t) k], 6)
                                 + "   in-300ms " + juce::String (in0[(size_t) (k - echo)], 6) + "   d2 " + juce::String (outputD2[(size_t) k], 6));
        }

        // CPU: time of each callback against its real-time budget (n / fs)
        if (! blockTimes.empty())
        {
            double small = 0.0, smallCount = 0.0, worstRatio = 0.0, worstUs = 0.0, totalUs = 0.0;
            int worstAt = 0, worstN = 0;
            for (size_t i = 0; i < blockTimes.size(); ++i)
            {
                const int n = blockSizes[i];
                const double us = blockTimes[i].second;
                totalUs += us;
                if (n <= 8) { small += us; smallCount += 1.0; }
                const double ratio = us / (n * 1.0e6 / fs);
                if (n >= 64 && ratio > worstRatio) { worstRatio = ratio; worstAt = blockTimes[i].first; worstN = n; worstUs = us; }
            }
            // VELVOX_POPSCAN_SLOW=<ratio>: list every callback above that share of its budget (periodic spikes?)
            if (const auto slow = juce::SystemStats::getEnvironmentVariable ("VELVOX_POPSCAN_SLOW", {}); slow.isNotEmpty())
                for (size_t i = 0; i < blockTimes.size(); ++i)
                {
                    const double ratio = blockTimes[i].second / (blockSizes[i] * 1.0e6 / fs);
                    if (blockSizes[i] >= 64 && ratio > slow.getDoubleValue())
                        res.log.add ("  slow callback t=" + juce::String (blockTimes[i].first / fs, 3) + " s  " + juce::String (blockTimes[i].second, 0)
                                     + " us for " + juce::String (blockSizes[i]) + " samples = " + juce::String (100.0 * ratio, 1) + " %");
                }
            res.log.add ("  CPU: average " + juce::String (100.0 * totalUs / (seconds * 1.0e6), 2) + " % of one core; fixed cost per tiny block (<= 8 samples) "
                         + juce::String (small / std::max (1.0, smallCount), 1) + " us; worst block >= 64 samples: "
                         + juce::String (worstUs, 0) + " us for " + juce::String (worstN) + " samples at t=" + juce::String (worstAt / fs, 2)
                         + " s = " + juce::String (100.0 * worstRatio, 1) + " % of its real-time budget");
        }

        editor.reset();
        proc->setPlayHead (nullptr);
        return res;
    }
}
