#pragma once

// Deterministic synthetic "vocals" for reproducible tests of the analysis and
// the automatic mix. Not meant to sound like a singer - meant to have the
// measurable properties of one: a pitched, formant-shaped voice in phrases
// and syllables, S sounds, pauses, and optional faults (rumble, resonance,
// noise, room, clipping, heavy compression, dull/bright tone).

#include "AutoMix/CaptureBuffer.h"
#include "DSP/Filters.h"
#include <juce_core/juce_core.h>

namespace velvox::test
{
    struct VocalSpec
    {
        double sampleRate = 48000.0;
        double seconds = 16.0;
        int channels = 1;
        float f0 = 220.0f;            // centre pitch (Hz)
        float levelDb = -18.0f;       // RMS of the voice (dBFS)
        float tiltDb = 0.0f;          // > 0 brighter, < 0 darker (dB per octave above 1 kHz)
        float sibilance = 0.3f;       // 0..1 level of S bursts
        float rumbleDb = -100.0f;     // level of 25-70 Hz rumble (dBFS RMS)
        float resonanceHz = 0.0f;     // narrow resonance (0 = none)
        float resonanceDb = 12.0f;
        float noiseDb = -100.0f;      // broadband noise floor (dBFS RMS)
        float roomSeconds = 0.0f;     // RT60 of an added room (0 = dry)
        float roomMix = 0.4f;
        float phraseRangeDb = 8.0f;   // loudness difference between phrases
        float compress = 0.0f;        // 0..1 heavy static compression
        float clipGain = 0.0f;        // > 0: drives into hard clipping by this many dB
        float stereoDifference = 0.0f;// 0 = identical channels
        unsigned seed = 1;
    };

    inline automix::CapturedAudio makeVocal (const VocalSpec& s)
    {
        const double fs = s.sampleRate;
        const int n = (int) (s.seconds * fs);
        juce::Random rnd ((juce::int64) s.seed);
        std::vector<float> v ((size_t) n, 0.0f);

        // --- phrase / syllable plan
        struct Syl { int start, len; float f0, gain; int vowel; bool sib; };
        std::vector<Syl> syls;
        {
            double t = 0.15;
            int phrase = 0;
            while (t < s.seconds - 0.5)
            {
                const double phraseLen = 1.6 + 0.8 * rnd.nextDouble();
                const float phraseGain = juce::Decibels::decibelsToGain ((phrase % 3 == 0 ? 0.0f : (phrase % 3 == 1 ? -0.5f : -1.0f)) * s.phraseRangeDb);
                double p = t;
                while (p < std::min (t + phraseLen, s.seconds - 0.3))
                {
                    const double len = 0.18 + 0.14 * rnd.nextDouble();
                    const int semis = (int) rnd.nextInt (11) - 5;
                    syls.push_back ({ (int) (p * fs), (int) (len * fs), s.f0 * std::pow (2.0f, (float) semis / 12.0f),
                                      phraseGain * (0.8f + 0.4f * rnd.nextFloat()), rnd.nextInt (4), rnd.nextFloat() < 0.35f });
                    p += len + 0.02;
                }
                t = p + 0.3 + 0.35 * rnd.nextDouble();
                ++phrase;
            }
        }

        // Formants (F1, F2, F3) for a, e, i, o
        static const float formants[4][3] { { 800, 1200, 2600 }, { 500, 1900, 2600 }, { 300, 2300, 3000 }, { 500, 900, 2500 } };

        for (const auto& syl : syls)
        {
            dsp::Biquad f1, f2, f3, tilt;
            const auto& fm = formants[syl.vowel];
            f1.setCoeffs (dsp::design::peak (fs, fm[0], 5.0, 14.0));
            f2.setCoeffs (dsp::design::peak (fs, fm[1], 6.0, 12.0));
            f3.setCoeffs (dsp::design::peak (fs, fm[2], 7.0, 10.0));
            tilt.setCoeffs (dsp::design::highShelf (fs, 1000.0, 0.5, s.tiltDb * 4.0f));
            const int harmonics = std::max (1, (int) (std::min (9000.0, fs * 0.45) / syl.f0));
            double phase = 0.0;
            for (int i = 0; i < syl.len && syl.start + i < n; ++i)
            {
                const double tt = (double) i / fs;
                const float env = std::sin (juce::MathConstants<float>::pi * (float) i / (float) syl.len);
                const double f = syl.f0 * std::pow (2.0, 0.017 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.5 * tt) / 1.0);
                phase += f / fs;
                float saw = 0.0f;
                for (int k = 1; k <= harmonics; ++k)
                    saw += (float) std::sin (2.0 * juce::MathConstants<double>::pi * phase * k) / (float) k;
                float y = f1.process (saw, 0);
                y = f2.process (y, 0);
                y = f3.process (y, 0);
                y = tilt.process (y, 0);
                v[(size_t) (syl.start + i)] += 0.12f * y * env * syl.gain;
            }

            // S burst at the start of some syllables
            if (syl.sib && s.sibilance > 0.0f)
            {
                dsp::Biquad h1, h2;
                h1.setCoeffs (dsp::design::highPass (fs, 5000.0, 0.7071));
                h2.setCoeffs (dsp::design::highPass (fs, 5000.0, 0.7071));
                const int len = (int) (0.09 * fs);
                const int start = std::max (0, syl.start - len / 2);
                for (int i = 0; i < len && start + i < n; ++i)
                {
                    const float env = std::sin (juce::MathConstants<float>::pi * (float) i / (float) len);
                    const float x = rnd.nextFloat() * 2.0f - 1.0f;
                    const float tone = juce::Decibels::decibelsToGain (std::min (0.0f, s.tiltDb) * 3.0f);
                    v[(size_t) (start + i)] += 0.5f * s.sibilance * tone * env * syl.gain * h2.process (h1.process (x, 0), 0);
                }
            }
        }

