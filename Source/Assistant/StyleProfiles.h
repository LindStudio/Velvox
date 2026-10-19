#pragma once

#include "AssistantTypes.h"

namespace velvox::assistant
{
    // "Im Stil von ..." - a style is translated into ordinary sound targets
    // (presence, highs, compression ...), which then go through the same
    // planner as typed wishes. A profile describes typical, audible traits of
    // a vocal sound in general terms. It is NOT an analysis or a copy of any
    // recording or mix, and the assistant says so.
    struct StyleProfile
    {
        struct Trait { Goal goal; int direction; Intensity intensity; };

        juce::String label;                 // shown to the user
        juce::StringArray names;            // normalised spellings ("ski aggu", "aggu")
        juce::String character;             // one sentence: the sound in words
        std::vector<Trait> traits;
        juce::String caveat;                // what Velvox cannot do for this style (may be empty)
    };

    const std::vector<StyleProfile>& styleProfiles();
    juce::String styleProfileList();        // "Ski Aggu, Tream" (for messages)
}
