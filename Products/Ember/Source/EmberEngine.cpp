#include "EmberEngine.h"
#include "DSP/EqDesign.h"
#include <complex>

namespace ember
{
    namespace
    {
        // 4th-order Butterworth high-pass as two sections
        constexpr double q1 = 0.54119610, q2 = 1.30656296;
        // Coefficients (frequency / width smoothing) are updated on a fixed 16-sample grid
        constexpr int subBlock = 16, glideBlock = 4;
        constexpr float tiny = 1.0e-12f;

        int wrap (int i, int size) noexcept { return i < 0 ? i + size : (i >= size ? i - size : i); }

        // Input frame -> processing channels (stereo, mid/side, mid only, side only)
        int encode (int stereoMode, int numIn, const float* x, float* p) noexcept
        {
            if (numIn < 2)
            {
                p[0] = x[0];
                return 1;
            }
            switch (stereoMode)
            {
                case 1:  p[0] = 0.5f * (x[0] + x[1]); p[1] = 0.5f * (x[0] - x[1]); return 2;
                case 2:  p[0] = 0.5f * (x[0] + x[1]); return 1;
                case 3:  p[0] = 0.5f * (x[0] - x[1]); return 1;
                default: p[0] = x[0]; p[1] = x[1]; return 2;
            }
        }

        // Processing channels -> output channels (only for differences: delta, listen band)
        void decode (int stereoMode, int numIn, const float* p, float* y) noexcept
        {
            if (numIn < 2)
            {
                y[0] = p[0];
                return;
            }
            switch (stereoMode)
            {
                case 1:  y[0] = p[0] + p[1]; y[1] = p[0] - p[1]; break;
                case 2:  y[0] = p[0]; y[1] = p[0]; break;
                case 3:  y[0] = p[0]; y[1] = -p[0]; break;
                default: y[0] = p[0]; y[1] = p[1]; break;
            }
        }
    }

    float staticReductionDb (float inDb, float thresholdDb) noexcept
    {
        const float over = inDb - thresholdDb;
        const float slope = 1.0f - 1.0f / ratio;
        if (over <= -kneeDb * 0.5f)
            return 0.0f;
        if (over >= kneeDb * 0.5f)
            return slope * over;
        const float x = over + kneeDb * 0.5f;
        return slope * x * x / (2.0f * kneeDb);
    }

    BandShape splitBand (double freq, double widthOct) noexcept
    {
        const double n = juce::jlimit ((double) minWidthOct, (double) maxWidthOct, widthOct);
        const double fc = splitCenterRatio * freq;
        double lo = fc * std::exp2 (-0.5 * n);
        if (lo < splitLowestRatio * freq)
            lo = splitLowestRatio * freq;
        const double hi = lo * std::exp2 (n);
        const double centre = std::sqrt (lo * hi);
        // Regalia-Mitra bell (fixed poles, Q): its cut of gain g reaches half its dB at
        // f / centre - centre / f = +-sqrt(g) / Q
        const double x = hi / centre;
        const double gW = std::pow (10.0, -widthRefDb / 20.0);
        return { centre, std::sqrt (gW) / (x - 1.0 / x), lo, hi };
    }

    BiquadCoeffs splitBell (double fs, double freq, double widthOct) noexcept
    {
        const auto shape = splitBand (freq, widthOct);
        EqBand b;
        b.on = true;
        b.type = (int) EqBandType::bell;
        b.freq = (float) std::min (shape.centreHz, fs * 0.45);
        b.q = (float) (shape.poleQ * std::pow (10.0, bandRefDb / 40.0));   // matched cut: pole Q = q / A
        b.gain = -bandRefDb;
        return eqdesign::design (b, false, fs).sections[0];
    }

    BiquadCoeffs bellAt (double fs, double centreHz, double widthOct) noexcept
    {
        // Regalia-Mitra pole Q for half the dB of a widthRefDb cut at +-widthOct / 2 (see splitBand)
        const double x = std::exp2 (0.5 * widthOct), gW = std::pow (10.0, -widthRefDb / 20.0);
        EqBand b;
        b.on = true;
        b.type = (int) EqBandType::bell;
        b.freq = (float) std::min (centreHz, fs * 0.45);
        b.q = (float) (std::sqrt (gW) / (x - 1.0 / x) * std::pow (10.0, bandRefDb / 40.0));
        b.gain = -bandRefDb;
        return eqdesign::design (b, false, fs).sections[0];
    }

    TrackBells trackBells (double freq, double widthOct) noexcept
    {
        const auto shape = splitBand (freq, widthOct);
        TrackBells t;
        for (int k = 0; k < 3; ++k)
            t.centreHz[k] = shape.centreHz * std::exp2 ((k - 1) * (double) trackSpreadOct);
        t.widthOct = std::max ((double) trackMinBellOct, std::log2 (shape.highHz / shape.lowHz) - 2.0 * trackSpreadOct);
        return t;
    }

    void trackWeights (const float* e, float* w) noexcept
    {
        const float sum = e[0] + e[1] + e[2];
        float u = sum > tiny ? (e[1] + 2.0f * e[2]) / sum : 1.0f;   // energy centroid, 0 = low .. 2 = high
        // a dead zone around the middle: an S (or a test tone) on the band centre gets exactly the middle bell, not a
        // blend nudged by the band-passes' slightly unequal skirts
        const float d = u - 1.0f, ad = std::max (0.0f, std::abs (d) - trackDeadZone) / (1.0f - trackDeadZone);
        u = 1.0f + (d < 0.0f ? -ad : ad);
        w[0] = std::max (0.0f, 1.0f - u);
        w[1] = 1.0f - std::abs (u - 1.0f);
        w[2] = std::max (0.0f, u - 1.0f);
    }

    namespace
    {
        std::complex<double> responseAt (const BiquadCoeffs& c, double fs, double hz) noexcept
        {
            const double w = juce::MathConstants<double>::twoPi * hz / fs;
            const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
            return (c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2);
        }
    }

