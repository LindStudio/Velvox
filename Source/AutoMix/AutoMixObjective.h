#pragma once

#include "VocalProfile.h"

namespace vocalypse::automix
{
    struct RenderMetrics;
    struct MixPlan;

    // The objective the automatic mix minimises when it compares candidate chains
    // (docs/AUTOMIX_OBJECTIVE.md). Every term is a penalty (0 = inside the target window),
    // computed from a real render of the captured audio through the production DSP:
    //
    //   tonal     third-octave levels relative to the 300 Hz - 2 kHz core against the target curve of
    //             the genre / vibe (0.8; squared distance outside its spread)
    //   dynamics  phrase-level variation (dynamicsDb) outside 4 .. 8 dB, crest factor below
    //             7 dB (over-compression: flat, pumping, breathy)
    //   sibilance S / vowel balance above -6 dB, or more than 10 dB below the original (lisp)
    //   harshness 2-5 kHz above -4 dB against the core
    //   stereo    mono-sum loss beyond 1 dB, correlation below 0.5
    //   space     effect level in the gaps after words, outside the window for the style
    //             (spoken: drier, sustained melodic: more room)
    //   artifacts clipping runs, peak-to-loudness growth over the original (> 1.5 dB),
    //             and mildly the headroom: peak above -1 dBFS at the target loudness
    //   change    (U4: 0 - leaving the base plan is paid for by the margin of variantEarnsChange)
    //
    // Loudness is never rewarded: every candidate is compared at the same target loudness.
    struct ObjectiveBreakdown
    {
        float tonal = 0.0f, dynamics = 0.0f, sibilance = 0.0f, harshness = 0.0f;
        float stereo = 0.0f, space = 0.0f, artifacts = 0.0f, change = 0.0f;
        float beat = 0.0f;   // 0.8: the beat on the sidechain covers the words (1 - 5 kHz)
        float total() const noexcept { return tonal + dynamics + sibilance + harshness + stereo + space + artifacts + change + beat; }
        juce::String describe() const;   // "tonal 0.4, dynamics 1.2, ..." (non-zero terms)
    };

    enum class VocalStyle { spoken, sung, sustained };
    VocalStyle styleOf (const VocalProfile& p) noexcept;

    // The beat on the sidechain (plan 2026-10-28): dense = hardly any gaps for an effect tail
    // (gap share < 0.15), sparse = clear gaps (> 0.3). The space window moves with it.
    enum class BeatDensity { none, dense, medium, sparse };
    BeatDensity beatDensityOf (const VocalProfile& p) noexcept;

    // ---- Stable variant choice (plan 2026-11-04-automix-konsistenz, U4) ----
    // Complexity of a chain, from the plan's values: 0.3 per switched-on module instance (in the routing),
    // 0.05 per dB of the summed EQ gain magnitude (active bands of every EQ instance in the chain) and 0.1 per
    // dB of the median compression `medianCompressionDb` (the gain reduction measured on the render).
    float planComplexity (const MixPlan& plan, float medianCompressionDb);
    // What a variant has to beat the incumbent by: max (0.5, 8 % of the base plan's score)
    float variantMargin (float baseScore) noexcept;
    // incumbent - variant >= variantMargin (base) + complexity (variant) - complexity (incumbent)
    bool variantEarnsChange (float incumbentScore, float incumbentComplexity, float variantScore, float variantComplexity,
                             float baseScore) noexcept;

    // `effectsPlanned`: the plan adds reverb or delay (then a too dry result also costs)
    // 0.8: `target` = the genre / vibe target (tonal: third octaves against its curve; dynamics,
    // sibilance from it)
    ObjectiveBreakdown evaluate (const RenderMetrics& m, const RenderMetrics& original, const VocalProfile& profile,
                                 float targetLufs, bool isBasePlan, bool effectsPlanned,
                                 const MixTarget& target = targetFor (Genre::party, Vibe::punchy));
}
