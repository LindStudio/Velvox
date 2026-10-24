#include "MixLaw.h"

namespace velvox::dsp
{
    namespace
    {
        constexpr const char* marker = "v1";

        bool validInstance (ModuleId id, int instance) noexcept
        {
            return (int) id >= 0 && (int) id < numModules && instance >= 0 && instance < maxInstances (id);
        }
    }

    MixLaws MixLaws::allLegacy() noexcept
    {
        MixLaws l;
        for (int m = 0; m < numModules; ++m)
            l.legacyBits[(size_t) m] = (std::uint16_t) ((1u << maxInstances ((ModuleId) m)) - 1u);
        return l;
    }

    bool MixLaws::isLegacy (ModuleId id, int instance) const noexcept
    {
        return validInstance (id, instance) && (legacyBits[(size_t) id] & (1u << instance)) != 0;
    }

    void MixLaws::setLegacy (ModuleId id, int instance, bool legacy) noexcept
    {
        if (! validInstance (id, instance))
            return;
        auto& bits = legacyBits[(size_t) id];
        bits = (std::uint16_t) (legacy ? (bits | (1u << instance)) : (bits & ~(1u << instance)));
    }

    juce::String MixLaws::toString() const
    {
        juce::String s (marker);
        for (int m = 0; m < numModules; ++m)
            for (int k = 0; k < maxInstances ((ModuleId) m); ++k)
                if (isLegacy ((ModuleId) m, k))
                {
                    s << "," << moduleKey ((ModuleId) m);
                    if (k > 0)
                        s << "#" << (k + 1);
                }
        return s;
    }

    MixLaws MixLaws::fromString (const juce::String& text)
    {
        auto tokens = juce::StringArray::fromTokens (text, ",", {});
        tokens.trim();
        if (tokens.isEmpty() || tokens[0] != marker)
            return allLegacy();

        MixLaws l;
        for (int i = 1; i < tokens.size(); ++i)
        {
            const auto key = tokens[i].upToFirstOccurrenceOf ("#", false, false);
            const int instance = tokens[i].contains ("#") ? tokens[i].fromFirstOccurrenceOf ("#", false, false).getIntValue() - 1 : 0;
            if (const auto id = moduleFromKey (key))
                l.setLegacy (*id, instance, true);
        }
        return l;
    }
}
