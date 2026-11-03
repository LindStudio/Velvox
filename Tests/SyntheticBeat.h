#pragma once

// Deterministic synthetic "beats" for the sidechain tests of the automatic mix. Not
// meant to sound like a track - meant to have the measurable properties of one: a kick
// on every quarter at 128 BPM, hats on the 8ths, a pad of band noise, and a choice of
// how dense it is and where its energy sits.

#include "AutoMix/CaptureBuffer.h"
#include "AutoMix/VocalProfile.h"
#include "DSP/Filters.h"
#include <juce_core/juce_core.h>

namespace vocalypse::test
{
    enum class BeatKind
    {
        dense,           // pad and hats all the time
        sparse,          // every other bar only a kick on 1 and 3 (no pad, no hats)
        presenceHeavy,   // dense, 2-5 kHz lifted by 9 dB
        lowMidHeavy,     // dense, 250-500 Hz lifted by 9 dB
        dark             // kick and a pad below 1.5 kHz only: leaves the presence range free
    };

    // Stereo beat (identical channels) of `seconds`, normalised to `lufs` (BS.1770 gated).
    // `startSeconds` > 0: silence before the beat starts.
    inline automix::CapturedAudio makeBeat (BeatKind kind, double fs, double seconds, float lufs,
                                            double startSeconds = 0.0, unsigned seed = 7)
    {
        const int n = (int) (seconds * fs);
        const int first = (int) (startSeconds * fs);
        juce::Random rnd ((juce::int64) seed);
        std::vector<float> b ((size_t) n, 0.0f);

        const double beatLen = 60.0 / 128.0;              // one quarter
        const int quarter = (int) (beatLen * fs), eighth = quarter / 2, bar = 4 * quarter;
        const bool sparse = kind == BeatKind::sparse;

        // Pad: band noise 300 Hz - 6 kHz
        dsp::Biquad padHp, padLp, hatHp, hatHp2, tone;
        padHp.setCoeffs (dsp::design::highPass (fs, 300.0, 0.7071));
        padLp.setCoeffs (dsp::design::lowPass (fs, kind == BeatKind::dark ? 1500.0 : 6000.0, 0.7071));
        hatHp.setCoeffs (dsp::design::highPass (fs, 7000.0, 0.7071));
        hatHp2.setCoeffs (dsp::design::highPass (fs, 7000.0, 0.7071));
        if (kind == BeatKind::presenceHeavy)
            tone.setCoeffs (dsp::design::peak (fs, 3200.0, 0.9, 9.0));
        else if (kind == BeatKind::lowMidHeavy)
            tone.setCoeffs (dsp::design::peak (fs, 360.0, 1.4, 9.0));
        else
            tone.setCoeffs (dsp::design::peak (fs, 1000.0, 1.0, 0.0));

        double kickPhase = 0.0;
        for (int i = first; i < n; ++i)
        {
            const int t = i - first;
            const int barIndex = t / bar;
            float y = 0.0f;

            // Kick: a falling sine, 120 ms (sparse: only on 1 and 3 in the bars without pad)
            const int inQuarter = t % quarter;
            const bool kickOn = ! sparse || barIndex % 2 == 0 || (t % bar) / quarter % 2 == 0;
            if (inQuarter == 0)
                kickPhase = 0.0;
            if (kickOn && inQuarter < (int) (0.12 * fs))
            {
                const double tt = inQuarter / fs;
                kickPhase += (45.0 + 110.0 * std::exp (-tt * 30.0)) / fs;
                y += 0.9f * (float) std::exp (-tt * 22.0) * (float) std::sin (2.0 * juce::MathConstants<double>::pi * kickPhase);
            }

            // Hats: 30 ms noise bursts on the 8ths
            const float noise1 = rnd.nextFloat() * 2.0f - 1.0f, noise2 = rnd.nextFloat() * 2.0f - 1.0f;
            const bool hatsOn = kind != BeatKind::dark && (! sparse || barIndex % 2 == 0);
            const int inEighth = t % eighth;
            const float hatEnv = hatsOn && inEighth < (int) (0.03 * fs) ? (float) std::exp (-(inEighth / fs) * 120.0) : 0.0f;
            y += 0.35f * hatEnv * hatHp2.process (hatHp.process (noise1, 0), 0);

            // Pad
            const bool padOn = ! sparse || barIndex % 2 == 0;
            y += (padOn ? 0.25f : 0.0f) * padLp.process (padHp.process (noise2, 0), 0);

            b[(size_t) i] = tone.process (y, 0);
        }

        // Normalise to the requested loudness
        std::vector<const float*> ch { b.data(), b.data() };
        const float measured = automix::VocalProfiler::integratedLufs (ch, n, fs);
        if (measured > -90.0f)
        {
            const float g = juce::Decibels::decibelsToGain (lufs - measured);
            for (auto& x : b)
                x *= g;
        }

        automix::CapturedAudio a;
        a.sampleRate = fs;
        a.channels = 2;
        a.vocal = { b, b };
        return a;
    }
}
