#include "VocalProfile.h"
#include "AutoEq.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

// Segmented, start-independent analysis (plan 2026-11-04-automix-konsistenz, Task 5, U1): the capture is cut into
// segments that do not depend on where the capture began, every segment is analysed on its own, and the results
// are aggregated by fixed rules. The same audio gives the same segments - and so the same profile - from any start.
namespace vocalypse::automix
{
    namespace
    {
        constexpr int nb = VocalProfiler::numBands;
        constexpr int hop = VocalProfiler::fftSize / 4;
        constexpr double fixedSegmentSeconds = 8.0;
        constexpr float minSegmentVoicedSeconds = 2.0f;
        constexpr double ppqEps = 1.0e-6;   // quarter notes: 0.5 us at 120 BPM, far below one sample

        float toDb (double power) { return (float) (10.0 * std::log10 (std::max (power, 1.0e-20))); }

        float percentile (std::vector<float> v, float p)
        {
            if (v.empty())
                return 0.0f;
            const auto k = (size_t) juce::jlimit (0, (int) v.size() - 1, (int) std::round (p * (float) (v.size() - 1)));
            std::nth_element (v.begin(), v.begin() + (long) k, v.end());
            return v[k];
        }

        // 50 ms mid-signal levels of audio[from, from + length), aligned to `from`
        void blockLevels (const CapturedAudio& a, int from, int length, std::vector<float>& out)
        {
            const int block = std::max (64, (int) std::lround (0.05 * a.sampleRate));
            const auto& L = a.vocal[0];
            const auto& R = a.channels > 1 ? a.vocal[1] : a.vocal[0];
            for (int start = from; start + block <= from + length; start += block)
            {
                double s = 0.0;
                for (int i = start; i < start + block; ++i)
                {
                    const double m = 0.5 * ((double) L[(size_t) i] + (double) R[(size_t) i]);
                    s += m * m;
                }
                out.push_back (toDb (s / block));
            }
        }

        struct RawSegment
        {
            Segment seg;
            double expectedSamples = 0.0;
        };

        // Tempo class: 4 bars when that is 6 .. 12 s, 8 bars above 160 BPM, 2 bars below 80 BPM
        double segmentBeats (double bpm)
        {
            const double fourBars = 16.0 * 60.0 / bpm;
            return fourBars < 6.0 ? 32.0 : (fourBars > 12.0 ? 8.0 : 16.0);
        }

        std::vector<RawSegment> rawSegments (const CapturedAudio& a)
        {
            std::vector<RawSegment> out;
            const int n = a.numSamples();
            const double fs = a.sampleRate;
            constexpr int pb = CapturedAudio::ppqBlock;

            int known = 0;
            for (double q : a.blockPpq)
                known += q >= 0.0 ? 1 : 0;
            const bool withPpq = a.hostBpm > 0.0 && a.blockPpq.size() > 0 && known * 10 >= (int) a.blockPpq.size() * 9;

            if (! withPpq)
            {
                const int length = std::max (1, (int) std::lround (fixedSegmentSeconds * fs));
                for (int start = 0; start < n; start += length)
                {
                    RawSegment r;
                    r.seg.startSample = start;
                    r.seg.numSamples = std::min (length, n - start);
                    r.expectedSamples = length;
                    out.push_back (r);
                }
                return out;
            }

            const double beats = segmentBeats (a.hostBpm);
            const double perSample = a.hostBpm / 60.0 / fs;
            const double expected = beats / perSample;
            long long current = std::numeric_limits<long long>::min();
            int runStart = 0;
            const auto close = [&] (int end)
            {
                if (end > runStart && current != std::numeric_limits<long long>::min())
                {
                    RawSegment r;
                    r.seg.startSample = runStart;
                    r.seg.numSamples = end - runStart;
                    r.seg.startPpq = (double) current * beats;
                    r.expectedSamples = expected;
                    out.push_back (r);
                }
                if (current != std::numeric_limits<long long>::min())
                    runStart = end;
            };

            for (int c = 0; c * pb < n; ++c)
            {
                const int chunkStart = c * pb;
                if ((size_t) c >= a.blockPpq.size() || a.blockPpq[(size_t) c] < 0.0)
                    continue;   // unknown position: the block belongs to the segment it is in
                const double q = a.blockPpq[(size_t) c];
                const long long index = (long long) std::floor ((q + ppqEps) / beats);
                if (index != current)
                {
                    close (chunkStart);
                    current = index;
                }
                // The boundary may fall inside this block: cut at the sample
                const double next = (double) (current + 1) * beats;
                const long long offset = std::llround ((next - q) / perSample);
                const int chunkEnd = std::min (n, chunkStart + pb);
                if (offset > 0 && chunkStart + offset < chunkEnd)
                {
                    close (chunkStart + (int) offset);
                    current = index + 1;
                }
            }
            close (n);
            return out;
        }

