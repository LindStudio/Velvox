#include "StyleProfiles.h"

// Source is compiled as UTF-8 (/utf-8): German texts are plain literals.
namespace velvox::assistant
{
    namespace
    {
        juce::String de (const char* utf8) { return juce::String::fromUTF8 (utf8); }
    }

    const std::vector<StyleProfile>& styleProfiles()
    {
        using I = Intensity;
        static const std::vector<StyleProfile> profiles {
            { "Ski Aggu", { "ski aggu", "skiaggu", "aggu" },
              de ("Techno- und Party-Rap: trocken und weit vorne, hell, kräftig komprimiert und druckvoll."),
              { { Goal::presence, 1, I::moderate }, { Goal::brightness, 1, I::slight }, { Goal::compression, 1, I::moderate },
                { Goal::punch, 1, I::moderate }, { Goal::reverb, -1, I::slight }, { Goal::bass, -1, I::slight } },
              {} },
            { "Tream", { "tream" },
              de ("Melodischer Deutsch-Pop/Rap: hell und glatt, etwas Raum und Echo, etwas breiter."),
              { { Goal::brightness, 1, I::moderate }, { Goal::presence, 1, I::slight }, { Goal::harshness, -1, I::slight },
                { Goal::reverb, 1, I::slight }, { Goal::delay, 1, I::slight }, { Goal::width, 1, I::slight },
                { Goal::compression, 1, I::slight } },
              de ("Falls der Stil eine hörbare Tonhöhenkorrektur (Auto-Tune) nutzt: Das bietet Velvox nicht - das Pitch-Modul transponiert nur.") },
        };
        return profiles;
    }

    juce::String styleProfileList()
    {
        juce::StringArray labels;
        for (auto& p : styleProfiles())
            labels.add (p.label);
        return labels.joinIntoString (", ");
    }
}
