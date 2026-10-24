#pragma once

#include "DspUtilities.h"
#include "SatOversampler.h"
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>

namespace velvox::dsp
{
    // One band of the multiband mode
    struct SatBand
    {
        bool on = true;
        float driveDb = 6.0f;
        int character = 0;
    };

    struct SatSettings
    {
        bool enabled = false;
        float driveDb = 6.0f;
        float outputDb = 0.0f;
        float mix = 1.0f; // 0..1
        int character = 0; // 0 warm, 1 tape, 2 tube, 3 hard

        // Multiband (0.6): Low / Mid / High, each with its own drive, character and on/off.
        // Off = the single-band behaviour above (driveDb / character).
        bool multiband = false;
        float xoverLowHz = 250.0f;
        float xoverHighHz = 4000.0f;
        std::array<SatBand, 3> bands { SatBand { true, 6.0f, 0 }, SatBand { true, 6.0f, 1 }, SatBand { true, 6.0f, 1 } };
    };

    // Level meters for the UI (written by the audio thread, relaxed atomics): per band input and
    // output RMS in dBFS over the last block (single band: index 0)
    struct SatMeters
    {
        std::array<std::atomic<float>, 3> inDb {}, outDb {};
    };

    // Saturation (0.6): static transfer curves (four characters), level compensated, with
    // first-order antiderivative antialiasing (ADAA, double precision) inside a 4x oversampled
    // section (SatOversampler; 2x at 88.2 kHz and above). The dry/wet mix happens at the high rate
    // (the dry gets the same filters - no comb at partial Mix), the DC blocker acts on the added
    // distortion only. Multiband: LR4 crossovers (TPT state-variable filters) at the high rate,
    // the low band all-pass compensated, so the bands sum to a flat all-pass; one oversampler for
    // all three shapers.
    //
    // The module ALWAYS delays the signal by its latency - also when switched off - so the
    // plugin's reported latency stays constant; switching on/off or between single and multiband
    // crossfades (20 ms) against that delayed dry.
    class SaturationModule
    {
    public:
        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        void setSettings (const SatSettings& s) noexcept;
        void process (float* const* channels, int numChannels, int numSamples) noexcept;

        int getLatencySamples() const noexcept { return latency; }

        // Transfer curves incl. level compensation (also used by the UI)
        static constexpr int numCharacters = 4;
        static float shape (float x, float driveLin, int character = 0) noexcept;
        static float compensationFor (float driveLin, int character = 0) noexcept;
        // Antiderivative of shape (x, driveLin, character) with respect to x
        static double antiderivative (double x, double driveLin, int character) noexcept;

        // UI feed (message thread, no state):
        //  transferCurve: y = shape (x) * compensation for the given character and drive
        //  harmonicProfile: H1..H8 of a sine at levelDb through the curve, dB relative to H1
        //  (H1 itself = the level change of the fundamental incl. compensation, normally ~0 dB)
        static void transferCurve (int character, float driveDb, const float* x, float* y, int n) noexcept;
        static std::array<float, 8> harmonicProfile (int character, float driveDb, float levelDb) noexcept;

        static constexpr int numBands = 3;
        const SatMeters& getMeters() const noexcept { return meters; }

    private:
        struct Svf
        {
            double k = 1.4142135623730951, a1 = 1.0, a2 = 0.0, a3 = 0.0;
            void set (double fc, double fs) noexcept;
        };
        struct SvfState
        {
            double s1 = 0.0, s2 = 0.0;
            // returns the low-pass output, writes the band-pass output
            double tick (const Svf& f, double x, double& band) noexcept
            {
                const double v3 = x - s2;
                const double v1 = f.a1 * s1 + f.a2 * v3;
                const double v2 = s2 + f.a2 * s1 + f.a3 * v3;
                s1 = 2.0 * v1 - s1;
                s2 = 2.0 * v2 - s2;
                band = v1;
                return v2;
            }
        };
        struct Shaper   // ADAA state of one band, one channel
        {
            double xp = 0.0, fp = 0.0;
            bool stale = false;   // fp must be recomputed before the next use (band was off)
        };
        struct BandParams
        {
            juce::SmoothedValue<float> driveDb { 6.0f }, onAmount { 1.0f };
            double driveLin = 2.0;
            float compensation = 1.0f;
            int character = 0, preparedCharacter = -1;
        };

        double shapeAdaa (Shaper& s, const BandParams& p, double x) noexcept;
        void updateBandParams (int band, int skip, bool force) noexcept;
        void updateCrossovers (int skip, bool force) noexcept;
        void resetState() noexcept;

        SatOversampler oversampler;
        IntegerDelay dryDelay;
        juce::AudioBuffer<float> dry;
        juce::SmoothedValue<float> outGain { 1.0f }, amount { 0.0f }, weight { 0.0f };
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> xoverLow { 250.0f }, xoverHigh { 4000.0f };
        std::array<BandParams, numBands> bandParams;
        std::array<std::array<Shaper, maxChannels>, numBands> shapers {};
        std::array<Svf, 7> svf;
        std::array<std::array<SvfState, 7>, maxChannels> svfState {};
        std::array<double, maxChannels> dryPrev {}, dcX {}, dcY {};
        std::array<std::array<double, numBands>, maxChannels> bandPrev {};
        std::array<std::array<double, 2>, maxChannels> firHist {};
        double dcR = 0.9999;
        double highRate = 192000.0;
        SatSettings pending;
        float amountTarget = 0.0f;
        bool requestedMultiband = false, activeMultiband = false;
        bool needsReset = true;
        int latency = 0;
        int warmup = 0, warmupLength = 240;
        SatMeters meters;
    };
}
