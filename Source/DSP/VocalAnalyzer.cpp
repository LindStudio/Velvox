#include "VocalAnalyzer.h"
#include "../Check/CheckTargets.h"
#include "../Check/CheckRules.h"

namespace velvox::dsp
{
    namespace
    {
        constexpr float voicedAbsoluteDb = -50.0f;    // frames quieter than this are pauses
        constexpr float voicedRelativeDb = 35.0f;     // ... or more than this below the loud parts
        constexpr float continuousSeconds = 30.0f;
        constexpr float captureSeconds = 300.0f;

        float percentile (std::vector<float> v, float p)
        {
            if (v.empty())
                return 0.0f;
            const auto k = (size_t) juce::jlimit (0, (int) v.size() - 1, (int) std::round (p * (float) (v.size() - 1)));
            std::nth_element (v.begin(), v.begin() + (long) k, v.end());
            return v[k];
        }

        float toDb (double power) { return (float) (10.0 * std::log10 (std::max (power, 1.0e-20))); }

        juce::String hz (float f)
        {
            return f >= 1000.0f ? juce::String (f / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz";
        }

        juce::String db (float v) { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; }
    }

    VocalAnalyzer::VocalAnalyzer (CheckModule& module)
        : check (module),
          inL ((size_t) fftSize), inR ((size_t) fftSize),
          pullL ((size_t) fftSize), pullR ((size_t) fftSize),
          work ((size_t) fftSize * 2),
          pitchWork ((size_t) fftSize * 4)
    {
        // Autocorrelation of the analysis window: divides out the taper of the frame
        // autocorrelation, so a periodic signal reads ~1 at its period (computed once)
        static const std::vector<float> acf = []
        {
            std::vector<float> w ((size_t) fftSize), a ((size_t) fftSize);
            for (int i = 0; i < fftSize; ++i)
                w[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) (fftSize - 1));
            for (int k = 0; k < fftSize; ++k)
            {
                double sum = 0.0;
                for (int i = 0; i + k < fftSize; ++i)
                    sum += (double) w[(size_t) i] * w[(size_t) (i + k)];
                a[(size_t) k] = (float) sum;
            }
            const float a0 = a[0];
            for (auto& v : a)
                v /= a0;
            return a;
        }();
        windowAcf = acf;
        setSampleRate (48000.0);
    }

    namespace
    {
        // ITU-R BS.1770 K-weighting (the 48 kHz coefficients, evaluated at a frequency):
        // power gain of the pre-filter (high shelf) times the RLB high-pass
        double kWeightingPower (double freq)
        {
            const auto mag2 = [freq] (double b0, double b1, double b2, double a1, double a2)
            {
                const std::complex<double> z = std::polar (1.0, -2.0 * juce::MathConstants<double>::pi * freq / 48000.0);
                const auto h = (b0 + b1 * z + b2 * z * z) / (1.0 + a1 * z + a2 * z * z);
                return std::norm (h);
            };
            return mag2 (1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585)
                 * mag2 (1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621);
        }
    }

    void VocalAnalyzer::setSampleRate (double sampleRate)
    {
        if (sampleRate <= 0.0 || (sampleRate == fs && ! binToBand.empty()))
            return;
        fs = sampleRate;
        binToBand.assign ((size_t) fftSize / 2, -1);
        binsPerBand.fill (0);
        for (int k = 1; k < fftSize / 2; ++k)
        {
            const double f = k * fs / fftSize;
            const int band = (int) std::lround (12.0 * std::log2 (f / 40.0));
            if (band >= 0 && band < numBands)
            {
                binToBand[(size_t) k] = band;
                ++binsPerBand[(size_t) band];
            }
        }
        for (int b = 0; b < numBands; ++b)
            kWeight[(size_t) b] = (float) kWeightingPower (bandCentre (b));
        clear();
    }

    void VocalAnalyzer::setMode (Mode m)
    {
        if (m == mode)
            return;
        mode = m;
        capturing = false;
        clear();
    }

    void VocalAnalyzer::startCapture()
    {
        clear();
        capturing = true;
    }

    void VocalAnalyzer::stopCapture()
    {
        capturing = false;
    }

    void VocalAnalyzer::clear()
    {
        frames.clear();
        fill = 0;
        ++generation;
        check.resetStats();
    }

