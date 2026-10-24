#include "Routing.h"
#include "Instances.h"

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

        // Default processing order of 0.3. The nine v0.2 modules keep their relative
        // order; the 0.3 modules sit where they usually belong: leveling before
        // compression, parallel squash after it, resonance control after
        // de-essing, rhythm effects late, analysis last.
        constexpr ModuleId defaultOrder[numModules] {
            ModuleId::eq, ModuleId::ride, ModuleId::comp, ModuleId::smash, ModuleId::deess, ModuleId::smooth,
            ModuleId::sat, ModuleId::doubler, ModuleId::choir, ModuleId::pitch, ModuleId::delay, ModuleId::reverb,
            ModuleId::chop, ModuleId::pulse, ModuleId::check
        };

        constexpr int idx (ModuleId id) noexcept { return (int) id; }
        static_assert (Routing::capacity == maxChainSlots, "the routing must hold every instance of every module");

        // Parses "eq", "eq#2" -> (module, instance)
        std::optional<std::pair<ModuleId, int>> parseSlotKey (const juce::String& token)
        {
            const auto key = token.upToFirstOccurrenceOf ("#", false, false).trim();
            const auto id = moduleFromKey (key);
            if (! id.has_value())
                return std::nullopt;
            int instance = 0;
            if (token.containsChar ('#'))
                instance = token.fromFirstOccurrenceOf ("#", false, false).trim().getIntValue() - 1;
            if (instance < 0 || instance >= maxInstances (*id))
                return std::nullopt;
            return std::make_pair (*id, instance);
        }
    }

    const char* moduleKey (ModuleId id) noexcept         { return keys[idx (id)]; }
    const char* moduleDisplayName (ModuleId id) noexcept { return names[idx (id)]; }
    const char* moduleShortName (ModuleId id) noexcept   { return shortNames[idx (id)]; }
    int moduleInstanceLimit (ModuleId id) noexcept       { return maxInstances (id); }

    std::optional<ModuleId> moduleFromKey (const juce::String& key) noexcept
    {
        for (int i = 0; i < numModules; ++i)
            if (key.trim().equalsIgnoreCase (keys[i]))
                return (ModuleId) i;
        return std::nullopt;
    }

    //==========================================================================
    Routing Routing::defaults() noexcept
    {
        Routing r;
        for (auto id : { ModuleId::eq, ModuleId::comp, ModuleId::deess, ModuleId::delay, ModuleId::reverb })
            r.insertAt (r.count, id, 0, true);
        return r;
    }

    Routing Routing::legacyDefaults() noexcept
    {
        Routing r;
        for (auto id : { ModuleId::eq, ModuleId::comp, ModuleId::deess })
            r.insertAt (r.count, id, 0, true);
        r.insertAt (r.count, ModuleId::delay, 0, false);
        r.insertAt (r.count, ModuleId::reverb, 0, false);
        return r;
    }

    Routing Routing::legacyFull() noexcept
    {
        Routing r;
        for (auto id : defaultOrder)
            r.insertAt (r.count, id, 0, ! canBeSend (id));
        return r;
    }

    int Routing::indexOf (ModuleId id, int instance) const noexcept
    {
        for (int i = 0; i < count; ++i)
            if (slots[(size_t) i].sameInstance (id, instance))
                return i;
        return -1;
    }

    int Routing::countOf (ModuleId id) const noexcept
    {
        int n = 0;
        for (int i = 0; i < count; ++i)
            n += slots[(size_t) i].id == id ? 1 : 0;
        return n;
    }

    int Routing::nextFreeInstance (ModuleId id) const noexcept
    {
        for (int k = 0; k < maxInstances (id); ++k)
            if (! contains (id, k))
                return k;
        return -1;
    }

    bool Routing::isInsert (ModuleId id, int instance) const noexcept
    {
        if (! canBeSend (id))
            return true;
        const int i = indexOf (id, instance);
        return i >= 0 && slots[(size_t) i].insert;
    }

    bool Routing::isSend (ModuleId id, int instance) const noexcept
    {
        if (! canBeSend (id))
            return false;
        const int i = indexOf (id, instance);
        return i >= 0 && ! slots[(size_t) i].insert;
    }

    bool Routing::insertAt (int index, ModuleId id, int instance, bool insertMode) noexcept
    {
        if (count >= capacity || idx (id) >= numModules || instance < 0 || instance >= maxInstances (id) || contains (id, instance))
            return false;
        index = juce::jlimit (0, count, index);
        for (int i = count; i > index; --i)
            slots[(size_t) i] = slots[(size_t) i - 1];
        slots[(size_t) index] = { id, (std::uint8_t) instance, canBeSend (id) ? insertMode : true };
        ++count;
        return true;
    }

    bool Routing::remove (ModuleId id, int instance) noexcept
    {
        const int at = indexOf (id, instance);
        if (at < 0)
            return false;
        for (int i = at; i < count - 1; ++i)
            slots[(size_t) i] = slots[(size_t) i + 1];
        slots[(size_t) --count] = {};
        return true;
    }

    bool Routing::moveTo (ModuleId id, int instance, int newIndex) noexcept
    {
        const int from = indexOf (id, instance);
        if (from < 0)
            return false;
        newIndex = juce::jlimit (0, count - 1, newIndex);
        if (newIndex == from)
            return false;

        const auto moving = slots[(size_t) from];
        if (from < newIndex)
            for (int i = from; i < newIndex; ++i)
                slots[(size_t) i] = slots[(size_t) i + 1];
        else
            for (int i = from; i > newIndex; --i)
                slots[(size_t) i] = slots[(size_t) i - 1];
        slots[(size_t) newIndex] = moving;
        return true;
    }

    bool Routing::placeBefore (ModuleId id, int instance, ModuleId before, int beforeInstance) noexcept
    {
        if (id == before && instance == beforeInstance)
            return false;
        const int from = indexOf (id, instance), target = indexOf (before, beforeInstance);
        if (from < 0 || target < 0)
            return false;
        // Removing the moved entry first shifts everything after it one place to the left
        return moveTo (id, instance, from < target ? target - 1 : target);
    }

    bool Routing::setInsert (ModuleId id, int instance, bool insertMode) noexcept
    {
        const int i = indexOf (id, instance);
        if (i < 0 || ! canBeSend (id) || slots[(size_t) i].insert == insertMode)
            return false;
        slots[(size_t) i].insert = insertMode;
        return true;
    }

    bool Routing::insertDefault (ModuleId id, int instance) noexcept
    {
        int at = -1;
        for (int i = 0; i < count; ++i)
            if (slots[(size_t) i].id == id)
                at = i + 1;

        if (at < 0)
        {
            int d = 0;
            while (d < numModules && defaultOrder[d] != id)
                ++d;
            at = 0;
            for (int p = d - 1; p >= 0 && at == 0; --p)
                for (int i = count - 1; i >= 0; --i)
                    if (slots[(size_t) i].id == defaultOrder[p])
                    {
                        at = i + 1;
                        break;
                    }
        }
        return insertAt (at, id, instance, false);
    }

    bool Routing::isValid() const noexcept
    {
        if (count < 0 || count > capacity)
            return false;
        for (int i = 0; i < count; ++i)
        {
            const auto& s = slots[(size_t) i];
            if (idx (s.id) >= numModules || s.instance >= maxInstances (s.id))
                return false;
            for (int j = 0; j < i; ++j)
                if (slots[(size_t) j].sameInstance (s.id, s.instance))
                    return false;
        }
        return true;
    }

    bool Routing::operator== (const Routing& other) const noexcept
    {
        if (count != other.count)
            return false;
        for (int i = 0; i < count; ++i)
            if (slots[(size_t) i] != other.slots[(size_t) i])
                return false;
        return true;
    }

    //==========================================================================
    juce::String Routing::toString() const
    {
        juce::StringArray parts { "v2" };
        for (const auto& s : *this)
        {
            juce::String part (moduleKey (s.id));
            if (s.instance > 0)
                part << "#" << (s.instance + 1);
            if (canBeSend (s.id))
                part << (s.insert ? ":insert" : ":send");
            parts.add (part);
        }
        return parts.joinIntoString (",");
    }

    Routing Routing::fromString (const juce::String& text)
    {
        const auto tokens = juce::StringArray::fromTokens (text, ",", "");
        const bool v2 = tokens.size() > 0 && tokens[0].trim() == "v2";

        Routing r;
        for (const auto& token : tokens)
        {
            const auto slotKey = token.upToFirstOccurrenceOf (":", false, false);
            const auto mode = token.fromFirstOccurrenceOf (":", false, false).trim();
            const auto parsed = parseSlotKey (slotKey);
            if (! parsed.has_value())
                continue;
            const auto [id, instance] = *parsed;
            // A delay / reverb without an explicit mode is a send (as in 0.2 / 0.3)
            r.insertAt (r.count, id, instance, canBeSend (id) ? mode.equalsIgnoreCase ("insert") : true);
        }

        if (v2)
            return r;

        // 0.2 / 0.3 routing: every module is always part of the chain.
        //  - v0.2 modules keep the v0.2 rule: appended at the end in default order
        //  - 0.3 modules go to their default place, directly after the module that
        //    precedes them in the default order (or to the front)
        // so an old routing string gains the new modules exactly where a fresh 0.3
        // instance had them, and the given order is never rearranged.
        constexpr int firstV03Module = (int) ModuleId::pulse;
        for (int d = 0; d < numModules; ++d)
        {
            const auto id = defaultOrder[d];
            if (idx (id) < firstV03Module && ! r.contains (id, 0))
                r.insertAt (r.count, id, 0, false);   // delay / reverb default to send
        }

        for (int d = 0; d < numModules; ++d)
        {
            const auto id = defaultOrder[d];
            if (r.contains (id, 0))
                continue;

            int insertAt = 0;
            for (int p = d - 1; p >= 0; --p)
            {
                const int at = r.indexOf (defaultOrder[p], 0);
                if (at >= 0)
                {
                    insertAt = at + 1;
                    break;
                }
            }
            r.insertAt (insertAt, id, 0, false);
        }
        return r;
    }
}
