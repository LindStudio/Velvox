#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cstdint>
#include <optional>

namespace velvox::dsp
{
    // Every processing module that can be placed in the chain. The numeric
    // values are persisted (packed routing) - append only, never renumber.
    enum class ModuleId : std::uint8_t
    {
        eq = 0, comp, deess, sat, doubler, pitch, delay, reverb, chop
    };

    inline constexpr int numModules = 9;

    const char* moduleKey (ModuleId id) noexcept;          // stable key used in state/presets
    const char* moduleDisplayName (ModuleId id) noexcept;  // UI name
    std::optional<ModuleId> moduleFromKey (const juce::String& key) noexcept;

    // Signal routing:
    //  - `order` is the serial position of every module.
    //  - Delay and reverb are either parallel sends (fed from the end of the
    //    serial chain, wet return only) or inserts at their position in `order`.
    //  - Both forms use out = dry + wet * mix, so switching never jumps in level.
    struct Routing
    {
        std::array<ModuleId, numModules> order {};
        bool delayInsert = false;
        bool reverbInsert = false;

        static Routing defaults() noexcept;

        bool isInsert (ModuleId id) const noexcept;
        bool isSend (ModuleId id) const noexcept { return (id == ModuleId::delay || id == ModuleId::reverb) && ! isInsert (id); }
        int positionOf (ModuleId id) const noexcept;

        // Moves module `id` to serial position `newIndex` (0 .. numModules-1)
        void move (ModuleId id, int newIndex) noexcept;
        // Moves `id` directly in front of `before` / to the very end
        void placeBefore (ModuleId id, ModuleId before) noexcept;
        void placeAtEnd (ModuleId id) noexcept { move (id, numModules - 1); }

        // Lock-free hand-over to the audio thread
        std::uint64_t pack() const noexcept;
        static Routing unpack (std::uint64_t packed) noexcept;

        // "eq,comp,deess,sat,doubler,pitch,delay:send,reverb:send"
        juce::String toString() const;
        // Tolerant parser: unknown keys are ignored, missing modules are appended
        // in default order, duplicates dropped. Never fails.
        static Routing fromString (const juce::String& text);

        bool isValidPermutation() const noexcept;

        bool operator== (const Routing& other) const noexcept
        {
            return order == other.order && delayInsert == other.delayInsert && reverbInsert == other.reverbInsert;
        }
        bool operator!= (const Routing& other) const noexcept { return ! (*this == other); }
    };
}
