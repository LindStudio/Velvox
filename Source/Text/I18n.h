#pragma once

#include <juce_core/juce_core.h>
#include <optional>

// Interface language (1.0): German and English.
//
// The language is a per-user preference (UserSettings "language": "system" = the language of the
// operating system, "de", "en"), never part of a project. It never affects the audio.
//
//   tr ("Vocal analysieren", "Analyze vocal")       text in the current language, right now
//   both ("Präsenz ", "Presence ") + ...             text that is stored and shown later (the auto-mix
//                                                    report): both languages packed into one string;
//                                                    packed parts can be concatenated with plain text
//   show (packed)                                    picks the current language (plain text unchanged)
//   format (tr ("{0} von {1}", "{0} of {1}"), { a, b })
//   number (3.5, 1)                                  "3,5" in German, "3.5" in English
//
// current() is an atomic: the analysis thread may read it, the message thread sets it.
namespace velvox::i18n
{
    enum class Language { german, english };

    // The preference as stored in the user settings
    enum class Preference { system, german, english };

    Language current() noexcept;
    void setCurrent (Language) noexcept;
    bool isGerman() noexcept;

    // German when the user language of the operating system is German, English otherwise
    Language systemLanguage();
    // Tests and the snapshot renderer: pretend the computer runs in this language (nullopt = the real one)
    void overrideSystemLanguage (std::optional<Language>);
    Language resolve (Preference);

    Preference preferenceFromString (const juce::String&);   // "de" / "en" / anything else = system
    juce::String toString (Preference);

    // Text in the current language (UTF-8 literals; the project is compiled with /utf-8)
    juce::String tr (const char* german, const char* english);
    juce::String tr (const juce::String& german, const juce::String& english);

    // Both languages in one string (for stored text). Equal texts stay plain.
    juce::String both (const juce::String& german, const juce::String& english);
    juce::String both (const char* german, const char* english);

    // The part of a (possibly packed) string in a language; plain text is returned unchanged
    juce::String show (const juce::String& text);
    juce::String show (const juce::String& text, Language);
    bool isPacked (const juce::String& text) noexcept;

    // "{0}", "{1}" ... replaced by the arguments
    juce::String format (const juce::String& pattern, std::initializer_list<juce::String> args);

    // Decimal number with the decimal separator of the language
    juce::String number (double value, int decimals);
    juce::String number (double value, int decimals, Language);
    juce::String numberBoth (double value, int decimals);   // packed

    // Sets a language for the lifetime of the object (tests, snapshots)
    struct ScopedLanguage
    {
        explicit ScopedLanguage (Language l) : previous (current()) { setCurrent (l); }
        ~ScopedLanguage() { setCurrent (previous); }
        Language previous;
    };
}
