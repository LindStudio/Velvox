#pragma once

#include "VocalProfile.h"
#include "../DSP/Routing.h"
#include "../DSP/EqDesign.h"
#include "AutoEq.h"
#include "DecisionLog.h"
#include <optional>
#include <map>
#include <set>

namespace vocalypse::automix
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

    // Parameters the auto mix never writes: they belong to the session, not to the sound. They are
    // read from the live state ({ bypass, levelMatchWindow, levelMatchRange, checkOn, checkMode }).
    const std::set<juce::String>& sessionParameters();
    // Every parameter of the plugin at its default (real units)
    const std::map<juce::String, float>& defaultParameterValues();
    // The state an auto mix starts from: every parameter at its default, except the session
    // parameters, which are taken from `live` (when present there)
    std::map<juce::String, float> startValues (const std::map<juce::String, float>& live);

    // The complete target configuration plus the reasoning behind it. Values are
    // real parameter units for EVERY parameter except the session ones (sessionParameters()):
    // the plan starts from the defaults and writes everything, nothing is carried over.
    struct MixPlan
    {
        std::map<juce::String, float> values;
        dsp::Routing routing = dsp::Routing::legacyDefaults();   // the planner's chain keeps delay / reverb sends
        std::vector<Decision> decisions;
        std::vector<juce::String> notes;      // things left alone and why
        bool conservative = false;            // little or unreliable material: gentle settings only (caution >= 0.5)
        // 0 .. 1: how gentle the EQ / compressor / de-esser values are. 1 without enough material or at <= 6 s of
        // singing, 0 from 10 s, smooth in between (Ruling 19: no hard flip at 8 s when a start offset costs a segment)
        float caution = 0.0f;
        std::vector<ModuleLog> log;           // structured decision log: one entry per module the planner decided on

        // The entry of a module instance (created when missing)
        ModuleLog& logFor (dsp::ModuleId module, int instance = 0);

        void set (const juce::String& id, float value);
        float get (const juce::String& id, float fallback = 0.0f) const;
        bool on (const char* id) const { return get (id) > 0.5f; }

        // Modules the plan switches on
        std::set<dsp::ModuleId> activeModules() const;
        Decision* find (dsp::ModuleId m);
    };

    // ---- EQ role bands of a plan (spec 8, Q5). `current` (the values behind the plan's own, normally
    //      startValues()) is a fallback for what the plan has not written; empty = defaults.
    dsp::EqBand eqBand (dsp::EqBandType type, float freq, float gain, float q, int slope = 3);
    std::optional<dsp::EqBand> roleBand (const MixPlan& plan, const std::map<juce::String, float>& current, dsp::EqRole role, int instance = 0);
    float roleGain (const MixPlan& plan, const std::map<juce::String, float>& current, dsp::EqRole role, int instance = 0);   // 0 when absent / off

    // 0.7 (Jakob: "the sliders should stand where the auto mix set them"): the SIMPLE slider positions
    // (0..100 %) that show what a mix did, on each slider's own scale (Parameters/MacroMapper.h,
    // dsp::applyMacroMove), so pulling a slider to 0 takes that part of the mix back:
    //   PRESENCE  air shelf gain / 5 dB      WARMTH  warmth shelf gain / 3 dB
    //   SPACE     reverb mix / 30 %          WIDTH   doubler mix / 60 %
    //   PUNCH     (compressor ratio - 2) / 4
    // Cuts show as 0 (the sliders only go up). `values` = every parameter after the mix.
    std::map<juce::String, float> sliderPositions (const std::map<juce::String, float>& values);
    // Writes the band of `role` (all fields of `value`, role set); a missing one is created in a free slot
    // only when `createIfMissing`. Returns the band, -1 when it was not written.
    int setRoleBand (MixPlan& plan, const std::map<juce::String, float>& current, int instance, dsp::EqRole role,
                     const dsp::EqBand& value, bool createIfMissing);

    // 0.6.1: the auto-mix EQ of a plan (EQ 1, over `current`) for AutoEq: the kept bands (user, Vocal
    // Check, the low cut) and flags in `context`, the broad / narrow auto-mix bands that are on in
    // `owned`, and in context.budget how many more bands it may create (6 in all, free slots)
    struct AutoEqState
    {
        autoeq::Context context;
        std::vector<autoeq::Move> owned;
    };
    // `fresh`: plan from scratch - auto-mix bands already there (an earlier run) are free to reuse
    AutoEqState autoEqState (const MixPlan& plan, const std::map<juce::String, float>& current, const VocalProfile& profile, bool fresh = false);

    // Rule-based mixing decisions for party / EDM / hypertechno lead vocals.
    // Pure function: profile + current values in, plan out (any thread).
    //
    // Principles (all of them are checked by tests):
    //  - every setting follows from a measurement or a stated rule; nothing is
    //    switched on "because it is usually on"
    //  - one problem, one tool: e.g. harshness goes to SMOOTH, not also to the EQ
    //  - small steps; boosts are limited when the material is noisy or bright
    //  - not enough material -> conservative settings, and the plan says so
    class AutoMixPlanner
    {
    public:
        static MixPlan plan (const VocalProfile& profile, const std::map<juce::String, float>& current, const MixIntent& intent = {});

        // Serial order used by the automatic mix (delay and reverb as sends)
        static dsp::Routing standardRouting();

        // Switches the de-esser on with settings derived from the measured S and
        // vowel levels. `range` in dB. Also used by the verification when the
        // chain itself has made the S sounds more prominent.
        static void configureDeEsser (MixPlan& plan, const VocalProfile& profile, float inputGainDb, float range);
    };
}
