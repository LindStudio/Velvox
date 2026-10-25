#pragma once

#include "Filters.h"
#include <juce_core/juce_core.h>
#include <array>
#include <vector>

namespace velvox::dsp
{
    // Low-latency monophonic pitch shifter: two read heads move through a delay line at the
    // pitch ratio (resampling - the output frequency is exactly ratio x input), and when the
    // active head drifts out of its band it hands over to a head a whole number of pitch
    // periods away (the jump is refined by normalised cross-correlation, so the two heads play
    // the same waveform and the crossfade is inaudible). No phase vocoder: no phasiness.
    // Unvoiced / aperiodic input uses 8 ms splices at the best-matching offset with a
    // correlation-normalised fade. Formants move with the pitch (FormantCorrector puts them back).
    //
    // Latency is constant: the heads' delay oscillates around L = ceil(fs / 70) + 16 samples.
    class SpliceShifter
    {
    public:
        void prepare (double sampleRate, int maxBlockSize);
        void reset() noexcept;

        int getLatency() const noexcept { return latency; }
        void setRatio (float ratio) noexcept;

        // In place. `period`: input period per sample (tracker, 0 = unvoiced).
        void process (float* const* io, int numChannels, int numSamples, const float* period) noexcept;

    private:
        struct Splice { double jump; float ncc; };
        Splice findSplice (double fromDelay, double nominal, int direction, float period) const noexcept;
        float read (const std::vector<float>& buf, double delay) const noexcept;
        void designAntiAlias() noexcept;

        double fs = 48000.0;
        int latency = 702, maxJump = 680, guard = 4, size = 4096, mask = 4095, pos = 0;
        std::array<std::vector<float>, maxChannels> lines;
        std::vector<float> mono;

        float ratio = 1.0f;
        bool antiAlias = false;
        static constexpr int aaSections = 6;   // 12th-order Butterworth
        std::array<Biquad, aaSections> aa;

        double delayA = 0.0, delayB = 0.0;
        bool fading = false;
        float fadeCorr = 1.0f;
        double fadePos = 0.0, fadeInc = 0.0;
    };
}
