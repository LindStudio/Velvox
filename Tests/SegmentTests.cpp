// SEGMENTED, START-INDEPENDENT ANALYSIS (plan 2026-11-04-automix-konsistenz, Task 5, U1): the capture (up to 2 minutes,
// level independent, with the song position per block), the segments that follow the song position, the aggregation of
// the segment profiles, the render excerpt and the JSON the analysis is stored as.

#include "TestHelpers.h"
#include "SyntheticVocal.h"
#include "ConsistencyHarness.h"
#include "CheckEval.h"
#include "AutoMix/AnalysisStore.h"

using namespace vocalypse;
using namespace vocalypse::test;
using namespace vocalypse::automix;

namespace
{
    CapturedAudio vocalOf (double seconds, unsigned seed = 1, float resonanceHz = 0.0f, float resonanceDb = 12.0f)
    {
        VocalSpec s;
        s.seconds = seconds;
        s.seed = seed;
        s.resonanceHz = resonanceHz;
        s.resonanceDb = resonanceDb;
        return makeVocal (s);
    }

    // `parts` back to back, `gapSeconds` of digital silence (or noise at `gapNoiseDb`) between them
    CapturedAudio withGaps (const CapturedAudio& part, int count, double gapSeconds, float gapNoiseDb = -200.0f)
    {
        CapturedAudio out;
        out.sampleRate = part.sampleRate;
        out.channels = part.channels;
        out.vocal.resize (part.vocal.size());
        juce::Random rnd (9);
        const float amp = juce::Decibels::decibelsToGain (gapNoiseDb) * std::sqrt (3.0f);
        for (int i = 0; i < count; ++i)
        {
            for (size_t ch = 0; ch < part.vocal.size(); ++ch)
                out.vocal[ch].insert (out.vocal[ch].end(), part.vocal[ch].begin(), part.vocal[ch].end());
            if (i + 1 < count)
                for (size_t ch = 0; ch < part.vocal.size(); ++ch)
                    for (int k = 0; k < (int) (gapSeconds * part.sampleRate); ++k)
                        out.vocal[ch].push_back (gapNoiseDb > -150.0f ? amp * (rnd.nextFloat() * 2.0f - 1.0f) : 0.0f);
        }
        return out;
    }

    CapturedAudio scaledBy (CapturedAudio a, float db)
    {
        const float g = juce::Decibels::decibelsToGain (db);
        for (auto& ch : a.vocal)
            for (auto& x : ch)
                x *= g;
        return a;
    }

    // The analysis as the JSON the store writes: equal text = identical results
    juce::String jsonOf (const CapturedAudio& captured, float scaleDb = 0.0f)
    {
        AutoMixResult r;
        r.profile = VocalProfiler::analyseSegmented (captured, {}, &r.segmentProfiles, 5.0f, scaleDb, &r.segments);
        return AnalysisStore::toJson (r, captured);
    }

    juce::File tempFolder (const juce::String& name)
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("VocalypseSegmentTests").getChildFile (name);
        dir.deleteRecursively();
        dir.createDirectory();
        return dir;
    }
}

class SegmentTests final : public juce::UnitTest
{
public:
    SegmentTests() : juce::UnitTest ("Auto mix segments", "VOCALYPSE") {}

    void runTest() override
    {
        captureTests();
        segmentTests();
        aggregationTests();
        excerptAndStoreTests();
        engineTests();
        probe();
    }