        CapturedAudio sliceOf (const CapturedAudio& a, const Segment& s)
        {
            CapturedAudio out;
            out.sampleRate = a.sampleRate;
            out.channels = a.channels;
            out.stoppedEarly = a.stoppedEarly;
            out.hostBpm = a.hostBpm;
            for (auto& c : a.vocal)
                out.vocal.emplace_back (c.begin() + s.startSample, c.begin() + s.startSample + s.numSamples);
            if (a.hasBeat())
                for (auto& c : a.beat)
                    out.beat.emplace_back (c.begin() + s.startSample, c.begin() + s.startSample + s.numSamples);
            return out;
        }

        // The voice level distribution of the segments pooled: the 50 ms mid blocks of every segment (as
        // VocalProfiler::analyse measures them), gated against the loud blocks of all of them. Percentiles of the
        // pool, not a median of per-segment medians: with two or three segments that median is one segment's level
        // The word / phrase dynamics likewise: the 400 ms windows of every segment (95th - 10th percentile of the pool)
        struct PooledLevels
        {
            bool valid = false;
            float median = -100.0f, low = -100.0f, high = -100.0f;
            bool dynamicsValid = false;
            float dynamicsDb = 0.0f;
        };
        PooledLevels pooledLevelsOf (const CapturedAudio& a, const std::vector<Segment>& segments)
        {
            PooledLevels out;
            if (a.vocal.empty() || a.sampleRate <= 0.0)
                return out;
            const int block = std::max (64, (int) std::lround (0.05 * a.sampleRate));
            const auto& L = a.vocal[0];
            const auto& R = a.vocal.size() > 1 ? a.vocal[1] : a.vocal[0];
            std::vector<float> blockDb;
            std::vector<std::pair<size_t, size_t>> ranges;   // the blocks of each segment in blockDb
            for (auto& seg : segments)
            {
                const size_t first = blockDb.size();
                for (int start = seg.startSample; start + block <= seg.startSample + seg.numSamples; start += block)
                {
                    double sum = 0.0;
                    for (int i = start; i < start + block; ++i)
                    {
                        const float m = 0.5f * (L[(size_t) i] + R[(size_t) i]);
                        sum += (double) m * m;
                    }
                    blockDb.push_back (toDb (sum / block));
                }
                ranges.push_back ({ first, blockDb.size() });
            }
            if (blockDb.empty())
                return out;
            const float gate = std::max (-50.0f, percentile (blockDb, 0.95f) - 35.0f);

            // 8 x 50 ms windows, hop 4 blocks, inside one segment (no window across a cut), mostly voice
            std::vector<float> windowDb;
            for (auto [first, last] : ranges)
                for (size_t start = first; start + 8 <= last; start += 4)
                {
                    double pw = 0.0;
                    int voicedCount = 0;
                    for (size_t i = start; i < start + 8; ++i)
                    {
                        pw += std::pow (10.0, blockDb[i] / 10.0);
                        voicedCount += blockDb[i] > gate ? 1 : 0;
                    }
                    if (voicedCount >= 6)
                        windowDb.push_back (toDb (pw / 8.0));
                }
            if (windowDb.size() >= 8)
            {
                out.dynamicsValid = true;
                out.dynamicsDb = percentile (windowDb, 0.95f) - percentile (windowDb, 0.10f);
            }
            std::vector<float> voiced;
            for (float b : blockDb)
                if (b > gate)
                    voiced.push_back (b);
            if (voiced.empty())
                return out;
            out.valid = true;
            out.median = percentile (voiced, 0.5f);
            out.low = percentile (voiced, 0.1f);
            out.high = percentile (voiced, 0.9f);
            return out;
        }

