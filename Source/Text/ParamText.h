#pragma once

#include <juce_core/juce_core.h>

// Parameter texts shown in the interface (1.0). The host always sees the English names, choices
// and descriptions of Parameters/ParameterLayout.cpp (automation lanes and saved projects stay the
// same in every language); the interface shows these in the current language.
//
// Each function takes the English text (from the parameter) as the fallback: English returns it
// unchanged, German returns the translation from the table in ParamText.cpp, or the English text
// when the table has none. Extra instances ("compThreshold_2") use the entry of the first one.
namespace velvox::i18n
{
    juce::String paramName (const juce::String& paramId, const juce::String& english);
    juce::String paramDescription (const juce::String& paramId, const juce::String& english);
    juce::String choiceName (const juce::String& paramId, int choiceIndex, const juce::String& english);

    // The German choice name whatever the current language (typed values accept both languages)
    juce::String germanChoiceName (const juce::String& paramId, int choiceIndex, const juce::String& english);
}