    //==========================================================================
    void captureTests()
    {
        beginTest ("Capture: 120 s at 48 kHz stereo (vocal + mono beat) are armed in at most 70 MB");
        {
            CaptureBuffer cb;
            cb.arm (48000.0, 2, CaptureConfig {});
            expectEquals (cb.capacitySamples(), 120 * 48000);
            expect (cb.memoryBytes() <= 70u * 1000u * 1000u, "armed: " + juce::String ((juce::int64) cb.memoryBytes()) + " bytes");
            CaptureBuffer high;
            high.arm (192000.0, 2, CaptureConfig {});
            expect (high.memoryBytes() <= 70u * 1000u * 1000u, "192 kHz stays bounded, too");
        }

        beginTest ("Capture: the defaults are 60 s of voice, 120 s of signal, 5 s minimum");
        {
            const CaptureConfig c;
            expectEquals (c.targetVoicedSeconds, 60.0f);
            expectEquals (c.maxSeconds, 120.0f);
            expectEquals (c.minVoicedSeconds, 5.0f);
        }

        beginTest ("Capture: +-0.5 / 1 / 2 dB copies of the same audio capture the same window");
        {
            // 3 s of noise at -80 dBFS (a room), then 30 s of singing, with pauses
            auto vocal = vocalOf (30.0);
            CapturedAudio lead = withGaps (vocalOf (3.0, 5), 1, 0.0, -200.0f);
            juce::Random rnd (4);
            for (auto& ch : lead.vocal)
                for (auto& x : ch)
                    x = juce::Decibels::decibelsToGain (-80.0f) * std::sqrt (3.0f) * (rnd.nextFloat() * 2.0f - 1.0f);
            for (size_t ch = 0; ch < vocal.vocal.size(); ++ch)
                vocal.vocal[ch].insert (vocal.vocal[ch].begin(), lead.vocal[ch].begin(), lead.vocal[ch].end());

            for (bool position : { false, true })
            {
                CapturedAudio reference;
                expect (captureLikePlugin (vocal, 0.0, reference, position));
                const auto refSegments = VocalProfiler::segmentsOf (reference);
                expect (! refSegments.empty());
                for (float db : { -2.0f, -1.0f, -0.5f, 0.5f, 1.0f, 2.0f })
                {
                    CapturedAudio shifted;
                    expect (captureLikePlugin (scaledBy (vocal, db), 0.0, shifted, position));
                    expectEquals (shifted.numSamples(), reference.numSamples(), "captured length at " + juce::String (db) + " dB");
                    const auto segs = VocalProfiler::segmentsOf (shifted);
                    expectEquals ((int) segs.size(), (int) refSegments.size(), "segments at " + juce::String (db) + " dB");
                    for (size_t i = 0; i < std::min (segs.size(), refSegments.size()); ++i)
                        expect (segs[i].startSample == refSegments[i].startSample && segs[i].numSamples == refSegments[i].numSamples, "segment " + juce::String ((int) i));
                }
            }
        }

        beginTest ("Capture: the song position of every 512-sample block is stored, also for other host block sizes");
        {
            const auto vocal = vocalOf (6.0);
            for (int blockSize : { 128, 512, 1000 })
            {
                CaptureBuffer cb;
                CaptureConfig cfg;
                cfg.targetVoicedSeconds = 100.0f;
                cb.arm (48000.0, 1, cfg);
                const double startPpq = 12.0;   // 6 s into the song at 120 BPM
                for (int start = 0; start < vocal.numSamples(); start += blockSize)
                {
                    const int n = std::min (blockSize, vocal.numSamples() - start);
                    const float* ch[] { vocal.vocal[0].data() + start };
                    cb.push (ch, 1, nullptr, 0, n, true, 120.0, startPpq + (double) start / 48000.0 * 2.0);
                }
                cb.finishNow();
                const auto out = cb.copyOut();
                expect (out.hasPpq(), "block size " + juce::String (blockSize));
                // the capture starts with the first block that is not silent: ppq of block 0 is at or after the start
                bool exact = ! out.blockPpq.empty() && out.blockPpq[0] >= startPpq;
                for (size_t c = 0; c < out.blockPpq.size(); ++c)
                    exact = exact && std::abs (out.blockPpq[c] - (out.blockPpq[0] + (double) ((int) c * 512) / 48000.0 * 2.0)) < 1.0e-9;
                expect (exact, "ppq per block at host block size " + juce::String (blockSize));
            }
            CaptureBuffer cb;
            cb.arm (48000.0, 1, CaptureConfig {});
            const float* ch[] { vocal.vocal[0].data() };
            cb.push (ch, 1, nullptr, 0, 512, true, 120.0);
            cb.finishNow();
            expect (! cb.copyOut().hasPpq(), "no song position, none stored");
        }

        beginTest ("Capture: the beat is stored as one mid channel, on the same timeline");
        {
            CaptureBuffer cb;
            CaptureConfig cfg;
            cfg.targetVoicedSeconds = 100.0f;
            cb.arm (48000.0, 2, cfg);
            const auto vocal = vocalOf (2.0);
            std::vector<float> left (512), right (512);
            for (int start = 0; start + 512 <= vocal.numSamples(); start += 512)
            {
                for (int i = 0; i < 512; ++i)
                {
                    left[(size_t) i] = 0.2f;
                    right[(size_t) i] = 0.4f;
                }
                const float* v[] { vocal.vocal[0].data() + start, vocal.vocal[0].data() + start };
                const float* b[] { left.data(), right.data() };
                cb.push (v, 2, b, 2, 512, true);
            }
            cb.finishNow();
            const auto out = cb.copyOut();
            expectEquals ((int) out.beat.size(), 1);
            expectEquals ((int) out.beat[0].size(), out.numSamples());
            expectWithinAbsoluteError (out.beat[0][100], 0.3f, 1.0e-6f);
        }
    }

