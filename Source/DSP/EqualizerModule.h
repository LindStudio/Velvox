#pragma once

#include "DspUtilities.h"
#include "EqDesign.h"
#include "Audition.h"
#include <atomic>

namespace velvox::dsp
{
    struct EqSettings
    {
        // ---- 0.1-0.5 fixed bands. Still read from their (hidden) parameters, but NOT processed since
        //      0.6: a state that has no band values is migrated into bands 1-5 on load (legacyBands()).
        bool hpfOn = true;
        float hpfFreq = 80.0f;
        int hpfSlope = 1;          // 0 = 12, 1 = 24, 2 = 48 dB/oct
        int lowType = 0;           // 0 = shelf, 1 = bell
        float lowFreq = 150.0f, lowGain = 0.0f, lowQ = 0.7071f;
        float midFreq = 1500.0f, midGain = 0.0f, midQ = 1.0f;
        int highType = 0;          // 0 = shelf, 1 = bell
        float highFreq = 10000.0f, highGain = 0.0f, highQ = 0.7071f;
        bool lpfOn = false;
        float lpfFreq = 18000.0f;
        int lpfSlope = 1;          // 0 = 12, 1 = 24, 2 = 48 dB/oct

        // ---- 0.6: up to 16 bands, processed in this order
        std::array<EqBand, maxEqBands> bands {};
        bool legacyResponse = false;   // migrated 0.5 state: RBJ bilinear designs (sound-identical)

        // First band with `role` (role none: never searched), -1 if there is none
        int findRole (EqRole role) const noexcept;
        // First free slot (EqBand::isFree), -1 if all 16 are used
        int findFree() const noexcept;
    };

    // Up to 16 bands of minimum-phase biquad cascades (see EqDesign.h). Per band: frequency and Q
    // smoothed multiplicatively, gain linearly (50 ms), coefficients redesigned every 16 samples while
    // they move; on/off and changes of type, slope or design through a 30 ms crossfade. With
    // `legacyResponse` and the migrated bands 1-5 the output is bit-identical to the 0.5 EQ.
    class EqualizerModule
    {
    public:
        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        void setSettings (const EqSettings& s);
        void process (float* const* channels, int numChannels, int numSamples) noexcept;

        // ---- Band solo (0.6.1, Pro-Q-style headphone; Audition.h). A monitoring state, never a parameter:
        //      the UI sets the band to hear (-1 = none; any thread, lock-free), the audio thread follows at
        //      its next block through a 15 ms crossfade (from one band to another: region to region).
        //      A free slot is none; out of range is stored as -1.
        void setAuditionBand (int band) noexcept { auditionRequest.store (band >= 0 && band < maxEqBands ? band : -1, std::memory_order_relaxed); }
        int getAuditionBand() const noexcept { return auditionRequest.load (std::memory_order_relaxed); }
        // Audio thread, after process() and the module gate: replaces `io` by the region of the band,
        // filtered from `moduleInput` (the EQ's input; may be `io` itself while the EQ is bypassed).
        // Touches nothing (bit-identical output) while no band is or was just auditioned.
        void processAudition (const float* const* moduleInput, float* const* io, int numChannels, int numSamples) noexcept;
        bool isAuditionAudible() const noexcept   // audio thread / tests
        {
            return ! auditionVoices[0].path.isSilent() || ! auditionVoices[1].path.isSilent();
        }

        // ---- Curves for the UI. Pure functions of the settings: thread-safe on any thread when given a
        //      copy of the settings, e.g. processor.readEffectiveSettings().instance (k).eq (atomic reads).
        //      They describe exactly what the audio thread processes (same designs).
        // Total response of all bands that are on, in dB
        static double responseDb (const EqSettings& s, double freq, double sampleRate);
        // Response of one band (its on switch is ignored), in dB
        static double bandResponseDb (const EqBand& band, bool legacyResponse, double freq, double sampleRate);
        // Many frequencies at once (designs each band once): band = -1 -> all bands that are on,
        // band 0..15 -> that band alone (also when off). `freqs` and `outDb` hold `count` values.
        static void responseCurveDb (const EqSettings& s, double sampleRate, const float* freqs, float* outDb, int count, int band = -1);

        // The 0.5 fixed bands of `legacyFields` as bands 1-5 (low cut / low / mid / high / high cut) with
        // their roles (lowCut, warmth, autoMix, air, highCut). Used by the state migration.
        static std::array<EqBand, 5> legacyBands (const EqSettings& legacyFields) noexcept;

    private:
        using Mult = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>;

        struct BandState
        {
            EqBand target;                         // latest settings
            int type = 0, slope = 3;               // topology currently realised
            bool legacy = false;
            bool switching = false;                // fading out for a topology change
            Mult freq { 1000.0f }, q { 1.0f };
            juce::SmoothedValue<float> gain, amount;
            std::array<Biquad, 4> filters;
            int numSections = 0;
            bool isCut = false, needsDesign = true;
        };

        void adopt (BandState& b) noexcept;   // realises the target topology, values snapped
        void design (BandState& b) noexcept;
        void processBand (BandState& b, float* const* channels, int numChannels, int start, int n) noexcept;

        double fs = 44100.0;
        bool legacyTarget = false;
        std::array<BandState, maxEqBands> bands;
        bool snapNext = true;   // the first settings after prepare() / reset() apply without a sweep
        static constexpr int updateInterval = 16;

        // Two listening paths, so hopping from one band (or shape) to the next is a crossfade between
        // the two regions, never a blip of the full signal (audio thread)
        struct AuditionVoice
        {
            BandAudition path;
            int band = -1;   // band it plays or fades out
        };
        std::atomic<int> auditionRequest { -1 };
        std::array<AuditionVoice, 2> auditionVoices;
        static constexpr int auditionBlock = 256;
        std::array<std::array<float, auditionBlock>, maxChannels> auditionNormal {};
    };
}
