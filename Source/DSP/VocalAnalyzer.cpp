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
          work ((size_t) fftSize * 2)
    {
        setSampleRate (48000.0);
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
        fft.performFrequencyOnlyForwardTransform (work.data(), true);
        for (int k = 1; k < fftSize / 2; ++k)
        {
            const int band = binToBand[(size_t) k];
            if (band >= 0)
                f.bands[(size_t) band] += work[(size_t) k] * work[(size_t) k];
        }

        frames.push_back (f);
        while (frames.size() > maxFrames())
            frames.pop_front();
    }

    AnalysisResult VocalAnalyzer::analyse() const
    {
        AnalysisResult r;
        r.peakDb = juce::Decibels::gainToDecibels (check.peak.load(), -100.0f);
        r.clipEvents = check.clipEvents.load();
        r.stereo = check.stereoContent.load();
        r.analysedSeconds = (float) (frames.size() * hop / fs);

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
}
