#pragma once

#include "Filters.h"

// Band model and coefficient designs of the 0.6 EQ (up to 16 bands per instance).
//
// Two designs per band type:
//  - matched (default): minimum-phase biquads whose magnitude follows the ANALOG prototype up to
//    Nyquist (no bilinear "cramping"). Poles by impulse invariance, numerator by magnitude matching
//    after M. Vicanek, "Matched Second Order Digital Filters" (2016): DC, a match point and the slope
//    there. Cuts are the exact inverse of the corresponding boost (and high-shelf boosts the inverse of
//    the cut) so the poles always sit at the lower of the two corner frequencies.
//  - legacy: the RBJ bilinear designs of 0.5. Used only by states migrated from 0.5 (eqLegacyResponse),
//    which therefore sound bit-identical to the old fixed-band EQ.
namespace velvox::dsp
{
    inline constexpr int maxEqBands = 16;

    enum class EqBandType { bell = 0, lowShelf, highShelf, lowCut, highCut, notch, bandPass, tilt };
    inline constexpr int numEqBandTypes = 8;

    // Hidden owner of a band. User bands are `none` and are never changed by macros, the automatic
    // mix or Vocal Check. Migrated 0.5 bands: lowCut, warmth (low band), autoMix (mid bell), air (high
    // band), highCut (LPF). Macros: presence -> air, warmth -> warmth (+ air). Vocal Check is free to
    // use presence and mud. 0.6.1: the automatic mix also owns autoTone (a broad 2-5 kHz bell) and
    // autoNotch1..3 (narrow resonance cuts); roles are only ever appended (saved indices keep their meaning).
    enum class EqRole { none = 0, presence, warmth, air, lowCut, mud, autoMix, highCut, autoTone, autoNotch1, autoNotch2, autoNotch3 };
    inline constexpr int numEqRoles = 12;

    // Cut slopes: index 0..5 = 6, 12, 18, 24, 36, 48 dB/oct (Butterworth of order 1, 2, 3, 4, 6, 8)
    inline constexpr int numEqSlopes = 6;
    constexpr int eqSlopeDbPerOct (int index) noexcept
    {
        constexpr int table[numEqSlopes] { 6, 12, 18, 24, 36, 48 };
        return table[index < 0 ? 0 : (index >= numEqSlopes ? numEqSlopes - 1 : index)];
    }

    struct EqBand
    {
        bool on = false;
        int type = (int) EqBandType::bell;
        float freq = 1000.0f;   // Hz
        float gain = 0.0f;      // dB (bell, shelves, tilt = full tilt from lows to highs)
        float q = 1.0f;         // bell / shelf / notch / band pass; cuts are Butterworth, tilt has no Q
        int slope = 3;          // cut types only, index into eqSlopeDbPerOct (default 24 dB/oct)
        int role = (int) EqRole::none;

        EqBandType bandType() const noexcept { return (EqBandType) (type < 0 || type >= numEqBandTypes ? 0 : type); }
        EqRole bandRole() const noexcept { return (EqRole) (role < 0 || role >= numEqRoles ? 0 : role); }
        bool isCut() const noexcept { return bandType() == EqBandType::lowCut || bandType() == EqBandType::highCut; }
        // A slot that holds nothing: off, no role, every value at its default (the panel's "delete")
        // (tolerant: values that went through a normalised host parameter may be off by an ulp)
        bool isFree() const noexcept
        {
            return ! on && type == (int) EqBandType::bell && role == (int) EqRole::none && slope == 3
                && std::abs (freq - 1000.0f) < 0.05f && std::abs (gain) < 1.0e-3f && std::abs (q - 1.0f) < 1.0e-4f;
        }
        bool operator== (const EqBand&) const = default;
    };

    // Up to 4 second-order sections (48 dB/oct = 8th order); first-order sections have b2 = a2 = 0
    struct EqBandDesign
    {
        std::array<BiquadCoeffs, 4> sections {};
        int numSections = 0;
    };

    namespace eqdesign
    {
        // Parameter ranges (also the clamp of the designs)
        inline constexpr float minFreq = 10.0f, maxFreq = 30000.0f, minQ = 0.025f, maxQ = 40.0f, maxGainDb = 30.0f;

        // Coefficients of one band (its on switch is ignored). Real-time safe.
        EqBandDesign design (const EqBand& band, bool legacy, double sampleRate) noexcept;

        // |H(f)| of a designed band, linear
        double magnitude (const EqBandDesign& d, double freq, double sampleRate) noexcept;

        // |H(f)| of the analog prototype the matched design follows (reference for tests and docs)
        double analogMagnitude (const EqBand& band, double freq) noexcept;
    }
}
