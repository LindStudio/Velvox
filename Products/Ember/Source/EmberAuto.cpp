#include "EmberAuto.h"
#include "EmberEngine.h"
#include <juce_dsp/juce_dsp.h>
#include <map>
#include <thread>

namespace ember
{
    namespace
    {
        float percentile (std::vector<float> v, float p)
        {
            if (v.empty())
                return 0.0f;
            std::sort (v.begin(), v.end());
            const float pos = juce::jlimit (0.0f, 1.0f, p) * (float) (v.size() - 1);
            const auto i = (size_t) pos;
            const float frac = pos - (float) i;
            return i + 1 < v.size() ? v[i] + frac * (v[i + 1] - v[i]) : v[i];
        }

        double lossDb (double in, double out)
        {
            return in > 0.0 ? 10.0 * std::log10 (std::max (1.0e-20, in) / std::max (1.0e-20, out)) : 0.0;
        }

        enum Kind : char { other = 0, vowel = 1, sibilant = 2, mixed = 3 };
        // Bands every frame is measured in (bins of the analysis FFT)
        enum Band { full, sBand, presence, air, vowelPart, numBands };
    }

    AutoResult analyse (const float* mono, int numSamples, double fs, bool smart, const std::atomic<bool>* cancel)
    {
        Settings base;
        base.smart = smart;
        return analyse (mono, numSamples, fs, base, cancel);
    }

    AutoResult analyse (const float* mono, int numSamples, double fs, const Settings& base, const std::atomic<bool>* cancel)
    {
        const float* ch[1] { mono };
        return analyse (ch, 1, numSamples, fs, base, cancel);
    }

