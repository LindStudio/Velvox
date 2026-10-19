#pragma once

#include "AssistantTypes.h"

namespace velvox::assistant
{
    // Local, rule-based interpretation of free text (German first, common
    // English phrases too). Pure function of the text: no state, no I/O, no
    // network - it can run on any thread and is fully deterministic.
    //
    // What it understands:
    //  - sound targets via synonyms, compounds and comparatives
    //    ("weniger Bass", "basslastig", "praesenter", "nach vorne", "Hall")
    //  - direction words ("mehr", "weniger", "reduzieren", "zu ..." complaints)
    //  - intensity ("etwas", "ein wenig", "moderat", "deutlich", "stark",
    //    "aber nicht zu viel")
    //  - negation as a constraint, never as the opposite wish
    //    ("nicht schaerfer", "nicht mehr Hoehen" = do not raise the highs)
    //  - keep-wishes ("soll weiterhin klar klingen") and "mono-kompatibel"
    //  - conflicts (same target up and down) and vague requests ("alles
    //    extremer"), which become questions instead of changes
    class IntentParser
    {
    public:
        static ParsedRequest parse (const juce::String& text);

        // Lower case, umlauts folded (ae, oe, ue, ss), punctuation unified
        static juce::String normalise (const juce::String& text);
    };
}
