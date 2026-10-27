#pragma once

#include <juce_core/juce_core.h>
#include "../Text/I18n.h"

// Texts of the automatic mix (1.0). They are built on the analysis thread and saved with the
// project (AutoMixController::MixReport), so every one carries both languages (i18n::both) and is
// shown through i18n::show. Nothing here depends on the current language.
namespace velvox::automix::txt
{
    inline juce::String both (const char* german, const char* english)
    {
        return i18n::both (german, english);
    }

    // A sentence with values: {0}, {1} ... are replaced by the arguments; a packed argument puts
    // its German part into the German sentence and its English part into the English one
    inline juce::String both (const char* german, const char* english, std::initializer_list<juce::String> args)
    {
        const auto fill = [&args] (juce::String s, i18n::Language language)
        {
            int i = 0;
            for (const auto& a : args)
                s = s.replace ("{" + juce::String (i++) + "}", i18n::show (a, language));
            return s;
        };
        return i18n::both (fill (juce::String::fromUTF8 (german), i18n::Language::german),
                           fill (juce::String::fromUTF8 (english), i18n::Language::english));
    }

    // Numbers with the decimal separator of each language ("2,5" / "2.5")
    inline juce::String num (double value, int decimals) { return i18n::numberBoth (value, decimals); }
    inline juce::String db (float v) { return (v > 0.0f ? "+" : "") + num (v, 1) + " dB"; }
    inline juce::String hz (float f)
    {
        return f >= 1000.0f ? num (f / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz";
    }

    // Reverb type in a step title ("Plate-Hall 14 %, ...")
    inline juce::String reverbType (int type)
    {
        switch (type)
        {
            case 0:  return both ("Room-Hall", "Room reverb");
            case 1:  return both ("Plate-Hall", "Plate reverb");
            case 2:  return "Hall";
            case 3:  return "Large Hall";
            case 4:  return "Ambience";
            case 5:  return "Atmosphere";
            case 6:  return "Cinematic";
            default: return "Ethereal";
        }
    }
}