    AutoResult analyse (const float* const* channelsIn, int numChannels, int numSamples, double fs, const Settings& base,
                        const std::atomic<bool>* cancel)
    {
        AutoResult r;
        r.seconds = fs > 0.0 ? numSamples / fs : 0.0;
        if (channelsIn == nullptr || channelsIn[0] == nullptr || numChannels <= 0 || fs <= 0.0 || r.seconds < autoMinSeconds)
            return r;
        const auto cancelled = [cancel] { return cancel != nullptr && cancel->load (std::memory_order_relaxed); };

        // Stereo as the engine detects it (max of the channels); two equal channels are one (half the work)
        int nch = std::min (2, numChannels);
        const float* chans[2] { channelsIn[0], nch > 1 ? channelsIn[1] : channelsIn[0] };
        if (nch == 2 && (chans[1] == nullptr || std::equal (chans[0], chans[0] + numSamples, chans[1])))
            nch = 1;
        r.numChannels = nch;

        const int order = fs <= 50000.0 ? 10 : (fs <= 100000.0 ? 11 : 12);
        const int n = 1 << order, hop = n / 2;
        juce::dsp::FFT fft (order);
        juce::dsp::WindowingFunction<float> window ((size_t) n, juce::dsp::WindowingFunction<float>::hann, false);
        std::vector<float> work ((size_t) n * 2);
        std::vector<double> power ((size_t) (n / 2));
        const int bins = n / 2;
        const auto binOf = [&] (double hz) { return juce::jlimit (1, bins - 1, (int) std::lround (hz * n / fs)); };
        const int b80 = binOf (80.0), b200 = binOf (200.0), b2k = binOf (2000.0), b2500 = binOf (2500.0), b3k = binOf (3000.0);
        const int b4k = binOf (autoSBandHz), b5k = binOf (5000.0), b10k = binOf (10000.0);
        const int bTop = binOf (std::min (16000.0, fs * 0.45));
        const int b12k = std::min (bTop, binOf (12000.0));
        const int bVowelPart = binOf (autoVowelPartHz);

        const int numFrames = numSamples >= n ? 1 + (numSamples - n) / hop : 0;
        if (numFrames < 8)
            return r;

        // Power spectrum of one frame, the channels added (a wide double does not cancel itself out)
        const auto spectrum = [&] (const float* const* x, size_t offset)
        {
            std::fill (power.begin(), power.end(), 0.0);
            for (int c = 0; c < nch; ++c)
            {
                std::fill (work.begin(), work.end(), 0.0f);
                std::copy_n (x[c] + offset, n, work.begin());
                window.multiplyWithWindowingTable (work.data(), (size_t) n);
                fft.performFrequencyOnlyForwardTransform (work.data(), true);
                for (int b = 0; b < bins; ++b)
                    power[(size_t) b] += (double) work[(size_t) b] * work[(size_t) b];
            }
        };
        const auto bandSum = [&] (int lo, int hi)
        {
            double s = 0.0;
            for (int b = lo; b <= hi; ++b)
                s += power[(size_t) b];
            return s;
        };
        const auto bandsOf = [&] (std::array<double, numBands>& e)
        {
            e[full] = bandSum (b80, bTop);
            e[sBand] = bandSum (b4k, bTop);
            e[presence] = bandSum (b2k, b5k);
            e[air] = b10k < bTop ? bandSum (b10k, bTop) : 0.0;
            e[vowelPart] = bandSum (b80, bVowelPart);
        };

        // ---- 1) every frame: level and bands
        std::vector<float> rmsDb ((size_t) numFrames);
        std::vector<std::array<double, numBands>> inBands ((size_t) numFrames);
        std::vector<double> high ((size_t) numFrames), voice ((size_t) numFrames), top ((size_t) numFrames), sMid ((size_t) numFrames);
        std::vector<double> sibSpectrum ((size_t) bins, 0.0);
        for (int f = 0; f < numFrames; ++f)
        {
            if ((f & 63) == 0 && cancelled())
                return r.status = AutoResult::Status::cancelled, r;
            spectrum (chans, (size_t) f * (size_t) hop);
            bandsOf (inBands[(size_t) f]);
            const double total = inBands[(size_t) f][full];
            rmsDb[(size_t) f] = (float) (10.0 * std::log10 (std::max (1.0e-20, total) / ((double) n * n * 0.1875 * nch)));
            high[(size_t) f] = inBands[(size_t) f][sBand];
            voice[(size_t) f] = bandSum (b200, b2500);
            top[(size_t) f] = b12k < bTop ? bandSum (b12k + 1, bTop) : 0.0;
            sMid[(size_t) f] = bandSum (b4k, b12k);
        }
        const float loud = percentile (rmsDb, 0.95f);
        const float gate = std::max (-60.0f, loud - 40.0f);

        // ---- 2) vowels first: the S are judged against them
        std::vector<char> kind ((size_t) numFrames, other);
        std::vector<float> vowelLevels;
        for (int f = 0; f < numFrames; ++f)
            if (rmsDb[(size_t) f] >= gate && high[(size_t) f] <= 0.15 * inBands[(size_t) f][full])
            {
                kind[(size_t) f] = vowel;
                vowelLevels.push_back (rmsDb[(size_t) f]);
            }
        const float vowelRef = vowelLevels.size() >= 10 ? percentile (vowelLevels, 0.5f) : loud - 6.0f;

        // ---- 3) S: the high band outweighs the body, loud enough next to the vowels (a breath or the bleed of a
        // hi-hat sits far below) and not mostly above 12 kHz (hi-hat, cymbals, air alone). (A minimum length of 3
        // frames was tried: it threw out real short S and made the frequency of F_badboy jump between excerpts.)
        for (int f = 0; f < numFrames; ++f)
        {
            if (rmsDb[(size_t) f] < gate || kind[(size_t) f] == vowel)
                continue;
            ++r.voicedFrames;
            const double total = inBands[(size_t) f][full];
            if (! (high[(size_t) f] >= 0.5 * total && high[(size_t) f] > voice[(size_t) f]))
            {
                kind[(size_t) f] = mixed;   // voiced, mixed (consonant-vowel transitions, breathy parts)
                continue;
            }
            const float highDb = (float) (10.0 * std::log10 (std::max (1.0e-20, high[(size_t) f]) / ((double) n * n * 0.1875 * nch)));
            if (highDb >= vowelRef - autoSMaxBelowVowelDb && top[(size_t) f] <= sMid[(size_t) f])
                kind[(size_t) f] = sibilant;
            else
                ++r.rejectedFrames;   // breath, bleed, cymbals: neither S nor voice
        }
        r.voicedFrames += (int) vowelLevels.size();
        r.vowelFrames = (int) vowelLevels.size();

        // Where the S of this voice sit: the energy centre (on a log-frequency scale) of the S spectrum between 3 and
        // 12 kHz, every S frame weighing the same (weighted by their energy, or its square root, a few loud S
        // decided alone and the centre moved more between excerpts of the same take; breath and bleed no longer
        // get in since step 3 rejects them). The peak of that spectrum (used until 2026-11-22) jumped between the
        // two humps of s and sh.
        // The split bell sits on it, the detection corner autoCornerRatio below
        // (Jakob 2026-11-10: "nur subtil die S-Laute wegnehmen, nicht die Stimme zerstören")
        for (int f = 0; f < numFrames; ++f)
        {
            if (kind[(size_t) f] != sibilant)
                continue;
            ++r.sibilantFrames;
            if ((r.sibilantFrames & 31) == 0 && cancelled())
                return r.status = AutoResult::Status::cancelled, r;
            spectrum (chans, (size_t) f * (size_t) hop);
            const double w = 1.0 / std::max (1.0e-20, high[(size_t) f]);
            for (int b = b4k / 2; b <= bTop; ++b)
                sibSpectrum[(size_t) b] += power[(size_t) b] * w;
        }

        if (r.sibilantFrames < 12)
        {
            r.status = AutoResult::Status::noSibilance;
            return r;
        }

        {
            const int bLo = b3k, bHi = binOf (std::min (12000.0, fs * 0.45));
            double sum = 0.0, logSum = 0.0;
            for (int b = bLo; b <= bHi; ++b)
            {
                sum += sibSpectrum[(size_t) b];
                logSum += sibSpectrum[(size_t) b] * std::log2 (b * fs / n);
            }
            r.peakHz = sum > 0.0 ? (float) std::exp2 (logSum / sum) : 7000.0f;
        }
        r.freq = juce::jlimit (autoMinCornerHz, autoMaxCornerHz, r.peakHz * autoCornerRatio);
        r.freq = std::round (r.freq / 50.0f) * 50.0f;

        // ---- 4) detector levels with the chosen corner, exactly like the engine measures them (stereo-linked)
        const bool relative = base.relative || base.smart;
        float detMax = -200.0f;
        {
            Detector det;
            det.prepare (fs);
            det.setFrequency (r.freq);
            std::vector<float> s, v;
            float frameMax = -200.0f;
            int frame = 0, inFrame = 0;
            std::vector<float> chunkMax ((size_t) (numSamples / hop + 1), -200.0f);
            for (int i = 0; i < numSamples; ++i)
            {
                if ((i & 65535) == 0 && cancelled())
                    return r.status = AutoResult::Status::cancelled, r;
                const float x[2] { chans[0][i], chans[1][i] };
                const float level = det.process (x, nch, relative);
                frameMax = std::max (frameMax, level);
                if (++inFrame == hop)
                {
                    chunkMax[(size_t) frame++] = frameMax;
                    frameMax = -200.0f;
                    inFrame = 0;
                }
            }
            chunkMax[(size_t) frame] = std::max (chunkMax[(size_t) frame], frameMax);
            for (float c : chunkMax)
                detMax = std::max (detMax, c);
            for (int f = 0; f < numFrames; ++f)
            {
                const float m = std::max (chunkMax[(size_t) f], chunkMax[(size_t) f + 1]);
                if (kind[(size_t) f] == sibilant)
                    s.push_back (m);
                else if (kind[(size_t) f] == vowel)
                    v.push_back (m);
            }
            r.sibilantMedianDb = percentile (s, 0.5f);
            r.sibilant90Db = percentile (s, 0.9f);
            r.vowel95Db = v.size() >= 10 ? percentile (v, 0.95f) : r.sibilantMedianDb - 12.0f;
        }

        // How loud the S are next to the vowels decides how much to take off: harsh S (close to the vowel level)
        // get up to autoMaxTargetDb, soft S only autoMinTargetDb - never more than the voice needs
        {
            double sSum = 0.0, vSum = 0.0;
            int sN = 0, vN = 0;
            for (int f = 0; f < numFrames; ++f)
            {
                if (kind[(size_t) f] == sibilant) { sSum += rmsDb[(size_t) f]; ++sN; }
                if (kind[(size_t) f] == vowel) { vSum += rmsDb[(size_t) f]; ++vN; }
            }
            r.sibilanceVsVowelDb = (sN > 0 && vN > 0) ? (float) (sSum / sN - vSum / vN) : -8.0f;
            const float t = juce::jlimit (0.0f, 1.0f, (r.sibilanceVsVowelDb - autoSoftSDb) / (autoHarshSDb - autoSoftSDb));
            r.targetDb = autoMinTargetDb + t * (autoMaxTargetDb - autoMinTargetDb);
        }

        // ---- 5) threshold by listening to the result: the engine renders the take (stereo, as the plugin runs),
        // and the threshold is searched so the S frames lose r.targetDb above autoSBandHz while the voice keeps
        // its vowels (full band, presence, air) and the vowel part of the mixed frames
        struct Measured
        {
            bool valid = false;
            float sRed = 0.0f, vRed = 0.0f, vPres = 0.0f, vAir = 0.0f, mRed = 0.0f, mPres = 0.0f;
            std::vector<float> sFrames;
            // > 0: the voice loses more than allowed; the losses relative to their limits (-1 = nothing lost, 0 = at a limit)
            float harm() const noexcept
            {
                return std::max ({ vRed / autoVowelMaxDb, vPres / autoVowelPresenceMaxDb, vAir / autoVowelAirMaxDb,
                                   mRed / autoMixedMaxDb, mPres / autoMixedPresenceMaxDb }) - 1.0f;
            }
            bool keepsVoice() const noexcept { return harm() <= 0.0f; }
        };

        std::vector<float> y[2];
        std::map<std::pair<int, int>, Measured> cache;   // (threshold, range) in 1/100 dB
        const float scale = juce::jlimit (0.1f, 2.0f, amountScale (base.amount));
        const auto measure = [&] (float thr, float range) -> Measured
        {
            const auto key = std::make_pair ((int) std::lround (thr * 100.0f), (int) std::lround (range * 100.0f));
            if (auto it = cache.find (key); it != cache.end())
                return it->second;
            Measured m;
            if (cancelled())
                return m;
            // Above the loudest detector level of the take (plus the knee) the engine does nothing: no render needed
            if (thr >= detMax + kneeDb * 0.5f)
            {
                m.valid = true;
                m.sFrames.assign ((size_t) r.sibilantFrames, 0.0f);
                return cache[key] = m;
            }
            Settings st = base;   // the user's mode, Amount, detection, attack and lookahead
            st.freq = r.freq;
            st.thresholdDb = thr;
            st.rangeDb = range;
            st.releaseMs = autoReleaseMs;
            st.mix = 1.0f;
            st.outputDb = 0.0f;
            st.listen = 0;
            st.bypass = false;
            Engine e;
            e.setSettings (st);
            e.prepare (fs, 4096);
            const int lat = e.getLatencySamples();
            for (int c = 0; c < nch; ++c)
            {
                y[c].assign ((size_t) (numSamples + lat), 0.0f);
                std::copy_n (chans[c], numSamples, y[c].begin());
            }
            for (int start = 0; start < (int) y[0].size(); start += 4096)
            {
                if (cancelled())
                    return m;   // invalid
                float* ch[2] { y[0].data() + start, nch > 1 ? y[1].data() + start : nullptr };
                e.process (ch, nch, std::min (4096, (int) y[0].size() - start));
            }
            ++r.renders;
            const float* out[2] { y[0].data() + lat, nch > 1 ? y[1].data() + lat : nullptr };
            std::array<double, numBands> sIn {}, sOut {}, vIn {}, vOut {}, mIn {}, mOut {}, o {};
            for (int f = 0; f < numFrames; ++f)
            {
                const char k = kind[(size_t) f];
                if (k == other)
                    continue;
                const auto& in = inBands[(size_t) f];
                const size_t at = (size_t) f * (size_t) hop;
                // where EMBER did nothing the output is the input, bit for bit (a 0 dB bell is skipped): no FFT
                bool same = true;
                for (int c = 0; c < nch && same; ++c)
                    same = std::equal (out[c] + at, out[c] + at + n, chans[c] + at);
                if (same)
                    o = in;
                else
                {
                    spectrum (out, at);
                    bandsOf (o);
                }
                auto& accIn = k == sibilant ? sIn : (k == vowel ? vIn : mIn);
                auto& accOut = k == sibilant ? sOut : (k == vowel ? vOut : mOut);
                for (int b = 0; b < numBands; ++b)
                {
                    accIn[(size_t) b] += in[(size_t) b];
                    accOut[(size_t) b] += o[(size_t) b];
                }
                if (k == sibilant)
                    m.sFrames.push_back ((float) lossDb (in[sBand], o[sBand]));
            }
            m.sRed = (float) lossDb (sIn[sBand], sOut[sBand]);
            m.vRed = (float) lossDb (vIn[full], vOut[full]);
            m.vPres = (float) lossDb (vIn[presence], vOut[presence]);
            // the air of a vowel only counts where there is some (a dull voice has none to lose)
            m.vAir = vIn[air] > 1.0e-4 * vIn[full] ? (float) lossDb (vIn[air], vOut[air]) : 0.0f;
            m.mRed = (float) lossDb (mIn[vowelPart], mOut[vowelPart]);
            m.mPres = (float) lossDb (mIn[presence], mOut[presence]);
            m.valid = true;
            return cache[key] = m;
        };

        // Root of a falling function g(thr) on the 0.5 dB grid in [lo, hi] (both on the grid) with g(lo) >= 0 > g(hi):
        // regula falsi (Illinois) with a bisection step every third try. Stops early at a point whose g lies in
        // [acceptLo, acceptHi] (close enough). Returns the two neighbouring grid points around the root (both
        // measured), or that point twice.
        const auto grid = [] (float v) { return std::round (v * 2.0f) * 0.5f; };
        const auto falsePosition = [&] (float lo, float glo, float hi, float ghi, auto&& g, float acceptLo, float acceptHi)
        {
            int side = 0;
            for (int it = 0; it < 16 && hi - lo > 0.75f; ++it)
            {
                float mid = (glo - ghi) > 1.0e-6f ? lo + (hi - lo) * glo / (glo - ghi) : 0.5f * (lo + hi);
                if (it % 3 == 2)
                    mid = 0.5f * (lo + hi);
                mid = juce::jlimit (lo + 0.5f, hi - 0.5f, grid (mid));
                const float gm = g (mid);
                if (cancelled())
                    break;
                if (gm >= acceptLo && gm <= acceptHi)
                    return std::make_pair (mid, mid);
                if (gm >= 0.0f)
                {
                    lo = mid; glo = gm;
                    if (side == -1) ghi *= 0.5f;
                    side = -1;
                }
                else
                {
                    hi = mid; ghi = gm;
                    if (side == 1) glo *= 0.5f;
                    side = 1;
                }
            }
            return std::make_pair (lo, hi);
        };

        // Threshold for a given range: the S reach the target, then raised until the voice is left alone. Never
        // below autoMinThresholdDb: further down EMBER stops being dynamic (every hiss, also a soft f or sh, loses
        // the same) - a voice whose S are that tame gets less than the target instead.
        // The search starts at a guess and steps down from it until the target is bracketed (the reduction falls
        // by roughly 0.4 dB per dB of threshold), so a take needs 4-7 renders per search.
        const float top0 = std::min (0.0f, std::ceil (detMax + kneeDb * 0.5f));
        const auto searchThreshold = [&] (float range, float guess)
        {
            const auto sGap = [&] (float thr) { return measure (thr, range).sRed - r.targetDb; };
            float hiT = std::max (autoMinThresholdDb + 0.5f, top0);
            float gHi = sGap (hiT);
            float t = juce::jlimit (autoMinThresholdDb, hiT - 0.5f, grid (guess));
            float gt = sGap (t);
            while (gt < 0.0f && t > autoMinThresholdDb && ! cancelled())
            {
                hiT = t;
                gHi = gt;
                t = std::max (autoMinThresholdDb, grid (t - std::max (2.0f, -gt / 0.4f + 1.0f)));
                gt = sGap (t);
            }
            float thr = t;   // the floor when even that is not enough
            if (gt > 0.1f && gHi < 0.0f)
            {
                const auto [a, b] = falsePosition (t, gt, hiT, gHi, sGap, -0.1f, 0.1f);
                thr = std::abs (sGap (a)) <= std::abs (sGap (b)) ? a : b;   // the one nearer the target (both cached)
            }
            if (! measure (thr, range).keepsVoice())
            {
                // raise until the voice is left alone (the result is on the safe side)
                const auto harm = [&] (float x) { return measure (x, range).harm(); };
                const float topT = std::max (thr + 0.5f, top0);
                const auto [a, b] = falsePosition (thr, harm (thr), topT, harm (topT), harm, -0.1f, 0.0f);
                juce::ignoreUnused (a);
                thr = b;   // the lowest threshold that keeps the voice
            }
            return juce::jlimit (autoMinThresholdDb, 0.0f, thr);
        };

        // First with the widest range, which then shrinks to the room the strong S of this take need (many S
        // taken down a little instead of a few slammed hard); the threshold is searched again with that range so
        // the target is still met (until 2026-11-22 the range shrank last and AUTO fell 0.6 dB short)
        const float firstThr = searchThreshold (autoMaxRangeDb, r.sibilantMedianDb - 11.0f);
        const auto atThr = measure (firstThr, autoMaxRangeDb);
        if (cancelled() || ! atThr.valid)
            return r.status = AutoResult::Status::cancelled, r;
        // (the measured reduction is the range-capped one times Amount: back to range units)
        r.rangeDb = juce::jlimit (autoMinRangeDb, autoMaxRangeDb, std::round (percentile (atThr.sFrames, 0.9f) / scale + 1.0f));
        // a smaller range needs a lower threshold for the same target: the search starts just below the first one
        r.thresholdDb = r.rangeDb < autoMaxRangeDb ? searchThreshold (r.rangeDb, firstThr - 1.0f) : firstThr;
        const auto final = measure (r.thresholdDb, r.rangeDb);
        if (cancelled() || ! final.valid)
            return r.status = AutoResult::Status::cancelled, r;
        r.expectedReductionDb = final.sRed;
        r.vowelReductionDb = final.vRed;
        r.vowelPresenceDb = final.vPres;
        r.vowelAirDb = final.vAir;
        r.mixedReductionDb = final.mRed;
        r.mixedPresenceDb = final.mPres;
        r.status = AutoResult::Status::ok;
        return r;
    }

