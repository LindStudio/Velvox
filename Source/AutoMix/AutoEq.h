#pragma once

#include "VocalProfile.h"
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
namespace velvox::automix::autoeq
{
    // Limits of the automatic mix in EQ 1
    inline constexpr int maxBands = 6;                 // every band it uses, the low cut included
    inline constexpr int maxNotches = 3;
    inline constexpr float maxNarrowCutDb = 9.0f;      // narrow: Q 10 .. 16, cuts only
    inline constexpr float maxBroadCutDb = 4.0f;
    inline constexpr float maxBroadBoostDb = 4.0f;
    inline constexpr float minNarrowQ = 10.0f, maxNarrowQ = 16.0f;
    inline constexpr float minMoveDb = 1.0f;           // smaller new broad moves are not worth a band

    // The tonal windows of the objective (AutoMixObjective.cpp evaluate) - the target curve
    struct Window { float lo, hi; };
    inline constexpr Window bodyWindow { -9.0f, -1.0f }, lowMidWindow { -7.0f, 0.0f }, topWindow { -16.0f, -7.0f },
                            airWindow { -32.0f, -16.0f }, presenceWindow { -100.0f, -4.0f };

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
    // The objective's tonal + harshness terms for these levels (the target curve as one number;
    // a high voice - lowest notes above 250 Hz - only pays for too much body)
    float tonalPenalty (const Levels& levels, bool highVoice, float margin = 0.0f);   // margin: windows narrowed by it
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
        bool conservative = false, noisy = false, sibilant = false, harsh = false, masking = false;
        int budget = maxBands - 1;        // bands available besides the low cut
    };

    // Broad moves (roles warmth, autoMix, autoTone, air) and notches (autoNotch1..3), at most
    // `budget`, each within the limits above. Deterministic.
    std::vector<Move> planMoves (const Context& context);

    // Closed loop: `rendered` are the levels of the real render with the auto-mix bands `owned`
    // (broad and narrow, as in the plan). Returns the broad bands to change (gain 0 = flatten) or to
    // create (at most context.budget new ones) so that the levels move into the windows. Empty when
    // everything is inside (0.5 dB tolerance) or nothing can move any more.
    std::vector<Move> correct (const Levels& rendered, const Context& context, const std::vector<Move>& owned);

    // The title of a move (German): "Höhen +2,0 dB (Shelf 9 kHz)", "Resonanz 1,8 kHz -6,0 dB (schmal, Q 12)"
    juce::String titleOf (dsp::EqRole role, const dsp::EqBand& band);

    // True when `band` respects the limits (narrow: cut only, Q 10..16, >= -9 dB; broad: -4..+4 dB)
    bool withinLimits (const dsp::EqBand& band) noexcept;
}
