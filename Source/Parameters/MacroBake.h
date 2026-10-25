#pragma once

#include "EqBands.h"
#include "../DSP/Macros.h"

// 0.6.1: the six macro parameters hold the Simple sliders' positions; what a slider does is written
// into the module parameters (Parameters/MacroMapper.h). This file is the parameter side of that:
// writing the macro-moved ChainSettings fields back as parameter values, keeping the dry of additive
// inserts where the 0.6.0 macros kept it, and baking the 0.2 - 0.6.0 offset layer into old projects.
namespace velvox::macrobake
{
    // Instance-1 parameters a macro can change (EQ 1 bands included), in real units via `access`
    const juce::StringArray& touchedParamIds();

    // Every parameter read from `access` (real units) into the settings the DSP would get
    dsp::ChainSettings readSettings (const eqbands::Access& access, const dsp::Routing& routing);
    dsp::MacroValues readMacros (const eqbands::Access& access);

    // Writes the macro-moved fields of `to` that differ from `from`: values first, on-switches last
    // (an effect never sounds for a moment at its old Mix)
    void writeMacroFields (const eqbands::Access& access, const dsp::ChainSettings& from, const dsp::ChainSettings& to);

    // The 0.6.0 macros only ADDED wet to an additive insert (Doubler, Delay / Reverb inserts): its dry stayed
    // where the module's own Mix had put it. When `after` raises such a Mix of instance 1 above what keeps
    // that dry on the true law, the instance moves to the legacy law (dry at unity, out = dry + wet * mix);
    // a Doubler whose dry was already below unity keeps that level through its Dry parameter. Delay / Reverb
    // inserts whose own Mix was above 50 % cannot keep a dry below unity (no Dry parameter): they get the
    // legacy law too, the closest match. Returns true when `laws` or `after` changed.
    bool keepDry (const dsp::ChainSettings& before, dsp::ChainSettings& after, const dsp::Routing& routing, dsp::MixLaws& laws);

    // Bakes the 0.2 - 0.6.0 macro offset layer of an old project / preset into its module parameters:
    // afterwards the parameters alone sound exactly as `applyMacros (parameters, macros)` did. The macro
    // values stay (they are the Simple sliders' positions now). `laws` == nullptr: the law is left to the
    // caller (snapshots without a law; their law is derived from the Mix values when applied). Returns
    // true when there was something to bake (any macro above 0).
    bool bake (const eqbands::Access& access, const dsp::Routing& routing, dsp::MixLaws* laws);
}