    // ------------------------------------------------------------- AutoCapture
    // The audio thread raises inPush before it looks at the state, the message thread sets the state before it looks
    // at inPush (both sequentially consistent): after quiesce() the audio thread is out and stays out until the state
    // says capturing again - so the buffers and the chunk state can be reset or reallocated without a lock.
    void AutoCapture::quiesce() const noexcept
    {
        while (inPush.load())
            std::this_thread::yield();
    }

    void AutoCapture::resetLocked() noexcept
    {
        fill = silentRun = committed = committedVoiced = 0;
        chunkPeak = 0.0f;
        chunkBad = chunkStereo = false;
        const double rate = fs.load();
        // A fixed grid of ~5 ms chunks counted from start(): pauses are cut at the same samples whatever block size
        // the host uses
        chunkLen = std::max (32, (int) std::lround (rate * 0.005));
        gapChunks = std::max (1, (int) std::lround (autoGapSeconds * rate / chunkLen));
        stereo.store (false);
        voiced.store (0);
        count.store (0, std::memory_order_release);
    }

    void AutoCapture::prepare (double sampleRate)
    {
        if (sampleRate <= 0.0 || sampleRate == fs.load())
            return;
        const bool wasCapturing = state.exchange (idle) == capturing;
        quiesce();
        fs.store (sampleRate);
        const int wanted = (int) (autoCapacitySeconds * sampleRate);
        if (! buffer[0].empty() && capacity != wanted)
        {
            buffer[0].assign ((size_t) wanted, 0.0f);
            buffer[1].assign ((size_t) wanted, 0.0f);
            capacity = wanted;
        }
        resetLocked();
        if (wasCapturing && capacity > 0)
        {
            restarts.fetch_add (1);
            state.store (capturing);   // listening goes on at the new rate, from the start
        }
    }

