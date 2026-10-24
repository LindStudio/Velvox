#pragma once

#include "Filters.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace velvox::dsp
{
    // Monophonic f0 tracker for the pitch shifter: YIN (de Cheveigne & Kawahara 2002) on a
    // ~12 kHz decimated copy every 5 ms, refined on the full-rate signal (parabolic minimum of the
    // difference function), so the period is exact to a small fraction of a sample.
    // Everything is allocated in prepare(); push() is real-time safe.
    class PitchTracker
    {
    public:
        void prepare (double sampleRate);
        void reset() noexcept;

        // One mono input sample
        void push (float x) noexcept;

        // Period of the most recent analysis in full-rate samples (fractional); 0 = unvoiced / silent
        float getPeriod() const noexcept { return period; }
        // YIN aperiodicity of the last analysis (0 = perfectly periodic, ~1 = noise)
        float getAperiodicity() const noexcept { return aperiodicity; }

        // Longest period that can be reported (full-rate samples)
        float getMaxPeriod() const noexcept { return (float) (tauMax * decimation); }

    private:
        void analyse() noexcept;
        float refine (float coarseFull) const noexcept;

        double fs = 48000.0;
        int decimation = 4, phase = 0;
        Biquad aa1, aa2;                          // anti-alias for the decimation

        std::vector<float> dec;                   // decimated history (circular, power of 2)
        int decMask = 0, decPos = 0;
        std::vector<float> full;                  // full-rate history (circular, power of 2)
        int fullMask = 0, fullPos = 0;

        int window = 256, tauMin = 8, tauMax = 185, hop = 60, hopCounter = 0;
        std::vector<float> frame, diff;

        float period = 0.0f, aperiodicity = 1.0f;
    };
}
