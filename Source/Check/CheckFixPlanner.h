#pragma once

#include "CheckTypes.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>

// VOCAL CHECK fix model: abstract FixAction -> absolute parameter values (ResolvedFix) for the
// current chain -> written as ONE undo step.
//
// Amounts and the place of VOCAL CHECK. VOCAL CHECK measures at its own place in the chain:
//  - a module instance that is in the chain BEFORE VOCAL CHECK and audible is part of the
//    measurement, so the fix adds to it:   target = current + amount
//  - otherwise (after VOCAL CHECK, off, or new) the measurement does not contain it, so the
//    fix makes sure the module does at least the amount and never weakens what is there:
//                                          target = the more extreme of current and neutral + amount
// Applying the same finding twice therefore never doubles a cut when the module sits after
// VOCAL CHECK. When it sits before, the controller restarts the analysis after "Anwenden" and
// blocks a second apply on the stale measurement (VocalCheckController).
namespace velvox::check
{
    using ValueLookup = std::function<float (const juce::String& paramId)>;   // real units, full ids

    // Maps abstract EQ band requests onto the EQ's parameters. The EQ track replaces the
    // temporary LegacyEqResolver by one for its 16 user bands with roles (findOrCreateEqBand).
    // Contract:
    //  - role autoRole matches an existing band of that role within +-1/3 octave of freqHz;
    //    every other role (lowcut, mud, warmth, air, presence) matches its single band; role
    //    none is never used by VOCAL CHECK (user bands are never touched)
    //  - find() reports the matching band of EQ instance `instance` (nullopt = none yet)
    //  - write() returns the parameter values that make the band exactly `absolute` (gain in
    //    absolute dB), creating it in a free slot when find() found none. Pure: no side effects.
    class EqBandResolver
    {
    public:
        struct Band
        {
            float freqHz = 1000.0f, gainDb = 0.0f, q = 1.0f;
            int slopeDbPerOct = 12;
            bool on = false;
        };

        virtual ~EqBandResolver() = default;
        virtual std::optional<Band> find (const EqBandRequest& request, int instance, const ValueLookup& values) const = 0;
        virtual std::vector<ResolvedParam> write (const EqBandRequest& absolute, int instance, const ValueLookup& values) const = 0;
    };

    // Today's fixed EQ (HPF, low band, mid bell, high band, LPF): low cut -> HPF, low shelf ->
    // low band (shelf), bells below 600 Hz -> low band as bell (mid bell when the low band is a
    // shelf in use), bells 600 Hz - 8 kHz -> mid bell, high shelf and higher bells -> high band.
    // Two requests for the same slot: the later one wins (a limit of the fixed EQ).
    class LegacyEqResolver final : public EqBandResolver
    {
    public:
        std::optional<Band> find (const EqBandRequest& request, int instance, const ValueLookup& values) const override;
        std::vector<ResolvedParam> write (const EqBandRequest& absolute, int instance, const ValueLookup& values) const override;
    };

    struct FixContext
    {
        ValueLookup value;                                        // current values (full ids)
        dsp::Routing routing;
        std::function<bool (dsp::ModuleId, int)> audible;         // instance in the chain and audible
        const EqBandResolver* eq = nullptr;
    };

    // "eqOn", "deessOn", ... (base id of instance 1)
    const char* moduleOnParam (dsp::ModuleId module) noexcept;

    ResolvedFix planFix (const FixAction& action, const juce::String& name, const FixContext& context);
    // Several fixes as one step ("Alles anwenden"): every fix is planned against the same current
    // state; requests for the same EQ band keep the stronger change, parameters the value
    // farthest from the current one, low cuts the highest frequency and steepest slope.
    ResolvedFix planFixes (const std::vector<std::pair<FixAction, juce::String>>& fixes, const FixContext& context,
                           const juce::String& name);

    // Message thread. One undo transaction on apvts.undoManager (when the state has one):
    // every parameter as a gesture, the routing property, then a flush of the parameter values
    // into the state tree so all of it lands in the same transaction.
    void applyResolvedFix (juce::AudioProcessorValueTreeState& apvts, const ResolvedFix& fix);
}