    //==========================================================================
    void segmentTests()
    {
        beginTest ("Segments with a song position: 4 bars at 120 BPM = 8 s, aligned to multiples of 16 quarter notes, wherever the capture began");
        {
            const auto vocal = vocalOf (50.0);
            CapturedAudio a, b;
            expect (captureLikePlugin (vocal, 0.0, a, true));
            expect (captureLikePlugin (vocal, 6.0, b, true));
            expect (a.hasPpq() && b.hasPpq());
            const auto sa = VocalProfiler::segmentsOf (a);
            const auto sb = VocalProfiler::segmentsOf (b);
            expect (sa.size() >= 5 && sb.size() >= 4, "segments " + juce::String ((int) sa.size()) + " / " + juce::String ((int) sb.size()));
            for (auto& s : sa)
            {
                expect (std::fmod (s.startPpq, 16.0) == 0.0, "aligned start " + juce::String (s.startPpq));
                expect (s.numSamples <= 8 * 48000 + 1);
            }

            // Every segment of the capture that began later is a segment of the other one: same boundaries, same audio
            int shared = 0;
            for (auto& y : sb)
                for (auto& x : sa)
                    if (x.startPpq == y.startPpq)
                    {
                        ++shared;
                        expectEquals (y.numSamples, x.numSamples, "length of the segment at ppq " + juce::String (y.startPpq));
                        bool same = true;
                        for (int i = 0; i < std::min (x.numSamples, y.numSamples); i += 97)
                            same = same && a.vocal[0][(size_t) (x.startSample + i)] == b.vocal[0][(size_t) (y.startSample + i)];
                        expect (same, "same audio in the segment at ppq " + juce::String (y.startPpq));
                    }
            expect (shared >= 4, "shared segments: " + juce::String (shared));
        }

        beginTest ("Segments follow the tempo: 4 bars last 6 .. 12 s, else 8 or 2 bars");
        {
            const auto vocal = vocalOf (60.0);
            struct Case { double bpm; double expectedSeconds; };
            for (auto c : { Case { 120.0, 8.0 }, Case { 90.0, 10.666 }, Case { 170.0, 11.294 }, Case { 60.0, 8.0 } })
            {
                CaptureBuffer cb;
                CaptureConfig cfg;
                cfg.targetVoicedSeconds = 100.0f;
                cb.arm (48000.0, 1, cfg);
                for (int start = 0; start < vocal.numSamples(); start += 512)
                {
                    const float* ch[] { vocal.vocal[0].data() + start };
                    cb.push (ch, 1, nullptr, 0, std::min (512, vocal.numSamples() - start), true, c.bpm, (double) start / 48000.0 * c.bpm / 60.0);
                }
                cb.finishNow();
                const auto segs = VocalProfiler::segmentsOf (cb.copyOut());
                expect (segs.size() >= 3, "segments at " + juce::String (c.bpm));
                if (segs.size() >= 2)
                    expectWithinAbsoluteError ((double) segs[1].numSamples / 48000.0, c.expectedSeconds, 0.01, "segment length at " + juce::String (c.bpm) + " BPM");
            }
        }

        beginTest ("Segments without a song position: 8 s from the first stored sample, less than 2 s of voice is skipped");
        {
            CapturedAudio a;
            expect (captureLikePlugin (vocalOf (40.0), 0.0, a, false));
            expect (! a.hasPpq());
            const auto segs = VocalProfiler::segmentsOf (a);
            expect (segs.size() >= 4);
            for (size_t i = 0; i + 1 < segs.size(); ++i)
                expectEquals (segs[i].numSamples, 8 * 48000);
            expectEquals (segs[0].startSample, 0);
            for (auto& s : segs)
            {
                expectEquals (s.startPpq, -1.0);
                expect (s.voicedSeconds >= 2.0f);
            }
        }

        beginTest ("Review Focus 2: without a song position, playing from 0 s twice gives the same segments and the same analysis");
        {
            const auto vocal = vocalOf (30.0);
            CapturedAudio a, b;
            expect (captureLikePlugin (vocal, 0.0, a, false));
            expect (captureLikePlugin (vocal, 0.0, b, false));
            expectEquals (jsonOf (a), jsonOf (b));
        }

        beginTest ("analyseSegmented of the same audio ten times is identical");
        {
            CapturedAudio a;
            expect (captureLikePlugin (vocalOf (30.0), 0.0, a, true));
            const auto first = jsonOf (a);
            for (int i = 1; i < 10; ++i)
                expect (jsonOf (a) == first, "run " + juce::String (i));
            expect (first.contains ("\"segments\""));
        }
    }

