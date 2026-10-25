#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../DSP/VocalChain.h"

namespace velvox
{
    // Routing strings of 0.2 / 0.3 (and presets without a routing) contain every module.
    // From 0.4 a chain only holds what is actually used. The migration keeps the order
    // and drops modules that are inaudible with the current parameter values (off; an
    // old project's macros are baked into them first); EQ, compressor, de-esser and the delay / reverb sends
    // always stay, so the familiar starting point is kept. The sound never changes:
    // a dropped module was silent.
    dsp::Routing pruneInaudible (const dsp::Routing& full, const dsp::ChainSettings& effective);

    // Returns `text` unchanged when it already is a 0.4 routing; otherwise the migrated
    // 0.4 routing string for the current parameter values of `apvts` (an empty text
    // stands for "every module in the default order").
    juce::String migrateRoutingString (const juce::String& text, juce::AudioProcessorValueTreeState& apvts);

    bool isLegacyRoutingString (const juce::String& text);
}
