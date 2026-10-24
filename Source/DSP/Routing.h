#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cstdint>
#include <optional>

namespace velvox::dsp
{
    // Every processing module that can be placed in the chain. The numeric
    // values are persisted - append only, never renumber.
    enum class ModuleId : std::uint8_t
    {
        eq = 0, comp, deess, sat, doubler, pitch, delay, reverb, chop,
        // 0.3
        pulse, smash, choir, smooth, check, ride
    };

    inline constexpr int numModules = 15;

    const char* moduleKey (ModuleId id) noexcept;          // stable key used in state/presets
    const char* moduleDisplayName (ModuleId id) noexcept;  // UI name
    const char* moduleShortName (ModuleId id) noexcept;    // compact name for the chain tiles
    std::optional<ModuleId> moduleFromKey (const juce::String& key) noexcept;
    int moduleInstanceLimit (ModuleId id) noexcept;        // see Instances.h

    // One entry of the chain: a module instance. `insert` only matters for delay and
    // reverb (insert in the serial chain, or a parallel send fed from its end).
    struct ChainSlot
    {
        ModuleId id = ModuleId::eq;
        std::uint8_t instance = 0;
        bool insert = true;

        bool operator== (const ChainSlot& o) const noexcept { return id == o.id && instance == o.instance && insert == o.insert; }
        bool operator!= (const ChainSlot& o) const noexcept { return ! (*this == o); }
        bool sameInstance (ModuleId m, int inst) const noexcept { return id == m && instance == inst; }
    };

    // Signal routing (0.4): an ordered list of module instances.
    //  - The serial chain runs the entries in order; delay / reverb entries in send mode
    //    are skipped there and run in parallel from the end of the serial chain
    //    (wet return only, out = dry + wet * mix). An insert uses its instance's mix law
    //    (DSP/MixLaw.h): legacy = like the send, true = dry/wet.
    //  - A module may appear several times (different instances, each with its own
    //    parameters). Instances that are not in the list do not process at all.
    //  - Plain value type with a fixed capacity: copied lock-free to the audio thread.
    struct Routing
    {
        static constexpr int capacity = 31;   // == maxChainSlots (every instance of every module)

        std::array<ChainSlot, capacity> slots {};
        int count = 0;

        // A new plugin instance (0.6): EQ, compressor, de-esser, delay and reverb, all inserts
        static Routing defaults() noexcept;
        // The default of 0.1-0.5: EQ, compressor, de-esser, delay and reverb as sends
        static Routing legacyDefaults() noexcept;
        // Every module once in the 0.3 default order (what a 0.2 / 0.3 project contains)
        static Routing legacyFull() noexcept;

        int size() const noexcept { return count; }
        const ChainSlot& operator[] (int i) const noexcept { return slots[(size_t) i]; }
        const ChainSlot* begin() const noexcept { return slots.data(); }
        const ChainSlot* end() const noexcept { return slots.data() + count; }

        int indexOf (ModuleId id, int instance = 0) const noexcept;   // -1 when absent
        bool contains (ModuleId id, int instance = 0) const noexcept { return indexOf (id, instance) >= 0; }
        int countOf (ModuleId id) const noexcept;
        int nextFreeInstance (ModuleId id) const noexcept;            // -1 when the bank is full

        bool isInsert (ModuleId id, int instance = 0) const noexcept;
        bool isSend (ModuleId id, int instance = 0) const noexcept;
        static bool canBeSend (ModuleId id) noexcept { return id == ModuleId::delay || id == ModuleId::reverb; }

        // Edits (return false when nothing changed)
        bool insertAt (int index, ModuleId id, int instance, bool insertMode = true) noexcept;
        bool remove (ModuleId id, int instance) noexcept;
        bool moveTo (ModuleId id, int instance, int newIndex) noexcept;
        bool placeBefore (ModuleId id, int instance, ModuleId before, int beforeInstance) noexcept;
        bool placeAtEnd (ModuleId id, int instance) noexcept { return moveTo (id, instance, count - 1); }
        bool setInsert (ModuleId id, int instance, bool insertMode) noexcept;
        // Adds an instance where it usually belongs: after the other instances of the same
        // module, else after the module that precedes it in the default order (delay and
        // reverb join as sends: used when automation / macros switch an instance on, so the
        // SPACE calibration keeps its sends; the browser adds inserts itself)
        bool insertDefault (ModuleId id, int instance) noexcept;

        // Instance-0 shorthands (the API of 0.2 / 0.3)
        int positionOf (ModuleId id) const noexcept { return indexOf (id, 0); }
        void move (ModuleId id, int newIndex) noexcept { moveTo (id, 0, newIndex); }
        void placeBefore (ModuleId id, ModuleId before) noexcept { placeBefore (id, 0, before, 0); }
        void placeAtEnd (ModuleId id) noexcept { placeAtEnd (id, 0); }

        // "v2,eq,comp,eq#2,delay:send,reverb#2:insert". Strings without the "v2" marker are
        // 0.2 / 0.3 routings: every missing module is added as before (old projects keep
        // exactly their chain).
        juce::String toString() const;
        // Tolerant: unknown keys, out-of-range instances and duplicates are dropped. Never fails.
        static Routing fromString (const juce::String& text);

        bool isValid() const noexcept;   // no duplicate instance, every instance inside its bank

        bool operator== (const Routing& other) const noexcept;
        bool operator!= (const Routing& other) const noexcept { return ! (*this == other); }
    };
}