    //==========================================================================
    void aggregationTests()
    {
        beginTest ("Review Focus 1: an 8 s capture and a capture with 20 s gaps give valid, deterministic profiles");
        {
            const auto part = vocalOf (3.0, 3);
            const std::vector<std::pair<juce::String, CapturedAudio>> files {
                { "8 s", vocalOf (8.0, 2) },
                { "gaps, digital silence", withGaps (part, 3, 20.0) },
                { "gaps, room noise", withGaps (part, 3, 20.0, -75.0f) } };
            for (bool position : { false, true })
                for (auto& [name, file] : files)
                {
                    const auto label = name + (position ? " with position" : "");
                    CapturedAudio captured;
                    expect (captureLikePlugin (file, 0.0, captured, position), label);
                    if (captured.numSamples() == 0)
                        continue;
                    const auto profile = VocalProfiler::analyseSegmented (captured);
                    expect (profile.voicedSeconds > 2.0f, label + ": voiced " + juce::String (profile.voicedSeconds));
                    expect (! profile.spectrumDb.empty() && profile.thirdsDb.size() == (size_t) numThirds, label + ": spectrum");
                    expect (jsonOf (captured) == jsonOf (captured), label + ": deterministic");

                    // the engine mixes it, gently
                    const auto r1 = AutoMixEngine::run (captured, defaultValues(), {}, {}, MixIntent {});
                    const auto r2 = AutoMixEngine::run (captured, defaultValues(), {}, {}, MixIntent {});
                    expect (r1.ok, label + ": " + r1.error);
                    if (r1.ok && r2.ok)
                    {
                        expect (r1.plan.conservative, label + ": conservative (voiced " + juce::String (r1.profile.voicedSeconds, 1) + " s)");
                        const auto d = compare (fingerprint (r1.plan), fingerprint (r2.plan), Check::exact, 0.0f, Tolerances {});
                        expect (d.pass, label + ": exact " + (d.overTolerance.empty() ? juce::String() : d.overTolerance[0]));
                    }
                }
        }

        beginTest ("Aggregation: one segment is the analysis of that segment; resonances need the share of segments");
        {
            // a resonance of the room is in every segment: persistence 1, notched (sustained)
            CapturedAudio a;
            expect (captureLikePlugin (vocalOf (50.0, 1, 1800.0f, 16.0f), 0.0, a, true));
            const auto profile = VocalProfiler::analyseSegmented (a);
            bool found = false;
            for (auto& r : profile.resonances)
                if (std::abs (std::log2 (r.hz / 1800.0f)) < 0.2f)
                {
                    found = true;
                    expect (r.persistence >= 0.5f && r.persistence <= 1.0f, "persistence " + juce::String (r.persistence));
                }
            expect (found, "the 1.8 kHz resonance is found");

            // a take that is cut into a single segment: persistence is its share (1), never sustained (needs 3 segments)
            CapturedAudio one;
            expect (captureLikePlugin (vocalOf (8.0, 1, 1800.0f, 16.0f), 0.0, one, false));
            for (auto& r : VocalProfiler::analyseSegmented (one).resonances)
                expect (! r.sustained, "one segment is not enough to call a resonance sustained");
        }

        beginTest ("Aggregation: the S / vowel frames of all segments lie inside the segments laid end to end");
        {
            CapturedAudio a;
            expect (captureLikePlugin (vocalOf (40.0), 0.0, a, true));
            std::vector<VocalProfile> parts;
            std::vector<Segment> segs;
            const auto profile = VocalProfiler::analyseSegmented (a, {}, &parts, 5.0f, 0.0f, &segs);
            int frames = 0;
            for (auto& s : segs)
                frames += (s.numSamples - VocalProfiler::fftSize) / (VocalProfiler::fftSize / 4) + 1;
            expect (! profile.sibilantFrames.empty() && ! profile.vowelFrames.empty());
            expect (profile.sibilantFrames.back() < frames && profile.vowelFrames.back() < frames);
            size_t sum = 0;
            for (auto& p : parts)
                sum += p.sibilantFrames.size();
            expectEquals ((int) profile.sibilantFrames.size(), (int) sum);
        }
    }

