#pragma once

#include "DspUtilities.h"

namespace velvox::dsp
{
    struct DuckSettings
    {
        float amountDb = 0.0f;       // maximum reduction; 0 = ducking off
        float thresholdDb = -30.0f;  // detector level where ducking starts
        float attackMs = 10.0f;
        float releaseMs = 300.0f;
    };

    // Computes a gain curve for an effect return from a detector signal:
    // reduction = min (amount, level - threshold), smoothed with attack/release.
    // The vocal (or external sidechain) pushes the reverb / delay down while it
    // is active; in the gaps the effect comes back.
    class Ducker
    {
    public:
        void prepare (double sampleRate);
        void reset();
        void setSettings (const DuckSettings& s) noexcept;

        bool isActive() const noexcept { return settings.amountDb > 0.0f || envDb > 0.001f; }

        // Writes one linear gain per sample into `gains`
        void computeGains (const float* const* detector, int numChannels, int numSamples, float* gains) noexcept;

        PeakMeter gainReductionDb;

    private:
        double fs = 48000.0;
        DuckSettings settings;
        float attackCoeff = 0.0f, releaseCoeff = 0.0f, levelCoeff = 0.0f;
        float level = 0.0f; // detector envelope (linear peak)
        float envDb = 0.0f; // smoothed reduction
    };
}