        int framesIn (int numSamples) { return numSamples >= VocalProfiler::fftSize ? (numSamples - VocalProfiler::fftSize) / hop + 1 : 0; }

        //======================================================================
        // Aggregation helpers: all deterministic (sort with the index as tie-break)
        struct Weighted
        {
            const std::vector<VocalProfile>& ps;
            std::vector<double> w;
            double total = 0.0;

            explicit Weighted (const std::vector<VocalProfile>& profiles) : ps (profiles)
            {
                for (auto& p : ps)
                {
                    w.push_back (std::max (1.0e-3, (double) p.voicedSeconds));
                    total += w.back();
                }
            }

            template <typename Get, typename Use>
            float median (Get get, Use use, float fallback) const
            {
                std::vector<std::pair<float, size_t>> v;
                double sum = 0.0;
                for (size_t i = 0; i < ps.size(); ++i)
                    if (use (ps[i]) && std::isfinite ((double) get (ps[i])))
                    {
                        v.push_back ({ (float) get (ps[i]), i });
                        sum += w[i];
                    }
                if (v.empty())
                    return fallback;
                std::sort (v.begin(), v.end());
                double cum = 0.0;
                for (auto& [value, i] : v)
                {
                    cum += w[i];
                    if (cum >= 0.5 * sum)
                        return value;
                }
                return v.back().first;
            }
            template <typename Get>
            float median (Get get, float fallback = 0.0f) const { return median (get, [] (const VocalProfile&) { return true; }, fallback); }

            template <typename Use>
            bool majority (Use use) const
            {
                double yes = 0.0;
                for (size_t i = 0; i < ps.size(); ++i)
                    yes += use (ps[i]) ? w[i] : 0.0;
                return yes > 0.5 * total;
            }
        };
    }

    //==========================================================================
    std::vector<Segment> VocalProfiler::segmentsOf (const CapturedAudio& a)
    {
        std::vector<Segment> result;
        const int n = a.numSamples();
        if (n <= 0 || a.channels < 1 || a.sampleRate <= 0.0)
            return result;

        auto raw = rawSegments (a);

        // Voice: 50 ms blocks (aligned to the segment start) within 35 dB of the loud blocks of all segments -
        // relative only, so the same audio at another level gives the same segments
        std::vector<std::vector<float>> levels (raw.size());
        std::vector<float> all;
        for (size_t i = 0; i < raw.size(); ++i)
        {
            blockLevels (a, raw[i].seg.startSample, raw[i].seg.numSamples, levels[i]);
            all.insert (all.end(), levels[i].begin(), levels[i].end());
        }
        if (all.empty())
            return result;
        const float gate = std::max (-90.0f, percentile (all, 0.95f) - 35.0f);
        const double block = std::max (64, (int) std::lround (0.05 * a.sampleRate));
        for (size_t i = 0; i < raw.size(); ++i)
        {
            int voiced = 0;
            for (float b : levels[i])
                voiced += b > gate ? 1 : 0;
            raw[i].seg.voicedSeconds = (float) (voiced * block / a.sampleRate);
        }

        int complete = 0;
        for (auto& r : raw)
            if (r.seg.voicedSeconds >= minSegmentVoicedSeconds && (double) r.seg.numSamples >= 0.9 * r.expectedSamples)
                ++complete;
        for (auto& r : raw)
        {
            if (r.seg.voicedSeconds < minSegmentVoicedSeconds)
                continue;
            if (complete >= 3 && (double) r.seg.numSamples < 0.9 * r.expectedSamples)
                continue;
            result.push_back (r.seg);
        }
        return result;
    }

