#include "Routing.h"

namespace velvox::dsp
{
    namespace
    {
        constexpr const char* keys[numModules]  { "eq", "comp", "deess", "sat", "doubler", "pitch", "delay", "reverb" };
        constexpr const char* names[numModules] { "EQ", "Compressor", "De-esser", "Saturation", "Doubler", "Pitch", "Delay", "Reverb" };

        constexpr int idx (ModuleId id) noexcept { return (int) id; }
    }

    const char* moduleKey (ModuleId id) noexcept         { return keys[idx (id)]; }
    const char* moduleDisplayName (ModuleId id) noexcept { return names[idx (id)]; }

    std::optional<ModuleId> moduleFromKey (const juce::String& key) noexcept
    {
        for (int i = 0; i < numModules; ++i)
            if (key.trim().equalsIgnoreCase (keys[i]))
                return (ModuleId) i;
        return std::nullopt;
    }

    Routing Routing::defaults() noexcept
    {
        Routing r;
        for (int i = 0; i < numModules; ++i)
            r.order[(size_t) i] = (ModuleId) i;
        return r;
    }

    bool Routing::isInsert (ModuleId id) const noexcept
    {
        if (id == ModuleId::delay)  return delayInsert;
        if (id == ModuleId::reverb) return reverbInsert;
        return true; // all other modules are always serial
    }

    int Routing::positionOf (ModuleId id) const noexcept
    {
        for (int i = 0; i < numModules; ++i)
            if (order[(size_t) i] == id)
                return i;
        return -1;
    }

    void Routing::move (ModuleId id, int newIndex) noexcept
    {
        const int from = positionOf (id);
        if (from < 0)
            return;
        newIndex = juce::jlimit (0, numModules - 1, newIndex);

        if (from < newIndex)
            for (int i = from; i < newIndex; ++i)
                order[(size_t) i] = order[(size_t) i + 1];
        else
            for (int i = from; i > newIndex; --i)
                order[(size_t) i] = order[(size_t) i - 1];

        order[(size_t) newIndex] = id;
    }

    std::uint64_t Routing::pack() const noexcept
    {
        std::uint64_t v = 0;
        for (int i = 0; i < numModules; ++i)
            v |= (std::uint64_t) (idx (order[(size_t) i]) & 0xf) << (4 * i);
        v |= (std::uint64_t) (delayInsert ? 1 : 0) << 32;
        v |= (std::uint64_t) (reverbInsert ? 1 : 0) << 33;
        return v;
    }

    Routing Routing::unpack (std::uint64_t v) noexcept
    {
        Routing r;
        for (int i = 0; i < numModules; ++i)
            r.order[(size_t) i] = (ModuleId) ((v >> (4 * i)) & 0xf);
        r.delayInsert = ((v >> 32) & 1) != 0;
        r.reverbInsert = ((v >> 33) & 1) != 0;
        return r.isValidPermutation() ? r : defaults();
    }

    bool Routing::isValidPermutation() const noexcept
    {
        std::array<bool, numModules> seen {};
        for (auto id : order)
        {
            const int i = idx (id);
            if (i < 0 || i >= numModules || seen[(size_t) i])
                return false;
            seen[(size_t) i] = true;
        }
        return true;
    }

    juce::String Routing::toString() const
    {
        juce::StringArray parts;
        for (auto id : order)
        {
            juce::String part (moduleKey (id));
            if (id == ModuleId::delay || id == ModuleId::reverb)
                part << (isInsert (id) ? ":insert" : ":send");
            parts.add (part);
        }
        return parts.joinIntoString (",");
    }

    Routing Routing::fromString (const juce::String& text)
    {
        Routing r = defaults();
        std::array<bool, numModules> used {};
        int count = 0;

        for (const auto& token : juce::StringArray::fromTokens (text, ",", ""))
        {
            const auto key = token.upToFirstOccurrenceOf (":", false, false);
            const auto mode = token.fromFirstOccurrenceOf (":", false, false).trim();
            const auto id = moduleFromKey (key);
            if (! id.has_value() || used[(size_t) idx (*id)])
                continue;

            used[(size_t) idx (*id)] = true;
            r.order[(size_t) count++] = *id;

            if (*id == ModuleId::delay)  r.delayInsert  = mode.equalsIgnoreCase ("insert");
            if (*id == ModuleId::reverb) r.reverbInsert = mode.equalsIgnoreCase ("insert");
        }

        // Modules missing from the text (e.g. older presets) keep default order at the end
        for (int i = 0; i < numModules; ++i)
            if (! used[(size_t) i])
                r.order[(size_t) count++] = (ModuleId) i;

        return r;
    }
}
