#pragma once

// Product identity used in code (the CMake target defines the plugin metadata).
// VOCALYPSE by Lind Audio since 2026-11-03 (earlier names: the working title "Velvox" 0.1 - 0.3,
// "Headliner" 0.4 - 0.8). The plugin codes (Lnda / Vlvx) never change, so hosts open old projects
// with VOCALYPSE; the old persisted names below are still read (state, presets, settings, preset folder).
namespace vocalypse::product
{
    inline constexpr const char* name = "VOCALYPSE";
    // Earlier product names, newest first: user preset folders under Documents/Lind Audio/<name>/Presets
    inline constexpr const char* legacyNames[] = { "Headliner", "Velvox" };
    // Persisted identifiers before the rename (read, never written)
    inline constexpr const char* legacyStateType = "VelvoxState";
    inline constexpr const char* legacyPresetType = "VelvoxPreset";
    inline constexpr const char* legacySettingsName = "Velvox";
    inline constexpr const char* companyName = "Lind Audio";
    // Shop / licence (Lemon Squeezy, docs/LICENSING.md). 0 = the key's store / product is not checked.
    // Only used with -DVOCALYPSE_LICENSING=ON.
    inline constexpr long long lemonStoreId = 0;
    inline constexpr long long lemonProductId = 0;
    inline constexpr const char* buyUrl = "https://lind-audio.com";
   #ifdef VOCALYPSE_VERSION_STRING
    inline constexpr const char* versionString = VOCALYPSE_VERSION_STRING;   // from CMake project(... VERSION)
   #else
    #error "VOCALYPSE_VERSION_STRING comes from CMakeLists.txt"
   #endif
}
