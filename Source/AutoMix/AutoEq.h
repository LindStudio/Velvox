#pragma once

#include "VocalProfile.h"
#include "MixTarget.h"
#include "../DSP/EqDesign.h"

// 0.6.1 (spec item 7): the EQ moves of the automatic mix - narrow notches on sustained
// resonances and broad tonal moves towards the target curve of the objective
// (docs/AUTOMIX_OBJECTIVE.md, tonal windows). Pure functions (any thread).
//
// The moves are solved on a model: the profiler's long-term spectrum (1/12 octave) with the
// analog magnitude of EQ bands applied, measured in the same bands as the objective. The model
// knows where a band can act - a 9 kHz shelf does nothing on a take without content up there.
// The planner cuts what is clearly too much and lifts a dull top end (open loop, from the
// analysis); the engine then corrects the broad bands with the levels of the real render
// (`correct`), because compression, saturation and effects move the balance as well.
namespace vocalypse::automix::autoeq
{
    // Limits of the automatic mix in EQ 1
    inline constexpr int maxBands = 8;                 // every band it uses, the low cut included (0.8: 6 -> 8)
    inline constexpr int maxNotches = 3;
    inline constexpr float maxNarrowCutDb = 9.0f;      // narrow: Q 10 .. 16, cuts only
    inline constexpr float maxBroadCutDb = 6.0f;       // 0.8: 4 -> 6 (a clear mud / boxiness cut)
    inline constexpr float maxBroadBoostDb = 4.0f;
    // 0.8 (bench 2026-11-02: dry takes lie 8-17 dB under finished party vocals above 4 kHz): a lift above 5 kHz
    // may go to 6 dB on a clean take, 4 dB on a sibilant one (the de-esser follows), 2 dB on a noisy one
    inline constexpr float maxHighLiftDb = 6.0f, maxHighLiftSibilantDb = 4.0f, maxHighLiftNoisyDb = 2.0f;
    inline constexpr float minNarrowQ = 10.0f, maxNarrowQ = 16.0f;
    inline constexpr float minMoveDb = 1.0f;           // smaller new broad moves are not worth a band

    // The tonal windows of the objective (AutoMixObjective.cpp evaluate) - the target curve.
    // 2026-10-29 (Jakob: "Profis boosten die Höhen eher"): calibrated on finished professional vocals (8 song
    // stems, measured like the plugin: middle half presence -11.5 .. -5.2, above 2 kHz -8.7 .. -3.3, air
    // -17.3 .. -14.6 dB against the core). The old windows (top -16 .. -7, air -32 .. -16, no floor for the
    // presence) were about 5 dB too dark: the auto mix cut 2-5 kHz on dark voices instead of lifting it.
    struct Window { float lo, hi; };
    inline constexpr Window bodyWindow { -9.0f, -1.0f }, lowMidWindow { -7.0f, 0.0f }, topWindow { -11.0f, -3.0f },
                            airWindow { -22.0f, -13.0f }, presenceWindow { -12.0f, -5.0f };

    // Roles the automatic mix owns in EQ 1: it rewrites or clears them on every run. User bands
    // (none) and Vocal Check bands (presence, mud) are never written.
    bool ownedRole (dsp::EqRole role) noexcept;
    inline constexpr dsp::EqRole notchRoles[maxNotches] { dsp::EqRole::autoNotch1, dsp::EqRole::autoNotch2, dsp::EqRole::autoNotch3 };
    // The broad roles, in the order the moves are made
    inline constexpr dsp::EqRole broadRoles[4] { dsp::EqRole::autoMix, dsp::EqRole::warmth, dsp::EqRole::autoTone, dsp::EqRole::air };

    // Band levels relative to the 300 Hz - 2 kHz core (like VocalProfile / RenderMetrics)
    struct Levels { float body = -60.0f, lowMid = -60.0f, presence = -60.0f, top = -60.0f, air = -60.0f; };

    // `spectrumDb`: VocalProfiler::spectrumDb (power density per 1/12-octave band from 40 Hz,
    // -200 = empty). `bands`: EQ bands applied on top (only those switched on count).
    Levels levelsOf (const std::vector<float>& spectrumDb, const std::vector<dsp::EqBand>& bands);
    // Total magnitude (dB) of `bands` at `freq`
    float responseDb (const std::vector<dsp::EqBand>& bands, float freq);

    struct Move
    {
        dsp::EqRole role = dsp::EqRole::none;
        dsp::EqBand band;              // on, type, freq, gain, q (role set by the writer)
        juce::String title, reason;    // German, for the auto-mix report / overview
        bool narrow = false;
    };

