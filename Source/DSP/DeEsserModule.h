#pragma once

#include "DspUtilities.h"

namespace velvox::dsp
{
    struct DeEssSettings
    {
        float freq = 6500.0f;
        float thresholdDb = -30.0f;
        float rangeDb = 6.0f;
        bool listen = false;
        int mode = 0;   // 0 = split band (only the S band is reduced), 1 = wideband
        // 0.6: detection relative to the full-band level (Pro-DS "Single Vocal" style) -
        // loud and quiet phrases are de-essed alike. false = absolute (0.5 behaviour)
        bool relative = false;
    };

    // Split-band de-esser implemented as a dynamic high shelf:
    //  - detector: 2nd order high-pass at `freq`, stereo-linked peak envelope
    //  - processor: high shelf at `freq` whose gain follows the detected excess
    //    (0 dB when nothing is detected -> transparent; never below -range).
    // Only the band above `freq` is attenuated, the rest of the voice keeps its level.
    class DeEsserModule
    {
    public:
        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        void setSettings (const DeEssSettings& s) noexcept;
        void process (float* const* channels, int numChannels, int numSamples) noexcept;

        PeakMeter gainReductionDb;

        // Relative detection: band level - full-band envelope + referenceDb, so the
        // threshold means the same as in absolute mode for a voice peaking at -18 dBFS.
        // Below fullFloorDb the voice counts as that level (silence is never de-essed).
        static constexpr float referenceDb = -18.0f;
        static constexpr float fullFloorDb = -60.0f;

        // UI feed (Pro-DS style panel): input spectrum, current reduction, S band range
        SampleTap spectrumTap;
        std::atomic<float> currentReductionDb { 0.0f };
        std::atomic<float> bandLowHz { 6500.0f }, bandHighHz { 20000.0f };

    private:
        void updateShelf (float reductionDb) noexcept;

        double fs = 44100.0;
        DeEssSettings settings;
        Biquad detector, shelf;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> freq { 6500.0f };
        float attackCoeff = 0.0f, releaseCoeff = 0.0f;
        float envDb = 0.0f;
        float shelfDb = 0.0f;
        float fullEnv = 0.0f;   // full-band peak envelope (linear)
        float fullRelease = 0.0f;
        static constexpr float ratio = 4.0f;
        static constexpr float kneeDb = 6.0f;
    };
}
