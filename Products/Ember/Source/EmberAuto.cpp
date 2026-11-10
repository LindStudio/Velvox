#include "EmberAuto.h"
#include "EmberEngine.h"
#include <juce_dsp/juce_dsp.h>

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
    }

    AutoResult analyse (const float* mono, int numSamples, double fs)
    {
        AutoResult r;
        r.seconds = fs > 0.0 ? numSamples / fs : 0.0;
        if (mono == nullptr || fs <= 0.0 || r.seconds < autoMinSeconds)
            return r;

        const int order = fs <= 50000.0 ? 10 : (fs <= 100000.0 ? 11 : 12);
        const int n = 1 << order, hop = n / 2;
        juce::dsp::FFT fft (order);
        juce::dsp::WindowingFunction<float> window ((size_t) n, juce::dsp::WindowingFunction<float>::hann, false);
        std::vector<float> work ((size_t) n * 2);
        const int bins = n / 2;
        const auto binOf = [&] (double hz) { return juce::jlimit (1, bins - 1, (int) std::lround (hz * n / fs)); };
        const int b80 = binOf (80.0), b200 = binOf (200.0), b2500 = binOf (2500.0), b4k = binOf (4000.0);
        const int bTop = binOf (std::min (16000.0, fs * 0.45));

        // Detector trace at a provisional corner (refined below): needs the S range first, so the spectrum
        // pass comes first and the detector pass second
        const int numFrames = numSamples >= n ? 1 + (numSamples - n) / hop : 0;
        if (numFrames < 8)
            return r;

        std::vector<float> rmsDb ((size_t) numFrames);
        std::vector<char> kind ((size_t) numFrames, 0);   // 0 = other, 1 = vowel, 2 = sibilant
        std::vector<double> sibSpectrum ((size_t) bins, 0.0);

        for (int f = 0; f < numFrames; ++f)
        {
            const float* x = mono + (size_t) f * (size_t) hop;
            double sq = 0.0;
            for (int i = 0; i < n; ++i)
                sq += (double) x[i] * x[i];
            rmsDb[(size_t) f] = (float) (10.0 * std::log10 (std::max (1.0e-12, sq / n)));
        }
        const float loud = percentile (rmsDb, 0.95f);

        for (int f = 0; f < numFrames; ++f)
        {
            if (rmsDb[(size_t) f] < std::max (-60.0f, loud - 40.0f))
                continue;
            ++r.voicedFrames;
            std::fill (work.begin(), work.end(), 0.0f);
            std::copy_n (mono + (size_t) f * (size_t) hop, n, work.begin());
            window.multiplyWithWindowingTable (work.data(), (size_t) n);
            fft.performFrequencyOnlyForwardTransform (work.data(), true);

            double total = 0.0, high = 0.0, voice = 0.0;
            for (int b = b80; b <= bTop; ++b)
            {
                const double p = (double) work[(size_t) b] * work[(size_t) b];
                total += p;
                if (b >= b4k)
                    high += p;
                if (b >= b200 && b <= b2500)
                    voice += p;
            }
            if (total <= 0.0)
                continue;

            if (high >= 0.5 * total && high > voice)
            {
                kind[(size_t) f] = 2;
                ++r.sibilantFrames;
                for (int b = b4k / 2; b <= bTop; ++b)
                    sibSpectrum[(size_t) b] += (double) work[(size_t) b] * work[(size_t) b] / total;
            }
            else if (high <= 0.15 * total)
            {
                kind[(size_t) f] = 1;
                ++r.vowelFrames;
            }
        }

        if (r.sibilantFrames < 12)
        {
            r.status = AutoResult::Status::noSibilance;
            return r;
        }

        // S range: 1/6-octave smoothed average S spectrum and its peak between 4 and 12 kHz. The corner sits at
        // 70 % of the peak (where the S starts to rise), kept within 4.5-9 kHz: lower would touch the presence
        // range of the voice, higher would miss most of the S (real takes 2026-11-10: peaks 4.3-10.9 kHz)
        std::vector<double> smooth ((size_t) bins, 0.0);
        for (int b = 1; b < bins; ++b)
        {
            const double hz = b * fs / n;
            const int lo = binOf (hz / std::pow (2.0, 1.0 / 12.0)), hi = binOf (hz * std::pow (2.0, 1.0 / 12.0));
            double s = 0.0;
            for (int k = lo; k <= hi; ++k)
                s += sibSpectrum[(size_t) k];
            smooth[(size_t) b] = s / (hi - lo + 1);
        }
        const int bLo = binOf (4000.0), bHi = binOf (std::min (12000.0, fs * 0.45));
        int peak = bLo;
        for (int b = bLo; b <= bHi; ++b)
            if (smooth[(size_t) b] > smooth[(size_t) peak])
                peak = b;
        r.peakHz = (float) (peak * fs / n);
        r.freq = juce::jlimit (autoMinCornerHz, autoMaxCornerHz, r.peakHz * autoCornerRatio);
        r.freq = std::round (r.freq / 50.0f) * 50.0f;

        // Detector levels with the chosen corner, exactly like the engine measures them
        Detector det;
        det.prepare (fs);
        det.setFrequency (r.freq);
        std::vector<float> s, v;
        float frameMax = -200.0f;
        int frame = 0, inFrame = 0;
        std::vector<float> frameDet ((size_t) numFrames, -200.0f);
        // Frame f covers [f*hop, f*hop+n); assign each hop-sized chunk's max to the frames covering it
        std::vector<float> chunkMax ((size_t) (numSamples / hop + 1), -200.0f);
        for (int i = 0; i < numSamples; ++i)
        {
            const float level = det.process (mono + i, 1, true);
            frameMax = std::max (frameMax, level);
            if (++inFrame == hop)
            {
                chunkMax[(size_t) frame++] = frameMax;
                frameMax = -200.0f;
                inFrame = 0;
            }
        }
        for (int f = 0; f < numFrames; ++f)
        {
            const float m = std::max (chunkMax[(size_t) f], chunkMax[(size_t) f + 1]);
            frameDet[(size_t) f] = m;
            if (kind[(size_t) f] == 2)
                s.push_back (m);
            else if (kind[(size_t) f] == 1)
                v.push_back (m);
        }

        r.sibilantMedianDb = percentile (s, 0.5f);
        r.sibilant90Db = percentile (s, 0.9f);
        r.vowel95Db = v.size() >= 10 ? percentile (v, 0.95f) : r.sibilantMedianDb - 12.0f;

        // Threshold by listening to the result: the engine renders the take, and the threshold is searched so
        // the S frames lose autoTargetReductionDb of energy while the vowel frames lose at most autoVowelMaxDb
        std::vector<double> inEnergy ((size_t) numFrames);
        for (int f = 0; f < numFrames; ++f)
            inEnergy[(size_t) f] = std::pow (10.0, rmsDb[(size_t) f] / 10.0) * n;

        struct Measured { float sRed = 0.0f, vRed = 0.0f; std::vector<float> sFrames; };
        std::vector<float> y;
        const auto measure = [&] (float thr, float range)
        {
            Settings st;
            st.freq = r.freq;
            st.thresholdDb = thr;
            st.rangeDb = range;
            Engine e;
            e.setSettings (st);
            e.prepare (fs, 4096);
            const int lat = e.getLatencySamples();
            y.assign ((size_t) (numSamples + lat), 0.0f);
            std::copy_n (mono, numSamples, y.begin());
            for (int start = 0; start < (int) y.size(); start += 4096)
            {
                float* ch[1] { y.data() + start };
                e.process (ch, 1, std::min (4096, (int) y.size() - start));
            }
            Measured m;
            double sIn = 0.0, sOut = 0.0, vIn = 0.0, vOut = 0.0;
            for (int f = 0; f < numFrames; ++f)
            {
                if (kind[(size_t) f] == 0)
                    continue;
                double out = 0.0;
                const float* p = y.data() + (size_t) f * (size_t) hop + (size_t) lat;
                for (int i = 0; i < n; ++i)
                    out += (double) p[i] * p[i];
                if (kind[(size_t) f] == 2)
                {
                    sIn += inEnergy[(size_t) f];
                    sOut += out;
                    m.sFrames.push_back ((float) (10.0 * std::log10 (std::max (1.0e-20, inEnergy[(size_t) f]) / std::max (1.0e-20, out))));
                }
                else
                {
                    vIn += inEnergy[(size_t) f];
                    vOut += out;
                }
            }
            m.sRed = (float) (10.0 * std::log10 (std::max (1.0e-20, sIn) / std::max (1.0e-20, sOut)));
            m.vRed = vIn > 0.0 ? (float) (10.0 * std::log10 (std::max (1.0e-20, vIn) / std::max (1.0e-20, vOut))) : 0.0f;
            return m;
        };

        constexpr float searchRange = 12.0f;
        float lo = -60.0f, hi = 0.0f;   // more reduction at lo
        for (int it = 0; it < 9; ++it)
        {
            const float mid = 0.5f * (lo + hi);
            (measure (mid, searchRange).sRed > autoTargetReductionDb ? lo : hi) = mid;
        }
        float thr = 0.5f * (lo + hi);
        if (measure (thr, searchRange).vRed > autoVowelMaxDb)
        {
            lo = thr; hi = 0.0f;   // raise until the vowels are free
            for (int it = 0; it < 8; ++it)
            {
                const float mid = 0.5f * (lo + hi);
                (measure (mid, searchRange).vRed > autoVowelMaxDb ? lo : hi) = mid;
            }
            thr = hi;
        }
        r.thresholdDb = juce::jlimit (-60.0f, 0.0f, std::round (thr * 2.0f) * 0.5f);

        // Range: room for the strong S of this take, no more
        const auto atThr = measure (r.thresholdDb, searchRange);
        r.rangeDb = juce::jlimit (4.0f, 12.0f, std::round (percentile (atThr.sFrames, 0.95f) + 2.0f));
        const auto final = measure (r.thresholdDb, r.rangeDb);
        r.expectedReductionDb = final.sRed;
        r.vowelReductionDb = final.vRed;
        r.status = AutoResult::Status::ok;
        return r;
    }

    // ------------------------------------------------------------- AutoCapture
    void AutoCapture::prepare (double sampleRate)
    {
        if (sampleRate == fs && ! buffer.empty())
            return;
        state.store (idle, std::memory_order_release);
        fs = sampleRate;
        buffer.assign ((size_t) (autoCapacitySeconds * sampleRate), 0.0f);
        count.store (0, std::memory_order_release);
    }

    void AutoCapture::start() noexcept
    {
        if (buffer.empty())
            return;
        count.store (0, std::memory_order_release);
        state.store (capturing, std::memory_order_release);
    }

    void AutoCapture::stop() noexcept
    {
        state.store (idle, std::memory_order_release);
    }

    void AutoCapture::push (const float* const* channels, int numChannels, int numSamples) noexcept
    {
        if (state.load (std::memory_order_acquire) != capturing || numChannels <= 0)
            return;

        float peak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < numSamples; ++i)
                peak = std::max (peak, std::abs (channels[ch][i]));
        if (peak < 0.00316f)   // -50 dBFS: pauses are not part of the take
            return;

        int c = count.load (std::memory_order_relaxed);
        const int room = (int) buffer.size() - c;
        const int n = std::min (room, numSamples);
        const float scale = 1.0f / (float) numChannels;
        for (int i = 0; i < n; ++i)
        {
            float s = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                s += channels[ch][i];
            buffer[(size_t) (c + i)] = s * scale;
        }
        c += n;
        count.store (c, std::memory_order_release);
        if (c >= (int) buffer.size())
            state.store (idle, std::memory_order_release);
    }
}