    struct Context
    {
        const VocalProfile* profile = nullptr;
        std::vector<dsp::EqBand> fixed;   // bands that stay (user / Vocal Check bands, the planned low cut)
        bool conservative = false;
        float caution = -1.0f;            // 0 .. 1 (MixPlan::caution); < 0 = unset: 1 when conservative, else 0
        // 0 .. 1 weights (Task 7, continuous decisions): 0 = not the case, 1 = clearly the case. A bool still works
        // (false = 0, true = 1); the planner fills them with noisyWeight / sibilantWeight / harshWeight.
        float noisy = 0.0f, sibilant = 0.0f, harsh = 0.0f;
        int budget = maxBands - 1;        // bands available besides the low cut
        const MixTarget* target = nullptr;            // 0.8: the target curve (nullptr = party / punchy)
        std::array<float, numThirds> unmaskDb {};     // 0.8: target lift per third where the beat covers the voice
    };

    // 0.8 (spec: "EQ: Kurvenabgleich"): notches on sustained resonances first, then a greedy fit of the
    // voice's third-octave levels to the target curve: the largest connected stretch outside the
    // target's spread gets a bell (Q from its width) or a shelf at the ends, its gain chosen on the
    // model; then the next stretch, until the budget is used or nothing is left. Cuts down to
    // -6 dB (-3 dB below 300 Hz), lifts up to +4 dB (2 dB above 5 kHz on a noisy or sibilant take,
    // none on a resonance, none on the low end of a high voice, none of 2-5 kHz on a harsh take).
    // Deterministic, within `budget`.
    std::vector<Move> planCurveMoves (const Context& context);

    // Soft weights (0 .. 1) of the profile's properties that limit the EQ; they start at the edge of the
    // matching profile issue and reach 1 where that issue is "likely", so nothing flips at one number
    float noisyWeight (const VocalProfile& profile) noexcept;      // SNR 28 dB (0) .. 24 dB (1)
    float sibilantWeight (const VocalProfile& profile) noexcept;   // S sounds -5 dB (0) .. -2 dB (1) against the vowels
    float harshWeight (const VocalProfile& profile) noexcept;      // 2-5 kHz -2 dB (0) .. +1 dB (1), not on a dull take
    float dullWeight (const VocalProfile& profile) noexcept;       // above 2 kHz -16 dB (0) .. -20 dB (1)

    // What a broad move at `hz` does, by region (German / English): "Dröhnen" (cut 150-450 Hz),
    // "Pappig" (450-900), "Nasal" (900-1600), "Härte" (cut 2.5-5 kHz), "Körper" (lift below 300 Hz),
    // "Verständlichkeit" (lift 1.6-5 kHz), "Brillanz" (lift above 7 kHz), else Tiefen / Mitten / Höhen
    juce::String regionName (float hz, bool cut);

    // Closed loop (0.8): `rendered` are the third-octave levels of the real render with the auto-mix bands
    // `owned` (broad and narrow, as in the plan). The broad bands get their gains re-tuned on the render
    // (frequency and shape stay; gain 0 = flatten), and stretches still off the target get new moves
    // (at most context.budget). Empty when everything is within 0.5 dB of the target.
    std::vector<Move> correctCurve (const std::vector<float>& renderedThirds, const Context& context, const std::vector<Move>& owned);

    // The thirds the curve fit and the objective look at: from 160 Hz (and 1.3 x the lowest sung note) to 12.9 kHz
    std::pair<int, int> fitRange (const VocalProfile& profile);
    // The target lift per third where the beat on the sidechain covers the voice (1 - 5 kHz, 0 .. 2.5 dB)
    // `caution` 0 .. 1 (MixPlan::caution; a bool still works): the lift shrinks to 0.6 x at 1
    std::array<float, numThirds> unmaskOf (const VocalProfile& profile, float caution);

    // The title of a move: "Dröhnen -3,5 dB bei 340 Hz", "Brillanz +3,0 dB (Shelf 10 kHz)", "Resonanz 1,8 kHz -6,0 dB (schmal, Q 12)"
    juce::String titleOf (dsp::EqRole role, const dsp::EqBand& band);

    // True when `band` respects the limits (narrow: cut only, Q 10..16, >= -9 dB; broad: -6..+4 dB, +6 dB above 5 kHz)
    bool withinLimits (const dsp::EqBand& band) noexcept;

    // ---- The beat on the sidechain (plan 2026-10-28). The output puts the voice leadOverBeatLu above
    //      the beat; the masking is measured as it will be there.
    inline constexpr float leadOverBeatLu = 1.0f;
    inline constexpr float unmaskFromDb = 3.0f;        // the beat this much louder than the voice: lift
    struct Masking
    {
        bool valid = false;           // a beat was measured
        float hz = 0.0f;              // centre of the 1/4 octave of 1.5 - 6 kHz where the beat covers most
        float presenceDb = -100.0f;   // there: beat minus voice (dB)
        float lowMidDb = -100.0f;     // 250 - 500 Hz: beat minus voice (dB)
    };
    Masking maskingOf (const VocalProfile& profile);
    // The lift for a masking of `excessDb` (0 below unmaskFromDb), 0.5 .. 2.5 dB in 0.5 dB steps
    float unmaskGainDb (float excessDb, float caution) noexcept;
    // The caution of a context: Context::caution, or 1 / 0 from Context::conservative when it is unset
    float cautionOf (const Context& ctx) noexcept;
}
