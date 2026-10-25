#pragma once

#include "DspUtilities.h"
#include "FormantCorrector.h"
#include "PitchTracker.h"
#include "SpliceShifter.h"

namespace velvox::dsp
{
    struct PitchSettings
    {
        bool enabled = false;
        float semitones = 0.0f;      // -24 .. +24
        float cents = 0.0f;          // -100 .. +100
        bool preserveFormants = true;
        float formantShift = 0.0f;   // semitones, creative formant move (-12 .. +12)
        float mix = 1.0f;            // dry/wet (dry is latency aligned)
    };

    // Creative pitch shifter (a fixed transposition, NOT pitch correction).
    //
    // 0.6 engine (replaces Signalsmith Stretch, whose phase vocoder needed 120 ms and was
    // 2-7 ct off depending on where the input fell on its FFT bin grid):
    //   PitchTracker     - f0 of the input (decimated YIN, refined at full rate)
    //   SpliceShifter    - pitch-synchronous two-head resampling: exact ratio, no phasiness
    //   FormantCorrector - puts the formants back (Preserve) or moves them (Formant)
    // Latency is constant, ceil(fs / 70) + 16 samples (~14.9 ms at 48 kHz), whatever the
    // settings; it is reported to the host while the module is on (see VocalChain).
    // Everything is allocated in prepare(); process() is real-time safe.
    class PitchModule
    {
    public:
        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        void setSettings (const PitchSettings& s) noexcept;

        // Latency while enabled (samples)
        int getLatencySamples() const noexcept { return shifter.getLatency(); }

        // Called by the chain when the module becomes active
        void activate() noexcept;

        // Processes in place; only call while active (enabled)
        void process (float* const* channels, int numChannels, int numSamples) noexcept;

    private:
        void processChunk (float* const* ch, int numChannels, int n) noexcept;

        double fs = 48000.0;
        int maxBlock = 512;
        PitchSettings settings;
        juce::SmoothedValue<float> mix { 1.0f };
        IntegerDelay dryDelay;
        juce::AudioBuffer<float> dry;
        std::vector<float> monoIn, period;

        PitchTracker tracker;
        SpliceShifter shifter;
        FormantCorrector corrector;
    };
}
