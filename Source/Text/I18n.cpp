#include "I18n.h"

#include <atomic>

namespace velvox::i18n
{
    namespace
    {
        std::atomic<int> currentLanguage { (int) Language::german };

        // Private-use characters (valid in XML, never typed by a user) mark a packed part:
        // begin, German text, separator, English text, end
        constexpr juce::juce_wchar beginMark = 0xE000, separatorMark = 0xE001, endMark = 0xE002;
    }

    Language current() noexcept           { return (Language) currentLanguage.load (std::memory_order_relaxed); }
    void setCurrent (Language l) noexcept { currentLanguage.store ((int) l, std::memory_order_relaxed); }
    bool isGerman() noexcept              { return current() == Language::german; }

    namespace
    {
        std::atomic<int> systemOverride { -1 };
    }

    void overrideSystemLanguage (std::optional<Language> l)
    {
        systemOverride.store (l.has_value() ? (int) *l : -1);
    }

    Language systemLanguage()
    {
        if (const int o = systemOverride.load(); o >= 0)
            return (Language) o;

        // "de", "de-AT", "de_DE" ... (Windows: the user's display language)
        for (const auto& code : { juce::SystemStats::getDisplayLanguage(), juce::SystemStats::getUserLanguage() })
        {
            const auto c = code.trim().toLowerCase();
            if (c.isNotEmpty())
                return c.startsWith ("de") ? Language::german : Language::english;
        }
        return Language::english;
    }

    Language resolve (Preference p)
    {
        switch (p)
        {
            case Preference::german:  return Language::german;
            case Preference::english: return Language::english;
            case Preference::system:  break;
        }
        return systemLanguage();
    }

    Preference preferenceFromString (const juce::String& s)
    {
        if (s == "de") return Preference::german;
        if (s == "en") return Preference::english;
        return Preference::system;
    }

    juce::String toString (Preference p)
    {
        switch (p)
        {
            case Preference::german:  return "de";
            case Preference::english: return "en";
            case Preference::system:  break;
        }
        return "system";
    }

    juce::String tr (const char* german, const char* english)
    {
        return juce::String::fromUTF8 (isGerman() ? german : english);
    }

    juce::String tr (const juce::String& german, const juce::String& english)
    {
        return isGerman() ? german : english;
    }

    juce::String both (const juce::String& german, const juce::String& english)
    {
        if (german == english)
            return german;
        juce::String s;
        s.preallocateBytes ((size_t) (german.getNumBytesAsUTF8() + english.getNumBytesAsUTF8() + 12));
        s << juce::String::charToString (beginMark) << german << juce::String::charToString (separatorMark)
          << english << juce::String::charToString (endMark);
        return s;
    }

    juce::String both (const char* german, const char* english)
    {
        return both (juce::String::fromUTF8 (german), juce::String::fromUTF8 (english));
    }

    bool isPacked (const juce::String& text) noexcept
    {
        return text.containsChar (beginMark);
    }

    juce::String show (const juce::String& text, Language language)
    {
        if (! isPacked (text))
            return text;

        // Plain text between packed parts is shared by both languages
        juce::String out;
        auto p = text.getCharPointer();
        enum class In { plain, german, english } in = In::plain;
        const bool wantGerman = language == Language::german;
        while (! p.isEmpty())
        {
            const auto c = p.getAndAdvance();
            if (c == beginMark)          in = In::german;
            else if (c == separatorMark) in = In::english;
            else if (c == endMark)       in = In::plain;
            else if (in == In::plain || (in == In::german) == wantGerman)
                out += juce::String::charToString (c);
        }
        return out;
    }

    juce::String show (const juce::String& text)
    {
        return show (text, current());
    }

    juce::String format (const juce::String& pattern, std::initializer_list<juce::String> args)
    {
        auto s = pattern;
        int i = 0;
        for (const auto& a : args)
            s = s.replace ("{" + juce::String (i++) + "}", a);
        return s;
    }

    juce::String number (double value, int decimals, Language language)
    {
        auto s = decimals <= 0 ? juce::String (juce::roundToInt (value)) : juce::String (value, decimals);
        if (s == "-0")
            s = "0";
        return language == Language::german ? s.replaceCharacter ('.', ',') : s;
    }

    juce::String number (double value, int decimals)
    {
        return number (value, decimals, current());
    }

    juce::String numberBoth (double value, int decimals)
    {
        return both (number (value, decimals, Language::german), number (value, decimals, Language::english));
    }
}
