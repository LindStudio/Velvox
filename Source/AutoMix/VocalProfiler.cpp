#include "VocalProfile.h"
#include "../DSP/Loudness.h"
#include <juce_dsp/juce_dsp.h>
#include <map>

namespace velvox::automix
{
    namespace
    {
        constexpr int hop = VocalProfiler::fftSize / 4;
        constexpr int nb = VocalProfiler::numBands;

        juce::String de (const char* utf8) { return juce::String::fromUTF8 (utf8); }
        juce::String db (float v) { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; }
        juce::String hz (float f)
        {
            return f >= 1000.0f ? juce::String (f / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz";
        }

        float toDb (double power) { return (float) (10.0 * std::log10 (std::max (power, 1.0e-20))); }

        float percentile (std::vector<float> v, float p)
        {
            if (v.empty())
                return 0.0f;
            const auto k = (size_t) juce::jlimit (0, (int) v.size() - 1, (int) std::round (p * (float) (v.size() - 1)));
            std::nth_element (v.begin(), v.begin() + (long) k, v.end());
            return v[k];
        }

        struct Frame
        {
            float rmsDb = -100.0f, peakDb = -100.0f;
            double ll = 0.0, rr = 0.0, lr = 0.0;
            std::array<float, nb> bands {};
            float f0 = 0.0f;           // 0 = no clear pitch
        };

        // K-weighted mean square per 100 ms step, summed over channels
        std::vector<double> kWeightedPowers (const std::vector<const float*>& channels, int numSamples, double fs, int step)
        {
            dsp::BiquadCoeffs s, h;
            dsp::LoudnessMeter::designKWeighting (fs, s, h);
            std::vector<double> out;
            if (step <= 0)
                return out;
            out.reserve ((size_t) (numSamples / step + 1));

            std::vector<dsp::Biquad> shelves (channels.size()), highs (channels.size());
            for (size_t c = 0; c < channels.size(); ++c)
            {
                shelves[c].setCoeffs (s);
                highs[c].setCoeffs (h);
            }

            double acc = 0.0;
            int count = 0;
            for (int i = 0; i < numSamples; ++i)
            {
                for (size_t c = 0; c < channels.size(); ++c)
                {
                    const double y = highs[c].process (shelves[c].process (channels[c][i], 0), 0);
                    acc += y * y;
                }
                if (++count == step)
                {
                    out.push_back (acc / step);
                    acc = 0.0;
                    count = 0;
                }
            }
            return out;
        }

        // Mean power over `width` consecutive steps starting at each `stride`
        std::vector<double> windows (const std::vector<double>& steps, int width, int stride)
        {
            std::vector<double> out;
            for (size_t start = 0; start + (size_t) width <= steps.size(); start += (size_t) stride)
            {
                double sum = 0.0;
                for (size_t i = start; i < start + (size_t) width; ++i)
                    sum += steps[i];
                out.push_back (sum / width);
            }
            return out;
        }

        float lufsOf (double power) { return (float) (-0.691 + 10.0 * std::log10 (std::max (power, 1.0e-20))); }
    }

    //==========================================================================
    float VocalProfiler::integratedLufs (const std::vector<const float*>& channels, int numSamples, double fs)
    {
        const int step = (int) std::lround (0.1 * fs);
        const auto blocks = windows (kWeightedPowers (channels, numSamples, fs, step), 4, 1); // 400 ms, 75 % overlap
        if (blocks.empty())
            return -100.0f;

        // Absolute gate -70 LUFS, then relative gate -10 LU
        double sum = 0.0;
        int n = 0;
        for (double p : blocks)
            if (lufsOf (p) > -70.0f) { sum += p; ++n; }
        if (n == 0)
            return -100.0f;
        const float relGate = lufsOf (sum / n) - 10.0f;
        sum = 0.0;
        n = 0;
        for (double p : blocks)
            if (lufsOf (p) > -70.0f && lufsOf (p) > relGate) { sum += p; ++n; }
        return n > 0 ? lufsOf (sum / n) : -100.0f;
    }

    std::optional<float> VocalProfiler::loudnessRange (const std::vector<const float*>& channels, int numSamples, double fs)
    {
        const int step = (int) std::lround (0.1 * fs);
        const auto st = windows (kWeightedPowers (channels, numSamples, fs, step), 30, 5);   // 3 s windows, 0.5 s hop
        std::vector<float> levels;
        for (double p : st)
            if (lufsOf (p) > -70.0f)
                levels.push_back (lufsOf (p));
        if (levels.size() < 6)
            return std::nullopt;

        double sum = 0.0;
        for (float l : levels)
            sum += std::pow (10.0, (l + 0.691) / 10.0);
        const float relGate = lufsOf (sum / (double) levels.size()) - 20.0f;
        std::vector<float> gated;
        for (float l : levels)
            if (l > relGate)
                gated.push_back (l);
        if (gated.size() < 6)
            return std::nullopt;
        return percentile (gated, 0.95f) - percentile (gated, 0.10f);
    }

