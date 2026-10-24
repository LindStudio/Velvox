#pragma once

#include "VocalProfile.h"
#include "../DSP/Routing.h"
#include "../DSP/EqDesign.h"
#include <optional>
#include <map>
#include <set>

namespace velvox::automix
{
    // One processing step of the automatic mix, as shown to the user.
    struct Decision
    {
        dsp::ModuleId module = dsp::ModuleId::eq;
        bool outputStage = false;      // output gain / level (no module card)
        juce::String title;            // "Low cut auf 92 Hz"
        juce::String reason;           // why (measurement or rule)
        bool measured = false;         // based on a measurement of this recording
    };

    // The complete target configuration plus the reasoning behind it. Values are
    // real parameter units for every parameter the plan sets; everything else
    // keeps its current value.
    struct MixPlan
    {
        std::map<juce::String, float> values;
        dsp::Routing routing = dsp::Routing::legacyDefaults();   // the planner's chain keeps delay / reverb sends
        std::vector<Decision> decisions;
        std::vector<juce::String> notes;      // things left alone and why
        bool conservative = false;            // little or unreliable material: gentle settings only

        void set (const juce::String& id, float value);
        float get (const juce::String& id, float fallback = 0.0f) const;
        bool on (const char* id) const { return get (id) > 0.5f; }

        // Modules the plan switches on
        std::set<dsp::ModuleId> activeModules() const;
        Decision* find (dsp::ModuleId m);
    };

    // ---- EQ role bands of a plan (spec 8, Q5). The plan overlays `current` (the parameter values the
    //      plan starts from; empty = defaults). User bands (role none) are never written.
    dsp::EqBand eqBand (dsp::EqBandType type, float freq, float gain, float q, int slope = 3);
    std::optional<dsp::EqBand> roleBand (const MixPlan& plan, const std::map<juce::String, float>& current, dsp::EqRole role, int instance = 0);
    float roleGain (const MixPlan& plan, const std::map<juce::String, float>& current, dsp::EqRole role, int instance = 0);   // 0 when absent / off
    // Writes the band of `role` (all fields of `value`, role set); a missing one is created in a free slot
    // only when `createIfMissing`. Returns the band, -1 when it was not written.
    int setRoleBand (MixPlan& plan, const std::map<juce::String, float>& current, int instance, dsp::EqRole role,
                     const dsp::EqBand& value, bool createIfMissing);

    // Rule-based mixing decisions for party / EDM / hypertechno lead vocals.
    // Pure function: profile + current values in, plan out (any thread).
    //
    // Principles (all of them are checked by tests):
    //  - every setting follows from a measurement or a stated rule; nothing is
    //    switched on "because it is usually on"
    //  - one problem, one tool: e.g. harshness goes to SMOOTH, not also to the EQ
    //  - small steps; boosts are limited when the material is noisy or bright
    //  - creative effects (pitch, chop, pulse, choir) are never switched on
    //  - not enough material -> conservative settings, and the plan says so
    class AutoMixPlanner
    {
    public:
        static MixPlan plan (const VocalProfile& profile, const std::map<juce::String, float>& current);

        // Serial order used by the automatic mix (delay and reverb as sends)
        static dsp::Routing standardRouting();

        // Switches the de-esser on with settings derived from the measured S and
        // vowel levels. `range` in dB. Also used by the verification when the
        // chain itself has made the S sounds more prominent.
        static void configureDeEsser (MixPlan& plan, const VocalProfile& profile, float inputGainDb, float range);
    };
}