    double bellCentreGain (const BiquadCoeffs& c, double fs, double centreHz) noexcept
    {
        return juce::jlimit (0.05, 0.9, std::abs (responseAt (c, fs, std::min (centreHz, fs * 0.45))));
    }

    double splitResponseDb (double fs, double freq, double widthOct, double reductionDb, double hz, bool tracked) noexcept
    {
        // the band as the engine runs it for an S in the middle of the band: the middle bell (tracked) or the one
        // fixed bell, blended so its centre takes exactly reductionDb
        const auto tb = trackBells (freq, widthOct);
        const auto c = tracked ? bellAt (fs, tb.centreHz[1], tb.widthOct) : splitBell (fs, freq, widthOct);
        const double fc = std::min (tracked ? tb.centreHz[1] : splitBand (freq, widthOct).centreHz, fs * 0.45);
        const auto d = 1.0 - responseAt (c, fs, fc);
        const double blend = blendForGain ((float) std::pow (10.0, -reductionDb / 20.0), (float) d.real(), (float) std::norm (d), (float) (d.imag() * d.imag()));
        return 20.0 * std::log10 (std::abs (1.0 + blend * (responseAt (c, fs, hz) - 1.0)));
    }

    // ---------------------------------------------------------------- MinAverage
    void MinAverage::prepare (int maxWindow)
    {
        cap = std::max (1, maxWindow) + 1;
        dqVal.assign ((size_t) cap, 0.0f);
        dqIdx.assign ((size_t) cap, 0);
        hBuf.assign ((size_t) cap, 0.0f);
        window = std::min (window, cap - 1);
        reset();
    }

    void MinAverage::setWindow (int w) noexcept
    {
        window = juce::jlimit (1, cap - 1, w);
        invWindow = 1.0 / window;
        reset();
    }

    void MinAverage::reset() noexcept
    {
        head = count = hPos = t = 0;
        sum = 0.0;
        lastX = -1.0f;
        run = 0;
        std::fill (hBuf.begin(), hBuf.end(), 0.0f);
    }

    float MinAverage::process (float x) noexcept
    {
        if (window <= 1)
            return x;
        float* const val = dqVal.data();
        int* const idx = dqIdx.data();
        float* const hb = hBuf.data();
        // Steady input (the gate is shut on vowels and pauses, fully open inside an S) for two windows: the
        // window holds nothing else, so the output is the input - skip the bookkeeping, keep the state exact
        if (x == lastX)
        {
            if (++run >= 2 * window)
            {
                run = 2 * window;
                count = 1;
                val[head] = x;
                idx[head] = t;
                if (++t > (1 << 30))
                {
                    t -= (1 << 29);
                    idx[head] = t - 1;
                }
                hb[hPos] = x;
                hPos = hPos + 1 >= window ? 0 : hPos + 1;
                sum = (double) x * window;
                return juce::jlimit (0.0f, 1.0f, x);
            }
        }
        else
        {
            lastX = x;
            run = 1;
        }
        // monotonic deque (ring of cap entries): values increase from the front
        int c = count, h0 = head;
        while (c > 0 && val[wrap (h0 + c - 1, cap)] >= x)
            --c;
        const int back = wrap (h0 + c, cap);
        val[back] = x;
        idx[back] = t;
        ++c;
        while (idx[h0] <= t - window)
        {
            h0 = wrap (h0 + 1, cap);
            --c;
        }
        count = c;
        head = h0;
        // before `window` samples were seen the window still holds the (closed) gate from before
        const float hv = t + 1 < window ? 0.0f : val[h0];
        if (++t > (1 << 30))
        {
            // keep the indices small (the deque only compares differences)
            for (int k = 0; k < c; ++k)
                idx[wrap (h0 + k, cap)] -= (1 << 29);
            t -= (1 << 29);
        }
        sum += (double) hv - (double) hb[hPos];
        hb[hPos] = hv;
        hPos = hPos + 1 >= window ? 0 : hPos + 1;
        return juce::jlimit (0.0f, 1.0f, (float) (sum * invWindow));
    }

    // ---------------------------------------------------------------- Detector
    void Detector::prepare (double sampleRate)
    {
        fs = sampleRate;
        fullRelease = timeConstantCoeff (refVowelReleaseMs, fs);
        nowRelease = timeConstantCoeff (refNowReleaseMs, fs);
        voiceMaxN = (float) (voiceLevelMs * 0.001 * fs / 4.0);   // updated every 4th sample
        voiceMinCoeff = 1.0f / voiceMaxN;
        envCoeff = timeConstantCoeff (smartEnvMs, fs);
        designBands();
        reset();
    }

    void Detector::reset()
    {
        for (auto* f : { &bandIdx1, &bandIdx2, &body1, &body2 })
            f->reset();
        fullEnv = sEnv = bodyEnv = nowEnv = voiceLog = voiceN = 0.0f;
        weak = 1.0f;
        for (int c = 0; c < maxChannels; ++c)
        {
            fullEnvCh[c] = sEnvCh[c] = bodyEnvCh[c] = nowEnvCh[c] = voiceLogCh[c] = voiceNCh[c] = 0.0f;
            weakCh[c] = 1.0f;
        }
        indexDb = -60.0f;
        weight = 0.0f;
        indexPhase = 0;
        for (auto& w : weightCh)
            w = 0.0f;
    }

    float Detector::weightForIndex (float idx, bool allroundGate) noexcept
    {
        const float none = allroundGate ? allroundNoneDb : smartNoneDb, full = allroundGate ? allroundFullDb : smartFullDb;
        const float t = juce::jlimit (0.0f, 1.0f, (idx - none) / (full - none));
        return t * t * (3.0f - 2.0f * t);
    }

