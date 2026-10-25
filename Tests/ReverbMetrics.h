// Reverb impulse-response metrics for the unit tests: the same definitions as tools/refhost/qmetrics.py
// (schroeder_decay, modal_ringing_db), so a unit test and the bench agree on what "T30" and "ringing" mean.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "DSP/Filters.h"

namespace reverbmetrics
{
    inline int onsetIndex (const std::vector<double>& a, double rel = 0.01)
    {
        double mx = 0.0;
        for (auto v : a)
            mx = std::max (mx, std::abs (v));
        for (size_t i = 0; i < a.size(); ++i)
            if (std::abs (a[i]) > rel * mx)
                return (int) i;
        return 0;
    }

    // Least-squares T60 from the Schroeder curve of `h` (from its onset), fitted between hiDb and loDb
    inline double fitDecay (const std::vector<double>& h, double fs, double hiDb = -5.0, double loDb = -35.0)
    {
        const int on = onsetIndex (h);
        const int n = (int) h.size() - on;
        if (n < 16)
            return 0.0;
        std::vector<double> e ((size_t) n);
        double acc = 0.0;
        for (int i = n - 1; i >= 0; --i)
        {
            acc += h[(size_t) (on + i)] * h[(size_t) (on + i)];
            e[(size_t) i] = acc;
        }
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        int cnt = 0;
        for (int i = 0; i < n; ++i)
        {
            const double d = 10.0 * std::log10 (std::max (e[(size_t) i], 1.0e-30) / std::max (e[0], 1.0e-30));
            if (d <= hiDb && d >= loDb)
            {
                const double t = i / fs;
                sx += t; sy += d; sxx += t * t; sxy += t * d; ++cnt;
            }
        }
        if (cnt < 10)
            return 0.0;
        const double slope = (cnt * sxy - sx * sy) / (cnt * sxx - sx * sx);
        return slope < 0.0 ? -60.0 / slope : 0.0;
    }

    // Broadband T30 like the bench: energy sum of the channels
    inline double broadbandT30 (const juce::AudioBuffer<float>& b, double fs)
    {
        std::vector<double> h ((size_t) b.getNumSamples());
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            double s = 0.0;
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
                s += (double) b.getSample (ch, i) * b.getSample (ch, i);
            h[(size_t) i] = std::sqrt (s);
        }
        return fitDecay (h, fs);
    }

    inline std::vector<double> monoOf (const juce::AudioBuffer<float>& b)
    {
        std::vector<double> m ((size_t) b.getNumSamples());
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            double s = 0.0;
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
                s += b.getSample (ch, i);
            m[(size_t) i] = s / b.getNumChannels();
        }
        return m;
    }

    // T30 of one octave band (two 2nd-order high-passes + two low-passes around fc), from the mono sum
    inline double octaveT30 (const juce::AudioBuffer<float>& b, double fs, double fc)
    {
        auto m = monoOf (b);
        const int on = onsetIndex (m);
        std::vector<double> x (m.begin() + on, m.end());
        for (int k = 0; k < 2; ++k)
        {
            velvox::dsp::Biquad hp, lp;
            hp.setCoeffs (velvox::dsp::design::highPass (fs, fc / 1.414, 0.7071));
            lp.setCoeffs (velvox::dsp::design::lowPass (fs, fc * 1.414, 0.7071));
            for (auto& v : x)
                v = lp.process (hp.process ((float) v, 0), 0);
        }
        return fitDecay (x, fs);
    }

    // Strongest narrow spectral peak of the decay-compensated late tail above its 1/3-octave mean (dB),
    // qmetrics.modal_ringing_db: 1 s from 100 ms after the onset, Welch 8192 Hann / 50 %, 100 Hz..8 kHz
    inline double modalRingingDb (const juce::AudioBuffer<float>& b, double fs, double startMs = 100.0, double lengthS = 1.0,
                                  double fmin = 100.0, double fmax = 8000.0)
    {
        auto h = monoOf (b);
        const int on = onsetIndex (h);
        const int a = on + (int) (startMs * fs / 1000.0);
        const int len = std::min ((int) (lengthS * fs), (int) h.size() - a);
        constexpr int order = 13, nfft = 1 << order;
        if (len < nfft)
            return 0.0;
        std::vector<double> seg (h.begin() + a, h.begin() + a + len);

        // envelope flattening: sqrt of a centred 2400-sample moving average of the power ("same" mode)
        constexpr int w = 2400;
        std::vector<double> c ((size_t) len + 1, 0.0);
        for (int i = 0; i < len; ++i)
            c[(size_t) i + 1] = c[(size_t) i] + seg[(size_t) i] * seg[(size_t) i];
        std::vector<double> flat ((size_t) len);
        for (int i = 0; i < len; ++i)
        {
            const int lo = std::max (0, i - w / 2), hi = std::min (len, i - w / 2 + w);
            flat[(size_t) i] = seg[(size_t) i] / (std::sqrt ((c[(size_t) hi] - c[(size_t) lo]) / w) + 1.0e-12);
        }

        juce::dsp::FFT fft (order);
        std::vector<double> psd (nfft / 2 + 1, 0.0);
        std::vector<float> buf (2 * nfft);
        int frames = 0;
        for (int s = 0; s + nfft <= len; s += nfft / 2)
        {
            double mean = 0.0;
            for (int i = 0; i < nfft; ++i)
                mean += flat[(size_t) (s + i)];
            mean /= nfft;   // scipy welch detrend='constant'
            std::fill (buf.begin(), buf.end(), 0.0f);
            for (int i = 0; i < nfft; ++i)
            {
                const double win = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / nfft);
                buf[(size_t) i] = (float) ((flat[(size_t) (s + i)] - mean) * win);
            }
            fft.performFrequencyOnlyForwardTransform (buf.data(), true);
            for (int k = 0; k <= nfft / 2; ++k)
                psd[(size_t) k] += (double) buf[(size_t) k] * buf[(size_t) k];
            ++frames;
        }
        if (frames == 0)
            return 0.0;

        std::vector<double> f, d;
        for (int k = 0; k <= nfft / 2; ++k)
        {
            const double fk = k * fs / nfft;
            if (fk > 10.0)
            {
                f.push_back (fk);
                d.push_back (10.0 * std::log10 (std::max (psd[(size_t) k] / frames, 1.0e-30)));
            }
        }
        std::vector<double> cs (d.size() + 1, 0.0);
        for (size_t i = 0; i < d.size(); ++i)
            cs[i + 1] = cs[i] + d[i];
        double best = -1.0e9;
        const double r = std::pow (2.0, 1.0 / 6.0);
        for (size_t i = 0; i < f.size(); ++i)
        {
            if (f[i] < fmin || f[i] > fmax)
                continue;
            const auto lo = (size_t) (std::lower_bound (f.begin(), f.end(), f[i] / r) - f.begin());
            auto hi = (size_t) (std::upper_bound (f.begin(), f.end(), f[i] * r) - f.begin());
            hi = std::max (hi, lo + 1);
            const double med = (cs[hi] - cs[lo]) / (double) (hi - lo);
            best = std::max (best, d[i] - med);
        }
        return best;
    }
}