    void AutoCapture::start()
    {
        if (fs.load() <= 0.0)
            return;
        state.store (idle);
        quiesce();
        const int wanted = (int) (autoCapacitySeconds * fs.load());
        if (capacity != wanted || buffer[0].size() != (size_t) wanted)
        {
            buffer[0].assign ((size_t) wanted, 0.0f);
            buffer[1].assign ((size_t) wanted, 0.0f);
            capacity = wanted;
        }
        resetLocked();
        state.store (capturing);
    }

    void AutoCapture::stop() noexcept
    {
        state.store (idle);
        quiesce();
    }

    double AutoCapture::getSeconds() const noexcept
    {
        const double rate = fs.load();
        return rate > 0.0 ? getNumSamples() / rate : 0.0;
    }

    double AutoCapture::getVoicedSeconds() const noexcept
    {
        const double rate = fs.load();
        return rate > 0.0 ? voiced.load (std::memory_order_acquire) / rate : 0.0;
    }

    AutoCapture::Take AutoCapture::copyTake() const
    {
        quiesce();   // a push that started before stop() is done
        Take t;
        t.sampleRate = fs.load();
        const int len = getNumSamples();
        t.numChannels = stereo.load() ? 2 : 1;
        for (int c = 0; c < t.numChannels && len > 0; ++c)
            t.channel[c].assign (buffer[c].begin(), buffer[c].begin() + len);
        if (len == 0)
            t.numChannels = 0;
        return t;
    }