        // Narrow resonance (e.g. a boxy room or a cheap microphone)
        if (s.resonanceHz > 0.0f)
        {
            dsp::Biquad r;
            r.setCoeffs (dsp::design::peak (fs, s.resonanceHz, 12.0, s.resonanceDb));
            for (auto& x : v)
                x = r.process (x, 0);
        }

        // Normalise the voice to the requested RMS (voiced parts)
        {
            double sum = 0.0;
            int count = 0;
            for (auto x : v)
                if (std::abs (x) > 1.0e-4f) { sum += (double) x * x; ++count; }
            const float rms = (float) std::sqrt (sum / std::max (1, count));
            const float g = juce::Decibels::decibelsToGain (s.levelDb) / std::max (1.0e-9f, rms);
            for (auto& x : v)
                x *= g;
        }

        if (s.compress > 0.0f)
        {
            const float drive = 1.0f + 12.0f * s.compress;
            float peak = 0.0f;
            for (auto& x : v) { x = std::tanh (x * drive * 4.0f) / 4.0f; peak = std::max (peak, std::abs (x)); }
            double sum = 0.0;
            int count = 0;
            for (auto x : v) if (std::abs (x) > 1.0e-4f) { sum += (double) x * x; ++count; }
            const float g = juce::Decibels::decibelsToGain (s.levelDb) / std::max (1.0e-9f, (float) std::sqrt (sum / std::max (1, count)));
            for (auto& x : v) x *= g;
        }

        // Room: Schroeder reverb (parallel feedback combs + allpass) with the given RT60
        if (s.roomSeconds > 0.0f)
        {
            const int combDelays[4] { 1557, 1617, 1491, 1422 };
            std::vector<float> wet ((size_t) n, 0.0f);
            for (int c = 0; c < 4; ++c)
            {
                const int d = (int) (combDelays[c] * fs / 44100.0);
                const float fb = (float) std::pow (10.0, -3.0 * d / (s.roomSeconds * fs));
                std::vector<float> line ((size_t) d, 0.0f);
                int pos = 0;
                for (int i = 0; i < n; ++i)
                {
                    const float out = line[(size_t) pos];
                    line[(size_t) pos] = v[(size_t) i] + fb * out;
                    wet[(size_t) i] += 0.25f * out;
                    if (++pos == d) pos = 0;
                }
            }
            {
                const int d = (int) (225 * fs / 44100.0);
                std::vector<float> line ((size_t) d, 0.0f);
                int pos = 0;
                for (int i = 0; i < n; ++i)
                {
                    const float buf = line[(size_t) pos];
                    const float in = wet[(size_t) i];
                    const float out = -0.5f * in + buf;
                    line[(size_t) pos] = in + 0.5f * buf;
                    wet[(size_t) i] = out;
                    if (++pos == d) pos = 0;
                }
            }
            for (int i = 0; i < n; ++i)
                v[(size_t) i] += s.roomMix * 2.0f * wet[(size_t) i];
        }

        if (s.rumbleDb > -99.0f)
        {
            dsp::Biquad lp, lp2;
            lp.setCoeffs (dsp::design::lowPass (fs, 70.0, 0.7071));
            lp2.setCoeffs (dsp::design::lowPass (fs, 70.0, 0.7071));
            std::vector<float> r ((size_t) n);
            double e = 0.0;
            for (auto& x : r) { x = lp2.process (lp.process (rnd.nextFloat() * 2.0f - 1.0f, 0), 0); e += (double) x * x; }
            const float g = juce::Decibels::decibelsToGain (s.rumbleDb) / (float) std::sqrt (std::max (1.0e-12, e / n));
            for (int i = 0; i < n; ++i)
                v[(size_t) i] += g * r[(size_t) i];
        }

        if (s.noiseDb > -99.0f)
        {
            const float g = juce::Decibels::decibelsToGain (s.noiseDb) * 1.732f;
            for (auto& x : v)
                x += g * (rnd.nextFloat() * 2.0f - 1.0f);
        }

        if (s.clipGain > 0.0f)
        {
            const float g = juce::Decibels::decibelsToGain (s.clipGain);
            float peak = 0.0f;
            for (auto x : v) peak = std::max (peak, std::abs (x));
            for (auto& x : v)
                x = juce::jlimit (-1.0f, 1.0f, x / std::max (1.0e-6f, peak) * g);
        }

        automix::CapturedAudio a;
        a.sampleRate = fs;
        a.channels = s.channels;
        a.vocal.push_back (v);
        if (s.channels > 1)
        {
            auto r = v;
            if (s.stereoDifference > 0.0f)
            {
                // A short delayed, attenuated copy on the right: wider, partly decorrelated
                const int d = (int) (0.012 * fs);
                for (int i = n - 1; i >= d; --i)
                    r[(size_t) i] = (1.0f - s.stereoDifference) * v[(size_t) i] - s.stereoDifference * v[(size_t) (i - d)];
            }
            a.vocal.push_back (r);
        }
        return a;
    }
}
