#include "ParamText.h"
#include "I18n.h"

#include <unordered_map>

namespace velvox::i18n
{
    namespace
    {
        // German interface texts per parameter id. Empty fields fall back to the English text.
        // Standard audio terms (Threshold, Ratio, Attack, Release, Mix, Drive, Decay, Pre-Delay,
        // Feedback, Q, Gain, Low Cut, High Cut, Send ...) stay English in both languages.
        struct Entry
        {
            const char* name;
            const char* description;
            std::vector<const char*> choices;   // in the parameter's choice order
        };

        const std::unordered_map<std::string, Entry>& germanTable()
        {
            static const std::unordered_map<std::string, Entry> table {
                // { "paramId", { "Name", "Beschreibung", { "Wahl 1", "Wahl 2" } } },
            };
            return table;
        }

        const Entry* find (const juce::String& paramId)
        {
            // Extra instances: "compThreshold_2" -> "compThreshold"
            auto id = paramId;
            const int underscore = id.lastIndexOfChar ('_');
            if (underscore > 0 && id.substring (underscore + 1).containsOnly ("0123456789"))
                id = id.substring (0, underscore);

            const auto& t = germanTable();
            const auto it = t.find (id.toStdString());
            return it != t.end() ? &it->second : nullptr;
        }

        juce::String pick (const char* german, const juce::String& english)
        {
            return german != nullptr && *german != 0 ? juce::String::fromUTF8 (german) : english;
        }
    }

    juce::String paramName (const juce::String& paramId, const juce::String& english)
    {
        if (! isGerman())
            return english;
        const auto* e = find (paramId);
        return e != nullptr ? pick (e->name, english) : english;
    }

    juce::String paramDescription (const juce::String& paramId, const juce::String& english)
    {
        if (! isGerman())
            return english;
        const auto* e = find (paramId);
        return e != nullptr ? pick (e->description, english) : english;
    }

    juce::String choiceName (const juce::String& paramId, int choiceIndex, const juce::String& english)
    {
        if (! isGerman())
            return english;
        const auto* e = find (paramId);
        if (e == nullptr || choiceIndex < 0 || choiceIndex >= (int) e->choices.size())
            return english;
        return pick (e->choices[(size_t) choiceIndex], english);
    }
}