    //==========================================================================
    VocalProfile VocalProfiler::analyse (const CapturedAudio& audio, float minVoicedSeconds, const std::function<bool()>& shouldStop)
    {
        VocalProfile p;
        p.sampleRate = audio.sampleRate;
        p.channels = audio.channels;
        p.capturedSeconds = (float) audio.seconds();
        p.stoppedEarly = audio.stoppedEarly;

        const int n = audio.numSamples();
        const double fs = audio.sampleRate;
        if (n < fftSize || audio.channels < 1 || fs <= 0.0)
            return p;

        const auto stop = [&shouldStop] { return shouldStop && shouldStop(); };

        // ---- Channels and the mid signal used for spectral analysis
        std::vector<const float*> chans;
        for (auto& c : audio.vocal)
            chans.push_back (c.data());
        const float* L = audio.vocal[0].data();
        const float* R = audio.channels > 1 ? audio.vocal[1].data() : L;
        std::vector<float> mid ((size_t) n);
        for (int i = 0; i < n; ++i)
            mid[(size_t) i] = 0.5f * (L[i] + R[i]);

        // Stereo content: do the channels actually differ?
        if (audio.channels > 1)
        {
            double diff = 0.0, sum = 0.0;
            for (int i = 0; i < n; ++i)
            {
                diff += std::abs (L[i] - R[i]);
                sum += std::abs (L[i]) + std::abs (R[i]);
            }
            p.stereoContent = sum > 0.0 && diff / sum > 1.0e-3;
        }

        // ---- Level: loudness, peaks, clipping
        p.integratedLufs = integratedLufs (chans, n, fs);
        if (auto lra = loudnessRange (chans, n, fs))
        {
            p.loudnessRangeDb = *lra;
            p.loudnessRangeValid = true;
        }

        float peak = 0.0f;
        for (auto* c : chans)
        {
            int run = 0;
            for (int i = 0; i < n; ++i)
            {
                const float a = std::abs (c[i]);
                peak = std::max (peak, a);
                if (a >= 0.999f)
                {
                    if (++run == 3)
                        ++p.clipRuns;
                }
                else
                {
                    run = 0;
                }
            }
        }
        p.peakDb = juce::Decibels::gainToDecibels (peak, -100.0f);

        // 50 ms blocks of the mid signal: voice level distribution and noise floor
        const int block = std::max (64, (int) std::lround (0.05 * fs));
        std::vector<float> blockDb;
        for (int start = 0; start + block <= n; start += block)
        {
            double s = 0.0;
            for (int i = start; i < start + block; ++i)
                s += (double) mid[(size_t) i] * mid[(size_t) i];
            blockDb.push_back (toDb (s / block));
        }
        const float loudBlock = percentile (blockDb, 0.95f);
        const float gate = std::max (-50.0f, loudBlock - 35.0f);
        std::vector<float> voicedBlocks, pauseBlocks;
        for (float b : blockDb)
        {
            if (b > gate)
                voicedBlocks.push_back (b);
            else if (b > -110.0f)
                pauseBlocks.push_back (b);
        }
        p.voicedSeconds = (float) (voicedBlocks.size() * block / fs);
        p.enoughMaterial = p.voicedSeconds >= minVoicedSeconds;
        if (! voicedBlocks.empty())
        {
            p.voiceRmsDb = percentile (voicedBlocks, 0.5f);
            p.voiceRmsLowDb = percentile (voicedBlocks, 0.1f);
            p.voiceRmsHighDb = percentile (voicedBlocks, 0.9f);
        }
        // Noise floor: the quiet end of the level distribution. Only meaningful when
        // the capture contains real pauses (clearly below the voice); digital
        // silence is never stored, so a clean recording simply has no floor here.
        {
            std::vector<float> nonSilent;
            for (float b : blockDb)
                if (b > -110.0f)
                    nonSilent.push_back (b);
            const float floorDb = percentile (nonSilent, 0.05f);
            if (nonSilent.size() >= 40 && floorDb < p.voiceRmsDb - 15.0f)
            {
                p.noiseFloorDb = floorDb;
                p.snrDb = p.voiceRmsDb - p.noiseFloorDb;
                p.snrValid = true;
            }
        }

        // Word / phrase dynamics: 400 ms windows that are mostly voice
        {
            const int per = 8;   // 8 x 50 ms
            std::vector<float> windowDb;
            for (size_t start = 0; start + per <= blockDb.size(); start += 4)
            {
                double pw = 0.0;
                int voicedCount = 0;
                for (size_t i = start; i < start + per; ++i)
                {
                    pw += std::pow (10.0, blockDb[i] / 10.0);
                    voicedCount += blockDb[i] > gate ? 1 : 0;
                }
                if (voicedCount >= 6)
                    windowDb.push_back (toDb (pw / per));
            }
            if (windowDb.size() >= 8)
            {
                p.dynamicsDb = percentile (windowDb, 0.95f) - percentile (windowDb, 0.10f);
                p.dynamicsValid = true;
            }
        }

        if (stop())
            return p;

        // ---- Frame analysis (FFT 4096, 75 % overlap)
        juce::dsp::FFT fft (fftOrder), fftPadded (fftOrder + 1);   // padded: linear (not circular) autocorrelation
        juce::dsp::WindowingFunction<float> window ((size_t) fftSize, juce::dsp::WindowingFunction<float>::hann, false);
        std::vector<float> work ((size_t) fftSize * 2), acf ((size_t) fftSize * 4);

        std::vector<int> binToBand ((size_t) fftSize / 2, -1);
        std::array<int, nb> binsPerBand {};
        for (int k = 1; k < fftSize / 2; ++k)
        {
            const double f = k * fs / fftSize;
            const int band = (int) std::lround (12.0 * std::log2 (f / 40.0));
            if (band >= 0 && band < nb)
            {
                binToBand[(size_t) k] = band;
                ++binsPerBand[(size_t) band];
            }
        }

        // Window autocorrelation for an unbiased pitch estimate
        std::vector<float> windowAcf ((size_t) fftSize, 0.0f);
        {
            std::vector<float> w ((size_t) fftSize, 1.0f);
            window.multiplyWithWindowingTable (w.data(), (size_t) fftSize);
            for (int lag = 0; lag < fftSize / 2; lag += 1)
            {
                double s = 0.0;
                for (int i = 0; i + lag < fftSize; i += 4)
                    s += (double) w[(size_t) i] * w[(size_t) (i + lag)];
                windowAcf[(size_t) lag] = (float) s;
            }
        }
        const int minLag = (int) std::floor (fs / 1000.0), maxLag = std::min (fftSize / 2 - 1, (int) std::ceil (fs / 60.0));

        std::vector<Frame> frames;
        for (int start = 0; start + fftSize <= n; start += hop)
        {
            if (stop())
                return p;

            Frame f;
            double sum = 0.0;
            float pk = 0.0f;
            for (int i = 0; i < fftSize; ++i)
            {
                const float l = L[start + i], r = R[start + i];
                const float m = mid[(size_t) (start + i)];
                sum += (double) m * m;
                f.ll += (double) l * l;
                f.rr += (double) r * r;
                f.lr += (double) l * r;
                pk = std::max (pk, std::abs (m));
                work[(size_t) i] = m;
            }
            f.rmsDb = toDb (sum / fftSize);
            f.peakDb = juce::Decibels::gainToDecibels (pk, -100.0f);

            std::fill (work.begin() + fftSize, work.end(), 0.0f);
            window.multiplyWithWindowingTable (work.data(), (size_t) fftSize);
            std::copy (work.begin(), work.begin() + fftSize, acf.begin());
            std::fill (acf.begin() + fftSize, acf.end(), 0.0f);
            fft.performRealOnlyForwardTransform (work.data(), true);

            // Power per bin -> bands
            for (int k = 1; k < fftSize / 2; ++k)
            {
                const float re = work[(size_t) (2 * k)], im = work[(size_t) (2 * k + 1)];
                if (const int band = binToBand[(size_t) k]; band >= 0)
                    f.bands[(size_t) band] += re * re + im * im;
            }

            // Pitch: autocorrelation = inverse FFT of the power spectrum (zero padded)
            fftPadded.performRealOnlyForwardTransform (acf.data(), true);
            for (int k = 0; k <= fftSize; ++k)
            {
                const float re = acf[(size_t) (2 * k)], im = acf[(size_t) (2 * k + 1)];
                acf[(size_t) (2 * k)] = re * re + im * im;
                acf[(size_t) (2 * k + 1)] = 0.0f;
            }
            fftPadded.performRealOnlyInverseTransform (acf.data());
            const float r0 = acf[0];
            if (r0 > 1.0e-9f)
            {
                int best = -1;
                float bestV = 0.0f;
                for (int lag = minLag; lag <= maxLag; ++lag)
                {
                    const float v = acf[(size_t) lag] / r0 * (windowAcf[0] / std::max (1.0e-9f, windowAcf[(size_t) lag]));
                    if (v > bestV && acf[(size_t) lag] >= acf[(size_t) lag - 1] && acf[(size_t) lag] >= acf[(size_t) lag + 1])
                    {
                        bestV = v;
                        best = lag;
                    }
                }
                // Prefer the shortest period that is almost as periodic (avoids octave-down errors)
                if (best > 0)
                    for (int div = 4; div >= 2; --div)
                    {
                        const int sub = (int) std::lround ((double) best / div);
                        if (sub < minLag)
                            continue;
                        float v = 0.0f;
                        for (int d = -2; d <= 2; ++d)
                        {
                            const int lag = sub + d;
                            if (lag >= minLag && lag < fftSize / 2)
                                v = std::max (v, acf[(size_t) lag] / r0 * (windowAcf[0] / std::max (1.0e-9f, windowAcf[(size_t) lag])));
                        }
                        if (v > 0.9f * bestV)
                        {
                            best = sub;
                            break;
                        }
                    }
                if (best > 0 && bestV > 0.55f)
                    f.f0 = (float) (fs / best);
            }

            frames.push_back (f);
        }

        if (frames.empty())
            return p;

        // ---- Voiced frames
        std::vector<float> levels;
        for (auto& f : frames)
            levels.push_back (f.rmsDb);
        const float frameGate = std::max (-50.0f, percentile (levels, 0.95f) - 35.0f);
        std::vector<bool> voiced (frames.size());
        int voicedCount = 0;
        for (size_t i = 0; i < frames.size(); ++i)
        {
            voiced[i] = frames[i].rmsDb > frameGate;
            voicedCount += voiced[i] ? 1 : 0;
        }
        if (voicedCount == 0)
            return p;

        // ---- Long-term spectrum
        std::array<double, nb> avg {};
        for (size_t i = 0; i < frames.size(); ++i)
            if (voiced[i])
                for (int b = 0; b < nb; ++b)
                    avg[(size_t) b] += frames[i].bands[(size_t) b];
        for (auto& a : avg)
            a /= voicedCount;

        p.spectrumDb.resize (nb);
        for (int b = 0; b < nb; ++b)
            p.spectrumDb[(size_t) b] = binsPerBand[(size_t) b] > 0 ? toDb (avg[(size_t) b] / binsPerBand[(size_t) b]) : -200.0f;

        const auto energy = [&] (float lo, float hi, const std::array<float, nb>* fb = nullptr)
        {
            double e = 0.0;
            for (int b = 0; b < nb; ++b)
            {
                const float c = bandCentre (b);
                if (c >= lo && c < hi)
                    e += fb != nullptr ? (double) (*fb)[(size_t) b] : avg[(size_t) b];
            }
            return e;
        };
        const float coreDb = toDb (energy (300, 2000));
        p.rumbleDb = toDb (energy (40, 90)) - coreDb;
        p.bodyDb = toDb (energy (100, 300)) - coreDb;
        p.lowMidDb = toDb (energy (250, 500)) - coreDb;
        p.presenceDb = toDb (energy (2000, 5000)) - coreDb;
        p.sibilanceBandDb = toDb (energy (5000, 10000)) - coreDb;
        p.airDb = toDb (energy (10000, 16000)) - coreDb;
        p.topDb = toDb (energy (2000, 16000)) - coreDb;

        // Tilt: least squares of the per-band level over log2(frequency)
        {
            double sx = 0, sy = 0, sxx = 0, sxy = 0;
            int cnt = 0;
            for (int b = 0; b < nb; ++b)
            {
                const float c = bandCentre (b);
                if (c < 250.0f || c > 8000.0f || binsPerBand[(size_t) b] == 0)
                    continue;
                const double x = std::log2 (c), y = p.spectrumDb[(size_t) b];
                sx += x; sy += y; sxx += x * x; sxy += x * y;
                ++cnt;
            }
            if (cnt > 4)
                p.tiltDbPerOctave = (float) ((cnt * sxy - sx * sy) / std::max (1.0e-9, cnt * sxx - sx * sx));
        }

        // Strongest point of the low mids (for a "mud" cut)
        {
            float bestDb = -1.0e9f;
            for (int b = 0; b < nb; ++b)
            {
                const float c = bandCentre (b);
                if (c < 180.0f || c > 500.0f || binsPerBand[(size_t) b] == 0)
                    continue;
                // 1/3-octave smoothing so a single harmonic does not decide
                float s = 0.0f;
                int k = 0;
                for (int j = b - 2; j <= b + 2; ++j)
                    if (j >= 0 && j < nb && binsPerBand[(size_t) j] > 0) { s += p.spectrumDb[(size_t) j]; ++k; }
                if (k > 0 && s / k > bestDb)
                {
                    bestDb = s / k;
                    p.lowMidPeakHz = c;
                }
            }
        }

        // ---- Crest factor of voiced frames
        {
            std::vector<float> crests;
            for (size_t i = 0; i < frames.size(); ++i)
                if (voiced[i])
                    crests.push_back (frames[i].peakDb - frames[i].rmsDb);
            p.crestDb = percentile (crests, 0.5f);
        }

        // ---- Pitch statistics
        {
            std::vector<float> f0s;
            for (size_t i = 0; i < frames.size(); ++i)
                if (voiced[i] && frames[i].f0 > 0.0f)
                    f0s.push_back (frames[i].f0);
            p.pitchedShare = (float) f0s.size() / (float) voicedCount;
            if (f0s.size() >= 20 && p.pitchedShare > 0.25f)
            {
                p.f0MedianHz = percentile (f0s, 0.5f);
                p.f0LowHz = percentile (f0s, 0.1f);
                p.f0HighHz = percentile (f0s, 0.9f);
                p.pitchSpreadSemitones = 12.0f * std::log2 (std::max (1.0f, p.f0HighHz) / std::max (1.0f, p.f0LowHz));
                p.pitchValid = true;

                // The notes that were actually sung (semitone histogram)
                std::map<int, int> hist;
                for (float f : f0s)
                    ++hist[(int) std::lround (12.0f * std::log2 (f / 440.0f))];
                for (auto& [semi, count] : hist)
                    if ((float) count >= 0.04f * (float) f0s.size())
                        p.sungNotesHz.push_back (440.0f * std::pow (2.0f, (float) semi / 12.0f));
            }
        }

        // ---- Sibilance: S frames against vowel frames
        std::vector<size_t> sibFrames, vowelFrames;
        {
            std::vector<float> sibLevels, vowelLevels;
            for (size_t i = 0; i < frames.size(); ++i)
            {
                const auto& f = frames[i];
                const double total = energy (100, 16000, &f.bands);
                if (total <= 0.0 || f.rmsDb < frameGate - 15.0f)
                    continue;
                const double sibShare = energy (4500, 11000, &f.bands) / total;
                const double vowelShare = energy (100, 2500, &f.bands) / total;
                if (sibShare > 0.45)
                {
                    sibLevels.push_back (f.rmsDb);
                    sibFrames.push_back (i);
                }
                else if (vowelShare > 0.75 && voiced[i])
                {
                    vowelLevels.push_back (f.rmsDb);
                    vowelFrames.push_back (i);
                }
            }
            p.sibilantShare = (float) sibFrames.size() / (float) voicedCount;
            for (auto i : sibFrames)   p.sibilantFrames.push_back ((int) i);
            for (auto i : vowelFrames) p.vowelFrames.push_back ((int) i);
            if (sibLevels.size() >= 6 && vowelLevels.size() >= 20)
            {
                p.sibilanceVsVowelDb = percentile (sibLevels, 0.5f) - percentile (vowelLevels, 0.5f);
                p.sibilanceValid = true;

                // Centre of the S energy: average S spectrum, 3.5 .. 11 kHz, smoothed
                std::array<double, nb> sAvg {};
                for (auto i : sibFrames)
                    for (int b = 0; b < nb; ++b)
                        sAvg[(size_t) b] += frames[i].bands[(size_t) b];
                double bestE = 0.0;
                for (int b = 0; b < nb; ++b)
                {
                    const float c = bandCentre (b);
                    if (c < 3500.0f || c > 11000.0f)
                        continue;
                    double e = 0.0;
                    for (int j = b - 2; j <= b + 2; ++j)
                        if (j >= 0 && j < nb && binsPerBand[(size_t) j] > 0)
                            e += sAvg[(size_t) j] / binsPerBand[(size_t) j];
                    if (e > bestE)
                    {
                        bestE = e;
                        p.sibilanceHz = c;
                    }
                }
            }
        }

        // Peak levels of the de-esser detector (2nd order high-pass) on S and vowel frames
        {
            p.deessDetectorHz = juce::jlimit (4000.0f, 8000.0f, p.sibilanceHz * 0.78f);
            dsp::Biquad hp;
            hp.setCoeffs (dsp::design::highPass (fs, p.deessDetectorHz, 0.70710678));
            std::vector<float> hpPeak (frames.size(), 0.0f);
            for (int i = 0; i < n; ++i)
            {
                const float y = std::abs (hp.process (mid[(size_t) i], 0));
                // Frame k covers [k * hop, k * hop + fftSize): attribute each sample to the
                // frame whose central hop contains it
                const int fi = (i - (fftSize - hop) / 2) / hop;
                if (i >= (fftSize - hop) / 2 && (size_t) fi < hpPeak.size())
                    hpPeak[(size_t) fi] = std::max (hpPeak[(size_t) fi], y);
            }
            std::vector<float> v, s;
            for (auto i : vowelFrames) v.push_back (juce::Decibels::gainToDecibels (hpPeak[i], -100.0f));
            for (auto i : sibFrames)   s.push_back (juce::Decibels::gainToDecibels (hpPeak[i], -100.0f));
            if (! v.empty()) p.sibBandVowelP90Db = percentile (v, 0.9f);
            if (! s.empty()) p.sibBandSibilantP50Db = percentile (s, 0.5f);
        }

        // ---- Resonances: narrow peaks over their surroundings, 250 Hz .. 8 kHz
        {
            for (int b = 0; b < nb; ++b)
            {
                const float c = bandCentre (b);
                if (c < 250.0f || c > 8000.0f || binsPerBand[(size_t) b] < 2 || b < 1 || b >= nb - 1)
                    continue;
                float sum = 0.0f;
                int count = 0;
                for (int j = b - 6; j <= b + 6; ++j)
                    if (std::abs (j - b) >= 2 && j >= 0 && j < nb && binsPerBand[(size_t) j] > 0)
                    {
                        sum += p.spectrumDb[(size_t) j];
                        ++count;
                    }
                if (count < 6)
                    continue;
                const float s = p.spectrumDb[(size_t) b];
                const bool localMax = s >= p.spectrumDb[(size_t) b - 1] && s >= p.spectrumDb[(size_t) b + 1];
                const float excess = s - sum / (float) count;
                if (! localMax || excess <= 7.0f)
                    continue;

                // A peak on a harmonic of the sung notes is the voice itself, not a
                // resonance (held notes put their partials into the long-term spectrum)
                bool harmonic = false;
                if (p.pitchValid)
                    for (float f0 : p.sungNotesHz)
                        for (int k = 1; k <= 4 && ! harmonic; ++k)
                            harmonic = std::abs (std::log2 (c / (f0 * (float) k))) < 0.06f;   // +-70 cents
                if (! harmonic)
                    p.resonances.push_back ({ c, excess, excess > 10.0f });
            }
            std::sort (p.resonances.begin(), p.resonances.end(), [] (const Resonance& a, const Resonance& b) { return a.excessDb > b.excessDb; });
            if (p.resonances.size() > 3)
                p.resonances.resize (3);
        }

        // ---- Tails after words (room or effects already on the recording)
        {
            // After the end of a word the level falls; a dry voice falls 20 dB within
            // a few frames (window smearing included), a room or effect tail takes
            // much longer. The time from -6 to -26 dB below the word is scaled to a
            // 30 dB decay. Only clean drops into a pause count (no new word starts).
            const double frameSeconds = hop / fs;
            const int lookBack = std::max (2, (int) std::lround (0.25 / frameSeconds));
            std::vector<float> decays;
            for (size_t i = (size_t) lookBack; i + 1 < frames.size(); ++i)
            {
                float recent = -200.0f;
                for (size_t j = i - (size_t) lookBack; j < i; ++j)
                    recent = std::max (recent, frames[j].rmsDb);
                if (recent < frameGate + 10.0f || frames[i].rmsDb > recent - 6.0f || frames[i - 1].rmsDb <= recent - 6.0f)
                    continue;   // not the moment the level crosses -6 dB below the word
                // Decay speed (dB/s) after the -6 dB crossing: until 10 dB further down,
                // or - when the next word comes first - over whatever drop was seen
                const float start = frames[i].rmsDb;
                float minimum = start;
                double slope = 0.0;
                for (size_t k = i + 1; k < frames.size() && (double) (k - i) * frameSeconds < 2.5; ++k)
                {
                    const float lv = frames[k].rmsDb;
                    const double t = (double) (k - i) * frameSeconds;
                    if (lv > minimum + 4.0f)
                    {
                        if (start - minimum >= 6.0f)
                            slope = (start - minimum) / t;
                        break;
                    }
                    minimum = std::min (minimum, lv);
                    if (start - lv >= 10.0f || lv < -75.0f)
                    {
                        slope = (start - lv) / t;
                        break;
                    }
                }
                if (slope > 0.0)
                    decays.push_back ((float) (30.0 / slope));   // time for a 30 dB decay at this speed
            }
            p.tailCount = (int) decays.size();
            if (decays.size() >= 3)
            {
                p.tailSeconds = percentile (decays, 0.5f);
                p.reverberant = p.tailSeconds > 0.6f;
            }
        }

        // ---- Stereo
        if (p.stereoContent)
        {
            double ll = 0, rr = 0, lr = 0;
            for (size_t i = 0; i < frames.size(); ++i)
                if (voiced[i]) { ll += frames[i].ll; rr += frames[i].rr; lr += frames[i].lr; }
            if (ll > 0.0 && rr > 0.0)
            {
                p.correlation = (float) (lr / std::sqrt (ll * rr));
                p.balanceDb = toDb (ll) - toDb (rr);
                const double sideE = 0.25 * (ll + rr - 2.0 * lr), midE = 0.25 * (ll + rr + 2.0 * lr);
                p.sideToMidDb = toDb (sideE) - toDb (midE);
            }
        }

        p.heavilyCompressed = p.dynamicsValid && p.dynamicsDb < 6.0f && p.crestDb < 10.0f;
        p.distortionLikely = p.clipRuns >= 3 || (p.peakDb > -0.3f && p.crestDb < 6.0f);

        // ---- Beat (sidechain): level and overlap in the bands the vocal needs
        if (audio.hasBeat())
        {
            std::vector<const float*> beat;
            for (auto& c : audio.beat)
                beat.push_back (c.data());
            p.beatLufs = integratedLufs (beat, n, fs);
            if (p.beatLufs > -50.0f)
            {
                p.beatPresent = true;
                std::array<double, nb> bAvg {}, vAvg {};
                int count = 0;
                for (size_t fi = 0; fi < frames.size(); ++fi)
                {
                    if (! voiced[fi])
                        continue;
                    const int start = (int) fi * hop;
                    for (int i = 0; i < fftSize; ++i)
                        work[(size_t) i] = 0.5f * (beat[0][start + i] + beat[beat.size() > 1 ? 1 : 0][start + i]);
                    std::fill (work.begin() + fftSize, work.end(), 0.0f);
                    window.multiplyWithWindowingTable (work.data(), (size_t) fftSize);
                    fft.performFrequencyOnlyForwardTransform (work.data(), true);
                    for (int k = 1; k < fftSize / 2; ++k)
                        if (const int band = binToBand[(size_t) k]; band >= 0)
                            bAvg[(size_t) band] += (double) work[(size_t) k] * work[(size_t) k];
                    for (int b = 0; b < nb; ++b)
                        vAvg[(size_t) b] += frames[fi].bands[(size_t) b];
                    ++count;
                }
                if (count > 0)
                {
                    const auto range = [&] (const std::array<double, nb>& a, float lo, float hi)
                    {
                        double e = 0.0;
                        for (int b = 0; b < nb; ++b)
                            if (bandCentre (b) >= lo && bandCentre (b) < hi)
                                e += a[(size_t) b];
                        return toDb (e / count);
                    };
                    p.beatPresenceVsVocalDb = range (bAvg, 2000, 5000) - range (vAvg, 2000, 5000);
                    p.beatLowMidVsVocalDb = range (bAvg, 250, 500) - range (vAvg, 250, 500);
                    p.maskingHint = p.beatPresenceVsVocalDb > 3.0f;
                }
            }
        }

        // ======================================================================
        // Issues (German UI texts). Measurements are reported as such; heuristics
        // carry their confidence. Thresholds are deliberately conservative.
        using C = Confidence;
        const auto add = [&p] (const char* key, C c, juce::String title, juce::String detail, float severity)
        {
            p.issues.push_back ({ key, std::move (title), std::move (detail), c, severity });
        };

        if (p.clipRuns > 0)
            add ("clipping", C::measured, de ("Übersteuerung im Eingang"),
                 juce::String (p.clipRuns) + de (" Stelle(n) mit Samples auf Vollaussteuerung. Velvox kann Verzerrung nicht rückgängig machen - Aufnahme- oder Spurpegel senken."), 100.0f);
        else if (p.peakDb > -0.5f)
            add ("hotInput", C::measured, de ("Sehr hoher Eingangspegel"),
                 de ("Spitzenpegel ") + db (p.peakDb) + de (" dBFS - kaum Reserve."), 50.0f);

        if (p.enoughMaterial && p.voiceRmsDb < -40.0f)
            add ("quiet", C::measured, de ("Sehr leises Signal"),
                 de ("Mittlerer Stimmpegel ") + db (p.voiceRmsDb) + de (" dBFS. Input-Gain oder Spurpegel anheben."), 40.0f);

        if (! p.enoughMaterial)
            return p;

        if (p.snrValid && p.snrDb < 30.0f)
            add ("noise", p.snrDb < 22.0f ? C::likely : C::possible, de ("Hörbares Hintergrundrauschen"),
                 de ("Abstand Stimme zu Pausen ") + juce::String (p.snrDb, 1) + de (" dB. Anhebungen werden begrenzt, damit Rauschen nicht lauter wird."), 30.0f);

        if (p.rumbleDb > -24.0f)
            add ("rumble", p.rumbleDb > -16.0f ? C::likely : C::possible, de ("Viel Energie unter 90 Hz"),
                 de ("Rumpeln/Trittschall: ") + db (p.rumbleDb) + de (" gegenüber dem Kern der Stimme."), 35.0f + p.rumbleDb);

        if (p.lowMidDb > -1.0f)
            add ("mud", p.lowMidDb > 2.0f ? C::likely : C::possible, de ("Viel Tiefmitten-Energie"),
                 de ("250-500 Hz liegt bei ") + db (p.lowMidDb) + de (" gegenüber 300 Hz-2 kHz (Maximum um ") + hz (p.lowMidPeakHz) + ").", 20.0f + p.lowMidDb);

        if (p.topDb < -16.0f)
            add ("dull", p.topDb < -20.0f ? C::likely : C::possible, de ("Wenig Höhen und Präsenz"),
                 de ("Alles über 2 kHz liegt bei ") + db (p.topDb) + de (" gegenüber dem Kern der Stimme."), 25.0f - p.topDb * 0.5f);
        else if (p.presenceDb > -2.0f)
            add ("harsh", p.presenceDb > 1.0f ? C::likely : C::possible, de ("Betonte obere Mitten"),
                 de ("2-5 kHz liegt bei ") + db (p.presenceDb) + de (" gegenüber dem Kern der Stimme - kann scharf klingen."), 25.0f + p.presenceDb);

        if (p.sibilanceValid && p.sibilanceVsVowelDb > -5.0f)
            add ("sibilance", p.sibilanceVsVowelDb > -2.0f ? C::likely : C::possible, de ("Ausgeprägte S-Laute"),
                 de ("S-Laute liegen ") + db (p.sibilanceVsVowelDb) + de (" gegenüber den Vokalen, Schwerpunkt um ") + hz (p.sibilanceHz) + ".", 35.0f + p.sibilanceVsVowelDb);

        for (auto& r : p.resonances)
            add ("resonance", r.strong && p.pitchSpreadSemitones > 3.0f ? C::likely : C::possible, de ("Schmalbandige Resonanz"),
                 de ("Spitze um ") + hz (r.hz) + ", " + db (r.excessDb) + de (" über der Umgebung (kann bei wenig Tonhöhenwechsel auch ein Teilton sein)."),
                 10.0f + r.excessDb);

        if (p.dynamicsValid && p.dynamicsDb > 12.0f)
            add ("dynamics", p.dynamicsDb > 16.0f ? C::likely : C::possible, de ("Große Lautstärkeschwankungen"),
                 de ("Laute und leise Wörter liegen ") + juce::String (p.dynamicsDb, 1) + de (" dB auseinander (400-ms-Pegel, 10. bis 95. Perzentil)."), 20.0f + p.dynamicsDb);

        if (p.heavilyCompressed)
            add ("compressed", C::possible, de ("Bereits stark komprimiert"),
                 de ("Nur ") + juce::String (p.dynamicsDb, 1) + de (" dB Pegelunterschied zwischen Wörtern und ") + juce::String (p.crestDb, 1) + de (" dB Crest-Faktor."), 15.0f);

        if (p.reverberant)
            add ("reverberant", p.tailSeconds > 1.2f ? C::likely : C::possible, de ("Hörbarer Raum oder Hall auf der Aufnahme"),
                 de ("Nach Wörtern dauert es etwa ") + juce::String (p.tailSeconds, 1) + de (" s bis zum Abklingen um 30 dB."), 18.0f + p.tailSeconds);

        if (p.stereoContent && p.correlation < 0.3f)
            add ("mono", p.correlation < 0.0f ? C::measured : C::possible, de ("Eingeschränkte Monokompatibilität"),
                 de ("Korrelation links/rechts ") + juce::String (p.correlation, 2) + ".", 30.0f);

        if (p.maskingHint)
            add ("masking", C::possible, de ("Beat überlagert den Präsenzbereich"),
                 de ("Der Beat ist bei 2-5 kHz ") + db (p.beatPresenceVsVocalDb) + de (" lauter als die Stimme. Das ist nur ein Hinweis, kein Beweis für Verdeckung."), 12.0f);

        std::stable_sort (p.issues.begin(), p.issues.end(), [] (const Issue& a, const Issue& b)
        {
            if (a.confidence != b.confidence)
                return a.confidence > b.confidence;
            return a.severity > b.severity;
        });
        return p;
    }

    std::optional<float> VocalProfiler::sibilanceAt (const CapturedAudio& audio, const VocalProfile& ref)
    {
        if (ref.sibilantFrames.size() < 6 || ref.vowelFrames.size() < 20 || audio.channels < 1)
            return std::nullopt;
        const int n = audio.numSamples();
        const float* L = audio.vocal[0].data();
        const float* R = audio.channels > 1 ? audio.vocal[1].data() : L;
        const auto frameDb = [&] (int frame)
        {
            const int start = frame * hop;
            double s = 0.0;
            for (int i = start; i < std::min (n, start + fftSize); ++i)
            {
                const double m = 0.5 * (L[i] + R[i]);
                s += m * m;
            }
            return toDb (s / fftSize);
        };
        std::vector<float> sib, vowel;
        for (int f : ref.sibilantFrames) sib.push_back (frameDb (f));
        for (int f : ref.vowelFrames)    vowel.push_back (frameDb (f));
        return percentile (sib, 0.5f) - percentile (vowel, 0.5f);
    }
}
