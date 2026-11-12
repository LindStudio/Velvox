#pragma once

#include "Licensing/License.h"

// EMBER's licence (Lemon Squeezy, same rules as VOCALYPSE: docs/LICENSING.md). OFF unless CMake is run with
// -DEMBER_LICENSING=ON. Jakob's values from the Lemon Squeezy dashboard go here (docs/products/EMBER.md
// "Lizenz"); with the ids set, a key of another product (e.g. VOCALYPSE) is refused.
#ifndef EMBER_LICENSING
 #define EMBER_LICENSING 0
#endif

namespace ember
{
    inline constexpr long long lemonStoreId = 522230;      // Lemon Squeezy store "lindaudio" (2026-11-12)
    inline constexpr long long lemonProductId = 1522200;   // EMBER De-Esser (test mode; the live copy gets a new id)
    inline constexpr const char* buyUrl = "https://lindaudio.lemonsqueezy.com/checkout/buy/0cc81f7a-56f2-4444-bd83-5969eca82a9d";

    inline vocalypse::licensing::Config licenceConfig()
    {
        vocalypse::licensing::Config c;
        c.on = EMBER_LICENSING != 0;
        c.productName = "EMBER";   // file: %APPDATA%\Lind Audio\EMBER\EMBER.license
        c.identity = { "EMBER/Lnda/Embr/3f9a", "EmberLicence" };
        c.buyUrl = buyUrl;
        c.storeId = lemonStoreId;
        c.productId = lemonProductId;
        return c;
    }

    // One per process: juce::SharedResourcePointer<EmberLicence>
    class EmberLicence final : public vocalypse::licensing::Manager
    {
    public:
        EmberLicence() : Manager (licenceConfig()) {}
    };
}