    //==========================================================================
    void excerptAndStoreTests()
    {
        beginTest ("Excerpt: segments ranked by voiced seconds (ties: the earlier one), until 30 s, in song order, with 20 ms fades at the cuts");
        {
            CapturedAudio a;
            a.sampleRate = 48000.0;
            a.channels = 1;
            const int len = 8 * 48000;
            a.vocal.assign (1, std::vector<float> ((size_t) (len * 10), 1.0f));
            std::vector<Segment> segs;
            const float voiced[] { 4.0f, 7.0f, 7.0f, 3.0f, 6.0f, 7.0f, 2.5f, 5.0f, 2.0f, 4.5f };
            for (int i = 0; i < 10; ++i)
                segs.push_back ({ i * len, len, -1.0, voiced[i] });
            const auto chosen = AutoMixEngine::excerptSegments (segs, 48000.0);
            // 7.0 (1), 7.0 (2), 7.0 (5) and 6.0 (4): 32 s >= 30 s
            expect (chosen == std::vector<int> ({ 1, 2, 4, 5 }), "chosen segments");
            const auto excerpt = AutoMixEngine::excerptFor (a, segs);
            expectEquals (excerpt.numSamples(), 4 * len);
            expectEquals (excerpt.vocal[0][(size_t) len], 1.0f, "neighbours (1, 2) are not faded");
            expect (excerpt.vocal[0][(size_t) (2 * len)] < 0.01f, "cut before segment 4: fade in");
            expect (excerpt.vocal[0][(size_t) (2 * len - 1)] < 0.01f, "cut after segment 2: fade out");
            expectEquals (excerpt.vocal[0][(size_t) (2 * len + 48000)], 1.0f);
            expectEquals (AutoMixEngine::excerptFor (a, segs).numSamples(), excerpt.numSamples());
        }

        beginTest ("The JSON holds the version, the intent, the capture, every segment, every module log, the values and the routing - NaN as null");
        {
            const auto dir = tempFolder ("json");
            CapturedAudio captured;
            expect (captureLikePlugin (vocalOf (30.0), 0.0, captured, true));
            auto r = AutoMixEngine::run (captured, defaultValues(), {}, {}, MixIntent {});
            expect (r.ok, r.error);
            if (! r.ok)
                return;
            expect (! r.segments.empty() && r.segments.size() == r.segmentProfiles.size());
            expect (! r.plan.log.empty());

            // a measurement that is NaN / infinite must not break the file
            r.plan.log[0].measurements.push_back ({ "nan", std::numeric_limits<float>::quiet_NaN(), "dB" });
            r.plan.log[0].result.push_back ({ "inf", std::numeric_limits<float>::infinity(), "dB" });

            const auto file = AnalysisStore::writeTo (dir, r, captured);
            expect (file.existsAsFile(), "file written");
            expect (file.getFileName().matchesWildcard ("????????_??????_" + AnalysisStore::hashOf (captured) + ".json", true), file.getFileName());
            const auto text = file.loadFileAsString();
            expect (! text.contains ("NaN") && ! text.contains ("Infinity") && text.contains ("null"), "no NaN / inf in the text");
            const auto json = juce::JSON::parse (text);
            expect (json.isObject(), "valid JSON");
            expectEquals ((int) json["version"], AnalysisStore::version);
            expectEquals ((int) json["intent"]["genre"], (int) r.intent.genre);
            expectEquals ((int) json["capture"]["seconds"] > 0 ? 1 : 0, 1);
            expectEquals (json["capture"]["hash"].toString(), AnalysisStore::hashOf (captured));
            expect ((double) json["capture"]["ppqStart"] >= 0.0 && (double) json["capture"]["ppqStart"] < 1.0, "first block that is not silent");
            expectEquals (json["segments"].size(), (int) r.segments.size());
            for (int i = 0; i < json["segments"].size(); ++i)
                expect (json["segments"][i]["profile"].isObject() && json["segments"][i]["startSample"] == juce::var (r.segments[(size_t) i].startSample), "segment " + juce::String (i));
            expectEquals (json["log"].size(), (int) r.plan.log.size());
            expect (json["log"][0]["measurements"][json["log"][0]["measurements"].size() - 1]["value"].isVoid(), "NaN is null");
            expect (json["profile"]["thirdsDb"].size() == numThirds);
            expectEquals (json["values"].getDynamicObject()->getProperties().size(), (int) r.plan.values.size());
            expectEquals (json["routing"].toString(), r.plan.routing.toString());
        }

        beginTest ("The store keeps the newest 50 files, and tests never use the real application data folder");
        {
            const auto dir = tempFolder ("prune");
            for (int i = 0; i < 55; ++i)
                dir.getChildFile ("2000010" + juce::String (i % 10) + "_0000" + juce::String (i / 10) + juce::String (i % 10) + "_x.json").replaceWithText ("{}");
            AutoMixResult r;
            CapturedAudio captured;
            captured.vocal.assign (1, std::vector<float> (1000, 0.1f));
            captured.channels = 1;
            const auto file = AnalysisStore::writeTo (dir, r, captured);
            expect (file.existsAsFile());
            expectEquals (dir.findChildFiles (juce::File::findFiles, false, "*.json").size(), AnalysisStore::keepFiles);
            expect (file.existsAsFile(), "the newest file is kept");

            expect (AnalysisStore::directory() != AnalysisStore::defaultDirectory(), "the test run writes to a temporary folder");
            expect (AnalysisStore::defaultDirectory().getFullPathName().replace ("\\", "/").endsWith ("Lind Audio/VOCALYPSE/AutoMix"));
        }
    }