    float Detector::weakWeight (float sE, float voiceLog2) noexcept
    {
        const float d = 3.0103f * fast::log2 (sE + tiny) - 6.0205999f * voiceLog2;
        const float t = juce::jlimit (0.0f, 1.0f, (d - weakNoneDb) / (weakFullDb - weakNoneDb));
        return t * t * (3.0f - 2.0f * t);
    }

    void Detector::updateVoice (float& lg, float& n, float env) const noexcept
    {
        // the voice is present: above the floor and no more than voiceGateDb under its own level (pauses, tails and
        // breaths do not drag the level down); until voiceLevelMs of voice were heard it is a plain mean
        constexpr float floorGain = 1.0e-5f, gateLog2 = voiceGateDb / 6.0205999f;
        if (env < floorGain)
            return;
        const float l = fast::log2 (env);
        if (n > 0.0f && l < lg - gateLog2)
            return;
        n = std::min (n + 1.0f, voiceMaxN);
        lg += std::max (1.0f / n, voiceMinCoeff) * (l - lg);
    }

    void Detector::setFrequency (float hz) noexcept
    {
        freqHz = juce::jlimit (1000.0, fs * 0.45, (double) hz);
        designBands();
    }

    void Detector::setAllround (bool on) noexcept
    {
        if (on == allround)
            return;
        allround = on;
        designBands();
    }

    void Detector::designBands() noexcept
    {
        const double top = fs * 0.45;
        const double f = std::min (freqHz, top);
        const double sBand = std::min (top, juce::jlimit ((double) smartBandMinHz, (double) smartBandMaxHz, smartBandRatio * freqHz));
        const auto bodyLo = allround ? design::highPass (fs, std::min (top, allroundLowRatio * freqHz), 0.7071) : design::highPass (fs, 150.0, 0.7071);
        const auto bodyHi = allround ? design::lowPass (fs, std::min (top, allroundHighRatio * freqHz), 0.7071) : design::lowPass (fs, std::min (3000.0, top), 0.7071);
        for (int ch = 0; ch < 2; ++ch)
        {
            bandIdx1.setLane (ch, design::highPass (fs, f, q1));
            bandIdx2.setLane (ch, design::highPass (fs, f, q2));
            bandIdx1.setLane (2 + ch, design::highPass (fs, sBand, q1));
            bandIdx2.setLane (2 + ch, design::highPass (fs, sBand, q2));
            body1.setLane (ch, bodyLo);
            body2.setLane (ch, bodyHi);
        }
        tiltDb = detectorTiltDbPerOct * (float) std::log2 (f / detectorTiltRefHz);
    }

    float Detector::levelFor (float peak, float env, float nowE, bool relative) const noexcept
    {
        if (! relative)
            return fast::gainToDb (std::max (peak, 1.0e-9f)) + tiltDb;
        // Near-silence is never de-essed (a quiet onset would otherwise read as a loud S)
        constexpr float floorGain = 0.001f;   // fullFloorDb
        if (env < floorGain)
            return -200.0f;
        // against the voice now, or the vowel of the last 250 ms at most refVowelDb under it (env >= nowE always)
        constexpr float vowelGain = 0.31622777f;   // -refVowelDb
        return fast::gainToDb (std::max (peak, 1.0e-9f) / std::max (nowE, env * vowelGain)) + tiltDb + referenceDb;
    }

    float Detector::indexFor (float sE, float bE) const noexcept
    {
        if (sE < tiny && bE < tiny)
            return -60.0f;
        return juce::jlimit (-60.0f, 60.0f, 3.0103f * fast::log2 ((sE + tiny) / (bE + tiny)));
    }

    void Detector::process (const float* frame, int numChannels, bool relative, bool perChannel, Frame& out) noexcept
    {
        const float x0 = frame[0], x1 = numChannels > 1 ? frame[1] : 0.0f;
        alignas (16) float a[4], b[4];
       #if EMBER_SSE
        _mm_store_ps (a, bandIdx2.process (bandIdx1.process (_mm_setr_ps (x0, x1, x0, x1))));
        _mm_store_ps (b, body2.process (body1.process (_mm_setr_ps (x0, x1, 0.0f, 0.0f))));
       #else
        {
            const float in1[4] { x0, x1, x0, x1 }, in2[4] { x0, x1, 0.0f, 0.0f };
            float t[4];
            bandIdx1.process (in1, t);
            bandIdx2.process (t, a);
            body1.process (in2, t);
            body2.process (t, b);
        }
       #endif
        // The index comes from 5 ms envelopes: working it out every 4th sample changes nothing (the gate
        // smoothing after it is 2-6 ms long) and saves a log and a division per sample
        const bool indexDue = (indexPhase++ & 3) == 0;
        float peak = 0.0f, full = 0.0f, hiSq = 0.0f, bodySq = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float ax = std::abs (frame[ch]), ab = std::abs (a[ch]), h = a[2 + ch], bd = b[ch];
            peak = std::max (peak, ab);
            full = std::max (full, ax);
            hiSq += h * h;
            bodySq += bd * bd;
            out.band[ch] = a[ch];
            if (perChannel)
            {
                fullEnvCh[ch] = ax > fullEnvCh[ch] ? ax : ax + fullRelease * (fullEnvCh[ch] - ax);
                nowEnvCh[ch] = ax > nowEnvCh[ch] ? ax : ax + nowRelease * (nowEnvCh[ch] - ax);
                sEnvCh[ch] = h * h + envCoeff * (sEnvCh[ch] - h * h);
                bodyEnvCh[ch] = bd * bd + envCoeff * (bodyEnvCh[ch] - bd * bd);
                out.levelDb[ch] = levelFor (ab, fullEnvCh[ch], nowEnvCh[ch], relative);
                if (indexDue)
                {
                    weightCh[ch] = weightForIndex (indexFor (sEnvCh[ch], bodyEnvCh[ch]), allround);
                    if (! allround)
                    {
                        updateVoice (voiceLogCh[ch], voiceNCh[ch], fullEnvCh[ch]);
                        weakCh[ch] = voiceNCh[ch] > 0.0f ? weakWeight (sEnvCh[ch], voiceLogCh[ch]) : 1.0f;
                    }
                }
                out.weight[ch] = allround ? weightCh[ch] : weightCh[ch] * weakCh[ch];
            }
        }