    void AutoCapture::commitChunk() noexcept
    {
        const bool silent = chunkPeak < autoSilenceGain;
        const bool keep = ! chunkBad && (! silent || silentRun < gapChunks);
        if (keep)
        {
            committed += fill;
            if (silent)
                ++silentRun;
            else
            {
                silentRun = 0;
                committedVoiced += fill;
                voiced.store (committedVoiced, std::memory_order_release);
            }
            if (chunkStereo)
                stereo.store (true);
            count.store (committed, std::memory_order_release);
        }
        // a dropped chunk (long pause, broken audio) is simply overwritten by the next one
        fill = 0;
        chunkPeak = 0.0f;
        chunkBad = chunkStereo = false;
    }

    void AutoCapture::push (const float* const* channels, int numChannels, int numSamples) noexcept
    {
        inPush.store (true);
        if (state.load() != capturing || numChannels <= 0 || channels == nullptr)
        {
            inPush.store (false);
            return;
        }
        const float* l = channels[0];
        const float* rch = numChannels > 1 ? channels[1] : channels[0];
        float* bl = buffer[0].data();
        float* br = buffer[1].data();
        for (int i = 0; i < numSamples; ++i)
        {
            const int pos = committed + fill;
            if (pos >= capacity)
            {
                state.store (idle);   // full
                break;
            }
            float a = l[i], b = rch[i];
            if (! std::isfinite (a) || ! std::isfinite (b))
            {
                chunkBad = true;   // a broken chunk never reaches the analysis
                a = b = 0.0f;
            }
            bl[pos] = a;
            br[pos] = b;
            chunkPeak = std::max (chunkPeak, std::max (std::abs (a), std::abs (b)));
            chunkStereo = chunkStereo || a != b;
            if (++fill == chunkLen)
                commitChunk();
        }
        inPush.store (false);
    }
}
