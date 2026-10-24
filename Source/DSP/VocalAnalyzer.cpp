#include "VocalAnalyzer.h"

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

        using C = Finding::Certainty;
        // Material factor: little voice material lowers every spectral / dynamic confidence
        float material = 1.0f;
        const auto add = [&r, &material] (C c, juce::String title, juce::String detail, juce::String advice,
                                          std::optional<ModuleId> module, float severity, juce::String why, float margin01)
        {
            Finding f;
            f.certainty = c;
            f.title = std::move (title);
            f.detail = std::move (detail);
            f.advice = std::move (advice);
            f.module = module;
            f.severity = severity;
            f.why = std::move (why);
            // 0.5 at the threshold, up to 0.95 when the measurement is far beyond it
            f.confidence = juce::jlimit (0.05f, 0.99f, (0.5f + 0.45f * juce::jlimit (0.0f, 1.0f, margin01)) * material);
            r.findings.push_back (std::move (f));
        };
        // How far `v` lies beyond `threshold`, relative to `span` (0..1)
        const auto beyond = [] (float v, float threshold, float span) { return juce::jlimit (0.0f, 1.0f, (v - threshold) / span); };

        // ---- Level problems: measured sample-accurately in the audio thread
        if (r.clipEvents > 0)
            add (C::sure, "Clipping detected",
                 juce::String (r.clipEvents) + " run(s) of samples at full scale reached this point of the chain.",
                 "Lower the gain before this point (Input gain or the module that boosts), or keep Clip guard on.",
                 std::nullopt, 100.0f, "Clipped samples are distortion that no later module can remove - it sounds crunchy and harsh.", 1.2f);
        else if (r.peakDb > -0.3f)
            add (C::sure, "Peaks hit the ceiling", "Highest peak: " + juce::String (r.peakDb, 1) + " dBFS - no headroom left.",
                 "Reduce the level before this point; a few dB of headroom keep the vocal clean.", std::nullopt, 60.0f,
                 "Without headroom any boost later in the chain or on the master clips.", 1.0f);
        else if (r.peakDb > -1.0f)
            add (C::possible, "Very little headroom", "Highest peak: " + juce::String (r.peakDb, 1) + " dBFS.",
                 "Leave about 1-3 dB of headroom before the output stage.", std::nullopt, 20.0f,
                 "It works, but there is no safety margin for louder takes or later boosts.", 0.3f);

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
        material = juce::jlimit (0.6f, 1.0f, 0.6f + 0.4f * (r.voicedSeconds - minVoicedSeconds) / 11.0f);

        // Noise floor: frames at least 25 dB below the loud parts of the voice are pauses
        // (breaths, gaps between phrases). With less than 1 s of them nothing is claimed;
        // digital silence does not count.
        {
            std::vector<float> audible;
            for (auto& f : frames)
                if (f.rmsDb > -110.0f)
                    audible.push_back (f.rmsDb);
            if (! audible.empty())
            {
                std::vector<float> pauses;
                for (float l : audible)
                    if (l < loud - 25.0f)
                        pauses.push_back (l);
                if ((float) pauses.size() * hop / fs >= 1.0f)
                {
                    r.noiseFloorDb = percentile (pauses, 0.5f);
                    r.snrDb = loud - r.noiseFloorDb;
                }
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

        measureExtras (r, voiced, gate);

        if (! r.enoughMaterial)
            return r;

        // ---- Frequency balance (relative to the 300 Hz - 2 kHz core of the voice)
        const float presence = r.bandDb[3];
        if (presence > -2.0f)
            add (C::sure, "Upper mids are aggressive",
                 "The 2-5 kHz range is " + db (presence) + " against the core of the voice (usually about -10 to -5 dB).",
                 "SMOOTH can tame it dynamically; or pull the EQ mid / high band down around 3 kHz.", ModuleId::smooth, 40.0f + presence,
                 "The ear is most sensitive at 2-5 kHz: too much there sounds piercing and tiring at club volume. A bright lead can be intended - judge by ear.",
                 beyond (presence, -2.0f, 6.0f));
        else if (presence > -5.0f)
            add (C::possible, "Upper mids may sound harsh",
                 "The 2-5 kHz range is " + db (presence) + " against the core of the voice.",
                 "Listen for harshness; if so, try SMOOTH or a gentle EQ cut around 3 kHz.", ModuleId::smooth, 15.0f + presence,
                 "Slightly forward upper mids help a vocal cut through a dense EDM mix; they only become a problem if it sounds harsh.",
                 beyond (presence, -5.0f, 3.0f));

        const float top = toDb (ePres + eSib + eAir) - midDb;
        if (top < -20.0f)
            add (C::sure, "The vocal sounds dull",
                 "Everything above 2 kHz is " + db (top) + " against the core of the voice - very little presence and air.",
                 "Raise the EQ high band (shelf around 8-10 kHz) or use the Presence macro.", ModuleId::eq, 30.0f - top * 0.5f,
                 "Consonants and air carry intelligibility; a dull vocal sinks behind synths and hats.", beyond (-top, 20.0f, 8.0f));
        else if (top < -16.0f)
            add (C::possible, "The vocal may lack presence",
                 "Everything above 2 kHz is " + db (top) + " against the core of the voice.",
                 "A small high-shelf boost or the Presence macro can open it up.", ModuleId::eq, 10.0f - top * 0.3f,
                 "A darker vocal can be a style choice; in a bright EDM mix it may need a little more top to stay clear.", beyond (-top, 16.0f, 4.0f));

        if (r.bandDb[0] > -10.0f)
            add (C::sure, "Too much low end / rumble",
                 "Below 100 Hz is " + db (r.bandDb[0]) + " against the core of the voice - a vocal rarely needs that.",
                 "Switch on the EQ low cut (about 80-120 Hz).", ModuleId::eq, 35.0f + r.bandDb[0],
                 "Rumble and handling noise fill the low end that belongs to kick and bass, and make compressors pump.", beyond (r.bandDb[0], -10.0f, 8.0f));
        else if (r.bandDb[0] > -16.0f)
            add (C::possible, "Noticeable low end below 100 Hz",
                 "Below 100 Hz is " + db (r.bandDb[0]) + " against the core of the voice.",
                 "Check the EQ low cut - it often cleans up the mix.", ModuleId::eq, 12.0f + r.bandDb[0] * 0.5f,
                 "Deep voices have real energy here; only cut what does not belong to the voice.", beyond (r.bandDb[0], -16.0f, 6.0f));
        if (r.bandDb[1] < -18.0f)
            add (C::possible, "The vocal may sound thin",
                 "100-300 Hz is " + db (r.bandDb[1]) + " against the core of the voice.",
                 "Less low cut or a little low shelf / the Warmth macro add body.", ModuleId::eq, 8.0f,
                 "The 100-300 Hz range gives a vocal weight and warmth; without it the voice can sound small. Thin can also be intended (telephone, lo-fi).",
                 beyond (-r.bandDb[1], 18.0f, 8.0f));

        // ---- Sibilance: level of S-frames against vowel frames
        std::vector<float> sibLevels, vowelLevels;
        for (size_t i = 0; i < frames.size(); ++i)
        {
            const auto& f = frames[i];
            const double total = energy (100, 16000, &f.bands);
            if (total <= 0.0 || f.rmsDb < gate - 15.0f)
                continue;
            const double sibShare = energy (4500, 11000, &f.bands) / total;
            const double vowelShare = energy (100, 2500, &f.bands) / total;
            if (sibShare > 0.5)
                sibLevels.push_back (f.rmsDb);
            else if (vowelShare > 0.75 && voiced[i])
                vowelLevels.push_back (f.rmsDb);
        }
        if (sibLevels.size() >= 8 && vowelLevels.size() >= 20)
        {
            const float diff = percentile (sibLevels, 0.5f) - percentile (vowelLevels, 0.5f);
            r.sibilanceVsVowelDb = diff;
            if (diff > -3.0f)
                add (C::sure, "S sounds stick out clearly",
                     "S / T sounds are " + db (diff) + " against the vowels (natural is about -10 to -6 dB).",
                     "Switch on the DE-ESSER and lower its threshold carefully.", ModuleId::deess, 45.0f + diff,
                     "Loud S / T sounds hurt at high volume and get even sharper with compression, saturation and reverb.", beyond (diff, -3.0f, 6.0f));
            else if (diff > -6.0f)
                add (C::possible, "S sounds are quite present",
                     "S / T sounds are " + db (diff) + " against the vowels.",
                     "A little more DE-ESSER range may help - listen on headphones.", ModuleId::deess, 18.0f + diff,
                     "Clear consonants help the words; they only need control if they start to hiss.", beyond (diff, -6.0f, 3.0f));
        }

        // ---- Resonances: narrow peaks of the long-term spectrum over their octave
        {
            struct Peak { int band; float excess; };
            std::vector<Peak> peaks;
            for (int b = 0; b < numBands; ++b)
            {
                const float c = bandCentre (b);
                if (c < 400.0f || c > 8000.0f || binsPerBand[(size_t) b] < 2)
                    continue;
                float sum = 0.0f;
                int count = 0;
                for (int j = b - 6; j <= b + 6; ++j)
                    if (j != b && j >= 0 && j < numBands && binsPerBand[(size_t) j] > 0)
                    {
                        sum += r.spectrumDb[(size_t) j];
                        ++count;
                    }
                if (count < 6)
                    continue;
                const float s = r.spectrumDb[(size_t) b];
                const bool localMax = s >= r.spectrumDb[(size_t) b - 1] && s >= r.spectrumDb[(size_t) b + 1];
                const float excess = s - sum / (float) count;
                if (localMax && excess > 8.0f)
                    peaks.push_back ({ b, excess });
            }
            std::sort (peaks.begin(), peaks.end(), [] (const Peak& a, const Peak& b) { return a.excess > b.excess; });
            for (size_t i = 0; i < std::min<size_t> (2, peaks.size()); ++i)
            {
                const float f = bandCentre (peaks[i].band);
                const juce::String region = f < 1000.0f ? "low mids (boxy)" : f < 2500.0f ? "mids (nasal)" : f < 5000.0f ? "upper mids (harsh)" : "highs (piercing)";
                const bool sure = peaks[i].excess > 12.0f;
                add (sure ? C::sure : C::possible,
                     juce::String (sure ? "Strong" : "Possible") + " resonance in the " + region,
                     "Narrow peak around " + hz (f) + ", " + db (peaks[i].excess) + " above its surroundings.",
                     "SMOOTH reduces it only when it rings; or cut with the EQ mid band at " + hz (f) + ".",
                     ModuleId::smooth, (sure ? 30.0f : 12.0f) + peaks[i].excess,
                     "A narrow peak rings on every word that hits it (room, mic or voice) - it sounds " + region.fromFirstOccurrenceOf ("(", false, false).upToFirstOccurrenceOf (")", false, false)
                         + " and draws attention away from the performance.",
                     beyond (peaks[i].excess, 8.0f, 8.0f));
            }
        }

        // ---- Dynamics: loudness range of 400 ms windows of the voice
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

            if (windowLevels.size() >= 8)
            {
                r.loudnessRangeDb = percentile (windowLevels, 0.95f) - percentile (windowLevels, 0.10f);
                r.crestDb = percentile (crests, 0.5f);
                const float lra = r.loudnessRangeDb;
                if (lra > 16.0f)
                    add (C::sure, "Very large level differences",
                         "Loud and quiet passages are " + juce::String (lra, 1) + " dB apart (10th to 95th percentile).",
                         "LEVEL RIDE evens out words and phrases without squashing them.", ModuleId::ride, 30.0f + lra,
                         "Quiet words disappear behind the beat, loud ones jump out; a compressor alone would have to work too hard.", beyond (lra, 16.0f, 8.0f));
                else if (lra > 12.0f)
                    add (C::possible, "Noticeable level differences",
                         "Loud and quiet passages are " + juce::String (lra, 1) + " dB apart.",
                         "LEVEL RIDE (moderate Amount) keeps the vocal steadier in the mix.", ModuleId::ride, 10.0f + lra,
                         "Some movement is musical; in a loud EDM mix a steadier vocal stays on top.", beyond (lra, 12.0f, 4.0f));
                else if (lra < 3.0f && r.crestDb < 8.0f)
                    add (C::possible, "Possibly over-compressed",
                         "Only " + juce::String (lra, 1) + " dB level range and " + juce::String (r.crestDb, 1) + " dB crest factor left.",
                         "If it sounds flat, reduce Compressor ratio or VOCAL SMASH blend.", ModuleId::comp, 9.0f,
                         "Already very even material: more compression adds pumping and breath noise, not loudness. Heavy density can be intended.",
                         beyond (3.0f - lra, 0.0f, 3.0f));
            }
        }

        // ---- Stereo compatibility
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
            {
                r.correlation = (float) (lr / std::sqrt (ll * rr));
                if (r.correlation < 0.0f)
                    add (C::sure, "Mono compatibility problem",
                         "Left / right correlation is " + juce::String (r.correlation, 2) + " - parts of the vocal cancel in mono.",
                         "Reduce Width, CHOIR width or raise the Doubler's Mono safe.", ModuleId::choir, 50.0f,
                         "Club systems and phones are often mono: a vocal with negative correlation loses level or tone there.", beyond (-r.correlation, 0.0f, 0.3f));
                else if (r.correlation < 0.3f)
                    add (C::possible, "Weak mono compatibility",
                         "Left / right correlation is " + juce::String (r.correlation, 2) + ".",
                         "Check with Mono; narrow the widest module if the vocal loses level.", ModuleId::choir, 14.0f,
                         "Wide vocals sound big on headphones but can thin out in mono.", beyond (0.3f - r.correlation, 0.0f, 0.3f));
            }
        }

        // ---- Long tails (effects): time from the end of a word until it has decayed by 30 dB
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
            if (decays.size() >= 5)
            {
                const float decay = percentile (decays, 0.5f);
                if (decay > 0.9f)
                    add (C::possible, "The vocal may sound washed out",
                         "After words the sound takes about " + juce::String (decay, 1) + " s to fade by 30 dB - long effect tails.",
                         "Lower Reverb / Delay mix or add ducking so the words stay clear.", ModuleId::reverb, 8.0f + decay,
                         "Long effect tails between words blur the next syllables. Big spaces can be intended - ducking keeps both.", beyond (decay, 0.9f, 1.5f));
            }
        }

        // ---- Background noise in the pauses (only with enough pause material)
        if (r.noiseFloorDb > -199.0f)
        {
            if (r.snrDb < 30.0f)
                add (r.snrDb < 22.0f ? C::sure : C::possible,
                     r.snrDb < 22.0f ? "Noisy recording" : "Audible background noise in the pauses",
                     "The pauses sit at " + juce::String (r.noiseFloorDb, 1) + " dBFS, only " + juce::String (r.snrDb, 1)
                         + " dB below the loud parts of the voice (clean studio takes: more than 40 dB).",
                     "Compression and LEVEL RIDE bring the noise up; keep RIDE's 'Ignore below' above the noise and use gentle compression.",
                     ModuleId::ride, 20.0f + (30.0f - r.snrDb),
                     "Noise, room or bleed between the words is lifted by every dynamics module and gets louder in the final mix. "
                     "The analysis of tone is also less reliable on noisy material.",
                     beyond (30.0f - r.snrDb, 0.0f, 12.0f));
        }

        std::stable_sort (r.findings.begin(), r.findings.end(), [] (const Finding& a, const Finding& b)
        {
            if (a.certainty != b.certainty)
                return a.certainty == Finding::Certainty::sure;
            return a.severity > b.severity;
        });
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
    }

    AnalysisResult VocalAnalyzer::analyse() const
    {
        auto r = measure();
        // 0.6 schema on the 0.4 rules (replaced by check::evaluate)
        std::vector<Finding> shown;
        for (auto& f : r.findings)
        {
            if (f.confidence < check::showThreshold)
                continue;
            f.id = f.module.has_value() ? juce::String (moduleKey (*f.module)) : juce::String ("level");
            f.problem = f.title;
            f.fix = juce::String::fromUTF8 ("\xe2\x86\x92 ") + f.advice;
            shown.push_back (f);
        }
        r.findings = std::move (shown);
        return r;
    }
}
