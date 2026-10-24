#pragma once

#include <juce_core/juce_core.h>
#include "../DSP/Routing.h"
#include <optional>
#include <vector>

// VOCAL CHECK 0.6 - shared vocabulary of the findings and of the fix model.
//
// A finding carries an abstract FixAction: what has to change, not yet which parameter values.
// The planner (CheckFixPlanner.h) turns it into absolute values for the current chain
// (ResolvedFix), which the processor writes as one undo step.
namespace velvox::check
{
    // Findings with a lower confidence are not shown (and do not count for the score)
    inline constexpr float showThreshold = 0.5f;

    enum class Category { level, lowEnd, tonal, resonance, harshness, sibilance, dynamics, stereo, noise, space };
    enum class Severity { low = 0, medium, high };

    // EQ band roles (spec Q5). Macros and the auto mix own their roles; user bands have role
    // none and are never touched. Vocal Check uses lowcut / mud / warmth / air / presence for
    // the matching problems and autoRole for frequency-specific cuts (boxy, harsh, resonances).
    enum class EqRole { none, presence, warmth, air, lowcut, mud, autoRole };
    enum class EqShape { bell, lowShelf, highShelf, lowCut, highCut };

    // One EQ band the fix asks for. `gainDb` is an AMOUNT: the change the measurement asks
    // for (e.g. -3 = cut 3 dB more than what the measurement already contains). The planner
    // turns it into an absolute band gain. Cut filters use freqHz / slopeDbPerOct, gain 0.
    struct EqBandRequest
    {
        EqRole role = EqRole::autoRole;
        EqShape shape = EqShape::bell;
        float freqHz = 1000.0f;
        float gainDb = 0.0f;
        float q = 1.0f;
        int slopeDbPerOct = 12;
    };

    // One parameter of a module (base id of instance 1; the planner maps it to the instance
    // that gets the fix). Kinds:
    //   set     - absolute value (switches, frequencies, modes)
    //   amount  - change relative to `neutral` (the value that does nothing), see the planner
    //   atLeast - raise to at least `value` (never lowers)
    //   atMost  - lower to at most `value` (never raises)
    struct ParamChange
    {
        enum class Kind { set, amount, atLeast, atMost };
        juce::String paramId;
        float value = 0.0f;
        Kind kind = Kind::set;
        float neutral = 0.0f;
    };

    struct FixAction
    {
        std::vector<dsp::ModuleId> modules;    // switched on and inserted when missing
        std::vector<ParamChange> params;
        std::vector<EqBandRequest> eq;

        bool empty() const noexcept { return modules.empty() && params.empty() && eq.empty(); }
    };

    struct ResolvedParam
    {
        juce::String paramId;   // full id incl. instance suffix
        float value = 0.0f;     // real units
    };

    // Absolute values to write - one undo step
    struct ResolvedFix
    {
        juce::String name;                       // undo transaction name ("Vocal Check: Mulm um 300 Hz")
        std::vector<ResolvedParam> params;
        std::optional<dsp::Routing> routing;     // set when a module has to be inserted

        bool empty() const noexcept { return params.empty() && ! routing.has_value(); }
    };
}