    size_t VocalAnalyzer::maxFrames() const noexcept
    {
        const float seconds = mode == Mode::continuous ? continuousSeconds : captureSeconds;
        return (size_t) (seconds * fs / hop);
    }

    void VocalAnalyzer::pump()
    {
        for (;;)
        {
            const int n = check.pull (pullL.data(), pullR.data(), (int) pullL.size());
            if (n <= 0)
                break;
            feed (pullL.data(), pullR.data(), n);
        }
    }

    void VocalAnalyzer::feed (const float* left, const float* right, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            inL[(size_t) fill] = left[i];
            inR[(size_t) fill] = right[i];
            if (++fill == fftSize)
            {
                analyseFrame();
                std::copy (inL.begin() + hop, inL.end(), inL.begin());
                std::copy (inR.begin() + hop, inR.end(), inR.begin());
                fill = fftSize - hop;
            }
        }
    }

    void VocalAnalyzer::analyseFrame()
    {
        if (! storing())
            return;

        Frame f;
        double sum = 0.0, lr = 0.0, ll = 0.0, rr = 0.0;
        float peak = 0.0f;
        for (int i = 0; i < fftSize; ++i)
        {
            const float l = inL[(size_t) i], r = inR[(size_t) i];
            const float m = 0.5f * (l + r);
            sum += (double) m * m;
            lr += (double) l * r;
            ll += (double) l * l;
            rr += (double) r * r;
            peak = std::max (peak, std::abs (m));
            work[(size_t) i] = m;
        }
        f.rmsDb = toDb (sum / fftSize);
        f.peakDb = juce::Decibels::gainToDecibels (peak, -100.0f);
        f.lr = (float) lr;
        f.ll = (float) ll;
        f.rr = (float) rr;

        std::fill (work.begin() + fftSize, work.end(), 0.0f);
        window.multiplyWithWindowingTable (work.data(), (size_t) fftSize);
        std::copy (work.begin(), work.begin() + fftSize, pitchWork.begin());
        f.f0 = estimatePitch (pitchWork.data());
        fft.performFrequencyOnlyForwardTransform (work.data(), true);
        for (int k = 1; k < fftSize / 2; ++k)
        {
            const int band = binToBand[(size_t) k];
            if (band >= 0)
                f.bands[(size_t) band] += work[(size_t) k] * work[(size_t) k];
        }

        // Band power -> mean square of the frame: one-sided Parseval with the Hann window
        // (sum of w^2 = 3/8 N)
        const double toMeanSquare = 2.0 / ((double) fftSize * 0.375 * (double) fftSize);
        double lf = 0.0, k = 0.0;
        for (int b = 0; b < numBands; ++b)
        {
            const double p = f.bands[(size_t) b];
            k += p * kWeight[(size_t) b];
            const float c = bandCentre (b);
            if (c >= 40.0f && c < 150.0f)
                lf += p;
        }
        f.kPower = (float) (k * toMeanSquare);

        // Spectral envelope of pitched frames: the harmonic peaks, interpolated (log
        // frequency, dB) onto the band grid between the fundamental and the last harmonic
        // below 8 kHz. Unlike the plain spectrum it does not depend on which notes were sung,
        // only on the sound of the voice and the chain (what an EQ changes).
        f.env.fill (-300.0f);
        if (f.f0 > 0.0f)
        {
            const double binHz = fs / fftSize;
            std::array<float, 160> hx {}, hy {};
            int nh = 0;
            const int search = std::max (1, (int) (0.3 * f.f0 / binHz));
            for (int h = 1; nh < (int) hx.size() && h * f.f0 < std::min (8000.0, 0.45 * fs); ++h)
            {
                const int centre = (int) std::lround (h * f.f0 / binHz);
                int best = centre;
                for (int j = std::max (1, centre - search); j <= std::min (fftSize / 2 - 2, centre + search); ++j)
                    if (work[(size_t) j] > work[(size_t) best])
                        best = j;
                best = juce::jlimit (1, fftSize / 2 - 2, best);
                const double p = (double) work[(size_t) best - 1] * work[(size_t) best - 1] + (double) work[(size_t) best] * work[(size_t) best]
                               + (double) work[(size_t) best + 1] * work[(size_t) best + 1];
                hx[(size_t) nh] = (float) std::log2 (h * f.f0);
                hy[(size_t) nh] = toDb (p);
                ++nh;
            }
            int i = 0;
            for (int b = 0; b < numBands && nh >= 2; ++b)
            {
                const float x = std::log2 (bandCentre (b));
                if (x < hx[0] || x > hx[(size_t) nh - 1])
                    continue;
                while (i + 2 < nh && hx[(size_t) i + 1] < x)
                    ++i;
                const float t = (x - hx[(size_t) i]) / std::max (1.0e-6f, hx[(size_t) i + 1] - hx[(size_t) i]);
                f.env[(size_t) b] = hy[(size_t) i] + juce::jlimit (0.0f, 1.0f, t) * (hy[(size_t) i + 1] - hy[(size_t) i]);
            }
        }
        f.lfDb = toDb (lf * toMeanSquare);

        frames.push_back (f);
        while (frames.size() > maxFrames())
            frames.pop_front();
    }

    float VocalAnalyzer::estimatePitch (const float* windowed)
    {
        // `windowed` = pitchWork: the Hann-windowed frame in the first fftSize values.
        // Autocorrelation = inverse FFT of the power spectrum (zero padded: no wrap-around).
        const int n2 = fftSize * 2;
        auto* w = pitchWork.data();
        jassert (windowed == w);
        juce::ignoreUnused (windowed);
        std::fill (w + fftSize, w + n2 * 2, 0.0f);
        pitchFft.performRealOnlyForwardTransform (w, true);
        for (int k = 0; k <= n2 / 2; ++k)
        {
            const float re = w[2 * k], im = w[2 * k + 1];
            w[2 * k] = re * re + im * im;
            w[2 * k + 1] = 0.0f;
        }
        pitchFft.performRealOnlyInverseTransform (w);
        const float r0 = w[0];
        if (r0 <= 1.0e-12f)
            return 0.0f;

        const int minLag = std::max (2, (int) (fs / 1000.0));
        const int maxLag = std::min (fftSize / 2, (int) (fs / 70.0));
        const auto acf = [&] (int k) { return w[k] / (r0 * std::max (0.05f, windowAcf[(size_t) k])); };

        // Search after the first dip below zero (the main lobe around lag 0 is not a period)
        int start = minLag;
        while (start < maxLag && acf (start) > 0.0f)
            ++start;
        float best = 0.0f;
        for (int k = start; k <= maxLag; ++k)
            best = std::max (best, acf (k));
        if (best < 0.5f)
            return 0.0f;
        // The shortest lag that is nearly as periodic as the best one (avoids sub-octaves)
        for (int k = start + 1; k < maxLag; ++k)
        {
            const float a = acf (k);
            if (a >= 0.9f * best && a >= acf (k - 1) && a >= acf (k + 1))
            {
                const float y0 = acf (k - 1), y1 = a, y2 = acf (k + 1);
                const float den = y0 - 2.0f * y1 + y2;
                const float shift = std::abs (den) > 1.0e-9f ? juce::jlimit (-0.5f, 0.5f, 0.5f * (y0 - y2) / den) : 0.0f;
                return (float) (fs / ((double) k + shift));
            }
        }
        return 0.0f;
    }

    AnalysisResult VocalAnalyzer::measure() const
    {
        AnalysisResult r;
        r.peakDb = juce::Decibels::gainToDecibels (check.peak.load(), -100.0f);
        r.clipEvents = check.clipEvents.load();
        r.stereo = check.stereoContent.load();
        r.analysedSeconds = (float) (frames.size() * hop / fs);
        r.generation = generation;

        if (frames.empty())
            return r;

        // ---- Voice activity: absolute floor and relative to the loud parts
        std::vector<float> levels;
        levels.reserve (frames.size());
        for (auto& f : frames)
            levels.push_back (f.rmsDb);
        const float loud = percentile (levels, 0.95f);
        const float gate = std::max (voicedAbsoluteDb, loud - voicedRelativeDb);

        std::vector<bool> voiced (frames.size());
        int voicedCount = 0;
        for (size_t i = 0; i < frames.size(); ++i)
        {
            voiced[i] = frames[i].rmsDb > gate;
            voicedCount += voiced[i] ? 1 : 0;
        }
        r.voicedSeconds = (float) (voicedCount * hop / fs);
        r.enoughMaterial = r.voicedSeconds >= minVoicedSeconds;

        // Noise floor: frames at least 25 dB below the loud parts of the voice are pauses
        // (breaths, gaps between phrases). With less than 1 s of them nothing is claimed;
        // digital silence does not count.
        {
            std::vector<float> pauses;
            for (auto& f : frames)
                if (f.rmsDb > -110.0f && f.rmsDb < loud - 25.0f)
                    pauses.push_back (f.rmsDb);
            if ((float) pauses.size() * hop / fs >= 1.0f)
            {
                r.noiseFloorDb = percentile (pauses, 0.5f);
                r.snrDb = loud - r.noiseFloorDb;
            }
        }

        // ---- Long-term spectrum of the voice
        std::array<double, numBands> avg {};
        for (size_t i = 0; i < frames.size(); ++i)
            if (voiced[i])
                for (int b = 0; b < numBands; ++b)
                    avg[(size_t) b] += frames[i].bands[(size_t) b];
        for (auto& a : avg)
            a /= std::max (1, voicedCount);

        r.spectrumDb.resize (numBands);
        for (int b = 0; b < numBands; ++b)
            r.spectrumDb[(size_t) b] = binsPerBand[(size_t) b] > 0 ? toDb (avg[(size_t) b] / binsPerBand[(size_t) b]) : -200.0f;

        const auto energy = [&] (float lo, float hi, const std::array<float, numBands>* frameBands = nullptr)
        {
            double e = 0.0;
            for (int b = 0; b < numBands; ++b)
            {
                const float c = bandCentre (b);
                if (c >= lo && c < hi)
                    e += frameBands != nullptr ? (*frameBands)[(size_t) b] : avg[(size_t) b];
            }
            return e;
        };

        const double eRumble = energy (40, 100), eBody = energy (100, 300), eMid = energy (300, 2000);
        const double ePres = energy (2000, 5000), eSib = energy (5000, 10000), eAir = energy (10000, 16000);
        const float midDb = toDb (eMid);
        r.bandDb = { toDb (eRumble) - midDb, toDb (eBody) - midDb, 0.0f, toDb (ePres) - midDb, toDb (eSib) - midDb, toDb (eAir) - midDb };
        // The bands of the auto-mix profile / objective (relative to the 300 Hz - 2 kHz core)
        r.rumbleDb = toDb (energy (40, 90)) - midDb;
        r.bodyDb = r.bandDb[1];
        r.lowMidDb = toDb (energy (250, 500)) - midDb;
        r.presenceDb = r.bandDb[3];
        r.airDb = r.bandDb[5];
        r.topDb = toDb (ePres + eSib + eAir) - midDb;

        measureExtras (r, voiced, gate);

        // ---- Sibilance: level of S-frames against vowel frames
        {
            std::vector<float> sibLevels, vowelLevels, hfLevels;
            const double toMeanSquare = 2.0 / ((double) fftSize * 0.375 * (double) fftSize);
            for (size_t i = 0; i < frames.size(); ++i)
            {
                const auto& f = frames[i];
                const double total = energy (100, 16000, &f.bands);
                if (total <= 0.0 || f.rmsDb < gate - 15.0f)
                    continue;
                hfLevels.push_back (toDb (energy (5000, 10000, &f.bands) * toMeanSquare));
                const double sibShare = energy (4500, 11000, &f.bands) / total;
                const double vowelShare = energy (100, 2500, &f.bands) / total;
                if (sibShare > 0.45)
                    sibLevels.push_back (f.rmsDb);
                else if (vowelShare > 0.75 && voiced[i])
                    vowelLevels.push_back (f.rmsDb);
            }
            if (sibLevels.size() >= 6 && vowelLevels.size() >= 20)
            {
                r.sibilanceVsVowelDb = percentile (sibLevels, 0.5f) - percentile (vowelLevels, 0.5f);
                r.sibilanceValid = true;
            }
            // The loudest moments of the S band (5-10 kHz, 95th percentile of the frames)
            // against the level of the vowels: independent of how frames are classified
            if (vowelLevels.size() >= 20 && hfLevels.size() >= 40)
            {
                r.sibilancePeakDb = percentile (hfLevels, 0.95f) - percentile (vowelLevels, 0.5f);
                r.sibilancePeakValid = true;
            }
        }

        // ---- Resonances: narrow peaks of the long-term spectrum over their +-1/2 octave
        // (the next bands excluded), 250 Hz - 8 kHz. A peak on a harmonic of the sung notes is
        // the voice itself (held notes put their partials into the long-term spectrum).
        {
            for (int b = 1; b + 1 < numBands; ++b)
            {
                const float c = bandCentre (b);
                if (c < 250.0f || c > 8000.0f || binsPerBand[(size_t) b] < 2)
                    continue;
                // Surroundings: the bands 3..7 away on both sides (outside the skirts of a
                // narrow peak); a straight line through them gives the level under the peak,
                // also on a sloping spectrum
                double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
                int count = 0;
                for (int j = b - 7; j <= b + 7; ++j)
                    if (std::abs (j - b) >= 3 && j >= 0 && j < numBands && binsPerBand[(size_t) j] > 0)
                    {
                        const double x = j - b, y = r.spectrumDb[(size_t) j];
                        sx += x; sy += y; sxx += x * x; sxy += x * y;
                        ++count;
                    }
                if (count < 6)
                    continue;
                const double slope = (count * sxy - sx * sy) / std::max (1.0e-9, count * sxx - sx * sx);
                const float base = (float) ((sy - slope * sx) / count);
                const float s = r.spectrumDb[(size_t) b];
                const float sl = r.spectrumDb[(size_t) b - 1], sr = r.spectrumDb[(size_t) b + 1];
                const bool localMax = s >= sl && s >= sr;
                if (! localMax)
                    continue;
                // A peak between two bands splits its energy: the pair with the stronger
                // neighbour counts as well (frequency: power-weighted centre of the pair)
                const float neighbour = std::max (sl, sr);
                const int nb = sl >= sr ? b - 1 : b + 1;
                const float ps = std::pow (10.0f, s / 10.0f), pn = std::pow (10.0f, neighbour / 10.0f);
                const float pairDb = 10.0f * std::log10 ((ps + pn) * 0.5f) + 1.5f;
                const float excess = std::max (s, pairDb) - base;
                if (excess <= 5.0f)
                    continue;
                const float hz = pairDb > s ? std::pow (2.0f, (ps * std::log2 (c) + pn * std::log2 (bandCentre (nb))) / (ps + pn)) : c;
                bool harmonic = false;
                if (r.pitchValid)
                    for (float f0 : r.sungNotesHz)
                        for (int k = 1; k <= 4 && ! harmonic; ++k)
                            harmonic = std::abs (std::log2 (hz / (f0 * (float) k))) < 0.06f;   // +-70 cents
                if (! harmonic)
                    r.resonances.push_back ({ hz, excess });
            }
            std::sort (r.resonances.begin(), r.resonances.end(), [] (const Resonance& a, const Resonance& b) { return a.excessDb > b.excessDb; });
            if (r.resonances.size() > 3)
                r.resonances.resize (3);
        }

        // ---- Dynamics: spread of 400 ms windows of the voice, crest factor
        {
            const int perWindow = std::max (1, (int) std::lround (0.4 * fs / hop));
            std::vector<float> windowLevels;
            std::vector<float> crests;
            for (size_t start = 0; start + (size_t) perWindow <= frames.size(); start += (size_t) perWindow)
            {
                double p = 0.0;
                int v = 0;
                for (size_t i = start; i < start + (size_t) perWindow; ++i)
                {
                    p += std::pow (10.0, frames[i].rmsDb / 10.0);
                    v += voiced[i] ? 1 : 0;
                }
                if (v * 10 >= perWindow * 7)
                    windowLevels.push_back (toDb (p / perWindow));
            }
            for (size_t i = 0; i < frames.size(); ++i)
                if (voiced[i])
                    crests.push_back (frames[i].peakDb - frames[i].rmsDb);
            r.crestDb = percentile (crests, 0.5f);
            if (windowLevels.size() >= 8)
            {
                r.loudnessRangeDb = percentile (windowLevels, 0.95f) - percentile (windowLevels, 0.10f);
                r.phraseValid = true;
            }
        }

        // ---- Stereo correlation of the voice
        if (r.stereo)
        {
            double lr = 0.0, ll = 0.0, rr = 0.0;
            for (size_t i = 0; i < frames.size(); ++i)
                if (voiced[i])
                {
                    lr += frames[i].lr;
                    ll += frames[i].ll;
                    rr += frames[i].rr;
                }
            if (ll > 0.0 && rr > 0.0)
                r.correlation = (float) (lr / std::sqrt (ll * rr));
        }

        // ---- Tails: time from the end of a word until it has decayed by 30 dB
        {
            std::vector<float> decays;
            const double frameSeconds = hop / fs;
            for (size_t i = 4; i < frames.size(); ++i)
            {
                float recent = -200.0f;
                for (size_t j = i - 4; j < i; ++j)
                    recent = std::max (recent, frames[j].rmsDb);
                if (recent < gate + 6.0f || frames[i].rmsDb > recent - 12.0f || frames[i - 1].rmsDb <= recent - 12.0f)
                    continue; // not the start of a clear drop

                float minimum = frames[i].rmsDb;
                for (size_t k = i + 1; k < frames.size() && (double) (k - i) * frameSeconds < 3.0; ++k)
                {
                    const float lv = frames[k].rmsDb;
                    if (lv > minimum + 6.0f)
                        break; // next word starts: no complete decay measured
                    minimum = std::min (minimum, lv);
                    if (lv < recent - 30.0f || lv < -70.0f)
                    {
                        decays.push_back ((float) ((double) (k - i) * frameSeconds));
                        break;
                    }
                }
            }
            r.tailCount = (int) decays.size();
            if (decays.size() >= 5)
                r.tailSeconds = percentile (decays, 0.5f);
        }
        return r;
    }

    void VocalAnalyzer::measureExtras (AnalysisResult& r, const std::vector<bool>& voiced, float gate) const
    {
        const double frameSeconds = hop / fs;
        const size_t nf = frames.size();
        const double toMeanSquare = 2.0 / ((double) fftSize * 0.375 * (double) fftSize);

        // ---- Voice level
        {
            std::vector<float> v;
            for (size_t i = 0; i < nf; ++i)
                if (voiced[i])
                    v.push_back (frames[i].rmsDb);
            if (! v.empty())
                r.voiceLevelDb = percentile (v, 0.5f);
        }

        // ---- Pitch statistics
        {
            std::vector<float> f0s;
            int voicedCount = 0;
            for (size_t i = 0; i < nf; ++i)
                if (voiced[i])
                {
                    ++voicedCount;
                    if (frames[i].f0 > 0.0f)
                        f0s.push_back (frames[i].f0);
                }
            r.pitchedShare = voicedCount > 0 ? (float) f0s.size() / (float) voicedCount : 0.0f;
            if (f0s.size() >= 20 && r.pitchedShare > 0.25f)
            {
                r.pitchValid = true;
                r.f0MedianHz = percentile (f0s, 0.5f);
                r.f0LowHz = percentile (f0s, 0.1f);
                r.f0HighHz = percentile (f0s, 0.9f);
                r.pitchSpreadSemitones = 12.0f * std::log2 (std::max (1.0f, r.f0HighHz) / std::max (1.0f, r.f0LowHz));
                r.highVoice = r.f0LowHz > 250.0f;
                std::map<int, int> hist;
                for (float f : f0s)
                    ++hist[(int) std::lround (12.0f * std::log2 (f / 440.0f))];
                for (auto& [semi, count] : hist)
                    if ((float) count >= 0.04f * (float) f0s.size())
                        r.sungNotesHz.push_back (440.0f * std::pow (2.0f, (float) semi / 12.0f));
            }
        }

        // ---- Loudness (BS.1770 style gating on the analysis frames) and loudness range
        {
            const auto lufs = [] (double ms) { return -0.691f + toDb (ms); };
            double sum = 0.0;
            int count = 0;
            for (auto& f : frames)
                if (lufs (f.kPower) > -70.0f) { sum += f.kPower; ++count; }
            if (count > 0)
            {
                const float relGate = lufs (sum / count) - 10.0f;
                double s2 = 0.0;
                int c2 = 0;
                for (auto& f : frames)
                    if (lufs (f.kPower) > -70.0f && lufs (f.kPower) > relGate) { s2 += f.kPower; ++c2; }
                if (c2 > 0)
                    r.loudnessLufs = lufs (s2 / c2);
            }

            // Short-term loudness: 3 s windows, step ~0.15 s; gates -70 LUFS and -20 LU
            const int perWindow = std::max (1, (int) std::lround (3.0 / frameSeconds));
            const int step = std::max (1, (int) std::lround (0.15 / frameSeconds));
            std::vector<float> st;
            for (size_t start = 0; start + (size_t) perWindow <= nf; start += (size_t) step)
            {
                double p = 0.0;
                for (size_t i = start; i < start + (size_t) perWindow; ++i)
                    p += frames[i].kPower;
                const float l = lufs (p / perWindow);
                if (l > -70.0f)
                    st.push_back (l);
            }
            if (st.size() >= 20)
            {
                double p = 0.0;
                for (float l : st)
                    p += std::pow (10.0, (l + 0.691) / 10.0);
                const float relGate = lufs (p / (double) st.size()) - 20.0f;
                std::vector<float> kept;
                for (float l : st)
                    if (l > relGate)
                        kept.push_back (l);
                if (kept.size() >= 10)
                {
                    r.lraLu = percentile (kept, 0.95f) - percentile (kept, 0.10f);
                    r.lraValid = true;
                }
            }
        }

        // ---- Plosives: low-frequency bursts far above the usual low end of the voice,
        // dominating their frame and short (well above the frames 3 hops before and after)
        {
            std::vector<float> lfVoiced;
            for (size_t i = 0; i < nf; ++i)
                if (voiced[i])
                    lfVoiced.push_back (frames[i].lfDb);
            if (! lfVoiced.empty() && nf > 6)
            {
                const float medLf = percentile (lfVoiced, 0.5f);
                bool inEvent = false;
                for (size_t i = 3; i + 3 < nf; ++i)
                {
                    const auto& f = frames[i];
                    double total = 0.0;
                    for (auto p : f.bands)
                        total += p;
                    const float share = (float) (std::pow (10.0, f.lfDb / 10.0) / std::max (1.0e-20, total * toMeanSquare));
                    const bool burst = f.lfDb > gate - 10.0f && f.lfDb >= medLf + 12.0f && share >= 0.4f
                                    && f.lfDb >= frames[i - 3].lfDb + 6.0f && f.lfDb >= frames[i + 3].lfDb + 6.0f;
                    if (burst && ! inEvent)
                        ++r.plosiveCount;
                    inEvent = burst;
                }
                const float voicedMinutes = (float) (lfVoiced.size() * frameSeconds / 60.0);
                r.plosivesPerMinute = voicedMinutes > 0.0f ? (float) r.plosiveCount / voicedMinutes : 0.0f;
            }
        }

        // ---- Stereo: mono-sum loss over the frames that carry signal in L / R
        if (nf > 0)
        {
            std::vector<float> lrLevels;
            for (auto& f : frames)
                lrLevels.push_back (toDb ((f.ll + f.rr) / (2.0 * fftSize)));
            const float loudLr = percentile (lrLevels, 0.95f);
            double ll = 0.0, rr = 0.0, lr = 0.0;
            for (size_t i = 0; i < nf; ++i)
                if (lrLevels[i] > std::max (-50.0f, loudLr - 35.0f))
                {
                    ll += frames[i].ll;
                    rr += frames[i].rr;
                    lr += frames[i].lr;
                }
            if (ll + rr > 0.0)
            {
                const double monoSum = std::max (1.0e-12 * (ll + rr), 0.5 * (ll + rr + 2.0 * lr));
                r.monoLossDb = juce::jmin (60.0f, toDb (ll + rr) - toDb (monoSum));
                r.sideToMidDb = toDb (std::max (0.0, 0.25 * (ll + rr - 2.0 * lr))) - toDb (std::max (1.0e-20, 0.25 * (ll + rr + 2.0 * lr)));
            }
        }

        // ---- Centre of the S energy (frames dominated by 4.5-11 kHz)
        {
            const auto range = [] (const Frame& f, float lo, float hi)
            {
                double e = 0.0;
                for (int b = 0; b < numBands; ++b)
                    if (bandCentre (b) >= lo && bandCentre (b) < hi)
                        e += f.bands[(size_t) b];
                return e;
            };
            std::array<double, numBands> sAvg {};
            int sCount = 0;
            for (size_t i = 0; i < nf; ++i)
            {
                const auto& f = frames[i];
                const double total = range (f, 100, 16000);
                if (total <= 0.0 || f.rmsDb < gate - 15.0f)
                    continue;
                if (range (f, 4500, 11000) / total > 0.45)
                {
                    for (int b = 0; b < numBands; ++b)
                        sAvg[(size_t) b] += f.bands[(size_t) b];
                    ++sCount;
                }
            }
            if (sCount >= 4)
            {
                double bestE = 0.0;
                for (int b = 0; b < numBands; ++b)
                {
                    const float c = bandCentre (b);
                    if (c < 3500.0f || c > 11000.0f)
                        continue;
                    double e = 0.0;
                    for (int j = b - 2; j <= b + 2; ++j)
                        if (j >= 0 && j < numBands && binsPerBand[(size_t) j] > 0)
                            e += sAvg[(size_t) j] / binsPerBand[(size_t) j];
                    if (e > bestE)
                    {
                        bestE = e;
                        r.sibilanceHz = c;
                    }
                }
            }
        }

        // ---- 1/3-octave smoothed long-term spectrum and its deviation from the voice template
        if (r.spectrumDb.size() == (size_t) numBands)
        {
            r.smoothDb.assign ((size_t) numBands, -200.0f);
            for (int b = 0; b < numBands; ++b)
            {
                double p = 0.0;
                int k = 0;
                for (int j = b - 2; j <= b + 2; ++j)
                    if (j >= 0 && j < numBands && binsPerBand[(size_t) j] > 0)
                    {
                        p += std::pow (10.0, r.spectrumDb[(size_t) j] / 10.0);
                        ++k;
                    }
                if (k > 0)
                    r.smoothDb[(size_t) b] = toDb (p / k);
            }
            // Tone curve: the averaged spectral envelope of the pitched frames where enough of
            // them reach a band (sung / spoken voice), else the long-term spectrum
            r.toneDb = r.smoothDb;
            if (r.pitchValid)
            {
                std::array<double, numBands> ep {};
                std::array<int, numBands> en {};
                int pitched = 0;
                for (size_t i = 0; i < frames.size(); ++i)
                {
                    if (! voiced[i] || frames[i].f0 <= 0.0f)
                        continue;
                    ++pitched;
                    for (int b = 0; b < numBands; ++b)
                        if (frames[i].env[(size_t) b] > -290.0f)
                        {
                            ep[(size_t) b] += std::pow (10.0, frames[i].env[(size_t) b] / 10.0);
                            ++en[(size_t) b];
                        }
                }
                std::vector<float> env ((size_t) numBands, -200.0f);
                int defined = 0;
                for (int b = 0; b < numBands; ++b)
                    if (en[(size_t) b] > 0 && (float) en[(size_t) b] >= 0.3f * (float) pitched)
                    {
                        env[(size_t) b] = toDb (ep[(size_t) b] / en[(size_t) b]);
                        ++defined;
                    }
                if (defined > 30)
                {
                    r.toneFromEnvelope = true;
                    for (int b = 0; b < numBands; ++b)
                    {
                        double p = 0.0;
                        int k = 0;
                        for (int j = b - 2; j <= b + 2; ++j)
                            if (j >= 0 && j < numBands && env[(size_t) j] > -190.0f)
                            {
                                p += std::pow (10.0, env[(size_t) j] / 10.0);
                                ++k;
                            }
                        r.toneDb[(size_t) b] = env[(size_t) b] > -190.0f && k > 0 ? toDb (p / k) : -200.0f;
                    }
                }
            }

            const auto& tpl = check::voiceTemplate (r.highVoice);
            std::vector<float> d;
            for (int b = 0; b < numBands; ++b)
                if (bandCentre (b) >= 150.0f && bandCentre (b) <= 8000.0f && r.toneDb[(size_t) b] > -190.0f)
                    d.push_back (r.toneDb[(size_t) b] - tpl[(size_t) b]);
            if (d.size() > 20)
            {
                const float offset = percentile (d, 0.5f);
                r.deviationDb.assign ((size_t) numBands, 0.0f);
                for (int b = 0; b < numBands; ++b)
                    if (r.toneDb[(size_t) b] > -190.0f)
                        r.deviationDb[(size_t) b] = r.toneDb[(size_t) b] - tpl[(size_t) b] - offset;
            }
        }
    }

    AnalysisResult VocalAnalyzer::analyse (check::Genre genre) const
    {
        auto r = measure();
        check::evaluate (r, genre);
        return r;
    }
}
