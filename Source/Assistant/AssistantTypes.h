#pragma once

#include <juce_core/juce_core.h>
#include <map>
#include <vector>

// ============================================================================
//  Velvox Smart Assistant - shared types
//
//  Fully local, rule-based. No network, no language model, no code execution:
//
//    text --IntentParser--> ParsedRequest --AssistantPlanner--> Plan
//                                            ^                     |
//                       AssistantContext ----'      AssistantSession (validate,
//                       (current values, analysis,  apply selected, undo)
//                        sidechain status)
//
//  The parser only understands language, the planner only knows the DSP
//  parameters, the session only touches the APVTS. Each part can be replaced
//  (e.g. a better local NLP stage) without changing the others.
// ============================================================================

namespace velvox::assistant
{
    // Sound targets (one axis each; direction says up or down)
    enum class Goal
    {
        bass,          // low end of the voice (rumble, boom, "basslastig")
        presence,      // voice in front, intelligible
        brightness,    // highs / air
        reverb,        // room, Hall
        delay,         // echo
        level,         // vocal level (plugin output)
        width,         // stereo width
        warmth,        // body, warm colour
        harshness,     // Schaerfe, harsh resonances
        sibilance,     // S sounds
        punch,         // Druck, punch
        compression,   // dynamics control / Kompression
        beatMasking,   // vocal against the beat (needs the sidechain for measurements)
        everything     // "alles extremer" - too vague to act on
    };

    enum class Intensity { slight, moderate, strong };

    // One recognised wish ("mehr Reverb, aber nicht zu viel")
    struct Intent
    {
        Goal goal = Goal::presence;
        int direction = 0;                 // +1 more, -1 less, 0 unknown (ask)
        Intensity intensity = Intensity::moderate;
        bool constraint = false;           // negated ("nicht schaerfer"): must not move in `direction`
        juce::String phrase;               // the words it came from (shown to the user)
        juce::String source;               // empty = typed wish, otherwise the style profile it came from
    };

    struct ParsedRequest
    {
        std::vector<Intent> intents;       // wishes (constraint == false)
        std::vector<Intent> constraints;   // "nicht schaerfer", "mono-kompatibel", "nicht mehr Hoehen"
        std::vector<Goal> conflicts;       // same goal asked both up and down
        std::vector<juce::String> ambiguous;    // questions to ask back
        std::vector<juce::String> unrecognised; // text parts that matched nothing
        bool empty = true;                 // no text at all
        juce::String original;             // the text as typed
        juce::StringArray styles;          // recognised style profiles ("Ski Aggu")
        juce::StringArray unknownStyles;   // "im Stil von <unknown>"
        std::vector<Goal> styleConflicts;  // several styles disagree on a target

        bool hasConstraint (Goal g, int direction) const
        {
            for (auto& c : constraints)
                if (c.goal == g && (direction == 0 || c.direction == direction))
                    return true;
            return false;
        }
    };

    // Everything the planner may look at - a plain copy, safe to use on any thread
    struct AssistantContext
    {
        std::map<juce::String, float> values;   // parameter id -> current value (real units)

        // VOCAL CHECK measurements (only when enough voice was analysed)
        bool analysisValid = false;
        float voicedSeconds = 0.0f;
        std::array<float, 6> bandDb {};         // rumble, body, mid, presence, sibilance, air (relative to mid)
        float sibilanceVsVowelDb = -100.0f;
        bool stereoSource = false;
        bool stereoKnown = false;

        float correlation = 1.0f;               // live, output
        bool correlationKnown = false;

        // Beat sidechain
        bool sidechainConnected = false;        // bus enabled by the host
        bool sidechainSignal = false;           // signal present recently
        float sidechainRmsDb = -100.0f;         // short-term level of the sidechain
        float vocalRmsDb = -100.0f;             // short-term level of the vocal input

        // Macros: offsets on top of the module parameters (plan works on the base values)
        bool macrosActive = false;

        float get (const juce::String& id, float fallback = 0.0f) const
        {
            auto it = values.find (id);
            return it != values.end() ? it->second : fallback;
        }
        bool isOn (const char* id) const { return get (id) > 0.5f; }
    };

    // One proposed parameter change
    struct Change
    {
        juce::String paramId;
        juce::String module;       // "EQ", "Reverb", ...
        juce::String title;        // "Tiefen reduzieren"
        juce::String paramName;    // "Low Gain"
        float current = 0.0f;
        float proposed = 0.0f;
        juce::String currentText, proposedText;
        juce::String reason;
        bool measured = false;     // based on a measurement (else: general rule)
        bool selected = true;
        juce::String group;        // changes that belong together (e.g. "switch on" + "amount")
    };

    struct Note
    {
        enum class Kind { info, question, warning, unavailable };
        Kind kind = Kind::info;
        juce::String text;
    };

    struct Plan
    {
        std::vector<Change> changes;
        std::vector<Note> notes;
        bool beatRequested = false;    // the request was about the beat

        bool hasChanges() const noexcept { return ! changes.empty(); }
        int numSelected() const noexcept
        {
            int n = 0;
            for (auto& c : changes)
                n += c.selected ? 1 : 0;
            return n;
        }
    };

    juce::String goalName (Goal g);   // German, for messages
}