        // Sibilance index for smart detection: 5 ms RMS of both bands (energy ratio in dB)
        sEnv = hiSq + envCoeff * (sEnv - hiSq);
        bodyEnv = bodySq + envCoeff * (bodyEnv - bodySq);
        if (indexDue)
        {
            indexDb = indexFor (sEnv, bodyEnv);
            weight = weightForIndex (indexDb, allround);
        }

        // Instant attack, 250 ms release: the band can never read louder relative to a voice whose
        // onset the envelope has not caught yet
        fullEnv = full > fullEnv ? full : full + fullRelease * (fullEnv - full);
        nowEnv = full > nowEnv ? full : full + nowRelease * (nowEnv - full);
        out.linkedDb = levelFor (peak, fullEnv, nowEnv, relative);
        // Weak gate (single vocal): quiet fricatives far under the voice level of the last seconds keep their S
        if (indexDue && ! allround)
        {
            updateVoice (voiceLog, voiceN, fullEnv);
            weak = voiceN > 0.0f ? weakWeight (sEnv, voiceLog) : 1.0f;
        }
        out.linkedWeight = allround ? weight : weight * weak;
        if (! perChannel)
            for (int ch = 0; ch < numChannels; ++ch)
            {
                out.levelDb[ch] = out.linkedDb;
                out.weight[ch] = out.linkedWeight;
            }
    }

    float Detector::process (const float* frame, int numChannels, bool relative, float* band) noexcept
    {
        Frame f;
        process (frame, numChannels, relative, false, f);
        if (band != nullptr)
            for (int ch = 0; ch < numChannels; ++ch)
                band[ch] = f.band[ch];
        return f.linkedDb;
    }

    bool Detector::isFinite() const noexcept
    {
        bool ok = bandIdx1.isFinite() && bandIdx2.isFinite() && body1.isFinite() && body2.isFinite()
               && std::isfinite (fullEnv) && std::isfinite (sEnv) && std::isfinite (bodyEnv) && std::isfinite (nowEnv)
               && std::isfinite (voiceLog);
        for (int c = 0; c < maxChannels; ++c)
            ok = ok && std::isfinite (fullEnvCh[c]) && std::isfinite (sEnvCh[c]) && std::isfinite (bodyEnvCh[c])
                    && std::isfinite (nowEnvCh[c]) && std::isfinite (voiceLogCh[c]);
        return ok;
    }

    // ------------------------------------------------------------------ Engine
    void Engine::prepare (double sampleRate, int maxBlockSize)
    {
        juce::ignoreUnused (maxBlockSize);
        fs = sampleRate;
        latency = latencyFor (sampleRate);
        delaySize = latency + 1;
        for (auto* lines : { &audioDelay, &bandDelay })
            for (auto& d : *lines)
                d.assign ((size_t) delaySize, 0.0f);
        for (auto* lines : { &envDelay, &gateDelay0, &gateDelay1 })
            for (auto& d : *lines)
                d.assign ((size_t) delaySize, 0.0f);
        for (auto& d : trackDelay)
            d.assign ((size_t) delaySize, 0.0f);
        trackCoeff = timeConstantCoeff (trackEnvMs, fs);

        gateWin[0] = juce::jlimit (1, latency + 1, (int) std::lround (gateSplitMs * 0.001 * fs));
        gateWin[1] = juce::jlimit (1, latency + 1, (int) std::lround (gateWideMs * 0.001 * fs));
        for (int m = 0; m < 2; ++m)
            for (auto& g : gate[m])
            {
                g.prepare (latency + 1);
                g.setWindow (gateWin[m]);
            }

        detector.prepare (sampleRate);
        freq.reset (sampleRate, 0.05);
        width.reset (sampleRate, 0.05);
        mixSm.reset (sampleRate, 0.02);
        outSm.reset (sampleRate, 0.02);
        bypassSm.reset (sampleRate, 0.02);
        modeSm.reset (sampleRate, switchFadeMs * 0.001);
        wetSm.reset (sampleRate, switchFadeMs * 0.001 * 0.5);
        for (auto& l : listenSm)
            l.reset (sampleRate, switchFadeMs * 0.001);
        laFadeLen = std::max (1, (int) std::lround (switchFadeMs * 0.001 * fs));
        setSettings (settings);
        freq.setCurrentAndTargetValue (settings.freq);
        width.setCurrentAndTargetValue (settings.widthOct);
        mixSm.setCurrentAndTargetValue (settings.mix);
        outSm.setCurrentAndTargetValue (dbToGain (settings.outputDb));
        bypassSm.setCurrentAndTargetValue (settings.bypass ? 1.0f : 0.0f);
        modeSm.setCurrentAndTargetValue (settings.mode == 1 ? 1.0f : 0.0f);
        for (int l = 0; l < 3; ++l)
            listenSm[l].setCurrentAndTargetValue (settings.listen == l ? 1.0f : 0.0f);
        activeStereoMode = settings.stereoMode;
        wetSm.setCurrentAndTargetValue (1.0f);
        reset();
    }

    int Engine::lookaheadSamples (float ms) const noexcept
    {
        return juce::jlimit (0, latency, (int) std::lround (ms * 0.001 * fs));
    }

    void Engine::reset()
    {
        detector.reset();
        detector.setAllround (settings.smart && settings.allround);
        detector.setFrequency (freq.getCurrentValue());
        updateBand();
        resetTrack();
        listenHp.reset();
        listenHp2.reset();
        listenHpLive = false;
        for (auto* lines : { &audioDelay, &bandDelay })
            for (auto& d : *lines)
                std::fill (d.begin(), d.end(), 0.0f);
        for (auto* lines : { &envDelay, &gateDelay0, &gateDelay1 })
            for (auto& d : *lines)
                std::fill (d.begin(), d.end(), 0.0f);
        for (auto& m : gate)
            for (auto& g : m)
                g.reset();
        writePos = 0;
        gridPhase = 0;
        for (auto& e : envDb)
            e = 0.0f;
        laCur = laPrev = lookaheadSamples (settings.lookaheadMs);
        laFadeLeft = 0;
        wetHold = 0;
        currentReductionDb.store (0.0f, std::memory_order_relaxed);
    }

    void Engine::resetTrack() noexcept
    {
        for (int c = 0; c < maxChannels; ++c)
        {
            bells[c].reset();
            trackBp1[c].reset();
            trackBp2[c].reset();
            trackHp[c].reset();
        }
        for (auto& e : trackE)
            e = 0.0f;
        // the middle bell until the tracker has heard something
        for (int k = 0; k < 3; ++k)
            std::fill (trackDelay[(size_t) k].begin(), trackDelay[(size_t) k].end(), k == 1 ? 1.0f : 0.0f);
    }

    void Engine::setSettings (const Settings& s) noexcept
    {
        settings = s;
        // A non-finite value (hand-edited preset, corrupt project) falls back to the default instead of
        // turning the filters into NaN and muting the plugin for good
        const Settings d;
        const auto finite = [] (float v, float fallback) { return std::isfinite (v) ? v : fallback; };
        settings.thresholdDb = finite (s.thresholdDb, d.thresholdDb);
        settings.rangeDb = finite (s.rangeDb, d.rangeDb);
        settings.attackMs = finite (s.attackMs, d.attackMs);
        settings.releaseMs = finite (s.releaseMs, d.releaseMs);
        settings.mix = finite (s.mix, d.mix);
        settings.outputDb = juce::jlimit (-48.0f, 24.0f, finite (s.outputDb, d.outputDb));
        settings.freq = juce::jlimit (1000.0f, 20000.0f, finite (s.freq, d.freq));
        settings.amount = juce::jlimit (0.0f, 1.0f, finite (s.amount, d.amount));
        settings.lookaheadMs = juce::jlimit (0.0f, maxLookaheadMs, finite (s.lookaheadMs, d.lookaheadMs));
        settings.widthOct = juce::jlimit (minWidthOct, maxWidthOct, finite (s.widthOct, d.widthOct));
        settings.stereoLink = juce::jlimit (0.0f, 1.0f, finite (s.stereoLink, d.stereoLink));
        settings.stereoMode = juce::jlimit (0, 3, s.stereoMode);
        settings.mode = s.mode == 1 ? 1 : 0;
        settings.listen = juce::jlimit (0, 2, s.listen);
        freq.setTargetValue (settings.freq);
        width.setTargetValue (settings.widthOct);
        mixSm.setTargetValue (juce::jlimit (0.0f, 1.0f, settings.mix));
        outSm.setTargetValue (dbToGain (settings.outputDb));
        bypassSm.setTargetValue (settings.bypass ? 1.0f : 0.0f);
        modeSm.setTargetValue (settings.mode == 1 ? 1.0f : 0.0f);
        for (int l = 0; l < 3; ++l)
            listenSm[l].setTargetValue (settings.listen == l ? 1.0f : 0.0f);
        attackCoeff = timeConstantCoeff (std::max (0.01f, settings.attackMs), fs);
        releaseCoeff = timeConstantCoeff (std::max (1.0f, settings.releaseMs), fs);
        detector.setAllround (settings.smart && settings.allround);
        if (settings.trackS != trackOn)
        {
            trackOn = settings.trackS;
            updateBand();
        }
        if (settings.stereoMode != activeStereoMode)
            wetSm.setTargetValue (0.0f);   // fade the processing out, switch at the bottom (process)
        else if (wetHold <= 0)
            wetSm.setTargetValue (1.0f);   // switched back before the fade was through

        const int la = lookaheadSamples (settings.lookaheadMs);
        if (la != laCur)
        {
            laPrev = laFadeLeft > 0 ? laPrev : laCur;
            laCur = la;
            laFadeLeft = laFadeLen;
        }
    }

    void Engine::updateBand (bool tracker) noexcept
    {
        const double f = freq.getCurrentValue(), w = width.getCurrentValue();
        const auto shape = splitBand (f, w);
        const auto tb = trackBells (f, w);
        // tracker band-passes: each covers the half octave around its bell (constant peak gain, RBJ)
        const double bpQ = 1.0 / (2.0 * std::sinh (0.5 * std::log (2.0) * trackSpreadOct));
        for (int k = 0; k < 3; ++k)
        {
            const auto bellK = trackOn ? bellAt (fs, tb.centreHz[k], tb.widthOct) : splitBell (fs, f, w);
            const double w0 = juce::MathConstants<double>::twoPi * std::min (tb.centreHz[k], fs * 0.45) / fs;
            const double al = std::sin (w0) / (2.0 * bpQ), a0 = 1.0 + al;
            BiquadCoeffs bp;
            bp.b0 = al / a0; bp.b1 = 0.0; bp.b2 = -al / a0; bp.a1 = -2.0 * std::cos (w0) / a0; bp.a2 = (1.0 - al) / a0;
            for (int c = 0; c < maxChannels; ++c)
            {
                bells[c].setLane (k, bellK);
                if (! tracker)
                    continue;
                trackBp1[c].setLane (k, bp);
                trackBp2[c].setLane (k, bp);
                trackHp[c].setLane (k, design::highPass (fs, std::min (shape.lowHz, fs * 0.45), 0.7071));
            }
        }
        // 1 / (1 - the middle bell's real gain at its centre): the blend then takes exactly the asked-for depth at
        // the centre, also far past the fixed cut (a nominal -12 dB that is really -11.8 dB would leave a 24 dB
        // reduction ~1 dB short)
        {
            const auto mid = trackOn ? bellAt (fs, tb.centreHz[1], tb.widthOct) : splitBell (fs, f, w);
            bandScale = (float) (1.0 / (1.0 - bellCentreGain (mid, fs, trackOn ? tb.centreHz[1] : shape.centreHz)));
            const auto d = 1.0 - responseAt (mid, fs, std::min (trackOn ? tb.centreHz[1] : shape.centreHz, fs * 0.45));
            dRe = (float) d.real();
            dAbs2 = (float) std::norm (d);
            dIm2 = (float) (d.imag() * d.imag());
        }
        listenHp.setCoeffs (design::highPass (fs, std::min (shape.lowHz, fs * 0.45), q1));
        listenHp2.setCoeffs (design::highPass (fs, std::min (shape.lowHz, fs * 0.45), q2));
    }

    void Engine::process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        const int numIn = std::min (numChannels, maxChannels);
        if (numIn <= 0 || numSamples <= 0)
            return;
        const juce::ScopedNoDenormals noDenormals;   // decaying filter states in pauses

        inputTap.push (channels, numIn, numSamples);
        {
            float pk = 0.0f;
            for (int ch = 0; ch < numIn; ++ch)
                for (int i = 0; i < numSamples; ++i)
                    pk = std::max (pk, std::abs (channels[ch][i]));
            inputPeak.push (pk);
        }

        const float scale = amountScale (settings.amount);
        const float link = settings.stereoLink;
        const bool relative = settings.relative || settings.smart;
        float maxReduction = 0.0f, lastReduction = 0.0f, outPk = 0.0f, maxDet = -200.0f, weightAtMax = 0.0f;
        const int lead0Max = latency - (gateWin[0] - 1), lead1Max = latency - (gateWin[1] - 1);

        for (int start = 0, n = 0; start < numSamples; start += n)
        {
            // while freq / width glide the coefficients follow every glideBlock samples (finer steps: the three bells
            // stepping every 16 samples were audible as a faint tick on a fast Width sweep), else on the 16 grid
            const bool gliding = freq.isSmoothing() || width.isSmoothing();
            const int grid = gliding ? glideBlock : subBlock;
            gridPhase %= grid;
            n = std::min (grid - gridPhase, numSamples - start);
            const bool onGrid = gridPhase == 0;
            gridPhase = (gridPhase + n) % grid;
            if (onGrid && gliding)
            {
                freq.skip (grid);
                width.skip (grid);
                detector.setFrequency (freq.getCurrentValue());
                // while freq / width glide the tracker keeps its band-passes (stepping them would jolt its energies
                // and with them the band); they follow once at the end of the glide
                updateBand (! freq.isSmoothing() && ! width.isSmoothing());
            }

            // Stereo mode switch: once the processing is faded out, the filters change domain
            if (activeStereoMode != settings.stereoMode && ! wetSm.isSmoothing() && wetSm.getCurrentValue() <= 0.0f)
            {
                activeStereoMode = settings.stereoMode;
                detector.reset();
                resetTrack();
                for (auto& m : gate)
                    for (auto& g : m)
                        g.reset();
                for (auto& e : envDb)
                    e = 0.0f;
                for (auto& d : bandDelay)
                    std::fill (d.begin(), d.end(), 0.0f);
                for (auto* lines : { &envDelay, &gateDelay0, &gateDelay1 })
                    for (auto& d : *lines)
                        std::fill (d.begin(), d.end(), 0.0f);
                // fade back in once the new control has run through the delay (no reduction popping in late)
                wetHold = latency + gateWin[1];
            }
            if (wetHold > 0 && (wetHold -= n) <= 0)
                wetSm.setTargetValue (1.0f);
            const int sm = numIn == 2 ? activeStereoMode : 0;
            const bool perChannel = numIn == 2 && (sm == 0 || sm == 1) && link < 0.9995f;
            if (perChannel != wasPerChannel)
            {
                // linked, slot 0 holds the shared control; per channel, slots 0 / 1 are the channels' own and slot 2
                // the linked one. Every slot starts from the control that ran until now (no jump)
                const auto copySlot = [this] (int from, int to)
                {
                    envDb[to] = envDb[from];
                    envDelay[(size_t) to] = envDelay[(size_t) from];
                    for (int g = 0; g < 2; ++g)
                        gate[g][to] = gate[g][from];
                    gateDelay0[(size_t) to] = gateDelay0[(size_t) from];
                    gateDelay1[(size_t) to] = gateDelay1[(size_t) from];
                };
                if (perChannel)
                {
                    copySlot (0, 2);
                    copySlot (0, 1);
                }
                else
                    copySlot (2, 0);
            }
            wasPerChannel = perChannel;
            const bool smoothing = modeSm.isSmoothing() || wetSm.isSmoothing() || mixSm.isSmoothing() || outSm.isSmoothing()
                                || bypassSm.isSmoothing() || listenSm[0].isSmoothing() || listenSm[1].isSmoothing() || listenSm[2].isSmoothing();

            // Raw pointers (MSVC reloads a vector's data pointer after every float store otherwise)
            float* aDl[maxChannels] { audioDelay[0].data(), audioDelay[1].data() };
            float* eDl[numSlots] { envDelay[0].data(), envDelay[1].data(), envDelay[2].data() };
            float* bDl[maxChannels] { bandDelay[0].data(), bandDelay[1].data() };
            float* g0Dl[numSlots] { gateDelay0[0].data(), gateDelay0[1].data(), gateDelay0[2].data() };
            float* g1Dl[numSlots] { gateDelay1[0].data(), gateDelay1[1].data(), gateDelay1[2].data() };
            float* tDl[3] { trackDelay[0].data(), trackDelay[1].data(), trackDelay[2].data() };
            const bool track = trackOn;
            float* io[maxChannels] { channels[0], numIn > 1 ? channels[1] : channels[0] };
            const float range = settings.rangeDb, thr = settings.thresholdDb;
            const bool smart = settings.smart;
            const int linkSlot = perChannel ? 2 : 0;
            // read offsets behind the write position: audio, envelope and gates for the current / previous lookahead
            const auto envLag = [&] (int la) { return latency - la; };
            const auto gateLag = [&] (int la, int mode) { return latency - std::min (la, mode == 0 ? lead0Max : lead1Max) - (gateWin[mode] - 1); };
            const int eLagCur = envLag (laCur), g0LagCur = gateLag (laCur, 0), g1LagCur = gateLag (laCur, 1);
            const int eLagPrev = envLag (laPrev), g0LagPrev = gateLag (laPrev, 0), g1LagPrev = gateLag (laPrev, 1);

            int wp = writePos, laLeft = laFadeLeft, laPrevL = laPrev;
            const int ds = delaySize, lat = latency;
            const float aC = attackCoeff, rC = releaseCoeff, bS = bandScale, dR = dRe, dA2 = dAbs2, dI2 = dIm2;
            float envL[numSlots] { envDb[0], envDb[1], envDb[2] };
            bool hpLive = listenHpLive;
            for (int k = 0; k < n; ++k)
            {
                const int i = start + k;
                float x[maxChannels], p[maxChannels];
                x[0] = io[0][i];
                x[1] = io[1][i];
                aDl[0][wp] = x[0];
                aDl[1][wp] = x[1];
                const int numProc = encode (sm, numIn, x, p);

                // 1) detection on the undelayed input: level, gate, attack / release, into the delay lines
                Detector::Frame det;
                detector.process (p, numProc, relative, perChannel, det);
                if (det.linkedDb > maxDet)
                {
                    maxDet = det.linkedDb;
                    weightAtMax = smart ? det.linkedWeight : 1.0f;
                }
                // Fully linked: one control for all channels (slot 0). Otherwise every channel has its own (slots 0 / 1)
                // and the linked one runs beside them (slot 2); the link blends the reductions they ask for
                for (int c = 0; c < numProc; ++c)
                    bDl[c][wp] = det.band[c];
                // S tracker: energy of the S on the three bell centres -> blend weights (linked), into the delay
                if (track)
                {
                    alignas (16) float te[4];
                   #if EMBER_SSE
                    __m128 acc = _mm_setzero_ps();
                    for (int c = 0; c < numProc; ++c)
                    {
                        const __m128 v = trackHp[c].process (trackBp2[c].process (trackBp1[c].process (_mm_set1_ps (p[c]))));
                        acc = _mm_add_ps (acc, _mm_mul_ps (v, v));
                    }
                    _mm_store_ps (te, acc);
                   #else
                    te[0] = te[1] = te[2] = te[3] = 0.0f;
                    for (int c = 0; c < numProc; ++c)
                    {
                        const float in[4] { p[c], p[c], p[c], p[c] };
                        float t1[4], t2[4];
                        trackBp1[c].process (in, t1);
                        trackBp2[c].process (t1, t2);
                        trackHp[c].process (t2, t1);
                        std::copy (t1, t1 + 4, t2);
                        for (int kk = 0; kk < 3; ++kk)
                            te[kk] += t2[kk] * t2[kk];
                    }
                   #endif
                    float tw[3];
                    for (int kk = 0; kk < 3; ++kk)
                        trackE[kk] = te[kk] + trackCoeff * (trackE[kk] - te[kk]);
                    trackWeights (trackE, tw);
                    for (int kk = 0; kk < 3; ++kk)
                        tDl[kk][wp] = tw[kk];
                }
                const int numOwn = perChannel ? numProc : 0;
                for (int slot = 0; slot < numOwn + 1; ++slot)
                {
                    const int c = slot < numOwn ? slot : linkSlot;
                    const bool own = slot < numOwn;
                    const float level = own ? det.levelDb[c] : det.linkedDb;
                    const float w = ! smart ? 1.0f : (own ? det.weight[c] : det.linkedWeight);
                    const float target = std::min (maxReductionDb, std::min (range, staticReductionDb (level, thr)) * scale);
                    float e = envL[c];
                    e = target + (target > e ? aC : rC) * (e - target);
                    envL[c] = e;
                    eDl[c][wp] = e;
                    g0Dl[c][wp] = gate[0][c].process (w);
                    g1Dl[c][wp] = gate[1][c].process (w);
                }

                // 2) the delayed audio and its reduction (the envelope and the gate lead it by the lookahead)
                float m = modeSm.getCurrentValue(), wet = wetSm.getCurrentValue(), mix = mixSm.getCurrentValue(),
                      outGain = outSm.getCurrentValue(), byp = bypassSm.getCurrentValue(), lOff = listenSm[0].getCurrentValue(),
                      lBand = listenSm[1].getCurrentValue(), lDelta = listenSm[2].getCurrentValue();
                if (smoothing)
                {
                    m = modeSm.getNextValue(); wet = wetSm.getNextValue(); mix = mixSm.getNextValue(); outGain = outSm.getNextValue();
                    byp = bypassSm.getNextValue(); lOff = listenSm[0].getNextValue(); lBand = listenSm[1].getNextValue();
                    lDelta = listenSm[2].getNextValue();
                }
                if (lBand > 0.0f && ! hpLive)
                {
                    listenHp.reset();   // starts fresh while its fade-in hides the start
                    listenHp2.reset();
                }
                hpLive = lBand > 0.0f;

                const int readAudio = wrap (wp - lat, ds);
                float dry[maxChannels] { aDl[0][readAudio], aDl[1][readAudio] }, q[maxChannels], delta[maxChannels] {}, band[maxChannels] {};
                encode (sm, numIn, dry, q);
                float gains[maxChannels] { 1.0f, 1.0f }, rs[numSlots] {};
                // bell weights (they lead the audio like the envelope; cross-faded with a lookahead change)
                float bw[3] { 0.0f, 1.0f, 0.0f };
                if (track)
                {
                    const int rc = wrap (wp - eLagCur, ds);
                    for (int kk = 0; kk < 3; ++kk)
                        bw[kk] = tDl[kk][rc];
                    if (laLeft > 0)
                    {
                        const float laMix = 1.0f - (float) laLeft / (float) laFadeLen;
                        const int rp = wrap (wp - eLagPrev, ds);
                        for (int kk = 0; kk < 3; ++kk)
                            bw[kk] = tDl[kk][rp] + laMix * (bw[kk] - tDl[kk][rp]);
                    }
                }
                for (int slot = 0; slot < numOwn + 1; ++slot)
                {
                    const int c = slot < numOwn ? slot : linkSlot;
                    const float g0 = g0Dl[c][wrap (wp - g0LagCur, ds)], g1 = g1Dl[c][wrap (wp - g1LagCur, ds)];
                    float r = eDl[c][wrap (wp - eLagCur, ds)] * (g0 + m * (g1 - g0));
                    if (laLeft > 0)
                    {
                        const float laMix = 1.0f - (float) laLeft / (float) laFadeLen;
                        const float p0 = g0Dl[c][wrap (wp - g0LagPrev, ds)], p1 = g1Dl[c][wrap (wp - g1LagPrev, ds)];
                        const float rp = eDl[c][wrap (wp - eLagPrev, ds)] * (p0 + m * (p1 - p0));
                        r = rp + laMix * (r - rp);
                    }
                    rs[c] = r;
                }
                for (int c = 0; c < (perChannel ? numProc : 1); ++c)
                {
                    const float r = perChannel ? rs[c] + link * (rs[linkSlot] - rs[c]) : rs[0];
                    maxReduction = std::max (maxReduction, r);
                    lastReduction = c == 0 ? r : std::max (lastReduction, r);
                    gains[c] = fast::reductionToGain (r);
                }
                if (laLeft > 0 && --laLeft == 0)
                    laPrevL = laCur;
                for (int c = 0; c < numProc; ++c)
                {
                    const float g = gains[perChannel ? c : 0];
                    // split: x + c (bell(x) - x), c = (1 - g) / (1 - bell gain); wideband: g x
                    // the three bells, blended: diff = (blended bell)(x) - x
                    alignas (16) float bo[4];
                   #if EMBER_SSE
                    _mm_store_ps (bo, bells[c].process (_mm_set1_ps (q[c])));
                   #else
                    {
                        const float in[4] { q[c], q[c], q[c], q[c] };
                        bells[c].process (in, bo);
                    }
                   #endif
                    const float diff = bw[0] * (bo[0] - q[c]) + bw[1] * (bo[1] - q[c]) + bw[2] * (bo[2] - q[c]);
                    const float dSplit = blendForGain (g, dR, dA2, dI2) * diff;
                    const float dWide = (g - 1.0f) * q[c];
                    delta[c] = dSplit + m * (dWide - dSplit);
                    if (hpLive)
                    {
                        const float bandSplit = listenHp2.process (listenHp.process (-bS * diff, c), c);
                        band[c] = bandSplit + m * (bDl[c][readAudio] - bandSplit);
                    }
                }

                // 3) back to L/R, listen / mix / output / bypass
                float dOut[maxChannels] {}, bOut[maxChannels] {};
                decode (sm, numIn, delta, dOut);
                if (hpLive)
                    decode (sm, numIn, band, bOut);
                for (int ch = 0; ch < numIn; ++ch)
                {
                    const float d = wet * mix * dOut[ch];
                    float y = lOff * (dry[ch] + d);
                    if (lBand > 0.0f)
                        y += lBand * wet * bOut[ch];   // faded like the rest while a stereo mode switch resets the filters
                    if (lDelta > 0.0f)
                        y -= lDelta * d;
                    y *= outGain;
                    y = byp >= 1.0f ? dry[ch] : y + byp * (dry[ch] - y);   // bypassed = the input, bit for bit
                    io[ch][i] = y;
                    outPk = std::max (outPk, std::abs (y));
                }
                wp = wp + 1 >= ds ? 0 : wp + 1;
            }
            writePos = wp;
            laFadeLeft = laLeft;
            laPrev = laPrevL;
            for (int c = 0; c < numSlots; ++c)
                envDb[c] = envL[c];
            listenHpLive = hpLive;
        }

        bool finite = detector.isFinite() && listenHp.isStateFinite() && listenHp2.isStateFinite();
        for (int c = 0; c < maxChannels; ++c)
            finite = finite && bells[c].isFinite() && trackBp1[c].isFinite() && trackBp2[c].isFinite() && trackHp[c].isFinite();
        for (float e : trackE)
            finite = finite && std::isfinite (e);
        for (int c = 0; c < numSlots; ++c)
            finite = finite && std::isfinite (envDb[c]);
        if (! finite)
        {
            reset();
            for (int ch = 0; ch < numIn; ++ch)
                std::fill_n (channels[ch], numSamples, 0.0f);
            outPk = 0.0f;
        }

        // The meters show what really comes off: below 100 % Mix only part of the reduction reaches the output
        const auto afterMix = [mix = juce::jlimit (0.0f, 1.0f, settings.mix)] (float r)
        {
            return r <= 0.0f || mix >= 1.0f ? r : std::max (0.0f, -fast::gainToDb (1.0f - mix * (1.0f - fast::reductionToGain (r))));
        };
        gainReductionDb.push (settings.bypass ? 0.0f : afterMix (maxReduction));
        currentReductionDb.store (settings.bypass ? 0.0f : afterMix (lastReduction), std::memory_order_relaxed);
        detectorDb.store (std::max (-100.0f, maxDet), std::memory_order_relaxed);
        detectorWeight.store (weightAtMax, std::memory_order_relaxed);
        outputPeak.push (outPk);
        outputTap.push (channels, numIn, numSamples);
    }
}