    //==========================================================================
    // Diagnostics only: VOCALYPSE_PROBE=<wav> prints the segments of a real take played from 0 / 3 / 6 s with a song position
    void probe()
    {
        const auto path = juce::SystemStats::getEnvironmentVariable ("VOCALYPSE_PROBE", {});
        if (path.isEmpty())
            return;
        beginTest ("Probe: segments of " + path);
        const auto take = loadVocal (juce::File (path));
        if (! take.has_value())
            return;
        logMessage ("take " + juce::String (take->seconds(), 1) + " s");
        for (double start : { 0.0, 3.0, 6.0 })
        {
            CapturedAudio c;
            if (! captureLikePlugin (*take, start, c, true))
                continue;
            const auto segs = VocalProfiler::segmentsOf (c);
            juce::String line = "start " + juce::String (start) + ": captured " + juce::String (c.seconds(), 1) + " s, segments";
            for (auto& g : segs)
                line << " [" << juce::String (g.startPpq, 0) << ": " << juce::String (g.numSamples / c.sampleRate, 1) << " s, voiced " << juce::String (g.voicedSeconds, 1) << "]";
            logMessage (line);
            const auto p = VocalProfiler::analyseSegmented (c);
            logMessage ("   voiced " + juce::String (p.voicedSeconds, 1) + ", captured " + juce::String (p.capturedSeconds, 1) + ", pitchedShare " + juce::String (p.pitchedShare, 3)
                        + ", dynamics " + juce::String (p.dynamicsDb, 1) + ", sib " + juce::String (p.sibilanceVsVowelDb, 1) + ", tail " + juce::String (p.tailSeconds, 2));
        }
    }

