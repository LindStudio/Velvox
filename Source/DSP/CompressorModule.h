#pragma once

#include "DspUtilities.h"

namespace velvox::dsp
{
    struct CompSettings
    {
        float thresholdDb = -18.0f;
        float ratio = 3.0f;
        float attackMs = 10.0f;
        float releaseMs = 120.0f;
        float kneeDb = 6.0f;
        float makeupDb = 0.0f;
        int detector = 0;           // 0 = peak, 1 = RMS (smoother, more "musical")
        float sidechainHpfHz = 20.0f; // <= 20 Hz = off; ignores low end when detecting
        // 0.6 (defaults = 0.5 behaviour)
        bool autoRelease = false;   // program-dependent dual release
        bool autoMakeup = false;    // adds the static gain reduction at autoMakeupLevelDb
    };

    // Running maximum over the last `window` samples (monotonic wedge, O(1) amortised).
    // Storage is sized in prepare(); push() never allocates.
    class PeakHold
    {
    public:
        void prepare (int windowSamples)
        {
            window = std::max (1, windowSamples);
            values.assign ((size_t) window + 1, 0.0f);
            stamps.assign ((size_t) window + 1, 0);
            reset();
        }

        void reset() noexcept { head = size = 0; now = 0; }

        float push (float v) noexcept
        {
            const int cap = (int) values.size();
            while (size > 0 && values[(size_t) ((head + size - 1) % cap)] <= v)
                --size;
            const int pos = (head + size) % cap;
            values[(size_t) pos] = v;
            stamps[(size_t) pos] = now;
            ++size;
            while (stamps[(size_t) head] <= now - window)
            {
                head = (head + 1) % cap;
                --size;
            }
            ++now;
            return values[(size_t) head];
        }

    private:
        std::vector<float> values { 0.0f, 0.0f };
        std::vector<std::int64_t> stamps { 0, 0 };
        int window = 1, head = 0, size = 0;
        std::int64_t now = 0;
    };

    // Feed-forward, stereo-linked compressor with soft knee (lookahead 0, no latency).
    // Detector (peak): the running maximum of |x| over `holdMs` (decoupled peak hold),
    // so a steady low note reads a constant level - no ripple in the gain, no ballistics
    // distortion. Gain reduction is smoothed in the log domain with a branching
    // attack/release smoother (Giannoulis, Massberg & Reiss 2012).
    class CompressorModule
    {
    public:
        // One rectified period of 50 Hz: the lowest vocal fundamentals read their peak
        static constexpr double holdMs = 10.0;

        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        void setSettings (const CompSettings& s) noexcept;
        void process (float* const* channels, int numChannels, int numSamples) noexcept;

        // Static curve: output level for a given input level (both dB)
        static float computeGainDb (float inputDb, float thresholdDb, float ratio, float kneeDb) noexcept;

        // Auto makeup: the static gain reduction of a tone whose detector level is
        // -18 dBFS (a typical vocal level), so a voice at that level keeps its loudness
        static constexpr float autoMakeupLevelDb = -18.0f;
        static float autoMakeupDb (const CompSettings& s) noexcept;
        // Complete static transfer curve (output dB for a steady input dB), incl. makeup and
        // auto makeup - for the Pro-C style curve display (UI thread builds the settings)
        static float transferDb (const CompSettings& s, float inputDb) noexcept;

        // Dual release (Auto release): fast branch attack A / release fastFactor * R,
        // slow branch attack slowAttackFactor * R / release slowFactor * R; GR = max of both
        static constexpr float fastReleaseFactor = 0.4f, slowAttackFactor = 4.0f, slowReleaseFactor = 2.5f;

        float getGainReductionDb() const noexcept { return grDb; }

        PeakMeter gainReductionDb; // positive dB of reduction

    private:
        double fs = 44100.0;
        CompSettings settings;
        float attackCoeff = 0.0f, releaseCoeff = 0.0f;
        float fastReleaseCoeff = 0.0f, slowAttackCoeff = 0.0f, slowReleaseCoeff = 0.0f;
        float envDb = 0.0f;   // gain reduction of the main (fixed release) branch in dB (>= 0)
        float fastDb = 0.0f, slowDb = 0.0f;   // dual release branches
        float grDb = 0.0f;    // applied gain reduction
        juce::SmoothedValue<float> dualMix { 0.0f };   // 0 = fixed release, 1 = dual (crossfaded on toggle)
        float rmsState = 0.0f, rmsCoeff = 0.0f;
        Biquad sidechainHpf;
        bool hpfActive = false;
        PeakHold peakHold;
        juce::SmoothedValue<float> makeup { 1.0f };
    };
}