    //==========================================================================
    void VocalProfiler::framesForExcerpt (VocalProfile& p, const std::vector<VocalProfile>& segmentProfiles,
                                          const std::vector<Segment>& segments, const std::vector<int>& chosen)
    {
        p.sibilantFrames.clear();
        p.vowelFrames.clear();
        // The audio is the segments end to end (AutoMixEngine::excerptFor keeps every segment's length), and
        // sibilanceAt reads frame i at i * hop: a segment that starts `prefix` samples in has its frame f at
        // round(prefix / hop) + f
        long long prefix = 0;
        for (int k : chosen)
        {
            const int offset = (int) std::llround ((double) prefix / hop);
            if (k < 0 || (size_t) k >= segments.size() || (size_t) k >= segmentProfiles.size())
                continue;
            for (int f : segmentProfiles[(size_t) k].sibilantFrames)
                p.sibilantFrames.push_back (offset + f);
            for (int f : segmentProfiles[(size_t) k].vowelFrames)
                p.vowelFrames.push_back (offset + f);
            prefix += segments[(size_t) k].numSamples;
        }
    }

    //==========================================================================
    VocalProfile VocalProfiler::analyseSegmented (const CapturedAudio& audio, const std::function<bool()>& shouldStop,
                                                  std::vector<VocalProfile>* segmentProfilesOut, float minVoicedSeconds,
                                                  float scaleDb, std::vector<Segment>* segmentsOut)
    {
        const auto stop = [&shouldStop] { return shouldStop && shouldStop(); };
        const auto segments = segmentsOf (audio);
        if (segmentProfilesOut != nullptr)
            segmentProfilesOut->clear();
        if (segmentsOut != nullptr)
            *segmentsOut = segments;

        if (segments.empty())
            return analyse (audio, minVoicedSeconds, shouldStop, scaleDb);

        std::vector<VocalProfile> ps;
        for (auto& seg : segments)
        {
            if (stop())
                return {};
            ps.push_back (analyse (sliceOf (audio, seg), 0.0f, shouldStop, scaleDb));
        }
        if (stop())
            return {};
        const Weighted wt (ps);
        const int n = audio.numSamples();
        const int numSegments = (int) ps.size();

        VocalProfile p;
        p.sampleRate = audio.sampleRate;
        p.channels = audio.channels;
        // The seconds that were analysed (the segments), not the length of the capture: a decision that looks at the
        // voiced share (the echo) must not depend on how much of a cut-off start or end the capture happened to hold
        p.stoppedEarly = audio.stoppedEarly;
        p.hostBpm = audio.hostBpm;
        for (auto& s : ps)
        {
            p.voicedSeconds += s.voicedSeconds;
            p.capturedSeconds += s.capturedSeconds;
        }
        p.enoughMaterial = p.voicedSeconds >= minVoicedSeconds;

        const auto valid = [] (auto flag) { return [flag] (const VocalProfile& q) { return q.*flag; }; };

        // ---- Level. Clipping and the peak describe the whole capture before the working-level scaling
        {
            const float clipLevel = 0.999f * juce::Decibels::decibelsToGain (scaleDb);
            float peak = 0.0f;
            for (auto& c : audio.vocal)
            {
                int run = 0;
                for (int i = 0; i < n; ++i)
                {
                    const float a = std::abs (c[(size_t) i]);
                    peak = std::max (peak, a);
                    if (a >= clipLevel)
                    {
                        if (++run == 3)
                            ++p.clipRuns;
                    }
                    else
                        run = 0;
                }
            }
            p.peakDb = peak > 0.0f ? juce::Decibels::gainToDecibels (peak) - scaleDb : -100.0f;
        }
        p.integratedLufs = wt.median ([] (auto& q) { return q.integratedLufs; }, -100.0f);
        p.loudnessRangeValid = wt.majority (valid (&VocalProfile::loudnessRangeValid));
        if (p.loudnessRangeValid)
            p.loudnessRangeDb = wt.median ([] (auto& q) { return q.loudnessRangeDb; }, valid (&VocalProfile::loudnessRangeValid), 0.0f);
        p.crestDb = wt.median ([] (auto& q) { return q.crestDb; });
        const auto pooled = pooledLevelsOf (audio, segments);
        p.dynamicsValid = pooled.dynamicsValid;
        if (p.dynamicsValid)
            p.dynamicsDb = pooled.dynamicsDb;
        if (const auto& levels = pooled; levels.valid)
        {
            p.voiceRmsDb = levels.median;
            p.voiceRmsLowDb = levels.low;
            p.voiceRmsHighDb = levels.high;
        }
        else
        {
            p.voiceRmsDb = wt.median ([] (auto& q) { return q.voiceRmsDb; }, -100.0f);
            p.voiceRmsLowDb = wt.median ([] (auto& q) { return q.voiceRmsLowDb; }, -100.0f);
            p.voiceRmsHighDb = wt.median ([] (auto& q) { return q.voiceRmsHighDb; }, -100.0f);
        }
        p.snrValid = wt.majority (valid (&VocalProfile::snrValid));
        if (p.snrValid)
        {
            p.noiseFloorDb = wt.median ([] (auto& q) { return q.noiseFloorDb; }, valid (&VocalProfile::snrValid), -100.0f);
            p.snrDb = wt.median ([] (auto& q) { return q.snrDb; }, valid (&VocalProfile::snrValid), 0.0f);
        }
        p.breathShare = wt.median ([] (auto& q) { return q.breathShare; });

        // ---- Pitch
        p.pitchedShare = wt.median ([] (auto& q) { return q.pitchedShare; });
        p.pitchValid = wt.majority (valid (&VocalProfile::pitchValid));
        if (p.pitchValid)
        {
            const auto use = valid (&VocalProfile::pitchValid);
            p.f0MedianHz = wt.median ([] (auto& q) { return q.f0MedianHz; }, use, 0.0f);
            p.f0LowHz = wt.median ([] (auto& q) { return q.f0LowHz; }, use, 0.0f);
            p.f0HighHz = wt.median ([] (auto& q) { return q.f0HighHz; }, use, 0.0f);
            p.pitchSpreadSemitones = 12.0f * std::log2 (std::max (1.0f, p.f0HighHz) / std::max (1.0f, p.f0LowHz));

            // A note counts when it carries at least a quarter of the pitched time of the segments (a semitone grid)
            std::map<int, double> weight;
            double pitched = 0.0;
            for (size_t i = 0; i < ps.size(); ++i)
                if (ps[i].pitchValid)
                {
                    pitched += wt.w[i];
                    for (float f : ps[i].sungNotesHz)
                        weight[(int) std::lround (12.0f * std::log2 (f / 440.0f))] += wt.w[i];
                }
            for (auto& [semi, w] : weight)
                if (w >= 0.25 * pitched)
                    p.sungNotesHz.push_back (440.0f * std::pow (2.0f, (float) semi / 12.0f));
        }

        // ---- Spectrum: power mean over all voiced frames (the segments' means, weighted by their voiced time)
        {
            std::array<double, nb> acc {};
            double weight = 0.0;
            for (size_t i = 0; i < ps.size(); ++i)
                if (ps[i].spectrumDb.size() == (size_t) nb)
                {
                    for (int b = 0; b < nb; ++b)
                        if (ps[i].spectrumDb[(size_t) b] > -190.0f)
                            acc[(size_t) b] += wt.w[i] * std::pow (10.0, ps[i].spectrumDb[(size_t) b] / 10.0);
                    weight += wt.w[i];
                }
            if (weight > 0.0)
            {
                p.spectrumDb.resize (nb);
                for (int b = 0; b < nb; ++b)
                    p.spectrumDb[(size_t) b] = acc[(size_t) b] > 0.0 ? toDb (acc[(size_t) b] / weight) : -200.0f;
                p.thirdsDb = thirdsOf (p.spectrumDb);
            }
        }
        p.rumbleDb = wt.median ([] (auto& q) { return q.rumbleDb; }, -60.0f);
        p.bodyDb = wt.median ([] (auto& q) { return q.bodyDb; }, -60.0f);
        p.lowMidDb = wt.median ([] (auto& q) { return q.lowMidDb; }, -60.0f);
        p.presenceDb = wt.median ([] (auto& q) { return q.presenceDb; }, -60.0f);
        p.sibilanceBandDb = wt.median ([] (auto& q) { return q.sibilanceBandDb; }, -60.0f);
        p.airDb = wt.median ([] (auto& q) { return q.airDb; }, -60.0f);
        p.topDb = wt.median ([] (auto& q) { return q.topDb; }, -60.0f);
        p.tiltDbPerOctave = wt.median ([] (auto& q) { return q.tiltDbPerOctave; });
        p.lowMidPeakHz = wt.median ([] (auto& q) { return q.lowMidPeakHz; }, 300.0f);

        // ---- Articulation
        p.onsetsValid = wt.majority (valid (&VocalProfile::onsetsValid));
        if (p.onsetsValid)
        {
            p.onsetRate = wt.median ([] (auto& q) { return q.onsetRate; }, valid (&VocalProfile::onsetsValid), 0.0f);
            p.onsetSharpnessDb = wt.median ([] (auto& q) { return q.onsetSharpnessDb; }, valid (&VocalProfile::onsetsValid), 0.0f);
        }

        // ---- Sibilance
        p.sibilantShare = wt.median ([] (auto& q) { return q.sibilantShare; });
        p.sibilanceValid = wt.majority (valid (&VocalProfile::sibilanceValid));
        if (p.sibilanceValid)
        {
            const auto use = valid (&VocalProfile::sibilanceValid);
            p.sibilanceVsVowelDb = wt.median ([] (auto& q) { return q.sibilanceVsVowelDb; }, use, -100.0f);
            p.sibilanceHz = wt.median ([] (auto& q) { return q.sibilanceHz; }, use, 6500.0f);
        }
        p.deessDetectorHz = juce::jlimit (4000.0f, 8000.0f, p.sibilanceHz * 0.78f);
        p.sibBandVowelP90Db = wt.median ([] (auto& q) { return q.sibBandVowelP90Db; }, [] (auto& q) { return q.sibBandVowelP90Db > -99.0f; }, -100.0f);
        p.sibBandSibilantP50Db = wt.median ([] (auto& q) { return q.sibBandSibilantP50Db; }, [] (auto& q) { return q.sibBandSibilantP50Db > -99.0f; }, -100.0f);

        // S and vowel frames of all segments, the segments laid end to end
        {
            std::vector<int> all;
            for (int i = 0; i < numSegments; ++i)
                all.push_back (i);
            framesForExcerpt (p, ps, segments, all);
        }

        // ---- Resonances: present in at least half of the segments; persistence = that share
        {
            struct Cluster { std::vector<float> hz, excess, q; int segments = 0, lastSegment = -1; };
            std::vector<Cluster> clusters;
            for (int i = 0; i < numSegments; ++i)
                for (auto& r : ps[(size_t) i].resonances)
                {
                    Cluster* found = nullptr;
                    for (auto& c : clusters)
                        if (std::abs (std::log2 (r.hz / c.hz.front())) < 1.5f / 12.0f)
                        {
                            found = &c;
                            break;
                        }
                    if (found == nullptr)
                    {
                        clusters.emplace_back();
                        found = &clusters.back();
                    }
                    found->hz.push_back (r.hz);
                    found->excess.push_back (r.excessDb);
                    found->q.push_back (r.q);
                    if (found->lastSegment != i)
                    {
                        found->lastSegment = i;
                        ++found->segments;
                    }
                }

            const auto middle = [] (std::vector<float> v)
            {
                std::sort (v.begin(), v.end());
                return v[v.size() / 2];
            };
            const auto& spec = p.spectrumDb;
            for (auto& c : clusters)
            {
                const float share = (float) c.segments / (float) numSegments;
                if (share < 0.5f)
                    continue;
                Resonance r;
                r.hz = middle (c.hz);
                r.excessDb = middle (c.excess);
                r.strong = r.excessDb > 10.0f;
                r.persistence = share;
                r.q = middle (c.q);
                const int b = (int) std::lround (12.0 * std::log2 (r.hz / 40.0f));
                if (spec.size() == (size_t) nb && b >= 2 && b < nb - 2)
                {
                    // Narrow: clearly above the bands 1/6 octave away; the width sets the notch Q
                    const float s = spec[(size_t) b];
                    const bool narrow = s - std::max (spec[(size_t) b - 2], spec[(size_t) b + 2]) >= 3.0f;
                    r.sustained = narrow && share >= 0.75f && numSegments >= 3;
                    const int wide = (spec[(size_t) b - 1] > s - 4.0f ? 1 : 0) + (spec[(size_t) b + 1] > s - 4.0f ? 1 : 0);
                    r.q = wide == 0 ? 16.0f : (wide == 1 ? 12.0f : 10.0f);
                }
                p.resonances.push_back (r);
            }
            std::stable_sort (p.resonances.begin(), p.resonances.end(), [] (const Resonance& a, const Resonance& b)
            {
                if (a.sustained != b.sustained)
                    return a.sustained;
                if (a.excessDb != b.excessDb)
                    return a.excessDb > b.excessDb;
                return a.hz < b.hz;
            });
            if (p.resonances.size() > 3)
                p.resonances.resize (3);
        }

        // ---- Tails
        {
            for (auto& s : ps)
                p.tailCount += s.tailCount;
            const auto use = [] (const VocalProfile& q) { return q.tailCount >= 3; };
            if (p.tailCount >= 3 && wt.majority (use))
            {
                p.tailSeconds = wt.median ([] (auto& q) { return q.tailSeconds; }, use, 0.0f);
                p.reverberant = p.tailSeconds > 0.6f;
            }
        }

        // ---- Stereo: do the channels differ (whole capture), and how
        if (audio.channels > 1)
        {
            double diff = 0.0, sum = 0.0;
            for (int i = 0; i < n; ++i)
            {
                diff += std::abs (audio.vocal[0][(size_t) i] - audio.vocal[1][(size_t) i]);
                sum += std::abs (audio.vocal[0][(size_t) i]) + std::abs (audio.vocal[1][(size_t) i]);
            }
            p.stereoContent = sum > 0.0 && diff / sum > 1.0e-3;
        }
        if (p.stereoContent)
        {
            const auto use = valid (&VocalProfile::stereoContent);
            p.correlation = wt.median ([] (auto& q) { return q.correlation; }, use, 1.0f);
            p.balanceDb = wt.median ([] (auto& q) { return q.balanceDb; }, use, 0.0f);
            p.sideToMidDb = wt.median ([] (auto& q) { return q.sideToMidDb; }, use, -100.0f);
        }

        p.heavilyCompressed = p.dynamicsValid && p.dynamicsDb < 6.0f && p.crestDb < 10.0f;
        p.distortionLikely = p.clipRuns >= 3 || (p.peakDb > -0.3f && p.crestDb < 6.0f);

        // ---- Beat
        {
            for (auto& s : ps)
                p.beatPlayingSeconds += s.beatPlayingSeconds;
            p.beatPresent = wt.majority (valid (&VocalProfile::beatPresent));
            if (p.beatPresent)
            {
                const auto use = valid (&VocalProfile::beatPresent);
                p.beatLufs = wt.median ([] (auto& q) { return q.beatLufs; }, use, -100.0f);
                p.vocalOverBeatLu = wt.median ([] (auto& q) { return q.vocalOverBeatLu; }, use, 0.0f);
                p.beatGapShare = wt.median ([] (auto& q) { return q.beatGapShare; }, use, 0.0f);
                p.beatPresenceVsVocalDb = wt.median ([] (auto& q) { return q.beatPresenceVsVocalDb; }, use, 0.0f);
                p.beatLowMidVsVocalDb = wt.median ([] (auto& q) { return q.beatLowMidVsVocalDb; }, use, 0.0f);
                p.beatOverVocalDb.assign ((size_t) nb, 0.0f);
                for (int b = 0; b < nb; ++b)
                    p.beatOverVocalDb[(size_t) b] = wt.median ([b] (auto& q) { return q.beatOverVocalDb.size() == (size_t) nb ? q.beatOverVocalDb[(size_t) b] : 0.0f; },
                                                              [] (auto& q) { return q.beatPresent && q.beatOverVocalDb.size() == (size_t) nb; }, 0.0f);
                p.maskingHint = autoeq::maskingOf (p).presenceDb > autoeq::unmaskFromDb;
            }
        }

        buildIssues (p, scaleDb);
        if (segmentProfilesOut != nullptr)
            *segmentProfilesOut = std::move (ps);
        return p;
    }
}