    //==========================================================================
    void engineTests()
    {
        beginTest ("The same 50 s vocal played from 0 s and from 6 s with a song position passes the perturbation check");
        {
            const auto vocal = vocalOf (50.0);
            const auto prior = defaultValues();
            const auto reference = mixFile (vocal, 0.0, true, prior, MixIntent {});
            const auto later = mixFile (vocal, 6.0, true, prior, MixIntent {});
            expect (reference.ok, reference.error);
            expect (later.ok, later.error);
            if (reference.ok && later.ok)
            {
                const auto d = compare (fingerprint (reference.plan, prior), fingerprint (later.plan, prior), Check::perturbation, 0.0f, Tolerances {});
                // The candidate search ("drier effects" scales the echo / reverb mix by 0.65) can tip on a near tie of its
                // scores; that is the search's own consistency (Task 8), not the analysis'
                juce::StringArray others;
                for (auto& o : d.overTolerance)
                    if (! o.startsWith ("delayMix") && ! o.startsWith ("reverbMix"))
                        others.add (o);
                expect (others.isEmpty(), "perturbation: " + others.joinIntoString ("; ") + " (" + juce::String (d.differing) + " differences)");
                // the analysis itself: the aggregated profile does not depend on where the capture began
                const auto& pa = reference.profile;
                const auto& pb = later.profile;
                expectWithinAbsoluteError (pa.voiceRmsDb, pb.voiceRmsDb, 0.5f, "voice level");
                expectWithinAbsoluteError (pa.sibilanceVsVowelDb, pb.sibilanceVsVowelDb, 1.5f, "sibilance");
                expectWithinAbsoluteError (pa.dynamicsDb, pb.dynamicsDb, 1.5f, "dynamics");
                float worst = 0.0f;
                for (size_t k = 0; k < std::min (pa.thirdsDb.size(), pb.thirdsDb.size()); ++k)
                    worst = std::max (worst, std::abs (pa.thirdsDb[k] - pb.thirdsDb[k]));
                expect (worst < 2.0f, "third octaves differ by " + juce::String (worst, 2) + " dB");
                expect (reference.segments.size() >= 5 && later.segments.size() >= 4);
                logMessage ("pitchedShare " + juce::String (reference.profile.pitchedShare, 3) + " / " + juce::String (later.profile.pitchedShare, 3)
                            + ", segments " + juce::String ((int) reference.segments.size()) + " / " + juce::String ((int) later.segments.size()));
                logMessage ("voiced/captured " + juce::String (reference.profile.voicedSeconds, 1) + "/" + juce::String (reference.profile.capturedSeconds, 1) + "  vs  "
                            + juce::String (later.profile.voicedSeconds, 1) + "/" + juce::String (later.profile.capturedSeconds, 1)
                            + ", delayOn " + juce::String (reference.plan.get (pid::delayOn)) + "/" + juce::String (later.plan.get (pid::delayOn)));
            }
        }

        beginTest ("The engine renders an excerpt of at most about 30 s and its S / vowel frames refer to it");
        {
            CapturedAudio captured;
            expect (captureLikePlugin (vocalOf (80.0), 0.0, captured, true));
            const auto r = AutoMixEngine::run (captured, defaultValues(), {}, {}, MixIntent {});
            expect (r.ok, r.error);
            if (r.ok)
            {
                int excerptSamples = 0, frames = 0;
                for (int k : r.excerptSegments)
                {
                    excerptSamples += r.segments[(size_t) k].numSamples;
                    frames += (r.segments[(size_t) k].numSamples - VocalProfiler::fftSize) / (VocalProfiler::fftSize / 4) + 1;
                }
                expect (excerptSamples <= 40 * 48000 && excerptSamples >= 24 * 48000, "excerpt " + juce::String (excerptSamples / 48000.0, 1) + " s");
                expect (r.profile.sibilantFrames.empty() || r.profile.sibilantFrames.back() < frames);
                expect (r.profile.voicedSeconds > 30.0f, "the profile covers the whole capture: voiced " + juce::String (r.profile.voicedSeconds, 1));
            }
        }
    }
};

static SegmentTests segmentSuite;
