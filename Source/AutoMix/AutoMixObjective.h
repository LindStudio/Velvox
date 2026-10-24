#pragma once

#include "VocalProfile.h"

namespace velvox::automix
{
    struct RenderMetrics;

    // The objective the automatic mix minimises when it compares candidate chains
    // (docs/AUTOMIX_OBJECTIVE.md). Every term is a penalty (0 = inside the target window),
    // computed from a real render of the captured audio through the production DSP:
    //
    //   tonal     band levels relative to the 300 Hz - 2 kHz core, outside a window that
    //             suits a lead vocal in a dense electronic mix (squared distance)
    //   dynamics  phrase-level variation (dynamicsDb) outside 4 .. 8 dB, crest factor below
    //             7 dB (over-compression: flat, pumping, breathy)
    //   sibilance S / vowel balance above -6 dB, or more than 10 dB below the original (lisp)
    //   harshness 2-5 kHz above -4 dB against the core
    //   stereo    mono-sum loss beyond 1 dB, correlation below 0.5
    //   space     effect level in the gaps after words, outside the window for the style
    //             (spoken: drier, sustained melodic: more room)
    //   artifacts clipping runs, peak-to-loudness growth over the original (> 1.5 dB),
    //             and mildly the headroom: peak above -1 dBFS at the target loudness
    //   change    a small fixed cost for leaving the base plan - variants must earn it
    //
    // Loudness is never rewarded: every candidate is compared at the same target loudness.
    struct ObjectiveBreakdown
    {
        float tonal = 0.0f, dynamics = 0.0f, sibilance = 0.0f, harshness = 0.0f;
        float stereo = 0.0f, space = 0.0f, artifacts = 0.0f, change = 0.0f;
        float total() const noexcept { return tonal + dynamics + sibilance + harshness + stereo + space + artifacts + change; }
        juce::String describe() const;   // "tonal 0.4, dynamics 1.2, ..." (non-zero terms)
    };

    enum class VocalStyle { spoken, sung, sustained };
    VocalStyle styleOf (const VocalProfile& p) noexcept;

    // `effectsPlanned`: the plan adds reverb or delay (then a too dry result also costs)
    ObjectiveBreakdown evaluate (const RenderMetrics& m, const RenderMetrics& original, const VocalProfile& profile,
                                 float targetLufs, bool isBasePlan, bool effectsPlanned);
}
