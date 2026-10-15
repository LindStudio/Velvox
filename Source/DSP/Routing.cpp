#include "Routing.h"

namespace velvox::dsp
{
    namespace
    {
        constexpr const char* keys[numModules]  { "eq", "comp", "deess", "sat", "doubler", "pitch", "delay", "reverb", "chop",
                                                  "pulse", "smash", "choir", "smooth", "check", "ride" };
        constexpr const char* names[numModules] { "EQ", "Compressor", "De-esser", "Saturation", "Doubler", "Pitch", "Delay", "Reverb", "Chop",
                                                  "Pulse", "Vocal Smash", "Choir", "Smooth", "Vocal Check", "Level Ride" };
        constexpr const char* shortNames[numModules] { "EQ", "Comp", "De-ess", "Sat", "Doubler", "Pitch", "Delay", "Reverb", "Chop",
                                                       "Pulse", "Smash", "Choir", "Smooth", "Check", "Ride" };

        // Default processing order. The nine v0.2 modules keep their relative
        // order; the 0.3 modules sit where they usually belong: leveling before
        // compression, parallel squash after it, resonance control after
        // de-essing, rhythm effects late, analysis last.
        constexpr ModuleId defaultOrder[numModules] {
            ModuleId::eq, ModuleId::ride, ModuleId::comp, ModuleId::smash, ModuleId::deess, ModuleId::smooth,
            ModuleId::sat, ModuleId::doubler, ModuleId::choir, ModuleId::pitch, ModuleId::delay, ModuleId::reverb,
            ModuleId::chop, ModuleId::pulse, ModuleId::check
        };

        constexpr int idx (ModuleId id) noexcept { return (int) id; }
        constexpr int flagShift = 4 * numModules; // send/insert flags sit above the order nibbles
        static_assert (flagShift + 2 <= 64, "routing must fit into one atomic 64-bit word");
    }

    const char* moduleKey (ModuleId id) noexcept         { return keys[idx (id)]; }
    const char* moduleDisplayName (ModuleId id) noexcept { return names[idx (id)]; }
    const char* moduleShortName (ModuleId id) noexcept   { return shortNames[idx (id)]; }

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
            r.order[(size_t) i] = defaultOrder[i];
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

    void Routing::placeBefore (ModuleId id, ModuleId before) noexcept
    {
        if (id == before)
            return;
        const int from = positionOf (id), target = positionOf (before);
        if (from < 0 || target < 0)
            return;
        // Removing `id` first shifts everything after it one place to the left
        move (id, from < target ? target - 1 : target);
    }

    std::uint64_t Routing::pack() const noexcept
    {
        std::uint64_t v = 0;
        for (int i = 0; i < numModules; ++i)
            v |= (std::uint64_t) (idx (order[(size_t) i]) & 0xf) << (4 * i);
        v |= (std::uint64_t) (delayInsert ? 1 : 0) << flagShift;
        v |= (std::uint64_t) (reverbInsert ? 1 : 0) << (flagShift + 1);
        return v;
    }

    Routing Routing::unpack (std::uint64_t v) noexcept
    {
        Routing r;
        for (int i = 0; i < numModules; ++i)
            r.order[(size_t) i] = (ModuleId) ((v >> (4 * i)) & 0xf);
        r.delayInsert = ((v >> flagShift) & 1) != 0;
        r.reverbInsert = ((v >> (flagShift + 1)) & 1) != 0;
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
        std::vector<ModuleId> parsed;
        std::array<bool, numModules> used {};
        bool delayInsert = false, reverbInsert = false;

        for (const auto& token : juce::StringArray::fromTokens (text, ",", ""))
        {
            const auto key = token.upToFirstOccurrenceOf (":", false, false);
            const auto mode = token.fromFirstOccurrenceOf (":", false, false).trim();
            const auto id = moduleFromKey (key);
            if (! id.has_value() || used[(size_t) idx (*id)])
                continue;

            used[(size_t) idx (*id)] = true;
            parsed.push_back (*id);

            if (*id == ModuleId::delay)  delayInsert  = mode.equalsIgnoreCase ("insert");
            if (*id == ModuleId::reverb) reverbInsert = mode.equalsIgnoreCase ("insert");
        }

        // Missing modules (older projects / presets):
        //  - v0.2 modules keep the v0.2 rule: appended at the end in default order
        //  - 0.3 modules go to their default place, directly after the module that
        //    precedes them in the default order (or to the front)
        // so a v0.2 routing string gains the new modules exactly where a fresh
        // instance has them, and the given order is never rearranged.
        constexpr int firstV03Module = (int) ModuleId::pulse;
        for (int d = 0; d < numModules; ++d)
        {
            const auto id = defaultOrder[d];
            if (idx (id) < firstV03Module && ! used[(size_t) idx (id)])
            {
                parsed.push_back (id);
                used[(size_t) idx (id)] = true;
            }
        }

        for (int d = 0; d < numModules; ++d)
        {
            const auto id = defaultOrder[d];
            if (used[(size_t) idx (id)])
                continue;

            auto insertAt = parsed.begin();
            for (int p = d - 1; p >= 0; --p)
            {
                const auto it = std::find (parsed.begin(), parsed.end(), defaultOrder[p]);
                if (it != parsed.end())
                {
                    insertAt = it + 1;
                    break;
                }
            }
            parsed.insert (insertAt, id);
            used[(size_t) idx (id)] = true;
        }

        Routing r;
        for (int i = 0; i < numModules; ++i)
            r.order[(size_t) i] = parsed[(size_t) i];
        r.delayInsert = delayInsert;
        r.reverbInsert = reverbInsert;
        return r;
    }
}
