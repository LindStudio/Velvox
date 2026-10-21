#pragma once

// Product identity used in code (the CMake target defines the plugin metadata).
// Product name since 0.4: HEADLINER by Lind Audio (formerly the working title "Velvox").
// The plugin codes (Lnda / Vlvx) and all persisted identifiers stay unchanged,
// so projects saved with Velvox open with Headliner.
namespace velvox::product
{
    inline constexpr const char* name = "Headliner";
    inline constexpr const char* legacyName = "Velvox";   // user preset folder of 0.1 - 0.3
    inline constexpr const char* companyName = "Lind Audio";
    inline constexpr const char* versionString = "0.4.0";
}
